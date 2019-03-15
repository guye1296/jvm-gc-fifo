#ifndef _NUMA_QUEUE_H
#define _NUMA_QUEUE_H

#include "numa_globals.h"

extern void numa_enqueue(Globals* context, Object arg, int pid);

extern Object numa_dequeue(Globals* context, int pid);

extern Globals* create_global_context();

#endif //_NUMA_QUEUE_H
