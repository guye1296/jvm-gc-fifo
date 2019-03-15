#include "constants.h"



#ifndef RING_POW
#define RING_POW        (17)
#endif
#define RING_SIZE       (1ull << RING_POW)

#define RING_STATS

// Definition: HAVE_HPTRS
// --------------------
// Define to enable hazard pointer setting for safe memory
// reclamation.  You'll need to integrate this with your
// hazard pointers implementation.
#define HAVE_HPTRS

#define LCRQ_NCLUSTERS NUMBER_OF_CLUSTERS

/*********************************************************************
 * Types
 *********************************************************************/

typedef struct RingNode {
    volatile uint64_t val;
    volatile uint64_t idx;
    uint64_t pad[14];
} RingNode __attribute__ ((aligned (128)));

typedef struct RingQueue {
    volatile int64_t head __attribute__ ((aligned (128)));
    volatile int64_t tail __attribute__ ((aligned (128)));
    volatile int64_t cluster __attribute__ ((aligned (128)));
    struct RingQueue *next __attribute__ ((aligned (128)));
    RingNode array[RING_SIZE];
} RingQueue __attribute__ ((aligned (128)));

typedef struct _hlcrq_t {
    volatile char pad0[CACHE_LINE_ALIGNED_SIZE] __attribute__ ((aligned (128)));

    RingQueue *Head  __attribute__ ((aligned (128)));
    RingQueue *Tail  __attribute__ ((aligned (128)));
    int FULL __attribute__ ((aligned (128)));
    uint64_t closes __attribute__ ((aligned (128)));;
    uint64_t unsafes __attribute__ ((aligned (128)));;

    volatile char pad1[CACHE_LINE_ALIGNED_SIZE] __attribute__ ((aligned (128)));
} hlcrq_t  __attribute__ ((aligned (CACHE_LINE_ALIGNED_SIZE)));

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
extern __thread RingQueue *nrq;
extern __thread RingQueue *hazardptr;

#ifdef RING_STATS
extern __thread uint64_t mycloses;
extern __thread uint64_t myunsafes;
#endif
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SHARED_OBJECT_INIT(hlcrq_t* p_hlcrq);

void enqueue(hlcrq_t* p_hlcrq, Object arg, int pid);
Object dequeue(hlcrq_t* p_hlcrq, int pid);
