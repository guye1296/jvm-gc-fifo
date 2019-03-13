#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <omp.h>
#include <string.h>
#include <stdint.h>

#include "config.h"
#include "primitives.h"
#include "backoff.h"
#include "rand.h"
#include "thread.h"

#include "cluster_scheduler.h"


/*********************************************************************
 * GLOBALS
 *********************************************************************/

typedef struct _Globals {
    volatile char pad0[CACHE_LINE_ALIGNED_SIZE] __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));

    queue_t msqueue __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));

    volatile char pad1[CACHE_LINE_ALIGNED_SIZE] __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));

    AtomicScheduler atomic_scheduler __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));

    volatile char pad2[CACHE_LINE_ALIGNED_SIZE] __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));

    pthread_barrier_t barr __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));

    int64_t d1 __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));
    int64_t d2 __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));

    volatile char pad3[CACHE_LINE_ALIGNED_SIZE] __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));
} Globals  __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));


typedef struct _node{
	struct _node * next;
    Object value;
} node_t;

typedef struct _queue {
    volatile char pad0[CACHE_LINE_ALIGNED_SIZE] __attribute__ ((aligned (128)));

    int MIN_BAK  __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));
    int MAX_BAK  __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));
    int FULL  __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));


    volatile node_t* Head __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));
    volatile node_t* Tail __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));

    volatile char pad1[CACHE_LINE_ALIGNED_SIZE] __attribute__ ((aligned (128)));
} queue_t  __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));

/*********************************************************************
 * Functions
 *********************************************************************/
export inline static void common_enqueue(queue_t* queue, void* task, long id);

export inline static void common_dequeue(queue_t* queue, long id);

export inline static Globals* create_queue();
