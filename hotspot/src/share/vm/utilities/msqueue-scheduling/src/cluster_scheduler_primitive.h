/*
 * cluster_scheduler_primitive.h
 *
 *  Created on: Oct 6, 2018
 *      Author: zilpal
 */

#ifndef CLUSTER_SCHEDULER_SRC_CLUSTER_SCHEDULER_PRIMITIVE_H_
#define CLUSTER_SCHEDULER_SRC_CLUSTER_SCHEDULER_PRIMITIVE_H_

/*
 * hcs_agent.h
 *
 *  Created on: May 5, 2017
 *      Author: zilpal
 */

/*********************************************************************
 * TYPES
 *********************************************************************/
/*
 * cluster agent
 */
typedef struct _ClusterAgent {
    volatile int64_t is_agent_exists __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));
} ClusterAgent __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));

/*
 * main structure
 */
typedef struct _AtomicWrapper {
    volatile void *atomic_ptr1 __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));
    volatile void *atomic_ptr2 __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));
    ClusterAgent cluster_array[NUMBER_OF_CLUSTERS] __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));
    volatile int64_t active_cluster __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));
} AtomicWrapper  __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/*********************************************************************
 * SCHEDULING METHODS
 *********************************************************************/

///////////////////////////////////////////////////////////////////////
// COMMON
///////////////////////////////////////////////////////////////////////

inline bool scheduler_take_ownership(AtomicWrapper* atomic_wrapper, uint64_t owner_cluster, uint64_t my_cluster, uint64_t start_time, uint64_t wait_for_cluster_timeout) {
    bool is_ownership = true;
    if ((owner_cluster != my_cluster) && (atomic_wrapper->active_cluster != my_cluster))
    {
        /*
        if ((read_tsc()-start_time) >= wait_for_cluster_timeout) {
            thread_perf_counters.took_due_to_timeout++;
        }
        thread_perf_counters.took_ownership_trial++;
        */
        if (!CAS64(&atomic_wrapper->active_cluster, owner_cluster, my_cluster)) {
            if (my_cluster != atomic_wrapper->active_cluster) {
                //printf("scheduler_take_ownership failed my_cluster=%ld took_ownership_failed=%ld\n", my_cluster, thread_perf_counters.took_ownership_failed);
                //thread_perf_counters.took_ownership_failed++;
                is_ownership = false;
            }
            /*else {
                thread_perf_counters.redundant_ownership_trial++;
            }
            */
        }
        /*else {
            cluster_timestamp = read_tsc();
            //fprintf(stderr, "scheduler_take_ownership owner_cluster=%ld my_cluster=%ld\n", owner_cluster, my_cluster);
        }
        */
    }
    return is_ownership;
}

inline bool is_time_to_schedule(AtomicWrapper* atomic_wrapper, uint64_t cluster_owner, int64_t pid)
{
    return true;
}
///////////////////////////////////////////////////////////////////////
//
///////////////////////////////////////////////////////////////////////

// return if we took ownership
inline void schedule_cluster_with_agent(AtomicWrapper* atomic_wrapper, int64_t pid) {
    uint64_t my_cluster = pid_to_cluster(pid);
    uint64_t cluster_owner = atomic_wrapper->active_cluster;
    ClusterAgent* myCluster = &atomic_wrapper->cluster_array[my_cluster];
    uint64_t start_time = read_tsc();
    uint64_t is_agent_exists_exists = 0;
    uint64_t am_i_agent = 0;
    uint64_t num_of_time_slots = AGENT_TIMEOUT/AGENT_SAMPLING_SLOT_TIME;

    while (cluster_owner != my_cluster) {
        int i;
        for (i=0; i<num_of_time_slots; i++) {
            cluster_owner = atomic_wrapper->active_cluster;
            while ((cluster_owner != -1) && (cluster_owner != my_cluster)
                    && ((read_tsc()-start_time) < (i+1)*AGENT_SAMPLING_SLOT_TIME)
                    && (myCluster->is_agent_exists == 1)
                    ){
                cluster_owner = atomic_wrapper->active_cluster;
            }

            if (cluster_owner == -1) {
                am_i_agent = 1;
                break;
            }

            if ((cluster_owner == my_cluster)){
                if (am_i_agent == 1) {
                    myCluster->is_agent_exists = 0;
                }
                return;
            }

            if ((am_i_agent == 0) && (myCluster->is_agent_exists == 1)) {
                continue;
            }
            if (am_i_agent == 0) {
                if ((myCluster->is_agent_exists == 0) && CAS64(&myCluster->is_agent_exists, 0, 1)) {
                    am_i_agent = 1;
					continue;
                }
            }

            if ((am_i_agent == 1) && is_time_to_schedule(atomic_wrapper, cluster_owner, pid)) {
                break;
            }
        }

        if ((am_i_agent != 1) && (read_tsc()-start_time) < AGENT_TIMEOUT) {
            fprintf(stderr,"am_i_agent=0, but got out as owner %d\n", (cluster_owner == my_cluster));
            assert(0);
        }
        myCluster->is_agent_exists = 0;
        if (scheduler_take_ownership(atomic_wrapper, cluster_owner, my_cluster, start_time, AGENT_TIMEOUT) == true){
            return;
        }

        start_time = read_tsc();
        cluster_owner = atomic_wrapper->active_cluster;
    }

    return;
}

/*********************************************************************
 * API
 *********************************************************************/

inline void cluster_scheduler_start_op_int(AtomicWrapper* atomic_wrapper, long thread_id)
{
    schedule_cluster_with_agent(atomic_wrapper, thread_id);
}

inline void cluster_scheduler_end_op_int(AtomicWrapper* atomic_wrapper, long thread_id) {}

inline void atomic_wrapper_print_counters(AtomicWrapper* atomic_wrapper) {}

#endif /* CLUSTER_SCHEDULER_SRC_CLUSTER_SCHEDULER_PRIMITIVE_H_ */
