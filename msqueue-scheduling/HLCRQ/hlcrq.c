#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <omp.h>
#include <string.h>
#include <stdint.h>

#include "hlcrq.h"
#include "config.h"
#include "primitives.h"
#include "rand.h"
#include "thread.h"

#include "cluster_scheduler.h"

#if 0
// Definition: RING_POW
// --------------------
// The LCRQ's ring size will be 2^{RING_POW}.
#ifndef RING_POW
#define RING_POW        (17)
#endif
#define RING_SIZE       (1ull << RING_POW)

// Definition: RING_STATS
// --------------------
// Define to collect statistics about CRQ closes and nodes
// marked unsafe.
//#define RING_STATS
#endif

// Definition: HAVE_HPTRS
// --------------------
// Define to enable hazard pointer setting for safe memory
// reclamation.  You'll need to integrate this with your
// hazard pointers implementation.
#define HAVE_HPTRS

#define LCRQ_NCLUSTERS NUMBER_OF_CLUSTERS


/*********************************************************************
 * THREAD GLOBALS
 *********************************************************************/

__thread RingQueue *nrq __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));
__thread RingQueue *hazardptr __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));

#ifdef RING_STATS
__thread uint64_t mycloses __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));
__thread uint64_t myunsafes __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));
#endif
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


//const char* ALGORITHM = "hlcrq";

inline int is_empty(uint64_t v) __attribute__ ((pure));
inline uint64_t node_index(uint64_t i) __attribute__ ((pure));
inline uint64_t set_unsafe(uint64_t i) __attribute__ ((pure));
inline uint64_t node_unsafe(uint64_t i) __attribute__ ((pure));
inline uint64_t tail_index(uint64_t t) __attribute__ ((pure));
inline int crq_is_closed(uint64_t t) __attribute__ ((pure));

inline void init_ring(RingQueue *r) {
    int i;

    for (i = 0; i < RING_SIZE; i++) {
        r->array[i].val = -1;
        r->array[i].idx = i;
    }

    r->head = r->tail = 0;
    r->cluster = -1;
    r->next = null;
}

inline int is_empty(uint64_t v)  {
    return (v == (uint64_t)-1);
}


inline uint64_t node_index(uint64_t i) {
    return (i & ~(1ull << 63));
}


inline uint64_t set_unsafe(uint64_t i) {
    return (i | (1ull << 63));
}


inline uint64_t node_unsafe(uint64_t i) {
    return (i & (1ull << 63));
}


inline uint64_t tail_index(uint64_t t) {
    return (t & ~(1ull << 63));
}


inline int crq_is_closed(uint64_t t) {
    return (t & (1ull << 63)) != 0;
}


void SHARED_OBJECT_INIT(hlcrq_t* p_hlcrq) {
     int i;

     RingQueue *rq = getMemory(sizeof(RingQueue));
     init_ring(rq);
     p_hlcrq->Head = p_hlcrq->Tail = rq;

     if (p_hlcrq->FULL) {
         // fill ring
         for (i = 0; i < RING_SIZE/2; i++) {
             rq->array[i].val = 0;
             rq->array[i].idx = i;
             rq->tail++;
         }
         p_hlcrq->FULL = 0;
    }
}


inline void fixState(RingQueue *rq) {

    uint64_t t, h, n;

    while (1) {
        uint64_t t = FAA64(&rq->tail, 0);
        uint64_t h = FAA64(&rq->head, 0);

        if (unlikely(rq->tail != t))
            continue;

        if (h > t) {
            if (CAS64(&rq->tail, t, h)) break;
            continue;
        }
        break;
    }
}


#ifdef RING_STATS
inline void count_closed_crq(void) {
    mycloses++;
}


inline void count_unsafe_node(void) {
    myunsafes++;
}
#else
inline void count_closed_crq(void) { }
inline void count_unsafe_node(void) { }
#endif


inline int close_q(RingQueue *rq, const uint64_t t, const int tries) {
    /*
     * ZILPA: Why do we do these tries and not set this bit immediately?
     */
    if (tries < 10)
        return CAS64(&rq->tail, t + 1, (t + 1)|(1ull<<63));
    else
        return BIT_TEST_AND_SET(&rq->tail, 63);
}

void enqueue(hlcrq_t* p_hlcrq, Object arg, int pid) {

    int try_close = 0;

    while (1) {
        RingQueue *rq = p_hlcrq->Tail;

#ifdef HAVE_HPTRS
        SWAP(&hazardptr, rq);
        if (unlikely(p_hlcrq->Tail != rq))
            continue;
#endif

        RingQueue *next = rq->next;

        if (unlikely(next != null)) {
            CASPTR(&p_hlcrq->Tail, rq, next);
            continue;
        }

        uint64_t t = FAA64(&rq->tail, 1);


        /*
         * ZILPA: why not using unlikely?
         */
        if (crq_is_closed(t)) {
alloc:
            if (nrq == null) {
                nrq = getMemory(sizeof(RingQueue));
                init_ring(nrq);
            }

            // Solo enqueue
            nrq->tail = 1, nrq->array[0].val = arg, nrq->array[0].idx = 0;

            if (CASPTR(&rq->next, null, nrq)) {
                CASPTR(&p_hlcrq->Tail, rq, nrq);
                nrq = null;
                return;
            }
            continue;
        }

        RingNode* cell = &rq->array[t & (RING_SIZE-1)];
        StorePrefetch(cell);

        uint64_t idx = cell->idx;
        uint64_t val = cell->val;

        /*
         * ZILPA: is this command correct?
         * Check if there was another enqueue that pass me (after a full round of enqueues while we were stalled on lines 363-367)
         * and reach that cell and set a value.
         * but we'll catch it latter when checking 'idx'
         *
         * This is an optimization BECAUSE We check it also in the following CAS2
         */
        if (likely(is_empty(val))) {
            /*
             * ZILPA:
             * Check if there was another enqueue that pass me (after a full round of enqueues while we were stalled on lines 363-366)
             * and reach that cell and set a new 'idx', if it happened,
             * then the "old" 't' that we saved will be smaller than the 'idx' we saved
             *
             * This is not an optimization because we check that "idx" value in the following CAS2
             */
            if (likely(node_index(idx) <= t)) {
            	/*
            	 * ZILPA: is this command correct?
            	 * If there was a dequeue on this cell and the cell was empty, we marked it as unsafe.
            	 * The unsafe tell us that the dequeue pass our cell and this cell is invalid for use
            	 * (no one will dequeue it).
            	 * But, if it unsafe and the head id smaller than our location, then the cell is safe.
            	 * It might happen when this cell was unsafe in the past and we skipped it and now,
            	 * when "all good", we revisit it and it holds the old 'idx' value
            	 */
                if ((likely(!node_unsafe(idx)) || rq->head < t) &&
                		/*
                		 * ZILPA: is this command correct?
                		 * Insure the two above checks we did (that no one pass us and used this cell)
                		 */
                		CAS2((uint64_t*)cell, -1, idx, arg, t)) {
                    return;
                }
            }
        }

        uint64_t h = rq->head;

        /*
		 * ZILPA: I think it'll be better to check:
		 * (int64_t)(t - h) >= (int64_t)(RING_SIZE + USE_CPUS))
		 * I can think about a corner case where the queue if full:
		 * 1. Thread 'A' start the enqueue, fail in line 375
		 * 2. Thread 'B' dequeue successfully
		 * 3. Thread 'A' now pass the first condition of "((int64_t)(t - h) >= (int64_t)RING_SIZE" and continue
		 * 4. Thread 'C' enqueue successfully
		 * 5. goto 1
		 *
		 * That means that every 3 thread will do 2 operations instead of 3.
		 * If will leave free room for all of them, they will always success or open a new ring
		 *
		 * There are more complex scenarios that will cost more than 33%
		 */
        if (unlikely((int64_t)(t - h) >= (int64_t)RING_SIZE) && close_q(rq, t, ++try_close)) {
            count_closed_crq();
            goto alloc;
        }
    }
}

Object dequeue(hlcrq_t* p_hlcrq, int pid) {

    while (1) {
        RingQueue *rq = p_hlcrq->Head;
        RingQueue *next;

        /*
         * ZILPA: same as above, why not using unlikely?
         */
#ifdef HAVE_HPTRS
        SWAP(&hazardptr, rq);
        if (unlikely(p_hlcrq->Head != rq))
            continue;
#endif

        uint64_t h = FAA64(&rq->head, 1);


        RingNode* cell = &rq->array[h & (RING_SIZE-1)];
        StorePrefetch(cell);

        uint64_t tt;
        int r = 0;

        while (1) {

            uint64_t cell_idx = cell->idx;
            uint64_t unsafe = node_unsafe(cell_idx);
            uint64_t idx = node_index(cell_idx);
            uint64_t val = cell->val;

            /*
             * ZILPA: is this command correct?
             * Check if there was another dequeue that pass me (after a full round of dequeue while we were stalled on lines 451-459)
             */
            if (unlikely(idx > h)) break;

            /*
             * ZILPA: is this command correct?
             * Check if the cell is empty, it might happen if:
             * 1. The head pass the tail (dequeue before enqueue)
             * 2. Another dequeue pass me (after a full round of dequeue while we were stalled on lines 460-462)
             */
            if (likely(!is_empty(val))) {
            	/*
				 * ZILPA: how can it happen that 'idx' != 'h'
				 */
                if (likely(idx == h)) {
                    if (CAS2((uint64_t*)cell, val, cell_idx, -1, unsafe | h + RING_SIZE))
                        return val;
                } else {
                    if (CAS2((uint64_t*)cell, val, cell_idx, val, set_unsafe(idx))) {
                        count_unsafe_node();
                        break;
                    }
                }
            } else {
                if ((r & ((1ull << 10) - 1)) == 0)
                    tt = rq->tail;

                // Optimization: try to bail quickly if queue is closed.
                int crq_closed = crq_is_closed(tt);
                uint64_t t = tail_index(tt);

                if (unlikely(unsafe)) { // Nothing to do, move along
                    if (CAS2((uint64_t*)cell, val, cell_idx, val, unsafe | h + RING_SIZE))
                        break;
                } else if (t < h + 1 || r > 200000 || crq_closed) {
                    if (CAS2((uint64_t*)cell, val, idx, val, h + RING_SIZE))
                        break;
                } else {
                    ++r;
                }
            }
        }

        if (tail_index(rq->tail) <= h + 1) {
            fixState(rq);
            // try to return empty
            next = rq->next;
            if (next == null)
                return NULL;  // EMPTY
            else
                printf("NOT NULL OMG\n");
            if (tail_index(rq->tail) <= h + 1) {
                CASPTR(&p_hlcrq->Head, rq, next);
                fprintf(stderr, "CASPTR(&head, rq, next)!!!!!!!!!!!!!!!!!!!!!!!!!!!");
            }
        }
    }


    /*
     * ZILPA: why break and not continue?
     * Check if there was another dequeue that pass me (after a full round of dequeue and then enqueue on this specific call)
     */
}
