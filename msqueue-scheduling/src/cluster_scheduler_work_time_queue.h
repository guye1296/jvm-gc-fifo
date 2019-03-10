/*
 * hcs_agent.h
 *
 *  Created on: May 5, 2017
 *      Author: zilpal
 */

#ifndef CLUSTER_SCHEDULER_H_
#define CLUSTER_SCHEDULER_H_

// should be number of threads in node x L2_CACHE_MISS + 2xL3_CACH_MISS
#define WAIT_FOR_CLUSTER MICRO_TO_CYCLES(100)

/*********************************************************************
 * TYPES
 *********************************************************************/

/*
 * cluster agent
 */
typedef struct _ClusterAgent {
	volatile int64_t is_agent_exists __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));
	volatile int64_t b_active __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));
	volatile int64_t last_updtae_time __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));
} ClusterAgent __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));

/*
 * Performance counters
 */
typedef struct __attribute__((__packed__)) _ThreadPerfCounters {
    // this is a moving avg of the work time (time where the thread is not in the critical section)
    volatile uint32_t thread_dead_time;
    //volatile uint16_t thread_num_of_runs;
    // this is a moving avg of the critical section time
    volatile uint32_t thread_in_queue_time;
    char pad[CACHE_LINE_ALIGNED_SIZE-2*sizeof(uint32_t)];
} ThreadPerfCounters  __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));

/*
 * Thread data
 */
typedef struct _ThreadData {
	volatile ThreadPerfCounters thread_perf_counters __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));
	volatile int64_t thread_start_op_timestamp  __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));
	volatile int64_t thread_end_op_timestamp  __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));
} ThreadData  __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));

typedef struct _AtomicWrapper {
	volatile ClusterAgent cluster_array[NUMBER_OF_CLUSTERS] __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));
	volatile ThreadData thread_data[N_THREADS] __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));
	// current active cluster, if a thread belongs to this cluster, it can run
	volatile int64_t active_cluster __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));
	volatile void* atomic_ptr __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));;
} AtomicWrapper  __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline static ThreadPerfCounters get_cluster_counters(AtomicWrapper* atomic_wrapper, uint64_t cluster_id) {
    int id;
    ThreadPerfCounters prefCntr;

    uint64_t total_dead_time = 0;
    uint64_t total_in_queue_time = 0;

    for (id=0; id<N_THREADS; id++) {
        if (pid_to_cluster(id) == cluster_id) {
            *((uint64_t*)&prefCntr) = *((uint64_t*)&atomic_wrapper->thread_data[id].thread_perf_counters);
            total_dead_time += prefCntr.thread_dead_time;
            total_in_queue_time += prefCntr.thread_in_queue_time;
        }
    }

    prefCntr.thread_dead_time = total_dead_time/(N_THREADS/NUMBER_OF_CLUSTERS);
    prefCntr.thread_in_queue_time = total_in_queue_time/(N_THREADS/NUMBER_OF_CLUSTERS);

    return prefCntr;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline bool scheduler_take_ownership(AtomicWrapper* atomic_wrapper, uint64_t my_cluster)
{
    bool is_ownership = true;
    uint64_t cluster_owner = atomic_wrapper->active_cluster;
    volatile ClusterAgent* myCluster = &atomic_wrapper->cluster_array[my_cluster];

    if (cluster_owner != my_cluster)
    {
    	myCluster->b_active = 1;
    	myCluster->last_updtae_time = read_tsc();
        if (!CAS64(&atomic_wrapper->active_cluster, cluster_owner, my_cluster) && (my_cluster != atomic_wrapper->active_cluster)) {
			is_ownership = false;
			myCluster->b_active = 0;
        }
    }

    return is_ownership;
}

// return if we waited for ownership
inline bool wait_for_cluster(AtomicWrapper* atomic_wrapper, int64_t pid, uint64_t start_time) {
    uint64_t my_cluster = pid_to_cluster(pid);
    uint64_t cluster_owner = atomic_wrapper->active_cluster;
    static uint64_t index = 0;
    volatile ClusterAgent* myCluster = &atomic_wrapper->cluster_array[my_cluster];
    uint64_t is_agent_exists_exists = 0;
    uint64_t is_agent = 0;


    uint64_t last_updtae_time = myCluster->last_updtae_time;
    if (((start_time - last_updtae_time) > WAIT_FOR_CLUSTER) &&
        CAS64(&myCluster->last_updtae_time, last_updtae_time, start_time)) {
        ThreadPerfCounters prefCntr;
        uint64_t is_active;
        prefCntr = get_cluster_counters(atomic_wrapper, my_cluster);
        if (prefCntr.thread_in_queue_time*(N_THREADS/NUMBER_OF_CLUSTERS) > prefCntr.thread_dead_time) {
            is_active = 1;
            /*
            index++;
            if (index%100 == 0) {
                fprintf(stderr,"Stats: index=%ld, cluster=%ld, pid=%ld, dead_time=%d in_queue_time=%d b_active=%ld\n", index, my_cluster, pid, prefCntr.thread_dead_time, prefCntr.thread_in_queue_time, myCluster->b_active);
            }
            */
        } else {
            is_active = 0;
        }
        if (myCluster->b_active != is_active) {
            myCluster->b_active = is_active;
            //fprintf(stderr,"Active Change: cluster=%ld, pid=%ld, dead_time=%d in_queue_time=%d b_active=%ld\n", my_cluster, pid, prefCntr.thread_dead_time, prefCntr.thread_in_queue_time, myCluster->b_active);
        }
        /*
        if (index%100000 == 0) {
            fprintf(stderr,"Stats: cluster=%ld, pid=%ld, dead_time=%d in_queue_time=%d b_active=%ld\n", my_cluster, pid, prefCntr.thread_dead_time, prefCntr.thread_in_queue_time, myCluster->b_active);
        }
        index++;
        */

        // owner node code did update - need to count start_op from the begining
        if ((cluster_owner == my_cluster) || (!myCluster->b_active)) {
            return true;
        }
    }

    // if not active, no need to wait
    if (!myCluster->b_active) {
        return false;
    }

    // owner node code
    if (cluster_owner == my_cluster) {
    	return false;
    }

    // agent code:
	if ((myCluster->is_agent_exists == 0) &&
		CAS64(&myCluster->is_agent_exists, 0, 1))
	{
		is_agent = 1;

		while (1) {
			// wait for other node to free ownership BY timeout
			while ((atomic_wrapper->active_cluster != -1) &&
					atomic_wrapper->cluster_array[atomic_wrapper->active_cluster].b_active &&
					(read_tsc()-atomic_wrapper->cluster_array[atomic_wrapper->active_cluster].last_updtae_time) < 10*WAIT_FOR_CLUSTER);
				/*
				 * start_time = read_tsc();
					&&
					((read_tsc()-start_time) < AGENT_TIMEOUT)) {

			}
			*/
			if (scheduler_take_ownership(atomic_wrapper, my_cluster) == true){
				myCluster->is_agent_exists = 0;
				return true;
			}
		}
	}

	// other threads code:
	// wait for agent to take ownership BY timeout
	start_time = read_tsc();
	while (	(myCluster->is_agent_exists == 1) &&
			(cluster_owner != my_cluster) &&
			((read_tsc()-start_time) <= AGENT_TIMEOUT))
	{
		cluster_owner = atomic_wrapper->active_cluster;
	}

	// on timeout:
	if ((read_tsc()-start_time) > AGENT_TIMEOUT)
	{
		myCluster->is_agent_exists = 0;
		if (is_agent) {
			fprintf(stderr,"ERROR: Agent out on timeout!!! pid=%ld\n", pid);
		} else {
			fprintf(stderr,"ERROR: Agent is stuck!!! pid=%ld\n", pid);
		}
	}

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline AtomicWrapper* atomic_var_to_atomic_wrapper(AtomicScheduler* pScheduler, volatile void *algorithm_contention_var)
{
	if (pScheduler->g_map_from_ptr_to_atomic_wrapper[0]->atomic_ptr == algorithm_contention_var)
		return pScheduler->g_map_from_ptr_to_atomic_wrapper[0];

	if (pScheduler->g_map_from_ptr_to_atomic_wrapper[1]->atomic_ptr == algorithm_contention_var)
		return pScheduler->g_map_from_ptr_to_atomic_wrapper[1];

	fprintf(stderr,"ERROR: no such atomic var in g_map_from_ptr_to_atomic_wrapper\n");
	assert(0);

	return NULL;
}

inline void cluster_scheduler_init(AtomicScheduler* pScheduler, volatile void *algorithm_contention_var1, volatile void *algorithm_contention_var2)
{
	AtomicWrapper* atomic_wrapper;

	/* init the first atomic var */
	atomic_wrapper = (AtomicWrapper*) getAlignedMemory(CACHE_LINE_ALIGNED_SIZE, sizeof(AtomicWrapper));
    memset(atomic_wrapper, 0, sizeof(AtomicWrapper));

    atomic_wrapper->active_cluster = -1;
    atomic_wrapper->atomic_ptr = algorithm_contention_var1;

    pScheduler->g_map_from_ptr_to_atomic_wrapper[0] = atomic_wrapper;

    /* init the second atomic var */
	atomic_wrapper = (AtomicWrapper*) getAlignedMemory(CACHE_LINE_ALIGNED_SIZE, sizeof(AtomicWrapper));
    memset(atomic_wrapper, 0, sizeof(AtomicWrapper));

    atomic_wrapper->active_cluster = -1;
    atomic_wrapper->atomic_ptr = algorithm_contention_var2;

    pScheduler->g_map_from_ptr_to_atomic_wrapper[1] = atomic_wrapper;
}

inline void cluster_scheduler_start_op_int(AtomicWrapper* atomic_wrapper, long thread_id)
{
	volatile ThreadData * threadData = &atomic_wrapper->thread_data[thread_id];
	volatile ThreadPerfCounters * threadPerfCounters = &threadData->thread_perf_counters;
	threadData->thread_start_op_timestamp = read_tsc();
	if (threadData->thread_end_op_timestamp) {
		threadPerfCounters->thread_dead_time = (threadPerfCounters->thread_dead_time*999 +
												(threadData->thread_start_op_timestamp - threadData->thread_end_op_timestamp))/1000;
	}

	// check if are waiting for cluster (so the thread_start_op_timestamp need to be updated)
	if (wait_for_cluster(atomic_wrapper, thread_id, threadData->thread_start_op_timestamp)) {
		threadData->thread_start_op_timestamp = read_tsc();
	}
}

inline void cluster_scheduler_end_op_int(AtomicWrapper* atomic_wrapper, long thread_id)
{
	volatile ThreadData * threadData = &atomic_wrapper->thread_data[thread_id];
	volatile ThreadPerfCounters * threadPerfCounters = &threadData->thread_perf_counters;
	threadData->thread_end_op_timestamp = read_tsc();
	threadPerfCounters->thread_in_queue_time = (threadPerfCounters->thread_in_queue_time*999 +
												(threadData->thread_end_op_timestamp - threadData->thread_start_op_timestamp))/1000;
}

inline static void atomic_wrapper_print_counters(AtomicWrapper* atomic_wrapper) {
    int thread_id;
    uint64_t total_dead_time = 0;
    uint64_t total_in_queue_time = 0;
    volatile ThreadPerfCounters * threadPerfCounters;


    for (thread_id=0; thread_id<N_THREADS; thread_id++) {
    	threadPerfCounters = &atomic_wrapper->thread_data[thread_id].thread_perf_counters;
		total_dead_time += threadPerfCounters->thread_dead_time;
		total_in_queue_time += threadPerfCounters->thread_in_queue_time;
    }

    printf("dead_time=%ld ", total_dead_time/N_THREADS);
    printf("in_queue_time=%ld ", total_in_queue_time/N_THREADS);
}

inline void cluster_scheduler_print_counters(AtomicScheduler* pScheduler) {
	if (pScheduler->g_map_from_ptr_to_atomic_wrapper[0]->atomic_ptr) {
		atomic_wrapper_print_counters(pScheduler->g_map_from_ptr_to_atomic_wrapper[0]);
	}
	if (pScheduler->g_map_from_ptr_to_atomic_wrapper[1]->atomic_ptr) {
		atomic_wrapper_print_counters(pScheduler->g_map_from_ptr_to_atomic_wrapper[1]);
	}
}

#endif /* CLUSTER_SCHEDULER_H_ */
