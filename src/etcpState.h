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

#include <stdbool.h>

#include "HashTable.h"
#include "CircularQueue.h"


#define DST_TAB_MAX_LOG2 (17) //2^17 = 128K dst Adrr/Port pairs, 1MB in memory
#define SRC_TAB_MAX_LOG2 (10) //210  = 1K src Adrr/Port pairs, 8kB in memory
#define MAXSEGS 1024
#define MAXSEGSIZE (2048 - sizeof(etcpConn_t) - sizeof(cqSlot_t)) //Should bound the CQ slots to 1/2 a page
#define MAX_FRAME (2 * 1024)

typedef struct etcpConn_s etcpConn_t;

//The core feature of ETCP is that transmission control is externalised. These two functions provide the interface to that
//feature.

//Returns: >0 this is the number of acknowledgement packets that can be generated. <=0 no ack packets will be generated
typedef int64_t (*etcpRxTc_f)(void* const rxTcState, const cq_t* const datRxQ, const cq_t* const ackTxQ );

typedef void (*etcpTxTc_f)(void* const txTcState, const cq_t* const datTxQ, cq_t* ackTxQ, const cq_t* ackRxQ, bool* const ackFirst, i64* const maxAck_o, i64* const maxDat_o );



//The ETCP internal state expects to be provided with hardware send and receive operations, these typedefs spell them out
//A generic wrapper around the "hardware" tx layer
//Returns: >0, number of bytes transmitted =0, no send capacity, try again, <0 hw specific error code
typedef int64_t (*ethHwTx_f)(void* const hwState, const void* const data, const int64_t len, uint64_t* const hwTxTimeNs );
//Returns: >0, number of bytes received, =0, nothing available right now, <0 hw specific error code
typedef int64_t (*ethHwRx_f)(void* const hwState, const void* const data, const int64_t len, uint64_t* const hwRxTimeNs );



typedef struct etcpState_s {

    void* ethHwState;  //Pointer to HW state structures
    ethHwTx_f ethHwTx; //Callback for abstracting ethernet hardware TX
    ethHwRx_f ethHwRx; //Callback for abstracting ethernet hardware RX

    void* etcpRxTcState;     //Pointer for user supplied Tranmission Control TX state
    etcpRxTc_f etcpRxTc; //Callback for implementing congestion control on the RX side (generating acks).

    void* etcpTxTcState;     //Pointer for user supplied Tranmission Control TX state
    etcpTxTc_f etcpTxTc; //Callback for implementing congestion control on the TX side (sending frames).

    //If you are using event triggered TX, you need to ensure that "send" is called regulalrly to trigger retransmit timeouts
    //ideally this should be called at least 2x as fast as your RTO so that RTOs are sent in a timely maner.
    bool eventTriggeredRx; //Should RX be triggered by a recv socket event, or should there be a thread spinning.
    bool eventTriggeredTx; //Should TX be triggered by a send socket event, or should there be a thread spinning.

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
etcpSrcsMap_t* srcsMapNew( const uint32_t listenWindowSize, const uint32_t listenBuffSize);

#endif /* SRC_ETCPSTATE_H_ */
