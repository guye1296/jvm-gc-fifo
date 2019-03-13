/*
 * cluster_scheduler_no_sheduling.h
 *
 *  Created on: Oct 6, 2018
 *      Author: zilpal
 */

#ifndef CLUSTER_SCHEDULER_SRC_CLUSTER_SCHEDULER_NO_SHEDULING_H_
#define CLUSTER_SCHEDULER_SRC_CLUSTER_SCHEDULER_NO_SHEDULING_H_

/*********************************************************************
 * TYPES
 *********************************************************************/
/*
 * main structure
 */
typedef struct _AtomicWrapper {
    volatile void *atomic_ptr1;
    volatile void *atomic_ptr2;
    volatile int64_t active_cluster __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));
} AtomicWrapper  __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


/*********************************************************************
 * API
 *********************************************************************/

inline void cluster_scheduler_start_op_int(AtomicWrapper* atomic_wrapper, long thread_id){}

inline void cluster_scheduler_end_op_int(AtomicWrapper* atomic_wrapper, long thread_id) {}

#endif /* CLUSTER_SCHEDULER_SRC_CLUSTER_SCHEDULER_NO_SHEDULING_H_ */
