/*
 * cluster_scheduler.h
 *
 *  Created on: Aug 25, 2017
 *      Author: zilpal
 */

/*
#define is_aligned(POINTER) \
    (((uintptr_t)(const void *)(POINTER)) % (128) == 0)
*/

#include "cluster_scheduler.h"

/*********************************************************************
 * THREAD GLOBALS
 *********************************************************************/

__thread long threadId __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));
__thread ThreadCasPerfCounters thread_cas_counters __attribute__ ((aligned (128)));

/*********************************************************************
 * scheduling methods
 *********************************************************************/
#if SCHEDULING_METHOD==0
#include "cluster_scheduler_no_sheduling.h"
#elif SCHEDULING_METHOD==1
#include "cluster_scheduler_primitive.h"
#elif SCHEDULING_METHOD==2
#include "cluster_scheduler_ref_count.h"
#elif SCHEDULING_METHOD==3
#include "cluster_scheduler_cache_miss.h"
#elif SCHEDULING_METHOD==4
#include "cluster_scheduler_work_time_queue.h"
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline void cluster_scheduler_thread_start(long thread_id) {
    threadId = thread_id;
}

inline bool NEW_CASPTR(void *A, void *B, void *C) {
#if USE_NEW_CAS
    bool ret;
    AtomicWrapper* atomic_wrapper = atomic_var_to_atomic_wrapper(A);
    cluster_scheduler_start_op_int(atomic_wrapper, threadId);
    ret = CASPTR(A, B, C);
    cluster_scheduler_end_op_int(atomic_wrapper, threadId);
    return ret;
#else
    return CASPTR(A, B, C);
#endif
}

inline void cluster_scheduler_start_op(AtomicScheduler* pScheduler, volatile void *atomic_ptr){
	AtomicWrapper* atomic_wrapper = atomic_var_to_atomic_wrapper(pScheduler, atomic_ptr);
	cluster_scheduler_start_op_int(atomic_wrapper,threadId);
}

inline void cluster_scheduler_end_op(AtomicScheduler* pScheduler, volatile void *atomic_ptr){
	AtomicWrapper* atomic_wrapper = atomic_var_to_atomic_wrapper(pScheduler, atomic_ptr);
	cluster_scheduler_end_op_int(atomic_wrapper,threadId);
}

//--------------------------------------------------------------------------------------------------------------------------

#if SCHEDULING_METHOD!=4
inline void cluster_scheduler_init(AtomicScheduler* pScheduler, volatile void *atomic_ptr1, volatile void *atomic_ptr2)
{
    AtomicWrapper* atomic_wrapper;

	/* init the first atomic var */
	atomic_wrapper = (AtomicWrapper*) getAlignedMemory(CACHE_LINE_ALIGNED_SIZE, sizeof(AtomicWrapper));
    memset(atomic_wrapper, 0, sizeof(AtomicWrapper));

    /*
    assert(((uintptr_t)(&atomic_wrapper->cluster_take_ownership_busy) % CACHE_LINE_ALIGNED_SIZE) == 0);
    assert(((uintptr_t)(&atomic_wrapper->atomic_ptr1) % CACHE_LINE_ALIGNED_SIZE) == 0);
    assert(((uintptr_t)(&atomic_wrapper->active_cluster) % CACHE_LINE_ALIGNED_SIZE) == 0);
    assert(((uintptr_t)(atomic_wrapper->cluster_array) % CACHE_LINE_ALIGNED_SIZE) == 0);
    assert(((uintptr_t)(&atomic_wrapper->cluster_array[0].is_agent_exists) % CACHE_LINE_ALIGNED_SIZE) == 0);
    assert(((uintptr_t)(&atomic_wrapper->cluster_array[0].last_successful_sensing_time) % CACHE_LINE_ALIGNED_SIZE) == 0);
    assert(((uintptr_t)(atomic_wrapper->cluster_array[0].thread_ref_count) % CACHE_LINE_ALIGNED_SIZE) == 0);
    assert(((uintptr_t)(&atomic_wrapper->cluster_array[0].thread_ref_count[0]) % CACHE_LINE_ALIGNED_SIZE) == 0);
    assert(((uintptr_t)(&atomic_wrapper->cluster_array[0].thread_ref_count[0].ref_count) % CACHE_LINE_ALIGNED_SIZE) == 0);
    assert(((uintptr_t)(&atomic_wrapper->cluster_array[0].is_agent_exists) % CACHE_LINE_ALIGNED_SIZE) == 0);
    assert(((uintptr_t)(&atomic_wrapper->cluster_array[1].last_successful_sensing_time) % CACHE_LINE_ALIGNED_SIZE) == 0);
    assert(((uintptr_t)(atomic_wrapper->cluster_array[1].thread_ref_count) % CACHE_LINE_ALIGNED_SIZE) == 0);
    assert(((uintptr_t)(&atomic_wrapper->cluster_array[1].thread_ref_count[1]) % CACHE_LINE_ALIGNED_SIZE) == 0);
    assert(((uintptr_t)(&atomic_wrapper->cluster_array[1].thread_ref_count[1].ref_count) % CACHE_LINE_ALIGNED_SIZE) == 0);
    */

    atomic_wrapper->active_cluster = -1;
    atomic_wrapper->atomic_ptr1 = atomic_ptr1;
#if !USE_NEW_CAS
    atomic_wrapper->atomic_ptr2 = atomic_ptr2;
#endif

    pScheduler->g_map_from_ptr_to_atomic_wrapper[0] = atomic_wrapper;

#if USE_NEW_CAS
    /* init the second atomic var */
	atomic_wrapper = (AtomicWrapper*) getAlignedMemory(CACHE_LINE_ALIGNED_SIZE, sizeof(AtomicWrapper));
    memset(atomic_wrapper, 0, sizeof(AtomicWrapper));

    atomic_wrapper->active_cluster = -1;
    atomic_wrapper->atomic_ptr1 = atomic_ptr2;
    atomic_wrapper->atomic_ptr2 = NULL;
#endif
}

inline AtomicWrapper* atomic_var_to_atomic_wrapper(AtomicScheduler* pScheduler, volatile void *atomic_ptr)
{
	return pScheduler->g_map_from_ptr_to_atomic_wrapper[0];
	/*
	if (g_map_from_ptr_to_atomic_wrapper[0]->atomic_ptr1 == atomic_ptr)
		return g_map_from_ptr_to_atomic_wrapper[0];

	return g_map_from_ptr_to_atomic_wrapper[1];
	*/
}

inline void cluster_scheduler_print_counters(AtomicScheduler* pScheduler) {
	/*
	if (g_map_from_ptr_to_atomic_wrapper[0]->atomic_ptr1) {
		atomic_wrapper_print_counters(g_map_from_ptr_to_atomic_wrapper[0]);
	}
	if (g_map_from_ptr_to_atomic_wrapper[1]->atomic_ptr1) {
		atomic_wrapper_print_counters(g_map_from_ptr_to_atomic_wrapper[1]);
	}
	*/
}
#endif
