/*
 * Copyright (c) 1997, 2010, Oracle and/or its affiliates. All rights reserved.
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
#include "code/icBuffer.hpp"
#include "gc_interface/collectedHeap.hpp"
#include "interpreter/bytecodes.hpp"
#include "memory/universe.hpp"
#include "prims/methodHandles.hpp"
#include "runtime/handles.inline.hpp"
#include "runtime/icache.hpp"
#include "runtime/init.hpp"
#include "runtime/safepoint.hpp"
#include "runtime/sharedRuntime.hpp"

// Initialization done by VM thread in vm_init_globals()
void check_ThreadShadow();
void eventlog_init();
void mutex_init();
void chunkpool_init();
void perfMemory_init();

// Initialization done by Java thread in init_globals()
void management_init();
void bytecodes_init();
void classLoader_init();
void codeCache_init();
void VM_Version_init();
void stubRoutines_init1();
jint universe_init();  // dependent on codeCache_init and stubRoutines_init
void interpreter_init();  // before any methods loaded
void invocationCounter_init();  // before any methods loaded
void marksweep_init();
void accessFlags_init();
void templateTable_init();
void InterfaceSupport_init();
void universe2_init();  // dependent on codeCache_init and stubRoutines_init
void referenceProcessor_init();
void jni_handles_init();
void vmStructs_init();

void vtableStubs_init();
void InlineCacheBuffer_init();
void compilerOracle_init();
void compilationPolicy_init();


// Initialization after compiler initialization
bool universe_post_init();  // must happen after compiler_init
void javaClasses_init();  // must happen after vtable initialization
void stubRoutines_init2(); // note: StubRoutines need 2-phase init

// Do not disable thread-local-storage, as it is important for some
// JNI/JVM/JVMTI functions and signal handlers to work properly
// during VM shutdown
void perfMemory_exit();
void ostream_exit();

void vm_init_globals() {
  check_ThreadShadow();
  basic_types_init();
  eventlog_init();
  mutex_init();
  chunkpool_init();
  perfMemory_init();
}


jint init_globals() {
  HandleMark hm;
  management_init();
  bytecodes_init();
  classLoader_init();
  codeCache_init();
  VM_Version_init();
  stubRoutines_init1();
  jint status = universe_init();  // dependent on codeCache_init and stubRoutines_init
  if (status != JNI_OK)
    return status;

  interpreter_init();  // before any methods loaded
  invocationCounter_init();  // before any methods loaded
  marksweep_init();
  accessFlags_init();
  templateTable_init();
  InterfaceSupport_init();
  SharedRuntime::generate_stubs();
  universe2_init();  // dependent on codeCache_init and stubRoutines_init
  referenceProcessor_init();
  jni_handles_init();
#ifndef VM_STRUCTS_KERNEL
  vmStructs_init();
#endif // VM_STRUCTS_KERNEL

  vtableStubs_init();
  InlineCacheBuffer_init();
  compilerOracle_init();
  compilationPolicy_init();
  VMRegImpl::set_regName();

  if (!universe_post_init()) {
    return JNI_ERR;
  }
  javaClasses_init();  // must happen after vtable initialization
  stubRoutines_init2(); // note: StubRoutines need 2-phase init

  // Although we'd like to, we can't easily do a heap verify
  // here because the main thread isn't yet a JavaThread, so
  // its TLAB may not be made parseable from the usual interfaces.
  if (VerifyBeforeGC && !UseTLAB &&
      Universe::heap()->total_collections() >= VerifyGCStartAt) {
    Universe::heap()->prepare_for_verify();
    Universe::verify();   // make sure we're starting with a clean slate
  }

  // All the flags that get adjusted by VM_Version_init and os::init_2
  // have been set so dump the flags now.
  if (PrintFlagsFinal) {
    CommandLineFlags::printFlags();
  }

  return JNI_OK;
}

extern unsigned long total_safepoint_time;
unsigned long vm_init_time;
#ifdef EXTRA_COUNTERS
unsigned long young_gc_time;
unsigned long young_par_time;
unsigned long young_resize_time;
unsigned long young_weak_ref_time;
size_t young_work_done;
unsigned young_gc_count;
unsigned old_gc_count;
unsigned long total_objects_copied = 0;
unsigned long total_remote_sent = 0;
#endif
void exit_globals() {
  static bool destructorsCalled = false;
  if (!destructorsCalled) {
    destructorsCalled = true;
    perfMemory_exit();
    if (PrintSafepointStatistics) {
      // Print the collected safepoint statistics.
      SafepointSynchronize::print_stat_on_exit();
    }
#ifdef EXTRA_COUNTERS
    tty->print_cr("Young GC Count: %u\nOld GC Count: %u", young_gc_count, old_gc_count);
    tty->print_cr("Young work done: %lu", young_work_done);
    tty->print_cr("Total object copied: %lu", total_objects_copied);
    tty->print_cr("Total remote sent: %lu", total_remote_sent);
    tty->print_cr("Young resize time: %lu\nYoung weak ref time: %lu", young_resize_time/1000000, young_weak_ref_time/1000000);
    tty->print_cr("Young || time: %lu\nYoung GC Time: %lu", young_par_time/1000000, young_gc_time/1000000);
    tty->print_cr("Total GC Time: %lu\nTotal Execution Time: %lu",total_safepoint_time, os::javaTimeMillis() - vm_init_time);
#else
    tty->print_cr("%lu\n%lu",total_safepoint_time, os::javaTimeMillis() - vm_init_time);
#endif
    ostream_exit();
  }
}


static bool _init_completed = false;

bool is_init_completed() {
  return _init_completed;
}


void set_init_completed() {
  assert(Universe::is_fully_initialized(), "Should have completed initialization");
  _init_completed = true;
  vm_init_time = os::javaTimeMillis();
}
