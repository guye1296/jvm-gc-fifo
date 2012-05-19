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
#include "classfile/systemDictionary.hpp"
#include "code/codeCache.hpp"
#include "gc_implementation/parallelScavenge/cardTableExtension.hpp"
#include "gc_implementation/parallelScavenge/gcTaskManager.hpp"
#include "gc_implementation/parallelScavenge/psMarkSweep.hpp"
#include "gc_implementation/parallelScavenge/psPromotionManager.hpp"
#include "gc_implementation/parallelScavenge/psPromotionManager.inline.hpp"
#include "gc_implementation/parallelScavenge/psScavenge.inline.hpp"
#include "gc_implementation/parallelScavenge/psTasks.hpp"
#include "memory/iterator.hpp"
#include "memory/universe.hpp"
#include "oops/oop.inline.hpp"
#include "oops/oop.psgc.inline.hpp"
#include "runtime/fprofiler.hpp"
#include "runtime/thread.hpp"
#include "runtime/vmThread.hpp"
#include "services/management.hpp"
#include "utilities/taskqueue.hpp"

//
// ScavengeRootsTask
//

void ScavengeRootsTask::do_it(GCTaskManager* manager, uint which) {
  assert(Universe::heap()->is_gc_active(), "called outside gc");

  PSPromotionManager* pm = PSPromotionManager::gc_thread_promotion_manager(which);
  PSScavengeRootsClosure roots_closure(pm);

  switch (_root_type) {
    case universe:
      Universe::oops_do(&roots_closure);
      ReferenceProcessor::oops_do(&roots_closure);
      break;

    case jni_handles:
      JNIHandles::oops_do(&roots_closure);
      break;

    case threads:
    {
      ResourceMark rm;
      Threads::oops_do(&roots_closure, NULL);
    }
    break;

    case object_synchronizer:
      ObjectSynchronizer::oops_do(&roots_closure);
      break;

    case flat_profiler:
      FlatProfiler::oops_do(&roots_closure);
      break;

    case system_dictionary:
      SystemDictionary::oops_do(&roots_closure);
      break;

    case management:
      Management::oops_do(&roots_closure);
      break;

    case jvmti:
      JvmtiExport::oops_do(&roots_closure);
      break;


    case code_cache:
      {
        CodeBlobToOopClosure each_scavengable_code_blob(&roots_closure, /*do_marking=*/ true);
        CodeCache::scavenge_root_nmethods_do(&each_scavengable_code_blob);
      }
      break;

    default:
      fatal("Unknown root type");
  }

  // Do the real work
  pm->drain_stacks(false);
}

//
// ThreadRootsTask
//

void ThreadRootsTask::do_it(GCTaskManager* manager, uint which) {
  assert(Universe::heap()->is_gc_active(), "called outside gc");

  PSPromotionManager* pm = PSPromotionManager::gc_thread_promotion_manager(which);
  PSScavengeRootsClosure roots_closure(pm);
  CodeBlobToOopClosure roots_in_blobs(&roots_closure, /*do_marking=*/ true);
#ifdef INTER_NODE_MSG_Q
  ((GCTaskThread*)Thread::current())->_msg_q_enabled = false;
#endif
  if (_java_thread != NULL)
    _java_thread->oops_do(&roots_closure, &roots_in_blobs);

  if (_vm_thread != NULL)
    _vm_thread->oops_do(&roots_closure, &roots_in_blobs);

  // Do the real work
#ifdef INTER_NODE_MSG_Q
  pm->drain_stacks_depth(true);
  if (UseNUMA && !Thread::current()->is_VM_thread())
    ((GCTaskThread*)Thread::current())->_msg_q_enabled = true;
#else
  pm->drain_stacks(false);
#endif
}

//
// StealTask
//

void StealTask::do_it(GCTaskManager* manager, uint which) {
  assert(Universe::heap()->is_gc_active(), "called outside gc");

  PSPromotionManager* pm =
    PSPromotionManager::gc_thread_promotion_manager(which);
  pm->drain_stacks(true);
  guarantee(pm->stacks_empty(),
            "stacks should be empty at this point");

  int random_seed = 17;
#ifdef INTER_NODE_MSG_Q
  if (UseNUMA && numa_used()) {
    intptr_t gen_counter = 0;
    random_seed = os::random();
    StarTask p;
    while(true) {
      // First try to steal from the message queues.
      while (PSPromotionManager::steal_depth_from_msg_queue(&random_seed,
                                      p, Thread::current()->lgrp_id())) {
#ifdef LOCAL_MSG_PER_THREAD
        msg_t* m = (msg_t*)((oop*)p);
        uint n = 0;
        while (OopStarMessageQueue::dequeue(p, m, n)) {
          TASKQUEUE_STATS_ONLY(pm->record_steal(p));
          pm->process_popped_location_depth(p);
        }
        pm->drain_stacks_depth(true);
      }
#else // !LOCAL_MSG_PER_THREAD
        TASKQUEUE_STATS_ONLY(pm->record_steal(p));
        pm->process_popped_location_depth(p);
      }
      pm->drain_stacks_depth(true);
#endif //LOCAL_MSG_PER_THREAD
      pm->flush_msg_queues();
      // Steal from local object queues now
#ifdef NUMA_AWARE_STEALING
      while(PSPromotionManager::steal_depth(which, &random_seed, p,
                                 Thread::current()->lgrp_id())) {
#else
      while(PSPromotionManager::steal_depth(which, &random_seed, p)) {
#endif
        TASKQUEUE_STATS_ONLY(pm->record_steal(p));
        pm->process_popped_location_depth(p);
        pm->drain_stacks_depth(true);
      }
      pm->flush_msg_queues();
      if (numa_terminator()->offer_termination(gen_counter)) {
        break;
      }
    } //while(true)
    if (((GCTaskThread*)Thread::current())->id_in_node() == 0) {
      //uint i = ((GCTaskThread*)Thread::current())->id_in_node();
      uint i = 0;
      while(i < PSPromotionManager::numa_node_count()) {
        if ((int)i != Thread::current()->lgrp_id()) {
          pm->message_queue(i)->swap_free_lists();
        }
        //i += manager->threads_on_node(Thread::current()->lgrp_id());
        i++;
      }
    }
  } else { //!UseNUMA
#endif //INTER_NODE_MSG_Q
    while(true) {
      StarTask p;
#ifdef NUMA_AWARE_STEALING
      if (PSPromotionManager::steal_depth(which, &random_seed, p,
                                 Thread::current()->lgrp_id())) {
#else
      if (PSPromotionManager::steal_depth(which, &random_seed, p)) {
#endif
        TASKQUEUE_STATS_ONLY(pm->record_steal(p));
        pm->process_popped_location_depth(p);
        pm->drain_stacks_depth(true);
      } else {
        if (terminator()->offer_termination()) {
          break;
        }
      }
    }
#ifdef INTER_NODE_MSG_Q
  }
#endif
  guarantee(pm->stacks_empty(), "stacks should be empty at this point");
}

#ifdef TERMINATOR_GCTASK
void TerminatorTask::do_it(GCTaskManager* manager, uint which) {
  assert(Universe::heap()->is_gc_active(), "called outside gc");
#ifdef INTER_NODE_MSG_Q
  if (numa_used()) {
    intptr_t gen_counter = 0;
    numa_terminator()->offer_termination(gen_counter);
  } else
#endif
  terminator()->offer_termination();
}
#endif

//
// SerialOldToYoungRootsTask
//

void SerialOldToYoungRootsTask::do_it(GCTaskManager* manager, uint which) {
  assert(_gen != NULL, "Sanity");
  assert(_gen->object_space()->contains(_gen_top) || _gen_top == _gen->object_space()->top(), "Sanity");

  {
    PSPromotionManager* pm = PSPromotionManager::gc_thread_promotion_manager(which);

    assert(Universe::heap()->kind() == CollectedHeap::ParallelScavengeHeap, "Sanity");
    CardTableExtension* card_table = (CardTableExtension *)Universe::heap()->barrier_set();
    // FIX ME! Assert that card_table is the type we believe it to be.

    card_table->scavenge_contents(_gen->start_array(),
                                  _gen->object_space(),
                                  _gen_top,
                                  pm);

    // Do the real work
    pm->drain_stacks(false);
  }
}

//
// OldToYoungRootsTask
//

void OldToYoungRootsTask::do_it(GCTaskManager* manager, uint which) {
  assert(_gen != NULL, "Sanity");
  assert(_gen->object_space()->contains(_gen_top) || _gen_top == _gen->object_space()->top(), "Sanity");
  assert(_stripe_number < ParallelGCThreads, "Sanity");

  {
    PSPromotionManager* pm = PSPromotionManager::gc_thread_promotion_manager(which);

    assert(Universe::heap()->kind() == CollectedHeap::ParallelScavengeHeap, "Sanity");
    CardTableExtension* card_table = (CardTableExtension *)Universe::heap()->barrier_set();
    // FIX ME! Assert that card_table is the type we believe it to be.

    card_table->scavenge_contents_parallel(_gen->start_array(),
                                           _gen->object_space(),
                                           _gen_top,
                                           pm,
                                           _stripe_number);

    // Do the real work
    pm->drain_stacks(false);
  }
}
