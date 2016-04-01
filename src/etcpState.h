/*
 * Copyright (c) 2016, All rights reserved.
 * See LICENSE.txt for full details. 
 * 
 *  Created:   1 Apr 2016
 *  File name: etcpState.h
 *  Description:
 *  Holds the global state for the etcp system
 */
#ifndef SRC_ETCPSTATE_H_
#define SRC_ETCPSTATE_H_

#include "HashTable.h"
#include "CircularQueue.h"

#define DST_TAB_MAX_LOG2 (17) //2^17 = 128K dst Adrr/Port pairs, 1MB in memory
#define SRC_TAB_MAX_LOG2 (10) //210  = 1K src Adrr/Port pairs, 8kB in memory
#define MAXSEGS 1024
#define MAXSEGSIZE (2048 - sizeof(etcpConn_t) - sizeof(cqSlot_t)) //Should bound the CQ slots to 1/2 a page
#define MAX_FRAME (2 * 1024)

typedef struct etcpConn_s etcpConn_t;

//The ETCP internal state expects to be provided with hardware send and receive operations, these typedefs spell them out
//A generic wrapper around the "hardware" tx layer
//Returns: >0, number of bytes transmitted =0, no send capacity, try again, <0 hw specific error code
typedef int64_t (*ethHwTx_f)(void* const hwState, const void* const data, const uint64_t len, uint64_t* const hwTxTimeNs );
//Returns: >0, number of bytes received, =0, nothing available right now, <0 hw specific error code
typedef int64_t (*ethHwRx_f)(void* const hwState, const void* const data, const uint64_t len, uint64_t* const hwRxTimeNs );


typedef struct etcpState_s {

    void* ethHwState;
    ethHwTx_f ethHwTx;
    ethHwRx_f ethHwRx;

    ht_t* dstMap; //All unique dst address/port combinations
} etcpState_t;


//This structure contains all unique source address/port combination connections for a given destination address/port combination.
typedef struct etcpSrcConns_s etcpSrcsMap_t;
typedef struct etcpSrcConns_s {

    //These are for new connections that happen when we're listening
    uint32_t listenWindowSize;
    uint32_t listenBuffSize;

    cq_t* listenQ; //Queue containing connections that have not yet been accepted

    ht_t* table; //All unique src address/port combinations

} etcpSrcsMap_t;


etcpState_t* etcpStateNew(void* const ethHwState, const ethHwTx_f ethHwTx, const ethHwRx_f ethHwRx);
etcpSrcsMap_t* srcConnsNew( const uint32_t listenWindowSize, const uint32_t listenBuffSize);

#endif /* SRC_ETCPSTATE_H_ */
