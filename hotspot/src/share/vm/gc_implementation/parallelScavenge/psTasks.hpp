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

#ifndef SHARE_VM_GC_IMPLEMENTATION_PARALLELSCAVENGE_PSTASKS_HPP
#define SHARE_VM_GC_IMPLEMENTATION_PARALLELSCAVENGE_PSTASKS_HPP

#include "memory/allocation.hpp"
#include "utilities/growableArray.hpp"

//
// psTasks.hpp is a collection of GCTasks used by the
// parallelScavenge collector.
//

class GCTask;
class OopClosure;
class OopStack;
class ObjectStartArray;
class ParallelTaskTerminator;
class MutableSpace;
class PSOldGen;
class Thread;
class VMThread;
#ifdef INTER_NODE_MSG_Q
class NUMAGlobalTerminator;
#ifdef INTER_NODE_STEALING
class PSPromotionManager;
#endif
#endif
//
// ScavengeRootsTask
//
// This task scans all the roots of a given type.
//
//

class ScavengeRootsTask : public GCTask {
 public:
  enum RootType {
    universe              = 1,
    jni_handles           = 2,
    threads               = 3,
    object_synchronizer   = 4,
    flat_profiler         = 5,
    system_dictionary     = 6,
    management            = 7,
    jvmti                 = 8,
    code_cache            = 9
  };
 private:
  RootType _root_type;
 public:
  ScavengeRootsTask(RootType value) : _root_type(value) {}

  char* name() { return (char *)"scavenge-roots-task"; }

  virtual void do_it(GCTaskManager* manager, uint which);
};

//
// ThreadRootsTask
//
// This task scans the roots of a single thread. This task
// enables scanning of thread roots in parallel.
//

class ThreadRootsTask : public GCTask {
 private:
  JavaThread* _java_thread;
  VMThread* _vm_thread;
 public:
#if defined(NUMA_AWARE_TASKQ) || defined(NUMA_AWARE_C_HEAP)
  ThreadRootsTask(JavaThread* root) : _java_thread(root), _vm_thread(NULL), GCTask((uint) root->lgrp_id()) {}
  ThreadRootsTask(VMThread* root) : _java_thread(NULL), _vm_thread(root), GCTask((uint) root->lgrp_id()) {}
#else
  ThreadRootsTask(JavaThread* root) : _java_thread(root), _vm_thread(NULL) {}
  ThreadRootsTask(VMThread* root) : _java_thread(NULL), _vm_thread(root) {}
#endif
  char* name() { return (char *)"thread-roots-task"; }
#ifdef NUMA_AWARE_C_HEAP
  virtual size_t size() { return sizeof(ThreadRootsTask);}
#endif
  virtual void do_it(GCTaskManager* manager, uint which);
};

//
// StealTask
//
// This task is used to distribute work to idle threads.
//

class StealTask : public GCTask {
 private:
#ifdef INTER_NODE_MSG_Q
   union {
     NUMAGlobalTerminator* const _numa_terminator;
     ParallelTaskTerminator* const _terminator;
   };
   bool _numa_used;
#else
   ParallelTaskTerminator* const _terminator;
#endif
#ifdef INTER_NODE_STEALING
   void do_inter_node_stealing(PSPromotionManager* pm, uint node);
#endif
 public:
#ifdef TERMINATOR_GCTASK
  virtual
#endif
  char* name() { return (char *)"steal-task"; }
#ifdef INTER_NODE_MSG_Q
  StealTask(NUMAGlobalTerminator* t) : _numa_terminator(t), _numa_used(true) {}

  NUMAGlobalTerminator* numa_terminator() { return _numa_terminator; }
  StealTask(ParallelTaskTerminator* t) : _terminator(t), _numa_used(false) {}
  bool numa_used() { return _numa_used; }
#else
  StealTask(ParallelTaskTerminator* t) : _terminator(t) {}
#endif

  ParallelTaskTerminator* terminator() { return _terminator; }
  virtual void do_it(GCTaskManager* manager, uint which);
};

#ifdef TERMINATOR_GCTASK
// This task is very similar to StealTask except that it only has
// the terminator implemented
class TerminatorTask : public StealTask {
 public:
  TerminatorTask(ParallelTaskTerminator* t) : StealTask(t) {}
#ifdef INTER_NODE_MSG_Q
  TerminatorTask(NUMAGlobalTerminator* t) : StealTask(t) {}
#endif
  char* name() { return (char *)"Terminator-task"; }
  virtual void do_it(GCTaskManager* manager, uint which);
};
#endif

//
// SerialOldToYoungRootsTask
//
// This task is used to scan for roots in the perm gen

class SerialOldToYoungRootsTask : public GCTask {
 private:
  PSOldGen* _gen;
  HeapWord* _gen_top;

 public:
  SerialOldToYoungRootsTask(PSOldGen *gen, HeapWord* gen_top) :
    _gen(gen), _gen_top(gen_top) { }

  char* name() { return (char *)"serial-old-to-young-roots-task"; }

  virtual void do_it(GCTaskManager* manager, uint which);
};

//
// OldToYoungRootsTask
//
// This task is used to scan old to young roots in parallel

class OldToYoungRootsTask : public GCTask {
 private:
  PSOldGen* _gen;
  HeapWord* _gen_top;
  uint _stripe_number;

 public:
  OldToYoungRootsTask(PSOldGen *gen, HeapWord* gen_top, uint stripe_number) :
    _gen(gen), _gen_top(gen_top), _stripe_number(stripe_number) { }

  char* name() { return (char *)"old-to-young-roots-task"; }

  virtual void do_it(GCTaskManager* manager, uint which);
};

#endif // SHARE_VM_GC_IMPLEMENTATION_PARALLELSCAVENGE_PSTASKS_HPP
