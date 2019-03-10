#ifndef _CONFIG_H_

#define _CONFIG_H_

// Definition: USE_CPUS
// --------------------
// Define the number of processing cores that your computation
// system offers or the maximum number of cores that you like to use.
#ifndef USE_CPUS
#    define USE_CPUS               80
#endif

// Defenition: N_THREADS
// ---------------------
// Define the number of threads that you like to run experiments.
// In case N_THREADS > USE_CPUS, two or more threads may run in
// any processing core.
#ifndef N_THREADS
#    define N_THREADS              80
#endif

// Defenition: THREADS_PACKED
// ---------------------
// Define how to pin threads to cores and sockets
#define THREADS_PACKED

// Conversion from cycles to microseconds
// --------------------------------------
// THIS IS HARDCODED FOR A 2.4 GHZ PROCESSOR.
#define MICRO_TO_CYCLES(m)      (((m * 1000ull) * 24) / 10)

// Location of thread NUMA cluster
// -------------------------------
// THIS IS HARDCODED FOR A MACHINE WITH 4 CLUSTERS, EACH OF
// WHICH HAS 20 THREADS.  WE ASSUME THREADS ARE INTERLEAVED
// ACROSS THE CLUSTERS IN A ROUND-ROBIN MANNER.
#if USE_CPUS <= 20
#define NUMBER_OF_CLUSTERS                1
#elif USE_CPUS <= 40
#define NUMBER_OF_CLUSTERS                2
#elif USE_CPUS <= 60
#define NUMBER_OF_CLUSTERS                3
#else
#define NUMBER_OF_CLUSTERS                4
#endif

static inline int pid_to_cluster(const int pid) __attribute__ ((pure));
static inline int pid_to_cluster(const int pid) {
    return pid % NUMBER_OF_CLUSTERS;
}

// Defenition: MAX_WORK
// --------------------
// Define the maximum local work that each thread executes 
// between two calls of some simulated shared object's
// operation. A zero value means no work between two calls.
// The exact value depends on the speed of processing cores.
// Try not to use big values (avoiding slow contention)
// or not to use small values (avoiding long runs and
// unrelistic cache misses ratios).
#ifndef MAX_WORK
#define MAX_WORK                   64
#endif

// definition: WORK_IN_NANO
// ------------------------
// If defined, MAX_WORK specifies the average number of
// naonseconds to wait between operation.
#define WORK_IN_NANO

#ifdef WORK_IN_NANO
#if (MAX_WORK < 2000) && (USE_CPUS < 40)
#define RUNS                       (10000000)
#else
#define RUNS                       (1000000)
#endif
#endif

// definition: RUNS
// ----------------
// Define the total naumber of the calls of object's 
// operations that will be executed.
#ifndef RUNS
#if (N_THREADS > USE_CPUS) || (USE_CPUS > 40)
#define RUNS                       (1000000)
#else
#define RUNS                       (10000000)
#endif
#endif

// Definition: DEBUG
// -----------------
// Enable this definition, in case you would like some
// parts of the code. Usually leads to performance loss.
// This way of debuging is deprecated. It is better to
// compile your code with debug option.
// See Readme for more details.
//#define DEBUG

// Definition OBJECT_SIZE
// ----------------------
// This definition is only used in lfobject.c, simopt.c
// and luobject.c experiments. In any other case it is
// ignored. Its default value is 1. It is used for simulating
// of an atomic array of Fetch&Multiply objects with
// OBJECT_SIZE elements. All elements are updated 
// simultaneously.
#ifndef OBJECT_SIZE
#    define OBJECT_SIZE            1
#endif

// Definition: DISABLE_BACKOFF
// ---------------------------
// By defining this, any backoff scheme in any algorithm
// is disabled. Be carefull, upper an lower bounds must 
// also used as experiments' arguments, but they are ignored.
#define DISABLE_BACKOFF


#define Object                     int32_t

// Definition: RetVal
// ------------------
// Define the type of the return value that simulated 
// atomic objects must return. Be careful, this type
// must be read/written atomically by target machine.
// Usually, 32 or 64 bit (in some cases i.e. x86_64
// types of 128 bits are supported). In case that you
// need a larger type use indirection.
#define RetVal                     int32_t

// Definition: ArgVal
// ------------------
// Define the type of the argument value of atomic objects.
// All atomic objects have same argument types. In case
// that you 'd like to use different argument values in each
// atomic object, redefine it in object's source file.
#define ArgVal                     int32_t

#ifndef RING_POW
#define RING_POW        (17)
#endif
#define RING_SIZE       (1ull << RING_POW)

#define RING_STATS


#ifndef N_OBJECTS
#define N_OBJECTS       (1)
#endif

#endif
