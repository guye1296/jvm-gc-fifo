/*
 * cluster_sceduler_cache_miss.h
 *
 *  Created on: Aug 25, 2017
 *      Author: zilpal
 */

#ifndef SRC_CLUSTER_SCHEDULER_CACHE_MISS_H_
#define SRC_CLUSTER_SCHEDULER_CACHE_MISS_H_

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
    ClusterAgent cluster_array[NUMBER_OF_CLUSTERS] __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));;
    volatile int64_t active_cluster __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));
    volatile int64_t cluster_take_ownership_busy __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));
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
        if (!CAS64(&atomic_wrapper->active_cluster, owner_cluster, my_cluster)) {
            if (my_cluster != atomic_wrapper->active_cluster) {
                is_ownership = false;
            }
        }
    }

    return is_ownership;
}

///////////////////////////////////////////////////////////////////////
// AGENT_SCHEDULING_CACHE_MISS:
///////////////////////////////////////////////////////////////////////

inline bool is_time_to_schedule_cache_miss_time(AtomicWrapper* atomic_wrapper, uint64_t cluster_owner)
{
    uint64_t t1;
    uint64_t t2;
    uint64_t diff;
    uint64_t start_time;
    uint64_t end_time;

    start_time = read_tsc();
    if (atomic_wrapper->cluster_take_ownership_busy == 0) {
        if (!CAS64(&atomic_wrapper->cluster_take_ownership_busy, 0, 1)){
            return false;
        }
    } else {
        return false;
    }

    t1 = read_tsc();
    // fetch the cache line
    if (atomic_wrapper->atomic_ptr1) {
        if (atomic_wrapper->active_cluster!=cluster_owner) {
            goto exit_fail;
        }
        CAS64(atomic_wrapper->atomic_ptr1, 0, 0);

        t2 = read_tsc();
        diff = t2-t1;
        // return if cache miss too long
        if (diff > CLUSTER_CACHE_LINE_MISS_LONG_TIME){
            goto exit_fail;
        }

        if (atomic_wrapper->active_cluster!=cluster_owner) {
            goto exit_fail;
        }

        CAS64(atomic_wrapper->atomic_ptr1, 0, 0);

        t1 = read_tsc();
        diff = t1-t2;
        // return if cache miss too long
        if (diff > CLUSTER_CACHE_LINE_HIT_TIME){
            goto exit_fail;
        }
    }

    if (atomic_wrapper->atomic_ptr2) {
        if (atomic_wrapper->active_cluster!=cluster_owner) {
            goto exit_fail;
        }
        CAS64(atomic_wrapper->atomic_ptr2, 0, 0);

        t2 = read_tsc();
        diff = t2-t1;
        // return if cache miss too long
        if (diff > CLUSTER_CACHE_LINE_MISS_LONG_TIME){
            goto exit_fail;
        }

        if (atomic_wrapper->active_cluster!=cluster_owner) {
            goto exit_fail;
        }

        CAS64(atomic_wrapper->atomic_ptr2, 0, 0);

        t1 = read_tsc();
        diff = t1-t2;
        // return if cache miss too long
        if (diff > CLUSTER_CACHE_LINE_HIT_TIME){
            goto exit_fail;
        }
    }

    atomic_wrapper->cluster_take_ownership_busy = 0;

    return true;

exit_fail:
    atomic_wrapper->cluster_take_ownership_busy = 0;
    return false;
}

///////////////////////////////////////////////////////////////////////
// COMMON BOTH FOR AGENT_SCHEDULING_REF_COUNT & AGENT_SCHEDULING_CACHE_MISS:
///////////////////////////////////////////////////////////////////////

inline bool is_time_to_schedule(AtomicWrapper* atomic_wrapper, uint64_t cluster_owner, int64_t pid)
{
    return is_time_to_schedule_cache_miss_time(atomic_wrapper, cluster_owner);
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
 * API
 *********************************************************************/

inline void cluster_scheduler_start_op_int(AtomicWrapper* atomic_wrapper, long thread_id)
{
    schedule_cluster_with_agent(atomic_wrapper, thread_id);
}

inline void cluster_scheduler_end_op_int(AtomicWrapper* atomic_wrapper, long thread_id) {}

inline void atomic_wrapper_print_counters(AtomicWrapper* atomic_wrapper) {}

#endif /* SRC_CLUSTER_SCHEDULER_CACHE_MISS_H_ */
