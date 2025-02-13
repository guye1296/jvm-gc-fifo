/*
 * Copyright (c) 1999, 2010, Oracle and/or its affiliates. All rights reserved.
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

#ifndef OS_LINUX_VM_OS_LINUX_HPP
#define OS_LINUX_VM_OS_LINUX_HPP

// Linux_OS defines the interface to Linux operating systems

/* pthread_getattr_np comes with LinuxThreads-0.9-7 on RedHat 7.1 */
typedef int (*pthread_getattr_func_type) (pthread_t, pthread_attr_t *);

class Linux {
  friend class os;

  // For signal-chaining
#define MAXSIGNUM 32
  static struct sigaction sigact[MAXSIGNUM]; // saved preinstalled sigactions
  static unsigned int sigs;             // mask of signals that have
                                        // preinstalled signal handlers
  static bool libjsig_is_loaded;        // libjsig that interposes sigaction(),
                                        // __sigaction(), signal() is loaded
  static struct sigaction *(*get_signal_action)(int);
  static struct sigaction *get_preinstalled_handler(int);
  static void save_preinstalled_handler(int, struct sigaction&);

  static void check_signal_handler(int sig);

  // For signal flags diagnostics
  static int sigflags[MAXSIGNUM];

  static int (*_clock_gettime)(clockid_t, struct timespec *);
  static int (*_pthread_getcpuclockid)(pthread_t, clockid_t *);

  static address   _initial_thread_stack_bottom;
  static uintptr_t _initial_thread_stack_size;

  static const char *_glibc_version;
  static const char *_libpthread_version;

  static bool _is_floating_stack;
  static bool _is_NPTL;
  static bool _supports_fast_thread_cpu_time;

  static GrowableArray<int>* _cpu_to_node;

 protected:

  static julong _physical_memory;
  static pthread_t _main_thread;
  static Mutex* _createThread_lock;
  static int _page_size;

  static julong available_memory();
  static julong physical_memory() { return _physical_memory; }
  static void initialize_system_info();

  static void set_glibc_version(const char *s)      { _glibc_version = s; }
  static void set_libpthread_version(const char *s) { _libpthread_version = s; }

  static bool supports_variable_stack_size();

  static void set_is_NPTL()                   { _is_NPTL = true;  }
  static void set_is_LinuxThreads()           { _is_NPTL = false; }
  static void set_is_floating_stack()         { _is_floating_stack = true; }

  static void rebuild_cpu_to_node_map();
  static GrowableArray<int>* cpu_to_node()    { return _cpu_to_node; }

  static bool hugetlbfs_sanity_check(bool warn, size_t page_size);

 public:
  static void init_thread_fpu_state();
  static int  get_fpu_control_word();
  static void set_fpu_control_word(int fpu_control);
  static pthread_t main_thread(void)                                { return _main_thread; }
  // returns kernel thread id (similar to LWP id on Solaris), which can be
  // used to access /proc
  static pid_t gettid();
  static void set_createThread_lock(Mutex* lk)                      { _createThread_lock = lk; }
  static Mutex* createThread_lock(void)                             { return _createThread_lock; }
  static void hotspot_sigmask(Thread* thread);

  static address   initial_thread_stack_bottom(void)                { return _initial_thread_stack_bottom; }
  static uintptr_t initial_thread_stack_size(void)                  { return _initial_thread_stack_size; }
  static bool is_initial_thread(void);

  static int page_size(void)                                        { return _page_size; }
  static void set_page_size(int val)                                { _page_size = val; }

  static address   ucontext_get_pc(ucontext_t* uc);
  static intptr_t* ucontext_get_sp(ucontext_t* uc);
  static intptr_t* ucontext_get_fp(ucontext_t* uc);

  // For Analyzer Forte AsyncGetCallTrace profiling support:
  //
  // This interface should be declared in os_linux_i486.hpp, but
  // that file provides extensions to the os class and not the
  // Linux class.
  static ExtendedPC fetch_frame_from_ucontext(Thread* thread, ucontext_t* uc,
    intptr_t** ret_sp, intptr_t** ret_fp);

  // This boolean allows users to forward their own non-matching signals
  // to JVM_handle_linux_signal, harmlessly.
  static bool signal_handlers_are_installed;

  static int get_our_sigflags(int);
  static void set_our_sigflags(int, int);
  static void signal_sets_init();
  static void install_signal_handlers();
  static void set_signal_handler(int, bool);
  static bool is_sig_ignored(int sig);

  static sigset_t* unblocked_signals();
  static sigset_t* vm_signals();
  static sigset_t* allowdebug_blocked_signals();

  // For signal-chaining
  static struct sigaction *get_chained_signal_action(int sig);
  static bool chained_handler(int sig, siginfo_t* siginfo, void* context);

  // GNU libc and libpthread version strings
  static const char *glibc_version()          { return _glibc_version; }
  static const char *libpthread_version()     { return _libpthread_version; }

  // NPTL or LinuxThreads?
  static bool is_LinuxThreads()               { return !_is_NPTL; }
  static bool is_NPTL()                       { return _is_NPTL;  }

  // NPTL is always floating stack. LinuxThreads could be using floating
  // stack or fixed stack.
  static bool is_floating_stack()             { return _is_floating_stack; }

  static void libpthread_init();
  static bool libnuma_init();
  static void* libnuma_dlsym(void* handle, const char* name);
  // Minimum stack size a thread can be created with (allowing
  // the VM to completely create the thread and enter user code)
  static size_t min_stack_allowed;

  // Return default stack size or guard size for the specified thread type
  static size_t default_stack_size(os::ThreadType thr_type);
  static size_t default_guard_size(os::ThreadType thr_type);

  static void capture_initial_stack(size_t max_size);

  // Stack overflow handling
  static bool manually_expand_stack(JavaThread * t, address addr);
  static int max_register_window_saves_before_flushing();

  // Real-time clock functions
  static void clock_init(void);

  // fast POSIX clocks support
  static void fast_thread_clock_init(void);

  static bool supports_monotonic_clock() {
    return _clock_gettime != NULL;
  }

  static int clock_gettime(clockid_t clock_id, struct timespec *tp) {
    return _clock_gettime ? _clock_gettime(clock_id, tp) : -1;
  }

  static int pthread_getcpuclockid(pthread_t tid, clockid_t *clock_id) {
    return _pthread_getcpuclockid ? _pthread_getcpuclockid(tid, clock_id) : -1;
  }

  static bool supports_fast_thread_cpu_time() {
    return _supports_fast_thread_cpu_time;
  }

  static jlong fast_thread_cpu_time(clockid_t clockid);

  // Stack repair handling

  // none present

  // LinuxThreads work-around for 6292965
  static int safe_cond_timedwait(pthread_cond_t *_cond, pthread_mutex_t *_mutex, const struct timespec *_abstime);


  // Linux suspend/resume support - this helper is a shadow of its former
  // self now that low-level suspension is barely used, and old workarounds
  // for LinuxThreads are no longer needed.
  class SuspendResume {
  private:
    volatile int _suspend_action;
    // values for suspend_action:
    #define SR_NONE               (0x00)
    #define SR_SUSPEND            (0x01)  // suspend request
    #define SR_CONTINUE           (0x02)  // resume request

    volatile jint _state;
    // values for _state: + SR_NONE
    #define SR_SUSPENDED          (0x20)
  public:
    SuspendResume() { _suspend_action = SR_NONE; _state = SR_NONE; }

    int suspend_action() const     { return _suspend_action; }
    void set_suspend_action(int x) { _suspend_action = x;    }

    // atomic updates for _state
    void set_suspended()           {
      jint temp, temp2;
      do {
        temp = _state;
        temp2 = Atomic::cmpxchg(temp | SR_SUSPENDED, &_state, temp);
      } while (temp2 != temp);
    }
    void clear_suspended()        {
      jint temp, temp2;
      do {
        temp = _state;
        temp2 = Atomic::cmpxchg(temp & ~SR_SUSPENDED, &_state, temp);
      } while (temp2 != temp);
    }
    bool is_suspended()            { return _state & SR_SUSPENDED;       }

    #undef SR_SUSPENDED
  };

private:
#ifdef YOUNGGEN_8TIMES
  typedef void (*numa_set_bind_policy_func_t)(int strict);
#endif
  typedef int (*sched_getcpu_func_t)(void);
  typedef int (*numa_node_to_cpus_func_t)(int node, unsigned long *buffer, int bufferlen);
  typedef int (*numa_max_node_func_t)(void);
  typedef int (*numa_available_func_t)(void);
  typedef int (*numa_tonode_memory_func_t)(void *start, size_t size, int node);
  typedef void (*numa_interleave_memory_func_t)(void *start, size_t size, unsigned long *nodemask);
#ifdef NUMA_AWARE_C_HEAP
  typedef void* (*numa_alloc_onnode_func_t)(size_t size, int node);
#endif
  static sched_getcpu_func_t _sched_getcpu;
  static numa_node_to_cpus_func_t _numa_node_to_cpus;
  static numa_max_node_func_t _numa_max_node;
  static numa_available_func_t _numa_available;
  static numa_tonode_memory_func_t _numa_tonode_memory;
  static numa_interleave_memory_func_t _numa_interleave_memory;
#ifdef NUMA_AWARE_C_HEAP
  static numa_alloc_onnode_func_t _numa_alloc_onnode;
#endif
  static unsigned long* _numa_all_nodes;

#ifdef YOUNGGEN_8TIMES
  static numa_set_bind_policy_func_t _numa_set_bind_policy;
  static void set_numa_set_bind_policy(numa_set_bind_policy_func_t func) { _numa_set_bind_policy = func; } 
#endif
  static void set_sched_getcpu(sched_getcpu_func_t func) { _sched_getcpu = func; }
  static void set_numa_node_to_cpus(numa_node_to_cpus_func_t func) { _numa_node_to_cpus = func; }
  static void set_numa_max_node(numa_max_node_func_t func) { _numa_max_node = func; }
  static void set_numa_available(numa_available_func_t func) { _numa_available = func; }
  static void set_numa_tonode_memory(numa_tonode_memory_func_t func) { _numa_tonode_memory = func; }
  static void set_numa_interleave_memory(numa_interleave_memory_func_t func) { _numa_interleave_memory = func; }
  static void set_numa_all_nodes(unsigned long* ptr) { _numa_all_nodes = ptr; }
#ifdef NUMA_AWARE_C_HEAP
  static void set_numa_alloc_onnode(numa_alloc_onnode_func_t func) { _numa_alloc_onnode = func; }
#endif
public:
#ifdef YOUNGGEN_8TIMES
  static void numa_set_bind_policy(int strict) {
    if (_numa_set_bind_policy != NULL) {
      _numa_set_bind_policy(strict);
    }
  }
#endif
  static int sched_getcpu()  { return _sched_getcpu != NULL ? _sched_getcpu() : -1; }
  static int numa_node_to_cpus(int node, unsigned long *buffer, int bufferlen) {
    return _numa_node_to_cpus != NULL ? _numa_node_to_cpus(node, buffer, bufferlen) : -1;
  }
  static int numa_max_node() { return _numa_max_node != NULL ? _numa_max_node() : -1; }
  static int numa_available() { return _numa_available != NULL ? _numa_available() : -1; }
  static int numa_tonode_memory(void *start, size_t size, int node) {
    return _numa_tonode_memory != NULL ? _numa_tonode_memory(start, size, node) : -1;
  }
  static void numa_interleave_memory(void *start, size_t size) {
    if (_numa_interleave_memory != NULL && _numa_all_nodes != NULL) {
      _numa_interleave_memory(start, size, _numa_all_nodes);
    }
  }
#ifdef NUMA_AWARE_C_HEAP
  static void* numa_alloc_onnode(size_t size, int node) {
    return _numa_alloc_onnode != NULL ? _numa_alloc_onnode(size, node) : NULL;
  }
#endif
  static int get_node_by_cpu(int cpu_id);
};


class PlatformEvent : public CHeapObj {
  private:
    double CachePad [4] ;   // increase odds that _mutex is sole occupant of cache line
    volatile int _Event ;
    volatile int _nParked ;
    pthread_mutex_t _mutex  [1] ;
    pthread_cond_t  _cond   [1] ;
    double PostPad  [2] ;
    Thread * _Assoc ;

  public:       // TODO-FIXME: make dtor private
    ~PlatformEvent() { guarantee (0, "invariant") ; }

  public:
    PlatformEvent() {
      int status;
      status = pthread_cond_init (_cond, NULL);
      assert_status(status == 0, status, "cond_init");
      status = pthread_mutex_init (_mutex, NULL);
      assert_status(status == 0, status, "mutex_init");
      _Event   = 0 ;
      _nParked = 0 ;
      _Assoc   = NULL ;
    }

    // Use caution with reset() and fired() -- they may require MEMBARs
    void reset() { _Event = 0 ; }
    int  fired() { return _Event; }
    void park () ;
    void unpark () ;
    int  TryPark () ;
    int  park (jlong millis) ;
    void SetAssociation (Thread * a) { _Assoc = a ; }
} ;

class PlatformParker : public CHeapObj {
  protected:
    pthread_mutex_t _mutex [1] ;
    pthread_cond_t  _cond  [1] ;

  public:       // TODO-FIXME: make dtor private
    ~PlatformParker() { guarantee (0, "invariant") ; }

  public:
    PlatformParker() {
      int status;
      status = pthread_cond_init (_cond, NULL);
      assert_status(status == 0, status, "cond_init");
      status = pthread_mutex_init (_mutex, NULL);
      assert_status(status == 0, status, "mutex_init");
    }
} ;

#endif // OS_LINUX_VM_OS_LINUX_HPP
