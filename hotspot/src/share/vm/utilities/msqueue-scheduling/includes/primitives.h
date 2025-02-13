#ifndef _PRIMITIVES_H_
#define _PRIMITIVES_H_

#include <stdlib.h>
#include <limits.h>
#include <math.h>
#include <math.h>
#include <stdint.h>
#include <sys/timeb.h>
#include <sys/mman.h>
#include <malloc.h>

#include "stats.h"
#include "types.h"
#include "system.h"
#include "rand.h"

//#define _EMULATE_FAA_
//#define _EMULATE_SWAP_

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

inline static void *getMemory(size_t size) {
    void *p = malloc(size);

    if (p == null) {
        perror("malloc failed");
        exit(EXIT_FAILURE);
    } else return p;
}

inline static void *getAlignedMemory(size_t align, size_t size) {
    void *p;

#ifdef MEMORY_32BIT	
    p = mmap(0, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT|MAP_POPULATE, -1, 0);
    if (p == MAP_FAILED) {
#else
    p = (void *)memalign(align, size);
    if (p == null) {
#endif
        perror("memalign");
        exit(EXIT_FAILURE);
    } else return p;
}

inline static int64_t getTimeMillis(void) {
	struct timeb tm;

	ftime(&tm);
	return 1000 * tm.time + tm.millitm;
}

#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)

#define BIT_TEST_AND_SET(ptr, b)                                \
({                                                              \
    char __ret;                                                 \
    asm volatile("lock btsq $63, %0; setnc %1" : "+m"(*ptr), "=a"(__ret) : : "cc"); \
    __ret;                                                      \
})

#if defined(__GNUC__) && (__GNUC__*10000 + __GNUC_MINOR__*100) >= 40100
#    define barrier()                  asm volatile ("": : :"memory")
#    define __CASPTR(A, B, C)          __sync_bool_compare_and_swap((long *)A, (long)B, (long)C)
#    define __CAS64(A, B, C)           __sync_bool_compare_and_swap(A, B, C)
#    define __CAS32(A, B, C)           __sync_bool_compare_and_swap(A, B, C)
#    define __SWAP(A, B)               __sync_lock_test_and_set((long *)A, (long)B)
#    define __FAA64(A, B)              __sync_fetch_and_add(A, B)
#    define __FAA32(A, B)              __sync_fetch_and_add(A, B)
#    define ReadPrefetch(A)            __builtin_prefetch((const void *)A, 0, 3);
#    define StorePrefetch(A)           __builtin_prefetch((const void *)A, 1, 3);
#    define bitSearchFirst(A)          __builtin_ctzll(A)
#    define nonZeroBits(A)             __builtin_popcountll(A)
#    if defined(__amd64__) || defined(__x86_64__)
#        define LoadFence()            asm volatile ("lfence":::"memory")
#        define StoreFence()           asm volatile ("sfence":::"memory")
#        define FullFence()            asm volatile ("mfence":::"memory")
#    else
#        define LoadFence()            __sync_synchronize()
#        define StoreFence()           __sync_synchronize()
#        define FullFence()            __sync_synchronize()
#    endif

#elif defined(__GNUC__) && (defined(__amd64__) || defined(__x86_64__))
#    warning A newer GCC version is recommended!
#    define LoadFence()                asm volatile ("lfence":::"memory") 
#    define StoreFence()               asm volatile ("sfence":::"memory") 
#    define FullFence()                asm volatile ("mfence":::"memory") 
#    define ReadPrefetch(A)            A;//asm volatile ("prefetch0 %0"::"m"((const void *)A))
#    define StorePrefetch(A)           A;//asm volatile ("prefetch0 %0"::"m"((const void *)A))

inline static bool __CASPTR(void *A, void *B, void *C) {
    uint64_t prev;
    uint64_t *p = (uint64_t *)A;

    asm volatile("lock;cmpxchgq %1,%2"
         : "=a"(prev)
         : "r"((uint64_t)C), "m"(*p), "0"((uint64_t)B)
         : "memory");
    return (prev == (uint64_t)B);
}

inline static bool __CAS64(uint64_t *A, uint64_t B, uint64_t C) {
    uint64_t prev;
    uint64_t *p = (int64_t *)A;

    asm volatile("lock;cmpxchgq %1,%2"
         : "=a"(prev)
         : "r"(C), "m"(*p), "0"(B)
         : "memory");
    return (prev == B);
}

inline static bool __CAS32(uint32_t *A, uint32_t B, uint32_t C) {
    uint32_t prev;
    uint32_t *p = (uint32_t *)A;

    asm volatile("lock;cmpxchgl %1,%2"
         : "=a"(prev)
         : "r"(C), "m"(*p), "0"(B)
         : "memory");
    return (prev == B);
}

inline static void *__SWAP(void *A, void *B) {
    int64_t *p = (int64_t *)A;

    asm volatile("lock;"
          "xchgq %0, %1"
          : "=r"(B), "=m"(*p)
          : "0"(B), "m"(*p)
          : "memory");
    return B;
}

inline static uint64_t __FAA64(volatile uint64_t *A, uint64_t B){
    asm("lock; xaddq %0,%1": "+r" (B), "+m" (*(A)): : "memory");
}

inline static uint32_t __FAA32(volatile uint32_t *A, uint32_t B){
    asm("lock; xaddl %0,%1": "+r" (B), "+m" (*(A)): : "memory");
}

inline static int bitSearchFirst(uint64_t B) {
    uint64_t A;

    asm("bsfq %0, %1;" : "=d"(A) : "d"(B));

	return (int)A;
}

inline static uint64_t nonZeroBits(uint64_t v) {
    uint64_t c;

    for (c = 0; v; v >>= 1)
        c += v & 1;

    return c;
}

#elif defined(sun) && defined(sparc) && defined(__SUNPRO_C)
#    warning Experimental support!

#    include <atomic.h>
#    include <sun_prefetch.h>

     extern void MEMBAR_ALL(void);
     extern void MEMBAR_STORE(void);
     extern void MEMBAR_LOAD(void);
     extern void *CASPO(void volatile*, void *);
     extern void *SWAPPO(void volatile*, void *);
     extern int32_t POPC(int32_t x);
     extern void NOP(void);

#    define __CASPTR(A, B, C)          (atomic_cas_ptr(A, B, C) == B)
#    define __CAS32(A, B, C)           (atomic_cas_32(A, B, C) == B)
#    define __CAS64(A, B, C)           (atomic_cas_64(A, B, C) == B)
#    define __SWAP(A, B)               SWAPPO(A, B)
#    define __FAA32(A, B)              atomic_add_32_nv(A, B)
#    define __FAA64(A, B)              atomic_add_64_nv(A, B)
#    define nonZeroBits(A)              (POPC((int32_t)A)+POPC((int32_t)(A>>32)))
#    define LoadFence()                MEMBAR_LOAD()
#    define StoreFence()               MEMBAR_STORE()
#    define FullFence()                MEMBAR_ALL()
#    define ReadPrefetch(A)            sparc_prefetch_read_many((void *)A)
#    define StorePrefetch(A)           sparc_prefetch_write_many((void *)A)

inline static uint32_t __bitSearchFirst32(uint32_t v) {
    register uint32_t r;     // result of log2(v) will be stored here
    register uint32_t shift;

    r =     (v > 0xFFFF) << 4; v >>= r;
    shift = (v > 0xFF  ) << 3; v >>= shift; r |= shift;
    shift = (v > 0xF   ) << 2; v >>= shift; r |= shift;
    shift = (v > 0x3   ) << 1; v >>= shift; r |= shift;
    r |= (v >> 1);
    return r;
}

inline static uint32_t bitSearchFirst(uint64_t v) {
    uint32_t r = __bitSearchFirst32((uint32_t)v);
	return (r == 0) ? __bitSearchFirst32((uint32_t)(v >> 32)) + 31 : r;
}

#else
#    error Current machine architecture and compiler are not supported yet!
#endif

#define CAS32(A, B, C) _CAS32((uint32_t *)A, (uint32_t)B, (uint32_t)C)
inline static bool _CAS32(uint32_t *A, uint32_t B, uint32_t C) {
#ifdef DEBUG
    int res;

    res = __CAS32(A, B, C);
    __executed_cas[__stats_thread_id].v++;
    __failed_cas[__stats_thread_id].v += 1 - res;
    
    return res;
#else
    return __CAS32(A, B, C);
#endif
}

#define CAS64(A, B, C) _CAS64((uint64_t *)A, (uint64_t)B, (uint64_t)C)
inline static bool _CAS64(uint64_t *A, uint64_t B, uint64_t C) {
#ifdef DEBUG
    int res;

    res = __CAS64(A, B, C);
    __executed_cas[__stats_thread_id].v++;
    __failed_cas[__stats_thread_id].v += 1 - res;
    
    return res;
#else
    return __CAS64(A, B, C);
#endif
}

#define CASPTR(A, B, C) _CASPTR((void *)A, (void *)B, (void *)C)
inline static bool _CASPTR(void *A, void *B, void *C) {
#ifdef DEBUG
    int res;

    res = __CASPTR(A, B, C);
    __executed_cas[__stats_thread_id].v++;
    __failed_cas[__stats_thread_id].v += 1 - res;
    
    return res;
#else
    return __CASPTR(A, B, C);
#endif
}

/*
 * if ((ptr[0] == o1) && (ptr[1] == o2))
 * ptr[0] = n1
 * ptr10] = n2
 */
#define __CAS2(ptr, o1, o2, n1, n2)                             \
({                                                              \
    char __ret;                                                 \
    __typeof__(o2) __junk;                                      \
    __typeof__(*(ptr)) __old1 = (o1);                           \
    __typeof__(o2) __old2 = (o2);                               \
    __typeof__(*(ptr)) __new1 = (n1);                           \
    __typeof__(o2) __new2 = (n2);                               \
    asm volatile("lock cmpxchg16b %2;setz %1"                   \
                   : "=d"(__junk), "=a"(__ret), "+m" (*ptr)     \
                   : "b"(__new1), "c"(__new2),                  \
                     "a"(__old1), "d"(__old2));                 \
    __ret; })

#ifdef DEBUG
#define CAS2(ptr, o1, o2, n1, n2)                               \
({                                                              \
    int res;                                                    \
    res = __CAS2(ptr, o1, o2, n1, n2);                          \
    __executed_cas[__stats_thread_id].v++;                      \
    __failed_cas[__stats_thread_id].v += 1 - res;               \
    res;                                                        \
})
#else
#define CAS2(ptr, o1, o2, n1, n2)    __CAS2(ptr, o1, o2, n1, n2)
#endif

#define SWAP(A, B) _SWAP((void *)A, (void *)B)
inline static void *_SWAP(void *A, void *B) {
#if defined(_EMULATE_SWAP_)
#    warning SWAP instructions are simulated!
    void *old_val;
    void *new_val;

    while (true) {
        old_val = (void *)*((volatile long *)A);
        new_val = B;
        if(((void *)*((volatile long *)A)) == old_val && CASPTR(A, old_val, new_val) == true)
            break;
    }
    return old_val;

#else
#ifdef DEBUG
    __executed_swap[__stats_thread_id].v++;
    return (void *)__SWAP(A, B);
#else
    return (void *)__SWAP(A, B);
#endif
#endif
}

#define FAA32(A, B) _FAA32((volatile uint32_t *)A, (uint32_t)B)
inline static uint32_t _FAA32(volatile uint32_t *A, uint32_t B) {
#if defined(_EMULATE_FAA_)
//#    warning Fetch&Add instructions are simulated!

    int32_t old_val;
    int32_t new_val;

    while (true) {
        old_val = *((int32_t * volatile)A);
        new_val = old_val + B;
        if(*A == old_val && CAS32(A, old_val, new_val) == true)
            break;
    }
    return old_val;

#else
#   ifdef DEBUG
    __executed_faa[__stats_thread_id].v++;
    return __FAA32(A, B);
#   else
    return __FAA32(A, B);
#   endif
#endif
}

#define FAA64(A, B) _FAA64((volatile uint64_t *)A, (uint64_t)B)
inline static uint64_t _FAA64(volatile uint64_t *A, uint64_t B) {

#if defined(_EMULATE_FAA_)
//#    warning Fetch&Add instructions are simulated!

    int64_t old_val;
    int64_t new_val;

    while (true) {
        old_val = *((volatile int64_t * volatile)A);
        new_val = old_val + B;
        if(*A == old_val && CAS64(A, old_val, new_val) == true)
            break;
    }
    return old_val;

#else
#   ifdef DEBUG
    __executed_faa[__stats_thread_id].v++;
    return __FAA64(A, B);
#   else
    return __FAA64(A, B);
#   endif
#endif
}

inline static uint64_t read_tsc(void)
{
    unsigned upper, lower;
    __asm__ __volatile__ ("rdtsc" : "=a"(lower), "=d"(upper));
    return ((uint64_t)lower)|(((uint64_t)upper)<<32 );
}

inline static uint64_t read_tsc_sync(void)
{
    unsigned upper, lower;
    __asm__ __volatile__ ("lfence; rdtsc" : "=a"(lower), "=d"(upper)::"memory");
    return ((uint64_t)lower)|(((uint64_t)upper)<<32 );
}

#define RDTSC_CYCLES    24
#define RANDOM_CYCLES   4
#define MIN_LATENCY     (RDTSC_CYCLES + RDTSC_CYCLES + RANDOM_CYCLES)
#define NANO_TO_CYCLES(m)      ((m) * 24) / 10

static inline void spin_work(void) {
#if (MAX_WORK > 0)
#ifdef WORK_IN_NANO
    uint64_t tt, t = read_tsc();
    long rnum = simRandomRange(0, 2*NANO_TO_CYCLES(MAX_WORK) - MIN_LATENCY) + RDTSC_CYCLES;
    while ((tt = read_tsc() + MIN_LATENCY) < t + rnum) {
    }
#else
    volatile int j;
    long rnum = simRandomRange(1, MAX_WORK);
    for (j = 0; j < rnum; j++)
        ;
#endif
#endif
}


#endif
