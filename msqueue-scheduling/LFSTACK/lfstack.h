#include "config.h"
#include "primitives.h"
#include "backoff.h"
#include "pool.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
extern __thread BackoffStruct backoff;
extern __thread PoolStruct pool;
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

typedef struct _lfstack_t {
    volatile char pad0[CACHE_LINE_ALIGNED_SIZE] __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));

    int MIN_BAK  __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));
    int MAX_BAK  __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));

    volatile node_t *top __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));

    volatile char pad1[CACHE_LINE_ALIGNED_SIZE] __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));
} lfstack_t  __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void SHARED_OBJECT_INIT(lfstack_t* p_lfstack);

inline void push(lfstack_t* p_lfstack, ArgVal arg);

inline RetVal pop(lfstack_t* p_lfstack);
