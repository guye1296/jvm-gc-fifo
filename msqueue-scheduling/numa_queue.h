#ifndef _NUMA_QUEUE_H
#define _NUMA_QUEUE_H


#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <omp.h>
#include <string.h>
#include <stdint.h>

#include "config.h"
#include "primitives.h"
#include "backoff.h"
#include "rand.h"
#include "thread.h"

#ifdef MSQUEUE
#include "msqueue.h"
#else
#include "hlcrq.h"
#endif

#include "numa_globals.h"
#include "cluster_scheduler.h"

extern inline void numa_enqueue(Globals* context, Object arg, int pid);

extern inline Object numa_dequeue(Globals* context, int pid);

extern inline Globals* create_global_context();

#endif //_NUMA_QUEUE_H
