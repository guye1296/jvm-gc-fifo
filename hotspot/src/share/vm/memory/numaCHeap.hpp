#ifndef NUMA_C_HEAP_HPP
#define NUMA_C_HEAP_HPP

#include "runtime/globals.hpp"
#include "runtime/os.hpp"
#include "runtime/orderAccess.hpp"
#include "runtime/atomic.hpp"
#include "utilities/globalDefinitions.hpp"
#ifdef COMPILER1
#include "c1/c1_globals.hpp"
#endif
#ifdef COMPILER2
#include "opto/c2_globals.hpp"
#endif

#include <new>

//We want to allocate a big heap on each numa-node
//to avoid thrashing of numa allocation. Its super
//expensive.
#define NUMA_MAP_SIZE (1 << 25)
#define NUMBER_OF_REGIONS (2048 - sizeof(void*)*33 - sizeof(int)*2)/sizeof(void*)

class NUMACHeap {
    class SizedList {//List of a specific sized objects.
      volatile void* _free_list;//Linked list of free elements
      volatile void* _next_in_page;
      volatile void* _next_page;
      size_t _cache_max;//max elements to be cached. 0 means infinite.
      const size_t _size;
     public:
      SizedList(size_t size, size_t cache_max, void* page, size_t page_size = 0) :
                            _cache_max(cache_max), _size(size), _free_list(NULL) {
        assert(_size > 0 && (_size & (_size - 1)) == 0,
               "_size should be a power of 2");
        if (page_size > 0) {
          assert(page != NULL, "page should not be null");
          assert(page_size >= (size_t) os::vm_page_size(), "page should be at least page size big");
          assert(page_size % _size == 0, "page_size should be power of 2");
          _next_in_page = page;
          _next_page = (char*)page + page_size;
        } else {
          _next_in_page = NULL;
          _next_page = NULL;
       }
      }

      void* operator new(size_t size, void* addr) {
        return addr;
      }
      void* fast_alloc() {
        //First try the free list, then page.
        void* addr = (void*)_free_list;
        void* temp = addr;
        while(addr != NULL) {
          if ((addr = Atomic::cmpxchg_ptr(*(void**)addr, &_free_list, addr)) == temp) {
            break;
          }
          temp = addr;
        }
        if (addr != NULL) {
          return addr;
        } else {
          //Try the page now.
          addr = (void*)_next_in_page;
          temp = addr;
          while(addr < _next_page && addr != NULL) {
            if ((addr = Atomic::cmpxchg_ptr((char*)addr + _size, &_next_in_page, addr)) == temp) {
              return addr;
            }
            temp = addr;
          }
          return NULL;
        }
      }

      void* alloc(void* page, size_t page_size) {
        Atomic::store_ptr(NULL, &_next_in_page);
        OrderAccess::fence();
        Atomic::store_ptr((char*)page + page_size, &_next_page);
        OrderAccess::fence();
        Atomic::store_ptr((char*)page + _size, &_next_in_page);
        return page;
      }

      void free(void* addr) {
        void* temp = (void*)_free_list;
        volatile void* tmp;
        do {
          tmp = *(void**)addr = temp;
        }while((temp = Atomic::cmpxchg_ptr(addr, &_free_list, temp)) != tmp);
      }
    };

    struct chunk {
      size_t _size;
      struct chunk* _next;
    };

    const int _lgrp_id;
    volatile int _lock;//0 means free, 1 means locked
    //Maintain a list of free chunks. The first word holds the size of
    //this chunk and the second word holds the next pointer.
    struct chunk* _free_chunk_list;
    //Less than 32 will be used actually. We took it because we can
    //easily fit 32 SizedList elements in second half of the page.
    SizedList* _list[32];
    //List of regions to help find out the node during delete.
    void* _region_list[NUMBER_OF_REGIONS];

    inline size_t align(size_t size) {
      if (size < 8) return 8;
      return size & (size - 1) ? 1 << index(size) + 4 : size;
    }

    inline uint index(size_t size) {
      uint i;
      for(i=0; size > 1; i++, size >>= 1);
      return i - 3;//since 2^3 = 8.
    }

    void add_region(void* addr, size_t size) {
      void* value = (void*)((unsigned long)addr | (unsigned long)(index(size)+3));
      uint i;
      for(i = 0; _region_list[i] != NULL; i++);

      while(Atomic::cmpxchg_ptr(value, &_region_list[i++], NULL));
    }

    void* fetch(size_t size) {
      if (size >= NUMA_MAP_SIZE) {
        void* addr =  os::numa_alloc_onnode(size, _lgrp_id);
        assert(addr, "numa allocation should succeed!");
        //tty->print_cr("addr:%p size:%lu lgrp_id:%d", addr, size, _lgrp_id);
        add_region(addr, size);
        return addr;
      }
      //find a big enough chunk first.
      struct chunk* addr = _free_chunk_list;
      struct chunk** ptr = &_free_chunk_list;
      void* temp;
      while(addr != NULL) {
        if (addr->_size > size) {
          *ptr = (chunk*)((char*)addr + size);
          (*ptr)->_next = addr->_next;
          (*ptr)->_size = addr->_size - size;
          return addr;
        } else if (addr->_size == size) {
          *ptr = addr->_next;
          return addr;
        } else {
          ptr = &(addr->_next);
          addr = addr->_next;
        }
      }

      temp = os::numa_alloc_onnode(NUMA_MAP_SIZE, _lgrp_id);
      assert(temp, "numa allocation should succeed!");
      add_region(temp, NUMA_MAP_SIZE);
      *ptr = (chunk*)((char*)temp + size);
      (*ptr)->_next = NULL;
      (*ptr)->_size = NUMA_MAP_SIZE - size;
      return temp;
    }

   public:
    NUMACHeap(int lgrp_id, size_t chunk_size) : _lgrp_id(lgrp_id), _lock(0) {
      assert(lgrp_id >= 0, "There should be at least one node");
      assert(((unsigned long)(_region_list+NUMBER_OF_REGIONS) & ((os::vm_page_size() >> 1) - 1)) == 0,
             "something wrong with the calculation, it must be half page aligned");
      char* addr = (char*)(this + 1);
      void* data_page = (char*)this + os::vm_page_size();
      unsigned long size = 8;
      uint i;
      for (i = 0; i < 32; i++) {
        _list[i] = new((void*)(addr + i*64)) SizedList(size, 0, data_page);
        size = size << 1;
      }
      for (i = 0; i < NUMBER_OF_REGIONS; i++) {
        _region_list[i] = NULL;
      }

      assert(((unsigned long)(sizeof(NUMACHeap) + i * 64) <= os::vm_page_size()) == 0,
             "something wrong with the calculation, it must be page aligned");
      _free_chunk_list = (struct chunk*)data_page;
      _free_chunk_list->_size = ((char*)this + chunk_size) - (char*)data_page;
      _free_chunk_list->_next = NULL;
      add_region(this, chunk_size);
    }
    void* operator new(size_t size, int lgrp_id, size_t map_size = NUMA_MAP_SIZE) {
      void* addr =  os::numa_alloc_onnode(map_size, lgrp_id);
      assert(addr, "numa allocation should succeed!");
      return addr;
    }
    void* alloc(size_t size) {
      //align up to the nearest power of 2.
      size = align(size);
      //fetch the index corresponding to the size.
      int idx = index(size);
      void* addr = _list[idx]->fast_alloc();
      if (addr == NULL) {//List is out of space.
        //acquire the lock first.
        while(Atomic::cmpxchg(1, &_lock, 0) != 0) {
          //Prefer sleeping as once we fail its likely that
          //its gonna take long to finish the job for other.
          os::naked_sleep();
        }
        //Try allocating fast path again!
        addr = _list[idx]->fast_alloc();
        if (addr != NULL) goto unlock;
        //No we still don't have anything.
        if (size < (size_t)os::vm_page_size()) {
          addr = _list[idx]->alloc(fetch(os::vm_page_size()), os::vm_page_size());
        } else {
          //No need to check with the sizedList for this.
          addr = fetch(size);
        }
unlock:
        Atomic::store(0, &_lock);
      }
      //assert(((unsigned long)addr & (size-1)) == 0,
        //     err_msg("addr:%p should be aligned with size:%lu", addr, size));
      return addr;
    }

    bool region_contains(void* addr) {
      for (uint i = 0; _region_list[i] != NULL && i < NUMBER_OF_REGIONS; i++) {
        unsigned long page = (unsigned long)_region_list[i] & ~(unsigned long)(os::vm_page_size() - 1);
        size_t log_size = (unsigned long)_region_list[i] & (unsigned long)(os::vm_page_size() - 1);
        if ((unsigned long)addr >= page && (unsigned long)addr < (page + (1 << log_size)))
          return true;
      }
      return false;
    }

    void free(void* addr, size_t size) {
      _list[index(align(size))]->free(addr);
    }
};

#endif //NUMA_C_HEAP_HPP
