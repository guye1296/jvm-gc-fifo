#ifndef _NUMA_GLOBALS_H
#define _NUMA_GLOBALS_H

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <omp.h>
#include <string.h>
#include <stdint.h>

#include "config.h"
#include "primitives.h"
#include "rand.h"
#include "thread.h"
#include "cluster_scheduler.h"

#ifdef MSQUEUE
#include "msqueue.h"
#else
#include "hlcrq.h"
#endif


typedef struct _Globals {
    volatile char pad0[CACHE_LINE_ALIGNED_SIZE] __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));

#ifdef MSQUEUE
    queue_t queue __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));
#else
    hlcrq_t queue __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));
#endif

    volatile char pad1[CACHE_LINE_ALIGNED_SIZE] __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));

    AtomicScheduler atomic_scheduler __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));

    volatile char pad2[CACHE_LINE_ALIGNED_SIZE] __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));

    pthread_barrier_t barr __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));

    int64_t d1 __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));
    int64_t d2 __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));

    volatile char pad3[CACHE_LINE_ALIGNED_SIZE] __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));
} Globals  __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));


#endif // _NUMA_GLOBALS_H
