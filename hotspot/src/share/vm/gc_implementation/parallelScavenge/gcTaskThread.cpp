
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
#include "memory/resourceArea.hpp"
#include "runtime/handles.hpp"
#include "runtime/handles.inline.hpp"
#include "runtime/os.hpp"
#include "runtime/thread.hpp"

GCTaskThread::GCTaskThread(GCTaskManager* manager,
                           uint           which,
                           uint           processor_id) :
  _manager(manager),
  _processor_id(processor_id),
  _time_stamps(NULL),
#ifdef REPLACE_MUTEX
  _futex_ts(0),
#endif
  _time_stamp_index(0)
{
  if (!os::create_thread(this, os::pgc_thread))
    vm_exit_out_of_memory(0, "Cannot create GC thread. Out of system resources.");

  if (PrintGCTaskTimeStamps) {
    _time_stamps = NEW_C_HEAP_ARRAY(GCTaskTimeStamp, GCTaskTimeStampEntries );

    guarantee(_time_stamps != NULL, "Sanity");
  }
  set_id(which);
  set_name("GC task thread#%d (ParallelGC)", which);
}

GCTaskThread::~GCTaskThread() {
  if (_time_stamps != NULL) {
    FREE_C_HEAP_ARRAY(GCTaskTimeStamp, _time_stamps);
  }
}

void GCTaskThread::start() {
  os::start_thread(this);
}

GCTaskTimeStamp* GCTaskThread::time_stamp_at(uint index) {
  guarantee(index < GCTaskTimeStampEntries, "increase GCTaskTimeStampEntries");

  return &(_time_stamps[index]);
}

void GCTaskThread::print_task_time_stamps() {
  assert(PrintGCTaskTimeStamps, "Sanity");
  assert(_time_stamps != NULL, "Sanity (Probably set PrintGCTaskTimeStamps late)");

  tty->print_cr("GC-Thread %u entries: %d", id(), _time_stamp_index);
  for(uint i=0; i<_time_stamp_index; i++) {
    GCTaskTimeStamp* time_stamp = time_stamp_at(i);
    tty->print_cr("\t[ %s " INT64_FORMAT " " INT64_FORMAT " ]",
                  time_stamp->name(),
                  time_stamp->entry_time(),
                  time_stamp->exit_time());
  }

  // Reset after dumping the data
  _time_stamp_index = 0;
}

void GCTaskThread::print_on(outputStream* st) const {
  st->print("\"%s\" ", name());
  Thread::print_on(st);
  st->cr();
}

void GCTaskThread::run() {
  // Set up the thread for stack overflow support
  this->record_stack_base_and_size();
  this->initialize_thread_local_storage();
  // Bind yourself to your processor.
  if (processor_id() != GCTaskManager::sentinel_worker()) {
    if (TraceGCTaskThread) {
      tty->print_cr("GCTaskThread::run: "
                    "  binding to processor %u", processor_id());
    }
    if (!os::bind_to_processor(processor_id())) {
      DEBUG_ONLY(
        warning("Couldn't bind GCTaskThread %u to processor %u",
                      which(), processor_id());
      )
    }
  }
#ifdef THREAD_AFFINITY
  if (UseNUMA)
    Thread::current()->set_lgrp_id(os::numa_get_group_id());
#endif
#ifdef INTER_NODE_MSG_Q
  _msg_q_enabled = true;
#endif

  // Part of thread setup.
  // ??? Are these set up once here to make subsequent ones fast?
  HandleMark   hm_outer;
  ResourceMark rm_outer;

  TimeStamp timer;

  for (;/* ever */;) {
    // These are so we can flush the resources allocated in the inner loop.
    HandleMark   hm_inner;
    ResourceMark rm_inner;
    for (; /* break */; ) {
      // This will block until there is a task to be gotten.
      GCTask* task = manager()->get_task(which());

      // In case the update is costly
      if (PrintGCTaskTimeStamps) {
        timer.update();
      }

      jlong entry_time = timer.ticks();
      char* name = task->name();

      task->do_it(manager(), which());
#ifdef NUMA_AWARE_C_HEAP
      size_t s = task->size();
      if (s > sizeof(GCTask) && task->affinity() != GCTaskManager::sentinel_worker()) {
        GCTask::operator delete(task, s);
      }
#ifdef REPLACE_MUTEX
      else
#endif
#endif
#ifdef REPLACE_MUTEX
      delete task;
#endif
      manager()->note_completion(which());

      if (PrintGCTaskTimeStamps) {
        assert(_time_stamps != NULL, "Sanity (PrintGCTaskTimeStamps set late?)");

        timer.update();

        GCTaskTimeStamp* time_stamp = time_stamp_at(_time_stamp_index++);

        time_stamp->set_name(name);
        time_stamp->set_entry_time(entry_time);
        time_stamp->set_exit_time(timer.ticks());
      }

      // Check if we should release our inner resources.
      if (manager()->should_release_resources(which())) {
        manager()->note_release(which());
        break;
      }
    }
  }
}
