#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <omp.h>
#include <string.h>
#include <stdint.h>

#include "lfstack.h"
#include "config.h"
#include "primitives.h"
#include "backoff.h"
#include "rand.h"
#include "thread.h"

#include "cluster_scheduler.h"

#define POP                        -1

/*********************************************************************
 * THREAD GLOBALS
 *********************************************************************/

// Each thread owns a private copy of the following variables
__thread PoolStruct pool __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));;
__thread BackoffStruct backoff __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



void SHARED_OBJECT_INIT(lfstack_t* p_lfstack) {
	p_lfstack->top = null;
    FullFence();
}

inline void push(lfstack_t* p_lfstack, ArgVal arg) {
    node_t *n;

    n = alloc_obj(&pool);
    reset_backoff(&backoff);
    n->value = arg;
    do {
        node_t *old_top = (node_t *)p_lfstack->top;   // top is volatile
        n->next = old_top;
        thread_cas_counters.cas_trials++;
        if (NEW_CASPTR(&p_lfstack->top, old_top, n) == true) {
            break;
        } else {
            thread_cas_counters.cas_failures++;
            backoff_delay(&backoff);
        }
    } while(true);
}

inline RetVal pop(lfstack_t* p_lfstack) {
    reset_backoff(&backoff);
    do {
        node_t *old_top = (node_t *) p_lfstack->top;
        if (old_top == null)
            return (RetVal)0;
        thread_cas_counters.cas_trials++;
        if(NEW_CASPTR(&p_lfstack->top, old_top, old_top->next)) {
            return old_top->value;
        }
        else {
            thread_cas_counters.cas_failures++;
            backoff_delay(&backoff);
        }
    } while (true) ;
}
