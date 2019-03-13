/*
 * hcs_agent.h
 *
 *  Created on: May 5, 2017
 *      Author: zilpal
 */

#ifndef CLUSTER_SCHEDULER_H_
#define CLUSTER_SCHEDULER_H_

/*********************************************************************
 * DEFINES
 *********************************************************************/

/*********************************************************************
 * TYPES
 *********************************************************************/

typedef struct _ThreadRefCount {
    volatile int64_t ref_count __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));
} ThreadRefCount __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));

/*
 * cluster agent
 */
typedef struct _ClusterAgent {
    volatile int64_t is_agent_exists __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));
    volatile int64_t last_successful_sensing_time __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));
    ThreadRefCount thread_ref_count[N_THREADS/NUMBER_OF_CLUSTERS] __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));
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

/*********************************************************************
 * THREAD GLOBALS
 *********************************************************************/

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
#if USE_PERF_COUNTERS
        if ((read_tsc()-start_time) >= wait_for_cluster_timeout) {
            thread_perf_counters.took_due_to_timeout++;
        }
        thread_perf_counters.took_ownership_trial++;
#endif
        if (!CAS64(&atomic_wrapper->active_cluster, owner_cluster, my_cluster)) {
            if (my_cluster == atomic_wrapper->active_cluster) {
#if USE_PERF_COUNTERS
                thread_perf_counters.redundant_ownership_trial++;
#endif
            } else {
#if USE_PERF_COUNTERS
                //printf("scheduler_take_ownership failed my_cluster=%ld took_ownership_failed=%ld\n", my_cluster, thread_perf_counters.took_ownership_failed);
                thread_perf_counters.took_ownership_failed++;
#endif
                is_ownership = false;
            }
        }
        else {
#if USE_PERF_COUNTERS
            cluster_timestamp = read_tsc();
            //fprintf(stderr, "scheduler_take_ownership owner_cluster=%ld my_cluster=%ld\n", owner_cluster, my_cluster);
#endif
        }
    }

    return is_ownership;
}

///////////////////////////////////////////////////////////////////////
// AGENT_SCHEDULING_REF_COUNT:
///////////////////////////////////////////////////////////////////////

inline int pid_to_thread_in_agent(const int pid) {
    return pid/(NUMBER_OF_CLUSTERS);
}

inline bool is_time_to_schedule_ref_count(AtomicWrapper* atomic_wrapper, uint64_t cluster_owner)
{
    uint64_t thread_num;
    int64_t num_of_active_threads = 0;
    for (thread_num=0; thread_num<N_THREADS/NUMBER_OF_CLUSTERS; thread_num++) {
        num_of_active_threads += atomic_wrapper->cluster_array[cluster_owner].thread_ref_count[thread_num].ref_count;
        if ((num_of_active_threads > 0) || (cluster_owner != atomic_wrapper->active_cluster)) {
            //fprintf(stderr, "cluster_owner=%ld, active_cluster=%ld, num_of_active_threads=%ld\n", cluster_owner, active_cluster, num_of_active_threads);
            return false;
        }
    }

    //printf("num_of_scheduling = %ld\n", num_of_scheduling);
    return (num_of_active_threads == 0);
}

inline bool is_time_to_schedule(AtomicWrapper* atomic_wrapper, uint64_t cluster_owner, int64_t pid)
{
    return is_time_to_schedule_ref_count(atomic_wrapper, cluster_owner);
}

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
 * AUTO SCHEDULING STRATEGY CHOICE
 *********************************************************************/

/*********************************************************************
 * API
 *********************************************************************/

inline void cluster_scheduler_start_op_int(AtomicWrapper* atomic_wrapper, long thread_id)
{
    uint64_t my_cluster = pid_to_cluster(thread_id);
    ClusterAgent* myCluster = &atomic_wrapper->cluster_array[my_cluster];
    myCluster->thread_ref_count[pid_to_thread_in_agent(thread_id)].ref_count += 1;
    schedule_cluster_with_agent(atomic_wrapper, thread_id);
    return;
}

inline void cluster_scheduler_end_op_int(AtomicWrapper* atomic_wrapper, long thread_id)
{
    uint64_t cluster_id = pid_to_cluster(thread_id);
    ClusterAgent* my_cluster = &atomic_wrapper->cluster_array[cluster_id];
    my_cluster->thread_ref_count[pid_to_thread_in_agent(thread_id)].ref_count -= 1;
    return;
}

#endif /* CLUSTER_SCHEDULER_H_ */
