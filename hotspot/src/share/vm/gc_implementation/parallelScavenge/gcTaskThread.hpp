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

#ifndef SHARE_VM_GC_IMPLEMENTATION_PARALLELSCAVENGE_GCTASKTHREAD_HPP
#define SHARE_VM_GC_IMPLEMENTATION_PARALLELSCAVENGE_GCTASKTHREAD_HPP

#include "runtime/thread.hpp"
#ifdef INTER_NODE_MSG_Q
#include "utilities/taskqueue.hpp"
#endif
// Forward declarations of classes defined here.
class GCTaskThread;
class GCTaskTimeStamp;

// Declarations of classes referenced in this file via pointer.
class GCTaskManager;

class GCTaskThread : public WorkerThread {
private:
  // Instance state.
  GCTaskManager* _manager;              // Manager for worker.
  const uint     _processor_id;         // Which processor the worker is on.
#ifdef REPLACE_MUTEX
  int            _futex_ts;             // For replacing mutex of GCTaskQueue with futex
#endif
  GCTaskTimeStamp* _time_stamps;
  uint _time_stamp_index;

  GCTaskTimeStamp* time_stamp_at(uint index);
#ifdef INTER_NODE_MSG_Q
  uint _id_in_node; // Maintains a thread id within node
 public:
  msg_t** local_msg; // Msg holding a locally claimed msg.
  uint id_in_node()            { return _id_in_node;}
  void set_id_in_node(uint id) { _id_in_node = id;}
#ifdef INTER_NODE_STEALING
  intptr_t* msg_count;
#endif
#else
 public:
#endif
  // Factory create and destroy methods.
  static GCTaskThread* create(GCTaskManager* manager,
                              uint           which,
                              uint           processor_id) {
#ifdef NUMA_AWARE_C_HEAP
    if (UseNUMA) {
      int lgrp_id = os::Linux::get_node_by_cpu(processor_id);
      GCTaskThread* t = new(lgrp_id) GCTaskThread(manager, which, processor_id);
#ifdef INTER_NODE_MSG_Q
      int node_count = os::numa_get_groups_num();
      t->local_msg = NUMA_NEW_C_HEAP_ARRAY(msg_t*, node_count, lgrp_id);
#ifdef INTER_NODE_STEALING
      t->msg_count = NUMA_NEW_C_HEAP_ARRAY(intptr_t, node_count, lgrp_id);
      memset(t->msg_count, 0, sizeof(intptr_t) * node_count);
#endif
      memset(t->local_msg, 0, sizeof(msg_t*) * node_count);
#endif
      return t;
    }
#endif
    return new GCTaskThread(manager, which, processor_id);
  }
  static void destroy(GCTaskThread* manager) {
    if (manager != NULL) {
#ifdef NUMA_AWARE_C_HEAP
      if (UseNUMA) {
#ifdef INTER_NODE_MSG_Q
        NUMA_FREE_C_HEAP_ARRAY(msg_t*, manager->local_msg, os::numa_get_groups_num());
#ifdef INTER_NODE_STEALING
        NUMA_FREE_C_HEAP_ARRAY(intptr_t, manager->msg_count, os::numa_get_groups_num());
#endif
#endif
        Thread::operator delete(manager, sizeof(GCTaskThread));
      } else
        Thread::operator delete(manager);
#else
      delete manager;
#endif
    }
  }
  // Methods from Thread.
  bool is_GC_task_thread() const {
    return true;
  }
  virtual void run();
  // Methods.
  void start();

#ifdef REPLACE_MUTEX
  int futex_ts()                                    { return _futex_ts; }
  void inc_futex_ts()                               { _futex_ts++;      }
#endif

  void print_task_time_stamps();
  void print_on(outputStream* st) const;
  void print() const                                { print_on(tty); }
#ifdef BANDWIDTH_TEST
  unsigned long bytes_read, bytes_write;
  uint64_t bytes_time;
#endif
protected:
  // Constructor.  Clients use factory, but there could be subclasses.
  GCTaskThread(GCTaskManager* manager, uint which, uint processor_id);
  // Destructor: virtual destructor because of virtual methods.
  virtual ~GCTaskThread();
  // Accessors.
  GCTaskManager* manager() const {
    return _manager;
  }
  uint which() const {
    return id();
  }
  uint processor_id() const {
    return _processor_id;
  }
};

class GCTaskTimeStamp : public CHeapObj
{
 private:
  jlong  _entry_time;
  jlong  _exit_time;
  char*  _name;

 public:
  jlong entry_time()              { return _entry_time; }
  jlong exit_time()               { return _exit_time; }
  const char* name() const        { return (const char*)_name; }

  void set_entry_time(jlong time) { _entry_time = time; }
  void set_exit_time(jlong time)  { _exit_time = time; }
  void set_name(char* name)       { _name = name; }
};

#endif // SHARE_VM_GC_IMPLEMENTATION_PARALLELSCAVENGE_GCTASKTHREAD_HPP
