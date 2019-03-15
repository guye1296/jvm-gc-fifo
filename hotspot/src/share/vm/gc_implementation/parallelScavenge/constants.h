#ifndef _CONSTANTS_H_
#define _CONSTANTS_H_


#ifndef CACHE_LINE_SIZE
#define CACHE_LINE_SIZE (64)
#endif

#ifndef CACHE_LINE_ALIGNED_SIZE
#define CACHE_LINE_ALIGNED_SIZE (2*CACHE_LINE_SIZE)
#endif 

#ifndef N_THREADS
#define N_THREADS              80
#endif

#ifndef Object
#define Object size_t
#endif


#endif
