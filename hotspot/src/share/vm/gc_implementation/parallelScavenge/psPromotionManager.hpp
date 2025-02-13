/*
 * Copyright (c) 2002, 2010, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#ifndef SHARE_VM_GC_IMPLEMENTATION_PARALLELSCAVENGE_PSPROMOTIONMANAGER_HPP
#define SHARE_VM_GC_IMPLEMENTATION_PARALLELSCAVENGE_PSPROMOTIONMANAGER_HPP

#include "gc_implementation/parallelScavenge/psPromotionLAB.hpp"
#include "memory/allocation.hpp"
#include "utilities/taskqueue.hpp"
#ifdef INTER_NODE_MSG_Q
#include "gc_implementation/parallelScavenge/psTaskTerminator.hpp"
#endif
//
// psPromotionManager is used by a single thread to manage object survival
// during a scavenge. The promotion manager contains thread local data only.
//
// NOTE! Be carefull when allocating the stacks on cheap. If you are going
// to use a promotion manager in more than one thread, the stacks MUST be
// on cheap. This can lead to memory leaks, though, as they are not auto
// deallocated.
//
// FIX ME FIX ME Add a destructor, and don't rely on the user to drain/flush/deallocate!
//

// Move to some global location
#define HAS_BEEN_MOVED 0x1501d01d
// End move to some global location

class MutableSpace;
class PSOldGen;
class ParCompactionManager;

class PSPromotionManager : public CHeapObj {
  friend class PSScavenge;
  friend class PSRefProcTaskExecutor;
 private:
  static PSPromotionManager**         _manager_array;
  static OopStarTaskQueueSet*         _stack_array_depth;
  static PSOldGen*                    _old_gen;
  static MutableSpace*                _young_space;
#ifdef INTER_NODE_MSG_Q
  static OopStarMessageQueueSet*      _message_queue_set;
  static OopStarMessageQueue**        _numa_message_queues;
  static uint                         _numa_node_count;
  static HeapWord**                   _eden_from_space_bottoms;
  static GrowableArray<NUMANodeLocalTerminator*>* _terminator_list;
#endif
#if TASKQUEUE_STATS
  size_t                              _masked_pushes;
  size_t                              _masked_steals;
  size_t                              _arrays_chunked;
  size_t                              _array_chunks_processed;

  void print_taskqueue_stats(uint i) const;
  void print_local_stats(uint i) const;
  static void print_stats();

  void reset_stats();
#endif // TASKQUEUE_STATS

  PSYoungPromotionLAB                 _young_lab;
  PSOldPromotionLAB                   _old_lab;
  bool                                _young_gen_is_full;
  bool                                _old_gen_is_full;

  OopStarTaskQueue                    _claimed_stack_depth;
#ifdef INTER_NODE_MSG_Q
  OopStarMessageQueue**               _message_queues;
#else
  OverflowTaskQueue<oop>              _claimed_stack_breadth;
#endif
  bool                                _totally_drain;
  uint                                _target_stack_size;

  uint                                _array_chunk_size;
  uint                                _min_array_size_for_chunking;
#ifdef EXTRA_COUNTERS
  unsigned long			      _remote_sent;
  unsigned long			      _objects_copied;
#endif
  // Accessors
  static PSOldGen* old_gen()         { return _old_gen; }
  static MutableSpace* young_space() { return _young_space; }

  inline static PSPromotionManager* manager_array(int index);
  template <class T> inline void claim_or_forward_internal_depth(T* p);

  // On the task queues we push reference locations as well as
  // partially-scanned arrays (in the latter case, we push an oop to
  // the from-space image of the array and the length on the
  // from-space image indicates how many entries on the array we still
  // need to scan; this is basically how ParNew does partial array
  // scanning too). To be able to distinguish between reference
  // locations and partially-scanned array oops we simply mask the
  // latter oops with 0x01. The next three methods do the masking,
  // unmasking, and checking whether the oop is masked or not. Notice
  // that the signature of the mask and unmask methods looks a bit
  // strange, as they accept and return different types (oop and
  // oop*). This is because of the difference in types between what
  // the task queue holds (oop*) and oops to partially-scanned arrays
  // (oop). We do all the necessary casting in the mask / unmask
  // methods to avoid sprinkling the rest of the code with more casts.

  // These are added to the taskqueue so PS_CHUNKED_ARRAY_OOP_MASK (or any
  // future masks) can't conflict with COMPRESSED_OOP_MASK
#ifdef INTER_NODE_MSG_Q
// Check comment on COMPRESSED_OOP_MASK in taskqueue.hpp
#define PS_CHUNKED_ARRAY_OOP_MASK  0x4000000000000000UL //1 << 62
#else
#define PS_CHUNKED_ARRAY_OOP_MASK  0x2
#endif
  bool is_oop_masked(StarTask p) {
    // If something is marked chunked it's always treated like wide oop*
    return (((intptr_t)(oop*)p) & PS_CHUNKED_ARRAY_OOP_MASK) ==
                                  PS_CHUNKED_ARRAY_OOP_MASK;
  }

  oop* mask_chunked_array_oop(oop obj) {
    assert(!is_oop_masked((oop*) obj), "invariant");
    oop* ret = (oop*) ((uintptr_t)obj | PS_CHUNKED_ARRAY_OOP_MASK);
    assert(is_oop_masked(ret), "invariant");
    return ret;
  }

  oop unmask_chunked_array_oop(StarTask p) {
    assert(is_oop_masked(p), "invariant");
    assert(!p.is_narrow(), "chunked array oops cannot be narrow");
    oop *chunk = (oop*)p;  // cast p to oop (uses conversion operator)
    oop ret = oop((oop*)((uintptr_t)chunk & ~PS_CHUNKED_ARRAY_OOP_MASK));
    assert(!is_oop_masked((oop*) ret), "invariant");
    return ret;
  }

  template <class T> void  process_array_chunk_work(oop obj,
                                                    int start, int end);
  void process_array_chunk(oop old);

  template <class T> void push_depth(T* p) {
    claimed_stack_depth()->push(p);
  }

 protected:
  static OopStarTaskQueueSet* stack_array_depth()   { return _stack_array_depth; }
#ifdef INTER_NODE_MSG_Q
  static OopStarMessageQueueSet* message_queue_set()   { return _message_queue_set; }
#endif
 public:
  // Static
  static void initialize();

  static void pre_scavenge();
  static void post_scavenge();

  static PSPromotionManager* gc_thread_promotion_manager(int index);
  static PSPromotionManager* vm_thread_promotion_manager();
#ifdef NUMA_AWARE_STEALING
  static bool steal_depth(int queue_num, int* seed, StarTask& t, int affinity) {
    return stack_array_depth()->steal(queue_num, seed, t, affinity);
  }
#else
  static bool steal_depth(int queue_num, int* seed, StarTask& t) {
    return stack_array_depth()->steal(queue_num, seed, t);
  }
#endif
#ifdef INTER_NODE_MSG_Q
  static bool steal_depth_from_msg_queue(int* seed, StarTask& t, int affinity) {
    return message_queue_set()->steal(~0, seed, t, affinity);
  }

  static OopStarMessageQueue* numa_message_queue(uint index) {
    return _numa_message_queues[index];
  }
  static GrowableArray<NUMANodeLocalTerminator*>* terminator_list() {
    return _terminator_list;
  }
  OopStarMessageQueue* message_queue(uint index) {
    return _message_queues[index];
  }
  static uint numa_node_count() { return _numa_node_count;}

  template <class T> inline bool forward_to_numa_node(T* p) {
    assert(Thread::current()->lgrp_id() >= 0,
           "lgrp_id should be set by now");
    void* o = (void*) oopDesc::load_decode_heap_oop_not_null(p);
    uint min = 0;
    uint max = numa_node_count() << 1;
    uint mid;
    while (min + 1 != max) {
      mid = (min + max) >> 1;
      if (_eden_from_space_bottoms[mid] > o)
        max = mid;
      else
        min = mid;
    }
    min %= numa_node_count();
    if (Thread::current()->lgrp_id() == (int) min)
      return false;
#ifdef NUMAMESSAGE_ELEM_COUNT
    message_queue(min)->enqueue(p, min);
#else
    message_queue(min)->enqueue(p);
#endif
#ifdef EXTRA_COUNTERS
    _remote_sent++;
#endif
    return true;
  }

  inline void flush_msg_queues() {
    if (_message_queues == NULL) return;
    for (uint i = 0; i < numa_node_count(); i++) {
      if (message_queue(i)) {
#ifdef NUMAMESSAGE_ELEM_COUNT
        message_queue(i)->flush_local(i);
#else
        message_queue(i)->flush_local();
#endif
      }
    }
  }

  void register_message_queues(OopStarMessageQueue** qs) { _message_queues = qs;}
#endif
#ifdef NUMA_AWARE_C_HEAP
  PSPromotionManager(int lgrp_id = -1);
#else
  PSPromotionManager();
#endif
  // Accessors
  OopStarTaskQueue* claimed_stack_depth() {
    return &_claimed_stack_depth;
  }

  bool young_gen_is_full()             { return _young_gen_is_full; }

  bool old_gen_is_full()               { return _old_gen_is_full; }
  void set_old_gen_is_full(bool state) { _old_gen_is_full = state; }

  // Promotion methods
  oop copy_to_survivor_space(oop o);
  oop oop_promotion_failed(oop obj, markOop obj_mark);

  void reset();
#if defined(LOCAL_MSG_PER_THREAD) && defined(INTER_NODE_STEALING)
  inline void process_1_msg(msg_t* m);
#endif
  void flush_labs();
#ifdef INTER_NODE_MSG_Q
  void drain_stacks(bool totally_drain);
#else
  void drain_stacks(bool totally_drain) {
    drain_stacks_depth(totally_drain);
  }
#endif
 public:
  void drain_stacks_cond_depth() {
    if (claimed_stack_depth()->size() > _target_stack_size) {
      drain_stacks_depth(false);
    }
  }
  void drain_stacks_depth(bool totally_drain);

  bool stacks_empty() {
    return claimed_stack_depth()->is_empty();
  }

  inline void process_popped_location_depth(StarTask p);

  template <class T> inline void claim_or_forward_depth(T* p);

  TASKQUEUE_STATS_ONLY(inline void record_steal(StarTask& p);)
};

#endif // SHARE_VM_GC_IMPLEMENTATION_PARALLELSCAVENGE_PSPROMOTIONMANAGER_HPP
