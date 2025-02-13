/*
 * Copyright (c) 2002, 2011, Oracle and/or its affiliates. All rights reserved.
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

#include "precompiled.hpp"
#include "gc_implementation/parallelScavenge/parallelScavengeHeap.hpp"
#include "gc_implementation/parallelScavenge/psOldGen.hpp"
#include "gc_implementation/parallelScavenge/psPromotionManager.inline.hpp"
#include "gc_implementation/parallelScavenge/psScavenge.inline.hpp"
#include "gc_implementation/shared/mutableSpace.hpp"
#include "memory/memRegion.hpp"
#include "oops/oop.inline.hpp"
#include "oops/oop.psgc.inline.hpp"

#ifdef BANDWIDTH_TEST
#include "gc_implementation/parallelScavenge/gcTaskThread.hpp"
#endif

#ifdef NUMA_AWARE_STEALING
#include "gc_implementation/parallelScavenge/gcTaskManager.hpp"
#include "gc_implementation/parallelScavenge/gcTaskThread.hpp"
#endif

#ifdef INTER_NODE_MSG_Q
#include "gc_implementation/shared/mutableNUMASpace.hpp"

OopStarMessageQueueSet*      PSPromotionManager::_message_queue_set = NULL;
OopStarMessageQueue**        PSPromotionManager::_numa_message_queues = NULL;
uint                         PSPromotionManager::_numa_node_count = 0;
HeapWord**                   PSPromotionManager::_eden_from_space_bottoms = NULL;
GrowableArray<NUMANodeLocalTerminator*>* PSPromotionManager::_terminator_list = NULL;
#endif
PSPromotionManager**         PSPromotionManager::_manager_array = NULL;
OopStarTaskQueueSet*         PSPromotionManager::_stack_array_depth = NULL;
PSOldGen*                    PSPromotionManager::_old_gen = NULL;
MutableSpace*                PSPromotionManager::_young_space = NULL;

void PSPromotionManager::initialize() {
  ParallelScavengeHeap* heap = (ParallelScavengeHeap*)Universe::heap();
  assert(heap->kind() == CollectedHeap::ParallelScavengeHeap, "Sanity");

  _old_gen = heap->old_gen();
  _young_space = heap->young_gen()->to_space();

  assert(_manager_array == NULL, "Attempt to initialize twice");
  _manager_array = NEW_C_HEAP_ARRAY(PSPromotionManager*, ParallelGCThreads+1 );
  guarantee(_manager_array != NULL, "Could not initialize promotion manager");
#if defined(NUMA_AWARE_STEALING) || defined(INTER_NODE_MSG_Q)
  if (UseNUMA) {
#ifdef INTER_NODE_MSG_Q
    int j;
    _numa_node_count = os::numa_get_groups_num();
    _eden_from_space_bottoms = NUMA_NEW_C_HEAP_ARRAY(HeapWord*, 2 * _numa_node_count, -1);
    _terminator_list = new (ResourceObj::C_HEAP) GrowableArray<NUMANodeLocalTerminator*>(_numa_node_count, true);
    _numa_message_queues = NEW_C_HEAP_ARRAY(OopStarMessageQueue*, _numa_node_count * _numa_node_count);
    _stack_array_depth = new OopStarTaskQueueSet(ParallelGCThreads, _numa_node_count);
    _message_queue_set = new OopStarMessageQueueSet(_numa_node_count * (_numa_node_count - 1), _numa_node_count);
    guarantee(_message_queue_set != NULL && _stack_array_depth != NULL,
                              "Cound not initialize promotion manager");

    for (int k = 0, i = 0; (uint) i < _numa_node_count; i++) {
      _terminator_list->append(new(i) NUMANodeLocalTerminator(heap->gc_task_manager()->threads_on_node(i)));
      for (j = 0; (uint) j < _numa_node_count; j++) {
        uint n = (i * _numa_node_count) + j;
        if (i == j) {
          _numa_message_queues[n] = NULL;
        } else {
          _numa_message_queues[n] = new(i) OopStarMessageQueue(i, heap->gc_task_manager()->threads_on_node(j),
                                                               heap->gc_task_manager()->threads_on_node(i));
          _message_queue_set->register_queue(k++, _numa_message_queues[n], j);
        }
      }
    }
#endif
#ifdef NUMA_AWARE_STEALING 
    _stack_array_depth = new OopStarTaskQueueSet(ParallelGCThreads, os::numa_get_groups_num());
#else
    _stack_array_depth = new OopStarTaskQueueSet(ParallelGCThreads);
#endif
  } else
#endif
  _stack_array_depth = new OopStarTaskQueueSet(ParallelGCThreads);
  guarantee(_stack_array_depth != NULL, "Cound not initialize promotion manager");

  // Create and register the PSPromotionManager(s) for the worker threads.
  for(uint i=0; i<ParallelGCThreads; i++) {
#ifndef NUMA_AWARE_C_HEAP
    _manager_array[i] = new PSPromotionManager();
    guarantee(_manager_array[i] != NULL, "Could not create PSPromotionManager");
#endif
#if defined(NUMA_AWARE_STEALING) || defined(INTER_NODE_MSG_Q)
    if (UseNUMA) {//set the lgrp_id same as the thread's on the queue as well.
      int lgrp_id = heap->gc_task_manager()->thread(i)->lgrp_id();
#ifdef NUMA_AWARE_C_HEAP
      _manager_array[i] = new(lgrp_id) PSPromotionManager(lgrp_id);
      guarantee(_manager_array[i] != NULL, "Could not create PSPromotionManager");
#endif
#ifdef INTER_NODE_MSG_Q
      _manager_array[i]->register_message_queues(_numa_message_queues +
                                          (_numa_node_count * lgrp_id));
#endif
#ifdef NUMA_AWARE_STEALING
      stack_array_depth()->register_queue(i, _manager_array[i]->claimed_stack_depth(),
                                                                             lgrp_id);
#else
      stack_array_depth()->register_queue(i, _manager_array[i]->claimed_stack_depth());
#endif
    } else {
#endif
#ifdef NUMA_AWARE_C_HEAP
      _manager_array[i] = new PSPromotionManager();
      guarantee(_manager_array[i] != NULL, "Could not create PSPromotionManager");
#endif
      stack_array_depth()->register_queue(i, _manager_array[i]->claimed_stack_depth());
#ifdef NUMA_AWARE_STEALING
    }
#endif
  }

  // The VMThread gets its own PSPromotionManager, which is not available
  // for work stealing.
  _manager_array[ParallelGCThreads] = new PSPromotionManager();
  guarantee(_manager_array[ParallelGCThreads] != NULL, "Could not create PSPromotionManager");
#ifdef INTER_NODE_MSG_Q
  if (UseNUMA)
    _manager_array[ParallelGCThreads]->register_message_queues(NULL);
#endif
}

PSPromotionManager* PSPromotionManager::gc_thread_promotion_manager(int index) {
  assert(index >= 0 && index < (int)ParallelGCThreads, "index out of range");
  assert(_manager_array != NULL, "Sanity");
  return _manager_array[index];
}

PSPromotionManager* PSPromotionManager::vm_thread_promotion_manager() {
  assert(_manager_array != NULL, "Sanity");
  return _manager_array[ParallelGCThreads];
}

void PSPromotionManager::pre_scavenge() {
  ParallelScavengeHeap* heap = (ParallelScavengeHeap*)Universe::heap();
  assert(heap->kind() == CollectedHeap::ParallelScavengeHeap, "Sanity");

  _young_space = heap->young_gen()->to_space();
#ifdef INTER_NODE_MSG_Q
  if (UseNUMA) {
    MutableNUMASpace* from = (MutableNUMASpace*)heap->young_gen()->from_space();
    MutableNUMASpace* eden = (MutableNUMASpace*)heap->young_gen()->eden_space();
    for (int j = 0; j < from->lgrp_spaces()->length(); j++) {
      _eden_from_space_bottoms[j] = eden->lgrp_spaces()->at(j)->space()->bottom();
      _eden_from_space_bottoms[j + from->lgrp_spaces()->length()] = 
                                    from->lgrp_spaces()->at(j)->space()->bottom();
      
    }
  }
#endif
  for(uint i=0; i<ParallelGCThreads+1; i++) {
    manager_array(i)->reset();
  }
}

#ifdef EXTRA_COUNTERS
extern unsigned long total_objects_copied;
extern unsigned long total_remote_sent;
#endif

void PSPromotionManager::post_scavenge() {
  TASKQUEUE_STATS_ONLY(if (PrintGCDetails && ParallelGCVerbose) print_stats());
#ifdef EXTRA_COUNTERS
  unsigned long temp_obj_copied = 0;
  unsigned long temp_remote_sent = 0;
#endif
  for (uint i = 0; i < ParallelGCThreads + 1; i++) {
    PSPromotionManager* manager = manager_array(i);
    assert(manager->claimed_stack_depth()->is_empty(), "should be empty");
    manager->flush_labs();
#ifdef EXTRA_COUNTERS
    temp_obj_copied += manager->_objects_copied;
    temp_remote_sent += manager->_remote_sent;
    manager->_objects_copied = manager->_remote_sent = 0;
#endif
  }
#ifdef EXTRA_COUNTERS
  total_objects_copied += temp_obj_copied;
  total_remote_sent += temp_remote_sent;
#endif
}

#if TASKQUEUE_STATS
void
PSPromotionManager::print_taskqueue_stats(uint i) const {
  tty->print("%3u ", i);
  _claimed_stack_depth.stats.print();
  tty->cr();
}

void
PSPromotionManager::print_local_stats(uint i) const {
  #define FMT " " SIZE_FORMAT_W(10)
  tty->print_cr("%3u" FMT FMT FMT FMT, i, _masked_pushes, _masked_steals,
                _arrays_chunked, _array_chunks_processed);
  #undef FMT
}

static const char* const pm_stats_hdr[] = {
  "    --------masked-------     arrays      array",
  "thr       push      steal    chunked     chunks",
  "--- ---------- ---------- ---------- ----------"
};

void
PSPromotionManager::print_stats() {
  tty->print_cr("== GC Tasks Stats, GC %3d",
                Universe::heap()->total_collections());

  tty->print("thr "); TaskQueueStats::print_header(1); tty->cr();
  tty->print("--- "); TaskQueueStats::print_header(2); tty->cr();
  for (uint i = 0; i < ParallelGCThreads + 1; ++i) {
    manager_array(i)->print_taskqueue_stats(i);
  }

  const uint hlines = sizeof(pm_stats_hdr) / sizeof(pm_stats_hdr[0]);
  for (uint i = 0; i < hlines; ++i) tty->print_cr(pm_stats_hdr[i]);
  for (uint i = 0; i < ParallelGCThreads + 1; ++i) {
    manager_array(i)->print_local_stats(i);
  }
}

void
PSPromotionManager::reset_stats() {
  claimed_stack_depth()->stats.reset();
  _masked_pushes = _masked_steals = 0;
  _arrays_chunked = _array_chunks_processed = 0;
}
#endif // TASKQUEUE_STATS

#ifdef NUMA_AWARE_C_HEAP
PSPromotionManager::PSPromotionManager(int lgrp_id) {
#else
PSPromotionManager::PSPromotionManager() {
#endif
  ParallelScavengeHeap* heap = (ParallelScavengeHeap*)Universe::heap();
  assert(heap->kind() == CollectedHeap::ParallelScavengeHeap, "Sanity");

  // We set the old lab's start array.
  _old_lab.set_start_array(old_gen()->start_array());

  uint queue_size;
#ifdef NUMA_AWARE_C_HEAP
  if (UseNUMA)
    claimed_stack_depth()->initialize(lgrp_id);
  else
#endif
  claimed_stack_depth()->initialize();
  queue_size = claimed_stack_depth()->max_elems();

  _totally_drain = (ParallelGCThreads == 1) || (GCDrainStackTargetSize == 0);
  if (_totally_drain) {
    _target_stack_size = 0;
  } else {
    // don't let the target stack size to be more than 1/4 of the entries
    _target_stack_size = (uint) MIN2((uint) GCDrainStackTargetSize,
                                     (uint) (queue_size / 4));
  }

  _array_chunk_size = ParGCArrayScanChunk;
  // let's choose 1.5x the chunk size
  _min_array_size_for_chunking = 3 * _array_chunk_size / 2;
#ifdef EXTRA_COUNTERS
  _remote_sent = _objects_copied = 0;
#endif
  reset();
}

void PSPromotionManager::reset() {
  assert(stacks_empty(), "reset of non-empty stack");

  // We need to get an assert in here to make sure the labs are always flushed.

  ParallelScavengeHeap* heap = (ParallelScavengeHeap*)Universe::heap();
  assert(heap->kind() == CollectedHeap::ParallelScavengeHeap, "Sanity");

  // Do not prefill the LAB's, save heap wastage!
  HeapWord* lab_base = young_space()->top();
  _young_lab.initialize(MemRegion(lab_base, (size_t)0));
  _young_gen_is_full = false;

  lab_base = old_gen()->object_space()->top();
  _old_lab.initialize(MemRegion(lab_base, (size_t)0));
  _old_gen_is_full = false;

  TASKQUEUE_STATS_ONLY(reset_stats());
}

#ifdef INTER_NODE_MSG_Q
void PSPromotionManager::drain_stacks(bool totally_drain) {
#ifdef INTER_NODE_STEALING
  drain_stacks_depth(totally_drain);
#endif
  if (UseNUMA) {
    uint idx = os::random() % (numa_node_count() - 1);
    OopStarMessageQueue* q = message_queue_set()->queue_on_node(idx,
                                      Thread::current()->lgrp_id());
    StarTask p;
    while(q->pop_global(p)) {
#ifdef LOCAL_MSG_PER_THREAD
#ifdef INTER_NODE_STEALING
      process_1_msg((msg_t*)((oop*)p));
#else
      msg_t* m = (msg_t*)((oop*)p);
      uint n = 0;
      while (OopStarMessageQueue::dequeue(p, m, n)) {
        process_popped_location_depth(p);
      }
#endif
#else //!LOCAL_MSG_PER_THREAD
     process_popped_location_depth(p);
#endif
#ifdef INTER_NODE_STEALING
      drain_stacks_depth(totally_drain);
    }
  }
#else
    }
  }
  drain_stacks_depth(totally_drain);
#endif
  if (UseNUMA)
    flush_msg_queues();
}
#endif

void PSPromotionManager::drain_stacks_depth(bool totally_drain) {
  totally_drain = totally_drain || _totally_drain;

#ifdef ASSERT
  ParallelScavengeHeap* heap = (ParallelScavengeHeap*)Universe::heap();
  assert(heap->kind() == CollectedHeap::ParallelScavengeHeap, "Sanity");
  MutableSpace* to_space = heap->young_gen()->to_space();
  MutableSpace* old_space = heap->old_gen()->object_space();
  MutableSpace* perm_space = heap->perm_gen()->object_space();
#endif /* ASSERT */

  OopStarTaskQueue* const tq = claimed_stack_depth();
  do {
    StarTask p;

    // Drain overflow stack first, so other threads can steal from
    // claimed stack while we work.
    while (tq->pop_overflow(p)) {
      process_popped_location_depth(p);
    }

    if (totally_drain) {
      while (tq->pop_local(p)) {
        process_popped_location_depth(p);
      }
    } else {
      while (tq->size() > _target_stack_size && tq->pop_local(p)) {
        process_popped_location_depth(p);
      }
    }
  } while (totally_drain && !tq->taskqueue_empty() || !tq->overflow_empty());

  assert(!totally_drain || tq->taskqueue_empty(), "Sanity");
  assert(totally_drain || tq->size() <= _target_stack_size, "Sanity");
  assert(tq->overflow_empty(), "Sanity");
}

void PSPromotionManager::flush_labs() {
  assert(stacks_empty(), "Attempt to flush lab with live stack");

  // If either promotion lab fills up, we can flush the
  // lab but not refill it, so check first.
  assert(!_young_lab.is_flushed() || _young_gen_is_full, "Sanity");
  if (!_young_lab.is_flushed())
    _young_lab.flush();

  assert(!_old_lab.is_flushed() || _old_gen_is_full, "Sanity");
  if (!_old_lab.is_flushed())
    _old_lab.flush();

  // Let PSScavenge know if we overflowed
  if (_young_gen_is_full) {
    PSScavenge::set_survivor_overflow(true);
  }
}
#ifdef BANDWIDTH_TEST
static uint64_t read_tsc(void) {
  uint32_t a, d;
  __asm __volatile("rdtsc" : "=a" (a), "=d" (d));
  return ((uint64_t) a) | (((uint64_t) d) << 32);
}
#endif
//
// This method is pretty bulky. It would be nice to split it up
// into smaller submethods, but we need to be careful not to hurt
// performance.
//
oop PSPromotionManager::copy_to_survivor_space(oop o) {
  assert(PSScavenge::should_scavenge(&o), "Sanity");

  oop new_obj = NULL;

  // NOTE! We must be very careful with any methods that access the mark
  // in o. There may be multiple threads racing on it, and it may be forwarded
  // at any time. Do not use oop methods for accessing the mark!
  markOop test_mark = o->mark();

  // The same test as "o->is_forwarded()"
  if (!test_mark->is_marked()) {
    bool new_obj_is_tenured = false;
    size_t new_obj_size = o->size();

    // Find the objects age, MT safe.
    int age = (test_mark->has_displaced_mark_helper() /* o->has_displaced_mark() */) ?
      test_mark->displaced_mark_helper()->age() : test_mark->age();

    // Try allocating obj in to-space (unless too old)
    if (age < PSScavenge::tenuring_threshold()) {
      new_obj = (oop) _young_lab.allocate(new_obj_size);
      if (new_obj == NULL && !_young_gen_is_full) {
        // Do we allocate directly, or flush and refill?
        if (new_obj_size > (YoungPLABSize / 2)) {
          // Allocate this object directly
          new_obj = (oop)young_space()->cas_allocate(new_obj_size);
        } else {
          // Flush and fill
          _young_lab.flush();

          HeapWord* lab_base = young_space()->cas_allocate(YoungPLABSize);
          if (lab_base != NULL) {
            _young_lab.initialize(MemRegion(lab_base, YoungPLABSize));
            // Try the young lab allocation again.
            new_obj = (oop) _young_lab.allocate(new_obj_size);
          } else {
            _young_gen_is_full = true;
          }
        }
      }
    }

    // Otherwise try allocating obj tenured
    if (new_obj == NULL) {
#ifndef PRODUCT
      if (Universe::heap()->promotion_should_fail()) {
        return oop_promotion_failed(o, test_mark);
      }
#endif  // #ifndef PRODUCT

      new_obj = (oop) _old_lab.allocate(new_obj_size);
      new_obj_is_tenured = true;

      if (new_obj == NULL) {
        if (!_old_gen_is_full) {
          // Do we allocate directly, or flush and refill?
          if (new_obj_size > (OldPLABSize / 2)) {
            // Allocate this object directly
            new_obj = (oop)old_gen()->cas_allocate(new_obj_size);
          } else {
            // Flush and fill
            _old_lab.flush();

            HeapWord* lab_base = old_gen()->cas_allocate(OldPLABSize);
            if(lab_base != NULL) {
              _old_lab.initialize(MemRegion(lab_base, OldPLABSize));
              // Try the old lab allocation again.
              new_obj = (oop) _old_lab.allocate(new_obj_size);
            }
          }
        }

        // This is the promotion failed test, and code handling.
        // The code belongs here for two reasons. It is slightly
        // different thatn the code below, and cannot share the
        // CAS testing code. Keeping the code here also minimizes
        // the impact on the common case fast path code.

        if (new_obj == NULL) {
          _old_gen_is_full = true;
          return oop_promotion_failed(o, test_mark);
        }
      }
    }

    assert(new_obj != NULL, "allocation should have succeeded");
#ifdef BANDWIDTH_TEST
    ((GCTaskThread*)Thread::current())->bytes_read += new_obj_size;
    ((GCTaskThread*)Thread::current())->bytes_write += new_obj_size;// + (64 / HeapWordSize); 
    //The extra 64 bytes is for cache miss while reading the object header.
    uint64_t a = read_tsc();
#endif
    // Copy obj
    Copy::aligned_disjoint_words((HeapWord*)o, (HeapWord*)new_obj, new_obj_size);
#ifdef BANDWIDTH_TEST
    uint64_t b = read_tsc();
    ((GCTaskThread*)Thread::current())->bytes_time += b - a;
#endif
    // Now we have to CAS in the header.
    if (o->cas_forward_to(new_obj, test_mark)) {
      // We won any races, we "own" this object.
#ifdef EXTRA_COUNTERS
      _objects_copied++;
#endif
      assert(new_obj == o->forwardee(), "Sanity");

      // Increment age if obj still in new generation. Now that
      // we're dealing with a markOop that cannot change, it is
      // okay to use the non mt safe oop methods.
      if (!new_obj_is_tenured) {
        new_obj->incr_age();
        assert(young_space()->contains(new_obj), "Attempt to push non-promoted obj");
      }

      // Do the size comparison first with new_obj_size, which we
      // already have. Hopefully, only a few objects are larger than
      // _min_array_size_for_chunking, and most of them will be arrays.
      // So, the is->objArray() test would be very infrequent.
      if (new_obj_size > _min_array_size_for_chunking &&
          new_obj->is_objArray() &&
          PSChunkLargeArrays) {
        // we'll chunk it
        oop* const masked_o = mask_chunked_array_oop(o);
        push_depth(masked_o);
        TASKQUEUE_STATS_ONLY(++_arrays_chunked; ++_masked_pushes);
      } else {
        // we'll just push its contents
        new_obj->push_contents(this);
      }
    }  else {
      // We lost, someone else "owns" this object
      guarantee(o->is_forwarded(), "Object must be forwarded if the cas failed.");

      // Try to deallocate the space.  If it was directly allocated we cannot
      // deallocate it, so we have to test.  If the deallocation fails,
      // overwrite with a filler object.
      if (new_obj_is_tenured) {
        if (!_old_lab.unallocate_object(new_obj)) {
          CollectedHeap::fill_with_object((HeapWord*) new_obj, new_obj_size);
        }
      } else if (!_young_lab.unallocate_object(new_obj)) {
        CollectedHeap::fill_with_object((HeapWord*) new_obj, new_obj_size);
      }

      // don't update this before the unallocation!
      new_obj = o->forwardee();
    }
  } else {
    assert(o->is_forwarded(), "Sanity");
    new_obj = o->forwardee();
  }

#ifdef DEBUG
  // This code must come after the CAS test, or it will print incorrect
  // information.
  if (TraceScavenge) {
    gclog_or_tty->print_cr("{%s %s " PTR_FORMAT " -> " PTR_FORMAT " (" SIZE_FORMAT ")}",
       PSScavenge::should_scavenge(&new_obj) ? "copying" : "tenuring",
       new_obj->blueprint()->internal_name(), o, new_obj, new_obj->size());
  }
#endif

  return new_obj;
}

template <class T> void PSPromotionManager::process_array_chunk_work(
                                                 oop obj,
                                                 int start, int end) {
  assert(start <= end, "invariant");
  T* const base      = (T*)objArrayOop(obj)->base();
  T* p               = base + start;
  T* const chunk_end = base + end;
  while (p < chunk_end) {
    if (PSScavenge::should_scavenge(p)) {
      claim_or_forward_depth(p);
    }
    ++p;
  }
}

void PSPromotionManager::process_array_chunk(oop old) {
  assert(PSChunkLargeArrays, "invariant");
  assert(old->is_objArray(), "invariant");
  assert(old->is_forwarded(), "invariant");

  TASKQUEUE_STATS_ONLY(++_array_chunks_processed);

  oop const obj = old->forwardee();

  int start;
  int const end = arrayOop(old)->length();
  if (end > (int) _min_array_size_for_chunking) {
    // we'll chunk more
    start = end - _array_chunk_size;
    assert(start > 0, "invariant");
    arrayOop(old)->set_length(start);
    push_depth(mask_chunked_array_oop(old));
    TASKQUEUE_STATS_ONLY(++_masked_pushes);
  } else {
    // this is the final chunk for this array
    start = 0;
    int const actual_length = arrayOop(obj)->length();
    arrayOop(old)->set_length(actual_length);
  }

  if (UseCompressedOops) {
    process_array_chunk_work<narrowOop>(obj, start, end);
  } else {
    process_array_chunk_work<oop>(obj, start, end);
  }
}

oop PSPromotionManager::oop_promotion_failed(oop obj, markOop obj_mark) {
  assert(_old_gen_is_full || PromotionFailureALot, "Sanity");

  // Attempt to CAS in the header.
  // This tests if the header is still the same as when
  // this started.  If it is the same (i.e., no forwarding
  // pointer has been installed), then this thread owns
  // it.
  if (obj->cas_forward_to(obj, obj_mark)) {
    // We won any races, we "own" this object.
    assert(obj == obj->forwardee(), "Sanity");

    obj->push_contents(this);

    // Save the mark if needed
    PSScavenge::oop_promotion_failed(obj, obj_mark);
  }  else {
    // We lost, someone else "owns" this object
    guarantee(obj->is_forwarded(), "Object must be forwarded if the cas failed.");

    // No unallocation to worry about.
    obj = obj->forwardee();
  }

#ifdef DEBUG
  if (TraceScavenge) {
    gclog_or_tty->print_cr("{%s %s 0x%x (%d)}",
                           "promotion-failure",
                           obj->blueprint()->internal_name(),
                           obj, obj->size());

  }
#endif

  return obj;
}
