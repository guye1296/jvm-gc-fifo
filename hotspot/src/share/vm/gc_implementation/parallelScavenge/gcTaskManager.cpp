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

#include "precompiled.hpp"
#include "gc_implementation/parallelScavenge/gcTaskManager.hpp"
#include "gc_implementation/parallelScavenge/gcTaskThread.hpp"
#include "memory/allocation.hpp"
#include "memory/allocation.inline.hpp"
#include "runtime/mutex.hpp"
#include "runtime/mutexLocker.hpp"
#include <stdio.h>

#ifdef REPLACE_MUTEX
#include <sys/syscall.h>
#include <linux/futex.h>
#endif
//
// GCTask
//

extern "C" {
#include "numa_queue.h"
}

const char* GCTask::Kind::to_string(kind value) {
  const char* result = "unknown GCTask kind";
  switch (value) {
  default:
    result = "unknown GCTask kind";
    break;
  case unknown_task:
    result = "unknown task";
    break;
  case ordinary_task:
    result = "ordinary task";
    break;
  case barrier_task:
    result = "barrier task";
    break;
  case noop_task:
    result = "noop task";
    break;
  }
  return result;
};

GCTask::GCTask() :
  _kind(Kind::ordinary_task),
  _affinity(GCTaskManager::sentinel_worker()){
  initialize();
}

GCTask::GCTask(Kind::kind kind) :
  _kind(kind),
  _affinity(GCTaskManager::sentinel_worker()) {
  initialize();
}

GCTask::GCTask(uint affinity) :
  _kind(Kind::ordinary_task),
  _affinity(affinity) {
#ifdef NUMA_AWARE_TASKQ
  assert(!UseNUMA || affinity < os::numa_get_groups_num(),
        "affinity should not be negative");
#endif
  initialize();
}

GCTask::GCTask(Kind::kind kind, uint affinity) :
  _kind(kind),
  _affinity(affinity) {
#ifdef NUMA_AWARE_TASKQ
  assert(!UseNUMA || affinity < os::numa_get_groups_num(),
        "affinity should not be negative");
#endif
  initialize();
}

#ifdef REPLACE_MUTEX
GCTask::~GCTask() {
  // Nothing to do.
}
#endif

void GCTask::initialize() {
#ifndef REPLACE_MUTEX
  _older = NULL;
#endif
  _newer = NULL;
}

void GCTask::destruct() {
#ifndef REPLACE_MUTEX
  assert(older() == NULL, "shouldn't have an older task");
#endif
  assert(newer() == NULL, "shouldn't have a newer task");
  // Nothing to do.
}

NOT_PRODUCT(
void GCTask::print(const char* message) const {
#ifndef REPLACE_MUTEX
  tty->print(INTPTR_FORMAT " <- " INTPTR_FORMAT "(%u) -> " INTPTR_FORMAT,
             newer(), this, affinity(), older());
#endif
}
)

//
// GCTaskQueue
//

GCTaskQueue* GCTaskQueue::create() {
  GCTaskQueue* result = new GCTaskQueue(false);
  if (TraceGCTaskQueue) {
    tty->print_cr("GCTaskQueue::create()"
                  " returns " INTPTR_FORMAT, result);
  }
  return result;
}

GCTaskQueue* GCTaskQueue::create_on_c_heap() {
  GCTaskQueue* result = new(ResourceObj::C_HEAP) GCTaskQueue(true);
  if (TraceGCTaskQueue) {
    tty->print_cr("GCTaskQueue::create_on_c_heap()"
                  " returns " INTPTR_FORMAT,
                  result);
  }
  return result;
}

GCTaskQueue::GCTaskQueue(bool on_c_heap) :
  _is_c_heap_obj(on_c_heap),
  context(NULL)  {
  initialize();
  if (TraceGCTaskQueue) {
    tty->print_cr("[" INTPTR_FORMAT "]"
                  " GCTaskQueue::GCTaskQueue() constructor",
                  this);
  }
}

#ifdef NUMA_AWARE_TASKQ
GCTaskQueue::GCTaskQueue() :
  _is_c_heap_obj(false),
  context(NULL) {
  initialize();
}

GCTaskQueue::~GCTaskQueue() {
  destroy(this);
}
#endif

void GCTaskQueue::destruct() {
    free(context);
}

void GCTaskQueue::destroy(GCTaskQueue* that) {
  if (TraceGCTaskQueue) {
    tty->print_cr("[" INTPTR_FORMAT "]"
                  " GCTaskQueue::destroy()"
                  "  is_c_heap_obj:  %s",
                  that,
                  that->is_c_heap_obj() ? "true" : "false");
  }
  // That instance may have been allocated as a CHeapObj,
  // in which case we have to free it explicitly.
  if (that != NULL) {
    that->destruct();
    assert(that->is_empty(), "should be empty");
    if (that->is_c_heap_obj()) {
      FreeHeap(that);
    }
  }
}

void GCTaskQueue::initialize() {
    if (!_is_c_heap_obj) {
        set_insert_end(NULL);
        set_remove_end(NULL);
        set_length(0);
    }
    else {
        if (context == NULL) {
            context = create_global_context();
        }
        set_length(0);
    }
}

// Enqueue one task.
void GCTaskQueue::orig_enqueue(GCTask* task) {
  if (TraceGCTaskQueue) {
    tty->print_cr("[" INTPTR_FORMAT "]"
                  " GCTaskQueue::enqueue(task: "
                  INTPTR_FORMAT ")",
                  this, task);
    print("before:");
  }
  assert(task != NULL, "shouldn't have null task");
  assert(task->newer() == NULL, "shouldn't be on queue");
  task->set_newer(NULL);
#ifndef REPLACE_MUTEX
  assert(task->older() == NULL, "shouldn't be on queue");
  task->set_older(insert_end());
#endif
  if (is_empty()) {
    set_remove_end(task);
  } else {
    insert_end()->set_newer(task);
  }
  set_insert_end(task);
  increment_length();
  if (TraceGCTaskQueue) {
    print("after:");
  }
}

// Enqueue one task.
void GCTaskQueue::enqueue(GCTask* task) {
    if (!_is_c_heap_obj) {
        orig_enqueue(task);
    }
    else {
        if (TraceGCTaskQueue) {
            tty->print_cr("[" INTPTR_FORMAT "]"
                    " GCTaskQueue::enqueue(task: "
                    INTPTR_FORMAT ")",
                    this, task);
            print("before:");
        }
        assert(task != NULL, "shouldn't have null task");
        numa_enqueue(context, (uint64_t)task, 0);
        increment_length();
        if (TraceGCTaskQueue) {
            print("after:");
        }
    }
}

// Enqueue a whole list of tasks.  Empties the argument list.
void GCTaskQueue::enqueue(GCTaskQueue* list) {
  if (TraceGCTaskQueue) {
    tty->print_cr("[" INTPTR_FORMAT "]"
                  " GCTaskQueue::enqueue(list: "
                  INTPTR_FORMAT ")",
                  this);
    print("before:");
    list->print("list:");
  }
  if (list->is_empty()) {
    // Enqueuing the empty list: nothing to do.
    return;
  }

  uint list_length = list->length();
  GCTask* task;

  for (uint i = 0; i < list_length; ++i) {
      task = list->dequeue();
      enqueue(task);
  }

  set_length(length() + list_length);
  
  list->set_length(0);
  if (TraceGCTaskQueue) {
    print("after:");
    list->print("list:");
  }
}

// Dequeue one task.
GCTask* GCTaskQueue::orig_dequeue() {
  if (TraceGCTaskQueue) {
    tty->print_cr("[" INTPTR_FORMAT "]"
                  " GCTaskQueue::dequeue()", this);
    print("before:");
  }
#ifndef REPLACE_MUTEX
  assert(!is_empty(), "shouldn't dequeue from empty list");
  GCTask* result = remove();
  assert(result != NULL, "shouldn't have NULL task");
#else
  GCTask* result = remove();
#endif
  if (TraceGCTaskQueue) {
    tty->print_cr("    return: " INTPTR_FORMAT, result);
    print("after:");
  }
  return result;
}

// Dequeue one task.
GCTask* GCTaskQueue::dequeue() {
    if (!_is_c_heap_obj) {
        return orig_dequeue();
    }
    else {
        if (TraceGCTaskQueue) {
            tty->print_cr("[" INTPTR_FORMAT "]"
                    " GCTaskQueue::dequeue()", this);
            print("before:");
        }

        GCTask* result = (GCTask*)numa_dequeue(context, 0);
        if (result != NULL) {
            decrement_length();
        }

        if (TraceGCTaskQueue) {
            tty->print_cr("    return: " INTPTR_FORMAT, result);
            print("after:");
        }
        return result;
    }
}

// Dequeue one task, preferring one with affinity.
GCTask* GCTaskQueue::dequeue(uint affinity) {
  if (TraceGCTaskQueue) {
    tty->print_cr("[" INTPTR_FORMAT "]"
                  " GCTaskQueue::dequeue(%u)", this, affinity);
    print("before:");
  }
  assert(!is_empty(), "shouldn't dequeue from empty list");
  // Look down to the next barrier for a task with this affinity.
  GCTask* result = NULL;
  for (GCTask* element = remove_end();
       element != NULL;
       element = element->newer()) {
    if (element->is_barrier_task()) {
      // Don't consider barrier tasks, nor past them.
      result = NULL;
      break;
    }
    if (element->affinity() == affinity) {
      result = remove(element);
      break;
    }
  }
  // If we didn't find anything with affinity, just take the next task.
  if (result == NULL) {
    result = remove();
  }
  if (TraceGCTaskQueue) {
    tty->print_cr("    return: " INTPTR_FORMAT, result);
    print("after:");
  }
  return result;
}
#ifndef REPLACE_MUTEX
GCTask* GCTaskQueue::remove() {
  // Dequeue from remove end.
  GCTask* result = remove_end();
  assert(result != NULL, "shouldn't have null task");
  assert(result->older() == NULL, "not the remove_end");
  set_remove_end(result->newer());
  if (remove_end() == NULL) {
    assert(insert_end() == result, "not a singleton");
    set_insert_end(NULL);
  } else {
    remove_end()->set_older(NULL);
  }
  result->set_newer(NULL);
  decrement_length();
  assert(result->newer() == NULL, "shouldn't be on queue");
  assert(result->older() == NULL, "shouldn't be on queue");
  return result;
}

GCTask* GCTaskQueue::remove(GCTask* task) {
  // This is slightly more work, and has slightly fewer asserts
  // than removing from the remove end.
  assert(task != NULL, "shouldn't have null task");
  GCTask* result = task;
  if (result->newer() != NULL) {
    result->newer()->set_older(result->older());
  } else {
    assert(insert_end() == result, "not youngest");
    set_insert_end(result->older());
  }
  if (result->older() != NULL) {
    result->older()->set_newer(result->newer());
  } else {
    assert(remove_end() == result, "not oldest");
    set_remove_end(result->newer());
  }
  result->set_newer(NULL);
  result->set_older(NULL);
  decrement_length();
  return result;
}
#else
GCTask* GCTaskQueue::remove() {
  // Dequeue from remove end.
  GCTask* temp = remove_end();
  GCTask* result;
  do {
    if (temp == NULL) // Q is empty now.
      return NULL;

    result = temp;
    temp = set_remove_end_atomic(result->newer(), result);
  } while (temp != result);
  assert(result != NULL, "shouldn't have null task");
  if (result == insert_end()) {// The last GCTask has been claimed. Set insert_end to NULL.
    assert(result->newer() == NULL && remove_end() == NULL, "sanity");
    set_insert_end(NULL);
  } else {
    result->set_newer(NULL);
  }
  return result;
}
GCTask* GCTaskQueue::remove(GCTask* task) {
  // We don't support it yet in lock-free code.
}
#endif

NOT_PRODUCT(
void GCTaskQueue::print(const char* message) const {
  tty->print_cr("[" INTPTR_FORMAT "] GCTaskQueue:"
                "  insert_end: " INTPTR_FORMAT
                "  remove_end: " INTPTR_FORMAT
                "  %s",
                this, insert_end(), remove_end(), message);
#ifndef REPLACE_MUTEX
  for (GCTask* element = insert_end();
       element != NULL;
       element = element->older()) {
    element->print("    ");
    tty->cr();
  }
#endif
}
)

//
// SynchronizedGCTaskQueue
//

SynchronizedGCTaskQueue::SynchronizedGCTaskQueue(GCTaskQueue* queue_arg,
                                                 Monitor *       lock_arg) :
  _unsynchronized_queue(queue_arg),
  _lock(lock_arg) {
  assert(unsynchronized_queue() != NULL, "null queue");
  assert(lock() != NULL, "null lock");
}

SynchronizedGCTaskQueue::~SynchronizedGCTaskQueue() {
  // Nothing to do.
}

//
// GCTaskManager
//
GCTaskManager::GCTaskManager(uint workers) :
  _workers(workers),
#ifdef REPLACE_MUTEX
  _futex(0),
  _terminator_idx(0),
#endif
  _ndc(NULL) {
  initialize();
}

GCTaskManager::GCTaskManager(uint workers, NotifyDoneClosure* ndc) :
  _workers(workers),
#ifdef REPLACE_MUTEX
  _futex(0),
  _terminator_idx(0),
#endif
  _ndc(ndc) {
  initialize();
}

void GCTaskManager::initialize() {
  if (TraceGCTaskManager) {
    tty->print_cr("GCTaskManager::initialize: workers: %u", workers());
  }
  assert(workers() != 0, "no workers");
#ifndef REPLACE_MUTEX
  _monitor = new Monitor(Mutex::barrier,                // rank
                         "GCTaskManager monitor",       // name
                         Mutex::_allow_vm_block_flag);  // allow_vm_block
  // The queue for the GCTaskManager must be a CHeapObj.
  GCTaskQueue* unsynchronized_queue = GCTaskQueue::create_on_c_heap();
  _queue = SynchronizedGCTaskQueue::create(unsynchronized_queue, lock());
  _noop_task = NoopGCTask::create_on_c_heap();
  reset_noop_tasks();
#else
  _queue = GCTaskQueue::create_on_c_heap();
#ifdef NUMA_AWARE_TASKQ
  if (UseGCTaskAffinity && UseNUMA) {
    uint n = os::numa_get_groups_num();
    _numa_queues = new (ResourceObj::C_HEAP) GrowableArray<GCTaskQueue*>(n, true);
    for (uint i=0; i < n; i++)
      _numa_queues->append(GCTaskQueue::create_on_c_heap());

    for (uint i=0; i < n; i++)
  } else
    _numa_queues = NULL;
#endif
#ifdef INTER_NODE_MSG_Q
  if (UseNUMA) {
    _numa_terminator[0] = new(-1) NUMAGlobalTerminator(0, NULL, NULL, NULL);
    _numa_terminator[1] = new(-1) NUMAGlobalTerminator(0, NULL, NULL, NULL);
  } else {
    _numa_terminator[0] = new NUMAGlobalTerminator(0, NULL, NULL, NULL);
    _numa_terminator[1] = new NUMAGlobalTerminator(0, NULL, NULL, NULL);
  }
#endif //INTER_NODE_MSG_Q
  _terminator[0] = new ParallelTaskTerminator(0, NULL);
  _terminator[1] = new ParallelTaskTerminator(0, NULL);
#endif
  _resource_flag = NEW_C_HEAP_ARRAY(bool, workers());
  {
    // Set up worker threads.
    //     Distribute the workers among the available processors,
    //     unless we were told not to, or if the os doesn't want to.
    uint* processor_assignment = NEW_C_HEAP_ARRAY(uint, workers());
    if (!BindGCTaskThreadsToCPUs ||
        !os::distribute_processes(workers(), processor_assignment)) {
      for (uint a = 0; a < workers(); a += 1) {
        processor_assignment[a] = sentinel_worker();
      }
    }
    _thread = NEW_C_HEAP_ARRAY(GCTaskThread*, workers());
#ifdef INTER_NODE_MSG_Q
    if (UseNUMA) {
      uint num_nodes = os::numa_get_groups_num();
      _threads_per_node = NEW_C_HEAP_ARRAY(uint, num_nodes);
      for (uint t = 0; t < num_nodes; _threads_per_node[t++] = 0);

      for (uint t = 0; t < workers(); t += 1) {
        uint node = os::Linux::get_node_by_cpu(processor_assignment[t]);
        set_thread(t, GCTaskThread::create(this, t, processor_assignment[t]));
        thread(t)->set_lgrp_id(node);
        thread(t)->set_id_in_node(_threads_per_node[node]++);
      }
    } else
#endif
    for (uint t = 0; t < workers(); t += 1) {
      set_thread(t, GCTaskThread::create(this, t, processor_assignment[t]));
    }
    if (TraceGCTaskThread) {
      tty->print("GCTaskManager::initialize: distribution:");
      for (uint t = 0; t < workers(); t += 1) {
        tty->print("  %u", processor_assignment[t]);
      }
      tty->cr();
    }
    FREE_C_HEAP_ARRAY(uint, processor_assignment);
  }
  reset_busy_workers();
  set_unblocked();
  for (uint w = 0; w < workers(); w += 1) {
    set_resource_flag(w, false);
  }
  reset_delivered_tasks();
  reset_completed_tasks();
  reset_barriers();
  reset_emptied_queue();
  for (uint s = 0; s < workers(); s += 1) {
    thread(s)->start();
  }
}

GCTaskManager::~GCTaskManager() {
  assert(busy_workers() == 0, "still have busy workers");
  assert(queue()->is_empty(), "still have queued work");
  if (_thread != NULL) {
    for (uint i = 0; i < workers(); i += 1) {
      GCTaskThread::destroy(thread(i));
      set_thread(i, NULL);
    }
    FREE_C_HEAP_ARRAY(GCTaskThread*, _thread);
    _thread = NULL;
  }
#ifdef INTER_NODE_MSG_Q
    FREE_C_HEAP_ARRAY(uint, _threads_per_node);
    _threads_per_node = NULL;
#endif
  if (_resource_flag != NULL) {
    FREE_C_HEAP_ARRAY(bool, _resource_flag);
    _resource_flag = NULL;
  }
#ifndef REPLACE_MUTEX
  if (queue() != NULL) {
    GCTaskQueue* unsynchronized_queue = queue()->unsynchronized_queue();
    GCTaskQueue::destroy(unsynchronized_queue);
    SynchronizedGCTaskQueue::destroy(queue());
    _queue = NULL;
  }
  NoopGCTask::destroy(_noop_task);
  _noop_task = NULL;
  if (monitor() != NULL) {
    delete monitor();
    _monitor = NULL;
  }
#else
  if (queue() != NULL) {
    GCTaskQueue::destroy(queue());
    _queue = NULL;
  }
#ifdef NUMA_AWARE_TASKQ
  if (_numa_queues != NULL)
    delete _numa_queues;
#endif
  delete _terminator[0];
  delete _terminator[1];
#ifdef INTER_NODE_MSG_Q
  if (UseNUMA) {
    CHeapObj::operator delete(_numa_terminator[0], sizeof(NUMAGlobalTerminator));
    CHeapObj::operator delete(_numa_terminator[1], sizeof(NUMAGlobalTerminator));
  } else {
    delete _numa_terminator[0];
    delete _numa_terminator[1];
  }
#endif // INTER_NODE_MSG_Q
#endif // REPLACE_MUTEX
}

void GCTaskManager::print_task_time_stamps() {
  for(uint i=0; i<ParallelGCThreads; i++) {
    GCTaskThread* t = thread(i);
    t->print_task_time_stamps();
  }
}

void GCTaskManager::print_threads_on(outputStream* st) {
  uint num_thr = workers();
  for (uint i = 0; i < num_thr; i++) {
    thread(i)->print_on(st);
    st->cr();
  }
}

void GCTaskManager::threads_do(ThreadClosure* tc) {
  assert(tc != NULL, "Null ThreadClosure");
  uint num_thr = workers();
  for (uint i = 0; i < num_thr; i++) {
    tc->do_thread(thread(i));
  }
}

GCTaskThread* GCTaskManager::thread(uint which) {
  assert(which < workers(), "index out of bounds");
  assert(_thread[which] != NULL, "shouldn't have null thread");
  return _thread[which];
}

void GCTaskManager::set_thread(uint which, GCTaskThread* value) {
  assert(which < workers(), "index out of bounds");
  assert(value != NULL, "shouldn't have null thread");
  _thread[which] = value;
}

void GCTaskManager::add_task(GCTask* task) {
  assert(task != NULL, "shouldn't have null task");
#ifndef REPLACE_MUTEX
  MutexLockerEx ml(monitor(), Mutex::_no_safepoint_check_flag);
#endif
  if (TraceGCTaskManager) {
    tty->print_cr("GCTaskManager::add_task(" INTPTR_FORMAT " [%s])",
                  task, GCTask::Kind::to_string(task->kind()));
  }
  queue()->enqueue(task);
#ifndef REPLACE_MUTEX
  // Notify with the lock held to avoid missed notifies.
  if (TraceGCTaskManager) {
    tty->print_cr("    GCTaskManager::add_task (%s)->notify_all",
                  monitor()->name());
  }
  (void) monitor()->notify_all();
  // Release monitor().
#else
  _futex++;
  syscall(SYS_futex, futex_addr(), FUTEX_WAKE_PRIVATE, workers(), 0, 0, 0);
#endif
}

#ifdef EXTRA_COUNTERS
#ifdef NUMA_AWARE_TASKQ
void GCTaskManager::add_list(GCTaskQueue* list, bool start_gc, bool have_affinity_tasks) {
#else
void GCTaskManager::add_list(GCTaskQueue* list, bool start_gc) {
#endif
#else
#ifdef NUMA_AWARE_TASKQ
void GCTaskManager::add_list(GCTaskQueue* list, bool have_affinity_tasks) {
#else
void GCTaskManager::add_list(GCTaskQueue* list) {
#endif
#endif
  assert(list != NULL, "shouldn't have null task");
#ifndef REPLACE_MUTEX
  MutexLockerEx ml(monitor(), Mutex::_no_safepoint_check_flag);
#endif
  if (TraceGCTaskManager) {
    tty->print_cr("GCTaskManager::add_list(%u)", list->length());
  }
#ifdef NUMA_AWARE_TASKQ
  if (UseGCTaskAffinity && UseNUMA && have_affinity_tasks) {
    GCTaskQueue q;
    GCTaskQueue numa_q[_numa_queues->length()];
    GCTask* task = list->dequeue();
    while (task != NULL) {
      if (task->affinity() >= (uint) _numa_queues->length())
        q.enqueue(task);
      else
        numa_q[task->affinity()].enqueue(task);
      task = list->dequeue();
    }
    for (int i = 0; i < _numa_queues->length(); i++) {
      _numa_queues->at(i)->enqueue(numa_q + i);
    }
    queue()->enqueue(&q);
    list->initialize();
  } else
#endif
  queue()->enqueue(list);

#ifdef EXTRA_COUNTERS
  if (start_gc) {
#endif
#ifndef REPLACE_MUTEX
  // Notify with the lock held to avoid missed notifies.
  if (TraceGCTaskManager) {
    tty->print_cr("    GCTaskManager::add_list (%s)->notify_all",
                  monitor()->name());
  }
  (void) monitor()->notify_all();
  // Release monitor().
#else
    _futex++;
    syscall(SYS_futex, futex_addr(), FUTEX_WAKE_PRIVATE, workers(), 0, 0, 0);
#endif
#ifdef EXTRA_COUNTERS
  }
#endif
}

#ifdef REPLACE_MUTEX
GCTask* GCTaskManager::get_task(uint which) {
  GCTask* result = NULL;
tryagain:
  if (is_blocked()) {
    // Wait while the queue is blocked
    do {
      if (TraceGCTaskManager) {
        tty->print_cr("GCTaskManager::get_task(%u)"
                    "  blocked: %s"
                    "  empty: %s"
                    "  release: %s",
                    which,
                    is_blocked() ? "true" : "false",
                    queue()->is_empty() ? "true" : "false",
                    should_release_resources(which) ? "true" : "false");
      }
      syscall(SYS_futex, futex_addr(), FUTEX_WAIT_PRIVATE,
             ((GCTaskThread*)Thread::current())->futex_ts(), 0, 0, 0);
      ((GCTaskThread*)Thread::current())->inc_futex_ts();
    } while (is_blocked());
  }
#ifdef NUMA_AWARE_TASKQ
  if (UseGCTaskAffinity && UseNUMA) {
    int lgrp_id = thread(which)->lgrp_id();
    result = _numa_queues->at(lgrp_id)->dequeue();
    if (result == NULL)
      result = queue()->dequeue();
  } else
#endif
  result = queue()->dequeue();
  if (result) {
    if (result->is_barrier_task()) {
      assert(which != sentinel_worker(),
             "blocker shouldn't be bogus");
      set_blocking_worker(which);
      OrderAccess::fence();
    }
    if (TraceGCTaskManager) {
      tty->print_cr("GCTaskManager::get_task(%u) => " INTPTR_FORMAT " [%s]",
                  which, result, GCTask::Kind::to_string(result->kind()));
      tty->print_cr("     %s", result->name());
    }
  } else {
    // The queue is empty.
    if (should_release_resources(which)) {
      return NoopGCTask::create();
    }
    if (TraceGCTaskManager) {
      tty->print_cr("GCTaskManager::get_task(%u)"
                    "  blocked: %s"
                    "  empty: %s"
                    "  release: %s",
                    which,
                    is_blocked() ? "true" : "false",
                    queue()->is_empty() ? "true" : "false",
                    should_release_resources(which) ? "true" : "false");
    }
    syscall(SYS_futex, futex_addr(), FUTEX_WAIT_PRIVATE,
           ((GCTaskThread*)Thread::current())->futex_ts(), 0, 0, 0);
    ((GCTaskThread*)Thread::current())->inc_futex_ts();
    goto tryagain;
  }

  assert (result != NULL, "There must be a valid GCTask at this point!");
  return result;
}
#else
GCTask* GCTaskManager::get_task(uint which) {
  GCTask* result = NULL;
  // Grab the queue lock.
  MutexLockerEx ml(monitor(), Mutex::_no_safepoint_check_flag);
  // Wait while the queue is block or
  // there is nothing to do, except maybe release resources.
  while (is_blocked() ||
         (queue()->is_empty() && !should_release_resources(which))) {
    if (TraceGCTaskManager) {
      tty->print_cr("GCTaskManager::get_task(%u)"
                    "  blocked: %s"
                    "  empty: %s"
                    "  release: %s",
                    which,
                    is_blocked() ? "true" : "false",
                    queue()->is_empty() ? "true" : "false",
                    should_release_resources(which) ? "true" : "false");
      tty->print_cr("    => (%s)->wait()",
                    monitor()->name());
    }
    monitor()->wait(Mutex::_no_safepoint_check_flag, 0);
  }
  // We've reacquired the queue lock here.
  // Figure out which condition caused us to exit the loop above.
  if (!queue()->is_empty()) {
    if (UseGCTaskAffinity) {
      result = queue()->dequeue(which);
    } else {
      result = queue()->dequeue();
    }
    if (result->is_barrier_task()) {
      assert(which != sentinel_worker(),
             "blocker shouldn't be bogus");
      set_blocking_worker(which);
    }
  } else {
    // The queue is empty, but we were woken up.
    // Just hand back a Noop task,
    // in case someone wanted us to release resources, or whatever.
    result = noop_task();
    increment_noop_tasks();
  }
  assert(result != NULL, "shouldn't have null task");
  if (TraceGCTaskManager) {
    tty->print_cr("GCTaskManager::get_task(%u) => " INTPTR_FORMAT " [%s]",
                  which, result, GCTask::Kind::to_string(result->kind()));
    tty->print_cr("     %s", result->name());
  }
  increment_busy_workers();
  increment_delivered_tasks();
  return result;
  // Release monitor().
}
#endif

void GCTaskManager::note_completion(uint which) {
#ifndef REPLACE_MUTEX
  MutexLockerEx ml(monitor(), Mutex::_no_safepoint_check_flag);
#endif
  if (TraceGCTaskManager) {
    tty->print_cr("GCTaskManager::note_completion(%u)", which);
  }
  // If we are blocked, check if the completing thread is the blocker.
  if (blocking_worker() == which) {
    assert(blocking_worker() != sentinel_worker(),
           "blocker shouldn't be bogus");
    increment_barriers();
    set_unblocked();
#ifdef REPLACE_MUTEX
    _futex++;
    syscall(SYS_futex, futex_addr(), FUTEX_WAKE_PRIVATE, workers(), 0, 0, 0);
  }
  assert(notify_done_closure() == NULL, "We dont support ndc thing in the lock-free implementation");
  if (TraceGCTaskManager) {
#else
  }
  increment_completed_tasks();
  uint active = decrement_busy_workers();
  if ((active == 0) && (queue()->is_empty())) {
    increment_emptied_queue();
    if (TraceGCTaskManager) {
      tty->print_cr("    GCTaskManager::note_completion(%u) done", which);
    }
    // Notify client that we are done.
    NotifyDoneClosure* ndc = notify_done_closure();
    if (ndc != NULL) {
      ndc->notify(this);
    }
  }
  if (TraceGCTaskManager) {
    tty->print_cr("    GCTaskManager::note_completion(%u) (%s)->notify_all",
                  which, monitor()->name());
#endif
    tty->print_cr("  "
                  "  blocked: %s"
                  "  empty: %s"
                  "  release: %s",
                  is_blocked() ? "true" : "false",
                  queue()->is_empty() ? "true" : "false",
                  should_release_resources(which) ? "true" : "false");
    tty->print_cr("  "
                  "  delivered: %u"
                  "  completed: %u"
                  "  barriers: %u"
                  "  emptied: %u",
                  delivered_tasks(),
                  completed_tasks(),
                  barriers(),
                  emptied_queue());
  }
#ifndef REPLACE_MUTEX
  // Tell everyone that a task has completed.
  (void) monitor()->notify_all();
  // Release monitor().
#endif
}

uint GCTaskManager::increment_busy_workers() {
#ifndef REPLACE_MUTEX
  assert(queue()->own_lock(), "don't own the lock");
#endif
  _busy_workers += 1;
  return _busy_workers;
}

uint GCTaskManager::decrement_busy_workers() {
#ifndef REPLACE_MUTEX
  assert(queue()->own_lock(), "don't own the lock");
#endif
  _busy_workers -= 1;
  return _busy_workers;
}

void GCTaskManager::release_all_resources() {
  // If you want this to be done atomically, do it in a BarrierGCTask.
  for (uint i = 0; i < workers(); i += 1) {
    set_resource_flag(i, true);
  }
}

bool GCTaskManager::should_release_resources(uint which) {
  // This can be done without a lock because each thread reads one element.
  return resource_flag(which);
}

void GCTaskManager::note_release(uint which) {
  // This can be done without a lock because each thread writes one element.
  set_resource_flag(which, false);
}

void GCTaskManager::execute_and_wait(GCTaskQueue* list) {
  WaitForBarrierGCTask* fin = WaitForBarrierGCTask::create();
  list->enqueue(fin);
  add_list(list);
  fin->wait_for();
#ifndef REPLACE_MUTEX
  // We have to release the barrier tasks!
  WaitForBarrierGCTask::destroy(fin);
#endif
}

bool GCTaskManager::resource_flag(uint which) {
  assert(which < workers(), "index out of bounds");
  return _resource_flag[which];
}

void GCTaskManager::set_resource_flag(uint which, bool value) {
  assert(which < workers(), "index out of bounds");
  _resource_flag[which] = value;
}

//
// NoopGCTask
//

NoopGCTask* NoopGCTask::create() {
  NoopGCTask* result = new NoopGCTask(false);
  return result;
}

NoopGCTask* NoopGCTask::create_on_c_heap() {
#ifdef REPLACE_MUTEX
  NoopGCTask* result = new NoopGCTask(true);
#else
  NoopGCTask* result = new(ResourceObj::C_HEAP) NoopGCTask(true);
#endif
  return result;
}

void NoopGCTask::destroy(NoopGCTask* that) {
  if (that != NULL) {
#ifndef REPLACE_MUTEX
    that->destruct();
    if (that->is_c_heap_obj()) {
      FreeHeap(that);
    }
#else
    delete that;
#endif
  }
}

void NoopGCTask::destruct() {
  // This has to know it's superclass structure, just like the constructor.
  this->GCTask::destruct();
  // Nothing else to do.
}

//
// BarrierGCTask
//

void BarrierGCTask::do_it(GCTaskManager* manager, uint which) {
  // Wait for this to be the only busy worker.
  // ??? I thought of having a StackObj class
  //     whose constructor would grab the lock and come to the barrier,
  //     and whose destructor would release the lock,
  //     but that seems like too much mechanism for two lines of code.
#ifndef REPLACE_MUTEX
  MutexLockerEx ml(manager->lock(), Mutex::_no_safepoint_check_flag);
#endif
  do_it_internal(manager, which);
  // Release manager->lock().
}

void BarrierGCTask::do_it_internal(GCTaskManager* manager, uint which) {
#ifndef REPLACE_MUTEX
  // Wait for this to be the only busy worker.
  assert(manager->monitor()->owned_by_self(), "don't own the lock");
  assert(manager->is_blocked(), "manager isn't blocked");
  while (manager->busy_workers() > 1) {
    if (TraceGCTaskManager) {
      tty->print_cr("BarrierGCTask::do_it(%u) waiting on %u workers",
                    which, manager->busy_workers());
    }
    manager->monitor()->wait(Mutex::_no_safepoint_check_flag, 0);
  }
#endif
}

void BarrierGCTask::destruct() {
  this->GCTask::destruct();
  // Nothing else to do.
}

//
// ReleasingBarrierGCTask
//

void ReleasingBarrierGCTask::do_it(GCTaskManager* manager, uint which) {
#ifndef REPLACE_MUTEX
  MutexLockerEx ml(manager->lock(), Mutex::_no_safepoint_check_flag);
#endif
  do_it_internal(manager, which);
  manager->release_all_resources();
  // Release manager->lock().
}

void ReleasingBarrierGCTask::destruct() {
  this->BarrierGCTask::destruct();
  // Nothing else to do.
}

//
// NotifyingBarrierGCTask
//

void NotifyingBarrierGCTask::do_it(GCTaskManager* manager, uint which) {
#ifndef REPLACE_MUTEX
  MutexLockerEx ml(manager->lock(), Mutex::_no_safepoint_check_flag);
#endif
  do_it_internal(manager, which);
  NotifyDoneClosure* ndc = notify_done_closure();
  if (ndc != NULL) {
    ndc->notify(manager);
  }
  // Release manager->lock().
}

void NotifyingBarrierGCTask::destruct() {
  this->BarrierGCTask::destruct();
  // Nothing else to do.
}

//
// WaitForBarrierGCTask
//
WaitForBarrierGCTask* WaitForBarrierGCTask::create() {
  WaitForBarrierGCTask* result = new WaitForBarrierGCTask(false);
  return result;
}

WaitForBarrierGCTask* WaitForBarrierGCTask::create_on_c_heap() {
  WaitForBarrierGCTask* result = new WaitForBarrierGCTask(true);
  return result;
}

WaitForBarrierGCTask::WaitForBarrierGCTask(bool on_c_heap) :
#ifdef REPLACE_MUTEX
  _is_c_heap_obj(on_c_heap),
  _futex(0),
  BarrierGCTask(false) {
#else
  _is_c_heap_obj(on_c_heap) {
  _monitor = MonitorSupply::reserve();
  if (TraceGCTaskManager) {
    tty->print_cr("[" INTPTR_FORMAT "]"
                  " WaitForBarrierGCTask::WaitForBarrierGCTask()"
                  "  monitor: " INTPTR_FORMAT,
                  this, monitor());
  }
#endif
  set_should_wait(true);
}

void WaitForBarrierGCTask::destroy(WaitForBarrierGCTask* that) {
  if (that != NULL) {
    if (TraceGCTaskManager) {
#ifndef REPLACE_MUTEX
      tty->print_cr("[" INTPTR_FORMAT "]"
                    " WaitForBarrierGCTask::destroy()"
                    "  is_c_heap_obj: %s"
                    "  monitor: " INTPTR_FORMAT,
                    that,
                    that->is_c_heap_obj() ? "true" : "false",
                    that->monitor());
#endif
    }
    that->destruct();
    if (that->is_c_heap_obj()) {
      FreeHeap(that);
    }
  }
}

void WaitForBarrierGCTask::destruct() {
#ifndef REPLACE_MUTEX
  assert(monitor() != NULL, "monitor should not be NULL");
  if (TraceGCTaskManager) {
    tty->print_cr("[" INTPTR_FORMAT "]"
                  " WaitForBarrierGCTask::destruct()"
                  "  monitor: " INTPTR_FORMAT,
                  this, monitor());
  }
  this->BarrierGCTask::destruct();
  // Clean up that should be in the destructor,
  // except that ResourceMarks don't call destructors.
   if (monitor() != NULL) {
     MonitorSupply::release(monitor());
  }
  _monitor = (Monitor*) 0xDEAD000F;
#endif
}

void WaitForBarrierGCTask::do_it(GCTaskManager* manager, uint which) {
#ifndef REPLACE_MUTEX
  if (TraceGCTaskManager) {
    tty->print_cr("[" INTPTR_FORMAT "]"
                  " WaitForBarrierGCTask::do_it() waiting for idle"
                  "  monitor: " INTPTR_FORMAT,
                  this, monitor());
  }
  {
    // First, wait for the barrier to arrive.
    MutexLockerEx ml(manager->lock(), Mutex::_no_safepoint_check_flag);
    do_it_internal(manager, which);
    // Release manager->lock().
  }
  {
    // Then notify the waiter.
    MutexLockerEx ml(monitor(), Mutex::_no_safepoint_check_flag);
#endif
    set_should_wait(false);
#ifdef REPLACE_MUTEX
    _futex++;
    syscall(SYS_futex, futex_addr(), FUTEX_WAKE_PRIVATE, 1, 0, 0, 0);
#else
    // Waiter doesn't miss the notify in the wait_for method
    // since it checks the flag after grabbing the monitor.
    if (TraceGCTaskManager) {
      tty->print_cr("[" INTPTR_FORMAT "]"
                    " WaitForBarrierGCTask::do_it()"
                    "  [" INTPTR_FORMAT "] (%s)->notify_all()",
                    this, monitor(), monitor()->name());
    }
    monitor()->notify_all();
    // Release monitor().
  }
#endif
}

void WaitForBarrierGCTask::wait_for() {
  if (TraceGCTaskManager) {
    tty->print_cr("[" INTPTR_FORMAT "]"
                  " WaitForBarrierGCTask::wait_for()"
      "  should_wait: %s",
      this, should_wait() ? "true" : "false");
  }
  {
#ifndef REPLACE_MUTEX
    // Grab the lock and check again.
    MutexLockerEx ml(monitor(), Mutex::_no_safepoint_check_flag);
#endif
    while (should_wait()) {
#ifndef REPLACE_MUTEX
      if (TraceGCTaskManager) {
        tty->print_cr("[" INTPTR_FORMAT "]"
                      " WaitForBarrierGCTask::wait_for()"
          "  [" INTPTR_FORMAT "] (%s)->wait()",
          this, monitor(), monitor()->name());
      }
      monitor()->wait(Mutex::_no_safepoint_check_flag, 0);
#else
      syscall(SYS_futex, futex_addr(), FUTEX_WAIT_PRIVATE, 0, 0, 0, 0);
#endif
    }
    // Reset the flag in case someone reuses this task.
    set_should_wait(true);
    if (TraceGCTaskManager) {
      tty->print_cr("[" INTPTR_FORMAT "]"
                    " WaitForBarrierGCTask::wait_for() returns"
        "  should_wait: %s",
        this, should_wait() ? "true" : "false");
    }
    // Release monitor().
  }
}

Mutex*                   MonitorSupply::_lock     = NULL;
GrowableArray<Monitor*>* MonitorSupply::_freelist = NULL;

Monitor* MonitorSupply::reserve() {
  Monitor* result = NULL;
  // Lazy initialization: possible race.
  if (lock() == NULL) {
    _lock = new Mutex(Mutex::barrier,                  // rank
                      "MonitorSupply mutex",           // name
                      Mutex::_allow_vm_block_flag);    // allow_vm_block
  }
  {
    MutexLockerEx ml(lock());
    // Lazy initialization.
    if (freelist() == NULL) {
      _freelist =
        new(ResourceObj::C_HEAP) GrowableArray<Monitor*>(ParallelGCThreads,
                                                         true);
    }
    if (! freelist()->is_empty()) {
      result = freelist()->pop();
    } else {
      result = new Monitor(Mutex::barrier,                  // rank
                           "MonitorSupply monitor",         // name
                           Mutex::_allow_vm_block_flag);    // allow_vm_block
    }
    guarantee(result != NULL, "shouldn't return NULL");
    assert(!result->is_locked(), "shouldn't be locked");
    // release lock().
  }
  return result;
}

void MonitorSupply::release(Monitor* instance) {
  assert(instance != NULL, "shouldn't release NULL");
  assert(!instance->is_locked(), "shouldn't be locked");
  {
    MutexLockerEx ml(lock());
    freelist()->push(instance);
    // release lock().
  }
}
