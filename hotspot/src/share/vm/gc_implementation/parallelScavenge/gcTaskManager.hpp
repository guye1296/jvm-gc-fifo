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

#ifndef SHARE_VM_GC_IMPLEMENTATION_PARALLELSCAVENGE_GCTASKMANAGER_HPP
#define SHARE_VM_GC_IMPLEMENTATION_PARALLELSCAVENGE_GCTASKMANAGER_HPP

#include "runtime/mutex.hpp"
#include "utilities/growableArray.hpp"

#ifdef REPLACE_MUTEX
#include "utilities/taskqueue.hpp"
#endif
#ifdef INTER_NODE_MSG_Q
#include "gc_implementation/parallelScavenge/psTaskTerminator.hpp"
#endif
//
// The GCTaskManager is a queue of GCTasks, and accessors
// to allow the queue to be accessed from many threads.
//

// Forward declarations of types defined in this file.
class GCTask;
class GCTaskQueue;
class SynchronizedGCTaskQueue;
class GCTaskManager;
class NotifyDoneClosure;
// Some useful subclasses of GCTask.  You can also make up your own.
class NoopGCTask;
class BarrierGCTask;
class ReleasingBarrierGCTask;
class NotifyingBarrierGCTask;
class WaitForBarrierGCTask;
// A free list of Monitor*'s.
class MonitorSupply;

// Forward declarations of classes referenced in this file via pointer.
class GCTaskThread;
class Mutex;
class Monitor;
class ThreadClosure;

// The abstract base GCTask.
#ifdef REPLACE_MUTEX
class GCTask : public CHeapObj {
#else
#ifdef NUMA_AWARE_C_HEAP
class GCTask : public CHeapObj, public ResourceObj {
#else
class GCTask : public ResourceObj {
#endif
#endif
public:
  // Known kinds of GCTasks, for predicates.
  class Kind : AllStatic {
  public:
    enum kind {
      unknown_task,
      ordinary_task,
      barrier_task,
      noop_task
    };
    static const char* to_string(kind value);
  };
private:
  // Instance state.
  const Kind::kind _kind;               // For runtime type checking.
  const uint       _affinity;           // Which worker should run task.
  GCTask*          _newer;              // Tasks are on doubly-linked ...
#ifndef REPLACE_MUTEX
  GCTask*          _older;              // ... lists.
#endif
public:
  virtual char* name() { return (char *)"task"; }
#ifdef NUMA_AWARE_C_HEAP
  virtual size_t size() { return sizeof(GCTask);}
#endif
  // Abstract do_it method
  virtual void do_it(GCTaskManager* manager, uint which) = 0;
  // Accessors
  Kind::kind kind() const {
    return _kind;
  }
  uint affinity() const {
    return _affinity;
  }
  GCTask* newer() const {
    return _newer;
  }
  void set_newer(GCTask* n) {
    _newer = n;
  }
#ifndef REPLACE_MUTEX
  GCTask* older() const {
    return _older;
  }
  void set_older(GCTask* p) {
    _older = p;
  }
#ifdef NUMA_AWARE_C_HEAP 
  //new and delete operators
  void* operator new(size_t size) { return ResourceObj::operator new(size);}
  void* operator new(size_t size, allocation_type type) {
    return ResourceObj::operator new(size, type);
  }
  void* operator new(size_t size, Arena* arena) {
    return ResourceObj::operator new(size, arena);
  }
  void operator delete(void* p) { ResourceObj::operator delete(p);}
  //for numa-aware allocation.
  void* operator new(size_t size, bool dummy, int lgrp_id) {
    return CHeapObj::operator new(size, lgrp_id);
  }
  void operator delete(void* p, size_t size) {
    CHeapObj::operator delete(p, size);
  }
#endif
#else
  virtual ~GCTask();
#endif
  // Predicates.
  bool is_ordinary_task() const {
    return kind()==Kind::ordinary_task;
  }
  bool is_barrier_task() const {
    return kind()==Kind::barrier_task;
  }
  bool is_noop_task() const {
    return kind()==Kind::noop_task;
  }
  void print(const char* message) const PRODUCT_RETURN;
protected:
  // Constructors: Only create subclasses.
  //     An ordinary GCTask.
  GCTask();
  //     A GCTask of a particular kind, usually barrier or noop.
  GCTask(Kind::kind kind);
  //     An ordinary GCTask with an affinity.
  GCTask(uint affinity);
  //     A GCTask of a particular kind, with and affinity.
  GCTask(Kind::kind kind, uint affinity);
  // We want a virtual destructor because virtual methods,
  // but since ResourceObj's don't have their destructors
  // called, we don't have one at all.  Instead we have
  // this method, which gets called by subclasses to clean up.
  virtual void destruct();
  // Methods.
  void initialize();
};

// A doubly-linked list of GCTasks.
// The list is not synchronized, because sometimes we want to
// build up a list and then make it available to other threads.
// See also: SynchronizedGCTaskQueue.
class GCTaskQueue : public ResourceObj {
private:
  // Instance state.
  GCTask*    _insert_end;               // Tasks are enqueued at this end.
  GCTask*    _remove_end;               // Tasks are dequeued from this end.
  uint       _length;                   // The current length of the queue.
  const bool _is_c_heap_obj;            // Is this a CHeapObj?
public:
  // Factory create and destroy methods.
  //     Create as ResourceObj.
  static GCTaskQueue* create();
  //     Create as CHeapObj.
  static GCTaskQueue* create_on_c_heap();
  //     Destroyer.
  static void destroy(GCTaskQueue* that);
#ifdef NUMA_AWARE_TASKQ
  GCTaskQueue();
  ~GCTaskQueue();
#endif
  // Accessors.
  //     These just examine the state of the queue.
  bool is_empty() const {
    assert(((insert_end() == NULL && remove_end() == NULL) ||
            (insert_end() != NULL && remove_end() != NULL)),
           "insert_end and remove_end don't match");
#ifndef REPLACE_MUTEX
    return insert_end() == NULL;
#else
    return remove_end() == NULL;
#endif
  }

  uint length() const {
    return _length;
  }
  // Methods.
  void initialize();
  //     Enqueue one task.
  void enqueue(GCTask* task);
  //     Enqueue a list of tasks.  Empties the argument list.
  void enqueue(GCTaskQueue* list);
  //     Dequeue one task.
  GCTask* dequeue();
  //     Dequeue one task, preferring one with affinity.
  GCTask* dequeue(uint affinity);
protected:
  // Constructor. Clients use factory, but there might be subclasses.
  GCTaskQueue(bool on_c_heap);
  // Destructor-like method.
  // Because ResourceMark doesn't call destructors.
  // This method cleans up like one.
  virtual void destruct();
  // Accessors.
  GCTask* insert_end() const {
    return _insert_end;
  }
  void set_insert_end(GCTask* value) {
    _insert_end = value;
  }
  GCTask* remove_end() const {
    return _remove_end;
  }
  void set_remove_end(GCTask* value) {
    _remove_end = value;
  }
#ifdef REPLACE_MUTEX
  GCTask* set_remove_end_atomic(GCTask* new_val, GCTask* old_val) {
    return (GCTask*) Atomic::cmpxchg_ptr(new_val, &_remove_end, old_val);
  }
#endif
  void increment_length() {
    _length += 1;
  }
  void decrement_length() {
    _length -= 1;
  }
  void set_length(uint value) {
    _length = value;
  }
  bool is_c_heap_obj() const {
    return _is_c_heap_obj;
  }
  // Methods.
  GCTask* remove();                     // Remove from remove end.
  GCTask* remove(GCTask* task);         // Remove from the middle.
  void print(const char* message) const PRODUCT_RETURN;
};

// A GCTaskQueue that can be synchronized.
// This "has-a" GCTaskQueue and a mutex to do the exclusion.
class SynchronizedGCTaskQueue : public CHeapObj {
private:
  // Instance state.
  GCTaskQueue* _unsynchronized_queue;   // Has-a unsynchronized queue.
  Monitor *    _lock;                   // Lock to control access.
public:
  // Factory create and destroy methods.
  static SynchronizedGCTaskQueue* create(GCTaskQueue* queue, Monitor * lock) {
    return new SynchronizedGCTaskQueue(queue, lock);
  }
  static void destroy(SynchronizedGCTaskQueue* that) {
    if (that != NULL) {
      delete that;
    }
  }
  // Accessors
  GCTaskQueue* unsynchronized_queue() const {
    return _unsynchronized_queue;
  }
  Monitor * lock() const {
    return _lock;
  }
  // GCTaskQueue wrapper methods.
  // These check that you hold the lock
  // and then call the method on the queue.
  bool is_empty() const {
    guarantee(own_lock(), "don't own the lock");
    return unsynchronized_queue()->is_empty();
  }
  void enqueue(GCTask* task) {
    guarantee(own_lock(), "don't own the lock");
    unsynchronized_queue()->enqueue(task);
  }
  void enqueue(GCTaskQueue* list) {
    guarantee(own_lock(), "don't own the lock");
    unsynchronized_queue()->enqueue(list);
  }
  GCTask* dequeue() {
    guarantee(own_lock(), "don't own the lock");
    return unsynchronized_queue()->dequeue();
  }
  GCTask* dequeue(uint affinity) {
    guarantee(own_lock(), "don't own the lock");
    return unsynchronized_queue()->dequeue(affinity);
  }
  uint length() const {
    guarantee(own_lock(), "don't own the lock");
    return unsynchronized_queue()->length();
  }
  // For guarantees.
  bool own_lock() const {
    return lock()->owned_by_self();
  }
protected:
  // Constructor.  Clients use factory, but there might be subclasses.
  SynchronizedGCTaskQueue(GCTaskQueue* queue, Monitor * lock);
  // Destructor.  Not virtual because no virtuals.
  ~SynchronizedGCTaskQueue();
};

// This is an abstract base class for getting notifications
// when a GCTaskManager is done.
class NotifyDoneClosure : public CHeapObj {
public:
  // The notification callback method.
  virtual void notify(GCTaskManager* manager) = 0;
protected:
  // Constructor.
  NotifyDoneClosure() {
    // Nothing to do.
  }
  // Virtual destructor because virtual methods.
  virtual ~NotifyDoneClosure() {
    // Nothing to do.
  }
};

class GCTaskManager : public CHeapObj {
 friend class ParCompactionManager;
 friend class PSParallelCompact;
 friend class PSScavenge;
 friend class PSRefProcTaskExecutor;
 friend class RefProcTaskExecutor;
private:
  // Instance state.
  NotifyDoneClosure*        _ndc;               // Notify on completion.
  const uint                _workers;           // Number of workers.
#ifdef REPLACE_MUTEX
  int                       _futex;
  GCTaskQueue*              _queue;
#ifdef NUMA_AWARE_TASKQ
  GrowableArray<GCTaskQueue*>* _numa_queues;    // This is a list of NUMA queues.
                                                // The above list is for non-affine tasks.
#endif
#ifdef INTER_NODE_MSG_Q
  NUMAGlobalTerminator*     _numa_terminator[2];
#endif
  ParallelTaskTerminator*   _terminator[2];
  uint                      _terminator_idx;
#else
  Monitor*                  _monitor;           // Notification of changes.
  SynchronizedGCTaskQueue*  _queue;             // Queue of tasks.
  NoopGCTask*               _noop_task;         // The NoopGCTask instance.
  uint                      _noop_tasks;        // Count of noop tasks.
#endif
#ifdef INTER_NODE_MSG_Q
  uint*                     _threads_per_node;  // Array holding number of threads per node.
#endif
  GCTaskThread**            _thread;            // Array of worker threads.
  uint                      _busy_workers;      // Number of busy workers.
  uint                      _blocking_worker;   // The worker that's blocking.
  bool*                     _resource_flag;     // Array of flag per threads.
  uint                      _delivered_tasks;   // Count of delivered tasks.
  uint                      _completed_tasks;   // Count of completed tasks.
  uint                      _barriers;          // Count of barrier tasks.
  uint                      _emptied_queue;     // Times we emptied the queue.
public:
  // Factory create and destroy methods.
  static GCTaskManager* create(uint workers) {
    return new GCTaskManager(workers);
  }
  static GCTaskManager* create(uint workers, NotifyDoneClosure* ndc) {
    return new GCTaskManager(workers, ndc);
  }
  static void destroy(GCTaskManager* that) {
    if (that != NULL) {
      delete that;
    }
  }
  // Accessors.
  uint busy_workers() const {
    return _busy_workers;
  }
#ifdef INTER_NODE_MSG_Q
  uint threads_on_node(uint node) {
    return _threads_per_node[node];
  }
#endif
#ifndef REPLACE_MUTEX
  //     Pun between Monitor* and Mutex*
  Monitor* monitor() const {
    return _monitor;
  }
  Monitor * lock() const {
    return _monitor;
  }
#else
#ifdef INTER_NODE_MSG_Q
  NUMAGlobalTerminator* terminator(intptr_t tc, TaskQueueSetSuper* msg_qs,
                                   TaskQueueSetSuper* local_qs,
                                   GrowableArray<NUMANodeLocalTerminator*>* nt) {
    _terminator_idx %= 2;
    _numa_terminator[_terminator_idx]->initialize(tc, msg_qs, local_qs, nt);
    return _numa_terminator[_terminator_idx++];
  }
#endif
  ParallelTaskTerminator* terminator(int n_threads, TaskQueueSetSuper* queue_set) {
    _terminator_idx %= 2;
    _terminator[_terminator_idx]->initialize(n_threads, queue_set);
    return _terminator[_terminator_idx++];
  }
#endif
  // Methods.
  GCTaskThread* thread(uint which);
  //     Add the argument task to be run.
  void add_task(GCTask* task);
  //     Add a list of tasks.  Removes task from the argument list.
#ifdef EXTRA_COUNTERS
#ifdef NUMA_AWARE_TASKQ
  void add_list(GCTaskQueue* list, bool start_gc = true, bool have_affinity_tasks = false);
#else
  void add_list(GCTaskQueue* list, bool start_gc = true);
#endif
#else
#ifdef NUMA_AWARE_TASKQ
  void add_list(GCTaskQueue* list, bool have_affinity_tasks = false);
#else
  void add_list(GCTaskQueue* list);
#endif
#endif
  //     Claim a task for argument worker.
  GCTask* get_task(uint which);
  //     Note the completion of a task by the argument worker.
  void note_completion(uint which);
  //     Is the queue blocked from handing out new tasks?
  bool is_blocked() const {
    return (blocking_worker() != sentinel_worker());
  }
  //     Request that all workers release their resources.
  void release_all_resources();
  //     Ask if a particular worker should release its resources.
  bool should_release_resources(uint which); // Predicate.
  //     Note the release of resources by the argument worker.
  void note_release(uint which);
  // Constants.
  //     A sentinel worker identifier.
  static uint sentinel_worker() {
    return (uint) -1;                   // Why isn't there a max_uint?
  }

  //     Execute the task queue and wait for the completion.
  void execute_and_wait(GCTaskQueue* list);

  void print_task_time_stamps();
  void print_threads_on(outputStream* st);
  void threads_do(ThreadClosure* tc);

protected:
  // Constructors.  Clients use factory, but there might be subclasses.
  //     Create a GCTaskManager with the appropriate number of workers.
  GCTaskManager(uint workers);
  //     Create a GCTaskManager that calls back when there's no more work.
  GCTaskManager(uint workers, NotifyDoneClosure* ndc);
  //     Make virtual if necessary.
  ~GCTaskManager();
  // Accessors.
  uint workers() const {
    return _workers;
  }
  NotifyDoneClosure* notify_done_closure() const {
    return _ndc;
  }
#ifndef REPLACE_MUTEX
  NoopGCTask* noop_task() const {
    return _noop_task;
  }
  SynchronizedGCTaskQueue* queue() const {
    return _queue;
  }
  //     Count of the number of noop tasks we've handed out,
  //     e.g., to handle resource release requests.
  uint noop_tasks() const {
    return _noop_tasks;
  }
  void increment_noop_tasks() {
    _noop_tasks += 1;
  }
  void reset_noop_tasks() {
    _noop_tasks = 0;
  }
#else
  int* futex_addr() {
    return &_futex;
  }
  GCTaskQueue* queue() const {
    return _queue;
  }
#endif
  
  //     Bounds-checking per-thread data accessors.
  void set_thread(uint which, GCTaskThread* value);
  bool resource_flag(uint which);
  void set_resource_flag(uint which, bool value);
  // Modifier methods with some semantics.
  //     Is any worker blocking handing out new tasks?
  uint blocking_worker() const {
    return _blocking_worker;
  }
  void set_blocking_worker(uint value) {
    _blocking_worker = value;
  }
  void set_unblocked() {
    set_blocking_worker(sentinel_worker());
  }
  //     Count of busy workers.
  void reset_busy_workers() {
    _busy_workers = 0;
  }
  uint increment_busy_workers();
  uint decrement_busy_workers();
  //     Count of tasks delivered to workers.
  uint delivered_tasks() const {
    return _delivered_tasks;
  }
  void increment_delivered_tasks() {
    _delivered_tasks += 1;
  }
  void reset_delivered_tasks() {
    _delivered_tasks = 0;
  }
  //     Count of tasks completed by workers.
  uint completed_tasks() const {
    return _completed_tasks;
  }
  void increment_completed_tasks() {
    _completed_tasks += 1;
  }
  void reset_completed_tasks() {
    _completed_tasks = 0;
  }
  //     Count of barrier tasks completed.
  uint barriers() const {
    return _barriers;
  }
  void increment_barriers() {
    _barriers += 1;
  }
  void reset_barriers() {
    _barriers = 0;
  }
  //     Count of how many times the queue has emptied.
  uint emptied_queue() const {
    return _emptied_queue;
  }
  void increment_emptied_queue() {
    _emptied_queue += 1;
  }
  void reset_emptied_queue() {
    _emptied_queue = 0;
  }
  // Other methods.
  void initialize();
};

//
// Some exemplary GCTasks.
//

// A noop task that does nothing,
// except take us around the GCTaskThread loop.
class NoopGCTask : public GCTask {
private:
  const bool _is_c_heap_obj;            // Is this a CHeapObj?
public:
  // Factory create and destroy methods.
  static NoopGCTask* create();
  static NoopGCTask* create_on_c_heap();
  static void destroy(NoopGCTask* that);
  // Methods from GCTask.
  void do_it(GCTaskManager* manager, uint which) {
    // Nothing to do.
  }
protected:
  // Constructor.
  NoopGCTask(bool on_c_heap) :
    GCTask(GCTask::Kind::noop_task),
    _is_c_heap_obj(on_c_heap) {
    // Nothing to do.
  }
  // Destructor-like method.
  void destruct();
  // Accessors.
  bool is_c_heap_obj() const {
    return _is_c_heap_obj;
  }
};

// A BarrierGCTask blocks other tasks from starting,
// and waits until it is the only task running.
class BarrierGCTask : public GCTask {
public:
  // Factory create and destroy methods.
  static BarrierGCTask* create() {
    return new BarrierGCTask();
  }
  static void destroy(BarrierGCTask* that) {
    if (that != NULL) {
      that->destruct();
      delete that;
    }
  }
  // Methods from GCTask.
  void do_it(GCTaskManager* manager, uint which);
protected:
  // Constructor.  Clients use factory, but there might be subclasses.
  BarrierGCTask() :
    GCTask(GCTask::Kind::barrier_task) {
    // Nothing to do.
  }
#ifdef REPLACE_MUTEX
  BarrierGCTask(bool dummy) : // We don't need to make the last task barrier.
    GCTask() {
    // Nothing to do.
  }
#endif
  // Destructor-like method.
  void destruct();
  // Methods.
  //     Wait for this to be the only task running.
  void do_it_internal(GCTaskManager* manager, uint which);
};

// A ReleasingBarrierGCTask is a BarrierGCTask
// that tells all the tasks to release their resource areas.
class ReleasingBarrierGCTask : public BarrierGCTask {
public:
  // Factory create and destroy methods.
  static ReleasingBarrierGCTask* create() {
    return new ReleasingBarrierGCTask();
  }
  static void destroy(ReleasingBarrierGCTask* that) {
    if (that != NULL) {
      that->destruct();
      delete that;
    }
  }
  // Methods from GCTask.
  void do_it(GCTaskManager* manager, uint which);
protected:
  // Constructor.  Clients use factory, but there might be subclasses.
  ReleasingBarrierGCTask() :
    BarrierGCTask() {
    // Nothing to do.
  }
  // Destructor-like method.
  void destruct();
};

// A NotifyingBarrierGCTask is a BarrierGCTask
// that calls a notification method when it is the only task running.
class NotifyingBarrierGCTask : public BarrierGCTask {
private:
  // Instance state.
  NotifyDoneClosure* _ndc;              // The callback object.
public:
  // Factory create and destroy methods.
  static NotifyingBarrierGCTask* create(NotifyDoneClosure* ndc) {
    return new NotifyingBarrierGCTask(ndc);
  }
  static void destroy(NotifyingBarrierGCTask* that) {
    if (that != NULL) {
      that->destruct();
      delete that;
    }
  }
  // Methods from GCTask.
  void do_it(GCTaskManager* manager, uint which);
protected:
  // Constructor.  Clients use factory, but there might be subclasses.
  NotifyingBarrierGCTask(NotifyDoneClosure* ndc) :
    BarrierGCTask(),
    _ndc(ndc) {
    assert(notify_done_closure() != NULL, "can't notify on NULL");
  }
  // Destructor-like method.
  void destruct();
  // Accessor.
  NotifyDoneClosure* notify_done_closure() const { return _ndc; }
};

// A WaitForBarrierGCTask is a BarrierGCTask
// with a method you can call to wait until
// the BarrierGCTask is done.
// This may cover many of the uses of NotifyingBarrierGCTasks.
class WaitForBarrierGCTask : public BarrierGCTask {
private:
  // Instance state.
#ifdef REPLACE_MUTEX
  int        _futex;
#else
  Monitor*   _monitor;                  // Guard and notify changes.
#endif
  bool       _should_wait;              // true=>wait, false=>proceed.
  const bool _is_c_heap_obj;            // Was allocated on the heap.
public:
  virtual char* name() { return (char *) "waitfor-barrier-task"; }

  // Factory create and destroy methods.
  static WaitForBarrierGCTask* create();
  static WaitForBarrierGCTask* create_on_c_heap();
  static void destroy(WaitForBarrierGCTask* that);
  // Methods.
  void     do_it(GCTaskManager* manager, uint which);
  void     wait_for();
protected:
  // Constructor.  Clients use factory, but there might be subclasses.
  WaitForBarrierGCTask(bool on_c_heap);
  // Destructor-like method.
  void destruct();
  // Accessors.
#ifdef REPLACE_MUTEX
  int* futex_addr() {
    return &_futex;
  }
#else
  Monitor* monitor() const {
    return _monitor;
  }
#endif
  bool should_wait() const {
    return _should_wait;
  }
  void set_should_wait(bool value) {
    _should_wait = value;
  }
  bool is_c_heap_obj() {
    return _is_c_heap_obj;
  }
};

class MonitorSupply : public AllStatic {
private:
  // State.
  //     Control multi-threaded access.
  static Mutex*                   _lock;
  //     The list of available Monitor*'s.
  static GrowableArray<Monitor*>* _freelist;
public:
  // Reserve a Monitor*.
  static Monitor* reserve();
  // Release a Monitor*.
  static void release(Monitor* instance);
private:
  // Accessors.
  static Mutex* lock() {
    return _lock;
  }
  static GrowableArray<Monitor*>* freelist() {
    return _freelist;
  }
};

#endif // SHARE_VM_GC_IMPLEMENTATION_PARALLELSCAVENGE_GCTASKMANAGER_HPP
