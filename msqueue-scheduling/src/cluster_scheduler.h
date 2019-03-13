/*
 * cluster_scheduler.h
 *
 *  Created on: Aug 25, 2017
 *      Author: zilpal
 */

#ifndef SRC_CLUSTER_SCHEDULER_INCLUDE_H_
#define SRC_CLUSTER_SCHEDULER_INCLUDE_H_

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <assert.h>

#include "config.h"
#include "primitives.h"
#include "defines.h"

/*********************************************************************
 * Counters
 *********************************************************************/

/*
 * Performance counters
 */
typedef struct _ThreadCasPerfCounters {
    volatile uint64_t cas_trials;
    volatile uint64_t cas_failures;
    volatile uint64_t tail_cas_trials;
    volatile uint64_t tail_cas_failures;
    volatile uint64_t head_cas_trials;
    volatile uint64_t head_cas_failures;
    volatile uint64_t next_cas_trials;
    volatile uint64_t next_cas_failures;
    char pad[CACHE_LINE_ALIGNED_SIZE-8*sizeof(uint64_t)];
} ThreadCasPerfCounters  __attribute__ ((aligned (128)));

/*********************************************************************
 * Forward Deceleration
 *********************************************************************/

extern __thread ThreadCasPerfCounters thread_cas_counters;

struct _AtomicWrapper;

/*********************************************************************
 * Types
 *********************************************************************/

typedef struct _AtomicScheduler{

    struct _AtomicWrapper* g_map_from_ptr_to_atomic_wrapper[2] __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));

    ThreadCasPerfCounters global_cas_counters[N_THREADS] __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));

} AtomicScheduler __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void cluster_scheduler_thread_start(long thread_id);

void cluster_scheduler_start_op(AtomicScheduler* pScheduler, volatile void *atomic_ptr);

void cluster_scheduler_end_op(AtomicScheduler* pScheduler, volatile void *atomic_ptr);

void cluster_scheduler_init(AtomicScheduler* pScheduler, volatile void *algorithm_contention_var1, volatile void *algorithm_contention_var2);

inline struct _AtomicWrapper* atomic_var_to_atomic_wrapper(AtomicScheduler* pScheduler, volatile void *atomic_ptr);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TO BE IMPLEMENTED BY SCHEDULING ALGORITHM
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline void cluster_scheduler_start_op_int(struct _AtomicWrapper* atomic_wrapper, long thread_id);

inline void cluster_scheduler_end_op_int(struct _AtomicWrapper* atomic_wrapper, long thread_id);

void cluster_scheduler_print_counters(AtomicScheduler* pScheduler);

inline bool NEW_CASPTR(void *A, void *B, void *C);

#endif /* SRC_CLUSTER_SCHEDULER_INCLUDE_H_ */
