#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <omp.h>
#include <string.h>
#include <stdint.h>

#include "msqueue.h"
#include "config.h"
#include "primitives.h"
#include "backoff.h"
#include "rand.h"
#include "thread.h"

#include "cluster_scheduler.h"

#define POOL_SIZE                  1024

/*********************************************************************
 * THREAD GLOBALS
 *********************************************************************/

__thread node_t *pool_node = null;
__thread int_fast32_t pool_node_index = 0;
__thread BackoffStruct backoff;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

node_t* new_node(void) {
	node_t* node;
    if ( (pool_node_index - 1) < 0) {
        pool_node = getMemory(POOL_SIZE * sizeof(node_t));
        pool_node_index = POOL_SIZE;
    }
    pool_node_index -= 1;
    node = &pool_node[pool_node_index];

    return node;
}

void free_node(volatile node_t* node) {

}

void SHARED_OBJECT_INIT(queue_t* p_msqueue) {
	node_t *node = getMemory(sizeof(node_t)); // Allocate a free node
	node->next= null; // Make it the only node in the linked list
	// Both Head and Tail point to it
	p_msqueue->Head = node;
	p_msqueue->Tail = node;

    if (p_msqueue->FULL) {
        int i;
        for (i = 0; i < RING_SIZE/2; i++) {
            enqueue(p_msqueue, i, 0);
        }
    }
    return;
}


void enqueue(queue_t* p_msqueu, Object arg, int pid) {
	node_t* node = new_node();	// Allocate a new node from the free list
	volatile node_t* tail = null;
	node_t* next = null;
	node->value = arg;	// Copy enqueued value into node
	node->next = null;	// Set next pointer of node to NULL
	reset_backoff(&backoff);
	while (true) {			// Keep trying until Enqueue is done
		tail = p_msqueu->Tail;	// Read Tail.ptr and Tail.count together
		next = tail->next;	// Read next ptr and count fields together
		if (tail == p_msqueu->Tail) {
			// Are tail and next consistent?
			// Was Tail pointing to the last node?
			if (next == null) {
                reset_backoff(&backoff);
                thread_cas_counters.next_cas_trials++;
				// Try to link node at the end of the linked list
				if (CAS64(&tail->next, next, node))
						break;	// Enqueue is done.  Exit loop
				thread_cas_counters.next_cas_failures++;
			} else {
				// Tail was not pointing to the last node
				// Try to swing Tail to the next node
				thread_cas_counters.tail_cas_trials++;
				if (!CAS64(&p_msqueu->Tail, tail, next)) {
					thread_cas_counters.tail_cas_failures++;
				}
				backoff_delay(&backoff);
			}
		}
	}
	// Enqueue is done.  Try to swing Tail to the inserted node
	thread_cas_counters.tail_cas_trials++;
    if (!CAS64(&p_msqueu->Tail, tail, node)){
        thread_cas_counters.tail_cas_failures++;
    }

	return;
}


Object dequeue(queue_t* p_msqueu, int pid) {
	volatile node_t *head, *tail, *next;
    Object value;

    while (true) { // Keep trying until Dequeue is done
        head = p_msqueu->Head; // Read Head
        next = head->next; // read next.ptr and next.count
        if (head == p_msqueu->Tail) { //Is queue empty
        		return NULL; // Queue is empty, couldn't dequeue
			backoff_delay(&backoff);
		} else {
			// No need to deal with Tail
			// Read value before CAS64
			// Otherwise, another dequeue might free the next node
			value = next->value;
			// Try to swing Head to the next node
            thread_cas_counters.head_cas_trials++;
			if (CAS64(&p_msqueu->Head, head, next)) {
				// Dequeue is done.  Exit loop
				break;
			}
			thread_cas_counters.head_cas_failures++;
            backoff_delay(&backoff);
		}
	}
    free_node(head);	// It is safe now to free the old node
	return value; // Queue was not empty, dequeue succeeded
}
