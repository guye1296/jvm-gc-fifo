/*
 * defines.h
 *
 *  Created on: Jul 22, 2017
 *      Author: zilpal
 */

#ifndef SRC_DEFINES_H_
#define SRC_DEFINES_H_

/*********************************************************************
 * SCHEDULER CONFIGURATION
 *********************************************************************/
// Definition: SCHEDULING_TICK
// ----------------------------
// rate of making strategy decision
#define SCHEDULING_TICK             MICRO_TO_CYCLES(1000)

// Definition: AGENT_SAMPLING_SLOT_TIME
// ----------------------------
// rate of making strategy decision
#define AGENT_SAMPLING_SLOT_TIME    MICRO_TO_CYCLES(100)

// Definition: AGENT_TIMEOUT
// ----------------------------
// rate of making strategy decision
#define AGENT_TIMEOUT               MICRO_TO_CYCLES(10*1000*1000)

// Definition: USE_NEW_CAS
// ----------------------------
// Whether using a wrapper for CAS operation
#define USE_NEW_CAS 0

///////////////////////////////////////////////////////////////////////
// SENSING_SCHEDULING & AGENT_SCHEDULING_CACHE_MISS
///////////////////////////////////////////////////////////////////////
#define CLUSTER_CACHE_LINE_MISS_LONG_TIME   1200
#define CLUSTER_CACHE_LINE_HIT_TIME         100

#endif /* SRC_DEFINES_H_ */
