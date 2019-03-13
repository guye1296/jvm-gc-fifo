#ifndef _STATS_H_
#define _STATS_H_

#define _TRACK_CPU_COUNTERS

#ifdef DEBUG
#include "types.h"
#include "config.h"

int_aligned32_t __failed_cas[N_THREADS] CACHE_ALIGN;
int_aligned32_t __executed_cas[N_THREADS] CACHE_ALIGN;
int_aligned32_t __executed_swap[N_THREADS] CACHE_ALIGN;
int_aligned32_t __executed_faa[N_THREADS] CACHE_ALIGN;

__thread int __stats_thread_id;
#endif

#ifdef _TRACK_CPU_COUNTERS
#include "system.h"
#include "papi.h"

static __thread int __cpu_events = PAPI_NULL;
long long __cpu_values[N_THREADS][6] CACHE_ALIGN;
#endif

static void init_cpu_counters(void) {
#ifdef _TRACK_CPU_COUNTERS
    unsigned long int tid;

    if (PAPI_library_init(PAPI_VER_CURRENT) != PAPI_VER_CURRENT)
        exit(EXIT_FAILURE);
#endif
}
 
static void start_cpu_counters(int id) {
#ifdef DEBUG
     __stats_thread_id = id;
     __failed_cas[id].v = 0;
     __executed_cas[id].v = 0;
     __executed_swap[id].v = 0;
#endif

#ifdef _TRACK_CPU_COUNTERS
    int r;
    if ((r = PAPI_thread_init(pthread_self)) != PAPI_OK) {
       fprintf(stderr, "PAPI_ERROR: unable to initialize thread %d: %s\n", id, PAPI_strerror(r));
       exit(EXIT_FAILURE);
    }
    if ((r = PAPI_create_eventset(&__cpu_events)) != PAPI_OK) {
       fprintf(stderr, "PAPI ERROR: unable to initialize counters: %s\n", PAPI_strerror(r));
       exit(EXIT_FAILURE);
    }
    if ((r = PAPI_add_event(__cpu_events, PAPI_L1_DCM)) != PAPI_OK) {
       fprintf(stderr, "PAPI ERROR: unable to create event L1 data cache misses: %s\n", PAPI_strerror(r));
       exit(EXIT_FAILURE);
    }
    if ((r = PAPI_add_event(__cpu_events, PAPI_L2_TCM)) != PAPI_OK) {
       fprintf(stderr, "PAPI ERROR: unable to create event L2 data cache misses: %s\n", PAPI_strerror(r));
       exit(EXIT_FAILURE);
    }
    if ((r = PAPI_add_event(__cpu_events, PAPI_L3_TCM)) != PAPI_OK) {
       fprintf(stderr, "PAPI ERROR: unable to create event L3 cache misses: %s\n", PAPI_strerror(r));
       exit(EXIT_FAILURE);
    }
    if ((r = PAPI_add_event(__cpu_events, PAPI_RES_STL)) != PAPI_OK) {
       fprintf(stderr, "PAPI ERROR: unable to create event stalls: %s\n", PAPI_strerror(r));
       exit(EXIT_FAILURE);
    }
    if ((r = PAPI_add_event(__cpu_events, PAPI_TOT_CYC)) != PAPI_OK) {
       fprintf(stderr, "PAPI ERROR: unable to create event cycles: %s\n", PAPI_strerror(r));
       exit(EXIT_FAILURE);
    }
    if ((r = PAPI_add_event(__cpu_events, PAPI_TOT_INS)) != PAPI_OK) {
       fprintf(stderr, "PAPI ERROR: unable to create event instructions: %s\n", PAPI_strerror(r));
       exit(EXIT_FAILURE);
    }
    if ((r = PAPI_start(__cpu_events)) != PAPI_OK) {
       fprintf(stderr, "PAPI ERROR: unable to start counter: %s\n", PAPI_strerror(r));
       exit(EXIT_FAILURE);
    }
#endif
}

static void stop_cpu_counters(int id) {
#ifdef _TRACK_CPU_COUNTERS
    int i;

    if (PAPI_read(__cpu_events, __cpu_values[id]) != PAPI_OK) {
        fprintf(stderr, "PAPI ERROR: unable to read counters\n");
        exit(EXIT_FAILURE);
    }
    if (PAPI_stop(__cpu_events, __cpu_values[id]) != PAPI_OK) {
        fprintf(stderr, "PAPI ERROR: unable to stop counters\n");
        exit(EXIT_FAILURE);
    }
#endif
}


static void doPrintStats(float ops, const char *prefix) {
    int i;
#ifdef DEBUG
    int __total_failed_cas = 0;
    int __total_executed_cas = 0;
    int __total_executed_swap = 0;
    int __total_executed_faa = 0;

    for (i = 0; i < N_THREADS; i++) {
        __total_failed_cas += __failed_cas[i].v;
        __total_executed_cas += __executed_cas[i].v;
        __total_executed_swap += __executed_swap[i].v;
        __total_executed_faa += __executed_faa[i].v;
    }

#if 0
    printf("failed_CAS_per_op: %f\t", (float)__total_failed_cas/(N_THREADS * RUNS * N_OBJECTS));
    printf("executed_CAS: %d\t", __total_executed_cas);
    printf("successful_CAS: %d\t", __total_executed_cas - __total_failed_cas);
    printf("executed_SWAP: %d\t", __total_executed_swap);
    printf("executed_FAA: %d\t", __total_executed_faa);
    printf("atomics: %d\t", __total_executed_cas + __total_executed_swap + __total_executed_faa);
    printf("atomics_per_op: %.2f\t", ((float)(__total_executed_cas + __total_executed_swap + __total_executed_faa))/(N_THREADS * RUNS * N_OBJECTS));
    printf("operations_per_CAS: %.2f", (N_THREADS * RUNS * N_OBJECTS)/((float)(__total_executed_cas - __total_failed_cas)));
#endif

#endif

    printf("%sthreads=%d %scpus=%d %sruns=%d ", prefix, N_THREADS, prefix, USE_CPUS, prefix, RUNS);

#ifdef _TRACK_CPU_COUNTERS
    long long __total_cpu_values[6];
    int j;

    for (j = 0; j < 6; j++) {
        __total_cpu_values[j] = 0;
        for (i = 0; i < N_THREADS; i++)
            __total_cpu_values[j] += __cpu_values[i][j];
    }

    /*fprintf(stderr, "L1 data cache misses: %.2f\t"
                    "L2 data cache misses: %.2f\t"
                    "Cycles stalled on any resource: %.2f\t"
                    "Mispredicted branches: %.2f\n",
                    __total_cpu_values[0]/ops, __total_cpu_values[1]/ops,
                    __total_cpu_values[2]/ops, __total_cpu_values[3]/ops);*/

	printf("%sL1_dcm=%.2f "
               "%sL2_dcm=%.2f "
               "%sL3_dcm=%.2f "
               "%sstalls=%.2f "
               "%scycles=%.2f "
               "%sinstructions=%.2f ",
                    prefix, __total_cpu_values[0]/ops, prefix, __total_cpu_values[1]/ops,
                    prefix, __total_cpu_values[2]/ops, prefix, __total_cpu_values[3]/ops,
                    prefix, __total_cpu_values[4]/ops, prefix, __total_cpu_values[5]/ops);
#endif

#ifdef DEBUG
    printf("%satomics_per_op=%.2f ", prefix, ((double)(__total_executed_cas + __total_executed_swap + __total_executed_faa))/((uint64_t)N_THREADS * (uint64_t)RUNS * (uint64_t)N_OBJECTS));

#endif
    if (prefix[0] == '\0')
        printf("\n"); 
}

static void printStats(void) {
    float ops = (uint64_t)RUNS * (uint64_t)N_THREADS * (uint64_t)N_OBJECTS;
    doPrintStats(ops, "");
}

static void printPartialStats(const char *prefix) {
    float ops = (uint64_t)RUNS * (uint64_t)N_THREADS * (uint64_t)N_OBJECTS;
    doPrintStats(ops, prefix);
}

#endif
