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

#include "lfstack.h"
#include "cluster_scheduler.h"

/*********************************************************************
 * GLOBALS
 *********************************************************************/

typedef struct _Globals {
    volatile char pad0[CACHE_LINE_ALIGNED_SIZE] __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));

    lfstack_t lfstack __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));

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

inline static void common_push(lfstack_t* p_lfstack, long id) {
    cluster_scheduler_start_op(&globals.atomic_scheduler, &p_lfstack->top);
    push(p_lfstack, id+1);
    cluster_scheduler_end_op(&globals.atomic_scheduler, &p_lfstack->top);
    spin_work();
}

inline static void common_pop(lfstack_t* p_lfstack) {
    cluster_scheduler_start_op(&globals.atomic_scheduler, &p_lfstack->top);
    pop(p_lfstack);
    cluster_scheduler_end_op(&globals.atomic_scheduler, &p_lfstack->top);
    spin_work();
}

inline static void common_execute(void* Arg) {
    long i;
    long id = (long) Arg;
    lfstack_t* p_lfstack = &globals.lfstack;

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
        common_push(p_lfstack, id);
        // perform a dequeue operation
        common_pop(p_lfstack);
#if 0
        if (i%10000 == 0)
        	printf("Thread %ld, progress %ld%% \n", id, ((i*100)/(RUNS/2)));
#endif
    }

    stop_cpu_counters(id);
}

inline void Execute(void* Arg) {
    init_backoff(&backoff, globals.lfstack.MIN_BAK, globals.lfstack.MAX_BAK, 1);
    init_pool(&pool, sizeof(node_t));
    common_execute(Arg);
}

inline static void* EntryPoint(void* Arg) {
    long thread_id = (long) Arg;
    Execute(Arg);
    globals.atomic_scheduler.global_cas_counters[thread_id] = thread_cas_counters;
    return null;
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

inline static void cas_print_counters()
{
    int id;
    uint64_t total_cas_failures = 0;
    uint64_t total_cas_trials = 0;

    for (id=0; id<N_THREADS; id++) {
    	total_cas_failures += globals.atomic_scheduler.global_cas_counters[id].cas_failures;
    	total_cas_trials += globals.atomic_scheduler.global_cas_counters[id].cas_trials;
    }

    printf("cas_failures=%.2f ", (float)total_cas_failures/total_cas_trials);
}

/*********************************************************************
 * Main
 *********************************************************************/

int main(int argc, char *argv[]) {
    pthread_t threads[N_THREADS];
    int i;

    cluster_scheduler_init(&globals.atomic_scheduler, &globals.lfstack.top, NULL);
    init_cpu_counters();
    if (argc < 3) {
        fprintf(stderr, "Please set upper and lower bound for backoff!\n");
        exit(EXIT_SUCCESS);
    } else {
        sscanf(argv[1], "%d", &globals.lfstack.MIN_BAK);
        sscanf(argv[2], "%d", &globals.lfstack.MAX_BAK);
    }

    if (pthread_barrier_init(&globals.barr, NULL, N_THREADS)) {
        printf("Could not create the barrier\n");
        return -1;
    }

    SHARED_OBJECT_INIT(&globals.lfstack);
    for (i = 0; i < N_THREADS; i++)
        threads[i] = StartThread(i);

    for (i = 0; i < N_THREADS; i++)
        pthread_join(threads[i], NULL);
    globals.d2 = getTimeMillis();

    printf("time=%d ", (int) (globals.d2 - globals.d1));
    cluster_scheduler_print_counters(&globals.atomic_scheduler);
    cas_print_counters();
    printStats();

#ifdef DEBUG
    int counter = 0;

    while(globals.lfstack.top != null) {
        counter++;
        globals.lfstack.top = globals.lfstack.top->next;
    }

    fprintf(stderr, "%d nodes were left in the stack!\n", counter);
#endif

    if (pthread_barrier_destroy(&globals.barr)) {
        printf("Could not destroy the barrier\n");
        return -1;
    }
    return 0;
}
