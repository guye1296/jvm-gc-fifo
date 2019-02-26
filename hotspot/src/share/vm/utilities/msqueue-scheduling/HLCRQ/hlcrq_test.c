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

#include "hlcrq.h"
#include "cluster_scheduler.h"

/*********************************************************************
 * GLOBALS
 *********************************************************************/

typedef struct _Globals {
    volatile char pad0[CACHE_LINE_ALIGNED_SIZE] __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));

    hlcrq_t hlcrq __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));

    volatile char pad1[CACHE_LINE_ALIGNED_SIZE] __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));

    AtomicScheduler atomic_scheduler __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));

    volatile char pad2[CACHE_LINE_ALIGNED_SIZE] __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));

    pthread_barrier_t barr __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));

    int64_t d1 __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));
    int64_t d2 __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));

    volatile char pad3[CACHE_LINE_ALIGNED_SIZE] __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));
} Globals  __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));

Globals globals;

/*********************************************************************
 * Functions
 *********************************************************************/

inline static void common_enqueue(hlcrq_t* p_hlcrq, long id) {
    cluster_scheduler_start_op(&globals.atomic_scheduler, &p_hlcrq->Tail->tail);
    enqueue(p_hlcrq, id, id);
    cluster_scheduler_end_op(&globals.atomic_scheduler, &p_hlcrq->Tail->tail);
    spin_work();
}

inline static void common_dequeue(hlcrq_t* p_hlcrq, long id) {
    cluster_scheduler_start_op(&globals.atomic_scheduler, &p_hlcrq->Head->head);
    dequeue(p_hlcrq, id);
    cluster_scheduler_end_op(&globals.atomic_scheduler, &p_hlcrq->Head->head);
    spin_work();
}

inline static void common_execute(void* Arg) {
    long i;
    long id = (long) Arg;
    hlcrq_t* p_hlcrq = &globals.hlcrq;

    cluster_scheduler_thread_start(id);

    _thread_pin(id);
    simSRandom(id + 1);

    if (id == N_THREADS - 1) {
        globals.d1 = getTimeMillis();
    }
    // Synchronization point
    int rc = pthread_barrier_wait(&globals.barr);
    if (rc != 0 && rc != PTHREAD_BARRIER_SERIAL_THREAD) {
        printf("Could not wait on barrier\n");
        exit(-1);
    }

    start_cpu_counters(id);

    for (i = 0; i < RUNS/2; i++) {
        // perform an enqueue operation
        common_enqueue(p_hlcrq, id);
        // perform a dequeue operation
        common_dequeue(p_hlcrq, id);
    }

    stop_cpu_counters(id);
}

inline void Execute(void* Arg) {
    nrq = null;

    common_execute(Arg);

#ifdef RING_STATS
    FAA64(&globals.hlcrq.closes, mycloses);
    FAA64(&globals.hlcrq.unsafes, myunsafes);
#endif
}

inline static void* EntryPoint(void* Arg) {
    Execute(Arg);
    return NULL;
}

inline pthread_t StartThread(int arg) {
    long id = (long) arg;
    void *Arg = (void*) id;
    pthread_t thread_p;
    int thread_id;

    pthread_attr_t my_attr;
    pthread_attr_init(&my_attr);
    thread_id = pthread_create(&thread_p, &my_attr, EntryPoint, Arg);

    return thread_p;
}

int main(int argc, char **argv) {
    pthread_t threads[N_THREADS];
    int i;

    init_cpu_counters();

    if (argc < 2) {
        fprintf(stderr, "Please specify whether to fill queue prior to benchmark!\n");
        exit(EXIT_SUCCESS);
    } else {
        sscanf(argv[1], "%d", &globals.hlcrq.FULL);
    }

    // Barrier initialization
    if (pthread_barrier_init(&globals.barr, NULL, N_THREADS)) {
        printf("Could not create the barrier\n");
        return -1;
    }

    int full = globals.hlcrq.FULL;

    SHARED_OBJECT_INIT(&globals.hlcrq);
    cluster_scheduler_init(&globals.atomic_scheduler, &globals.hlcrq.Head->head, &globals.hlcrq.Tail->tail);
    //cluster_scheduler_init(&head->head, NULL);


    for (i = 0; i < N_THREADS; i++)
        threads[i] = StartThread(i);

    for (i = 0; i < N_THREADS; i++)
        pthread_join(threads[i], NULL);
    globals.d2 = getTimeMillis();

    printf("time=%d full=%d ", (int) (globals.d2 - globals.d1), full);
#ifdef RING_STATS
    printf("closes=%ld kills=%ld ", globals.hlcrq.closes, globals.hlcrq.unsafes);
#endif
    cluster_scheduler_print_counters(&globals.atomic_scheduler);
    printStats();

    if (pthread_barrier_destroy(&globals.barr)) {
        printf("Could not destroy the barrier\n");
        return -1;
    }
    return 0;
}
