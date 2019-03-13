#ifndef MSQUEUE_H_
#define MSQUEUE_H_

#include "config.h"
#include "primitives.h"
#include "backoff.h"

#define POOL_SIZE                  1024

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
extern __thread BackoffStruct backoff;
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/*********************************************************************
 * Types
 *********************************************************************/
/*
 * main structure
 */

typedef struct _node{
	struct _node * next;
    Object value;
} node_t;

typedef struct _queue {
    volatile char pad0[CACHE_LINE_ALIGNED_SIZE] __attribute__ ((aligned (128)));

    int MIN_BAK  __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));
    int MAX_BAK  __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));
    int FULL  __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));


    volatile node_t* Head __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));
    volatile node_t* Tail __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));

    volatile char pad1[CACHE_LINE_ALIGNED_SIZE] __attribute__ ((aligned (128)));
} queue_t  __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

node_t* new_node(void);

void free_node(volatile node_t* node);

void SHARED_OBJECT_INIT(queue_t* p_msqueue);

void enqueue(queue_t* p_msqueue, Object arg, int pid);

Object dequeue(queue_t* p_msqueue, int pid);

#endif
