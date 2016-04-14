/*
 * Copyright (c) 2016, All rights reserved.
 * See LICENSE.txt for full details. 
 * 
 *  Created:   1 Apr 2016
 *  File name: etcpConn.h
 *  Description:
 *  <INSERT DESCRIPTION HERE> 
 */
#ifndef SRC_ETCPCONN_H_
#define SRC_ETCPCONN_H_


#include "types.h"
#include "etcpConn.h"
#include "CircularQueue.h"
#include "LinkedList.h"

typedef struct etcpState_s etcpState_t;

typedef struct  __attribute__((packed)){
    i32 dstPort;
    i32 srcPort;
    i64 dstAddr;
    i64 srcAddr;
} etcpFlowId_t;


typedef struct etcpConn_s etcpConn_t;
struct etcpConn_s {

    etcpFlowId_t flowId;

    etcpState_t* state; //For working back to the global state

    cq_t* rxQ; //Queue for incoming packets
    cq_t* txQ; //Queue for outgoing packets
    ll_t* staleQ; //An ordered list for holding stale packets that have missed the sequence number RX window.
    i64 lastTxIdx;

    i64 seqAck; //The current acknowledge sequence number
    i64 seqSnd; //The current send sequence number

    //XXX HACKS BELOW!
    i64 vlan; //XXX HACK - this should be in some nice ethernet place, not here.
    i64 priority; //XXX HACK - this should be in some nice ethernet place, not here

};

etcpConn_t* etcpConnNew(etcpState_t* const state, const i64 windowSize, const i32 buffSize, const uint32_t srcAddr, const uint32_t srcPort, const uint64_t dstAddr, const uint32_t dstPort, const i64 vlan, const i64 priority);
void etcpConnDelete(etcpConn_t* const conn );

#endif /* SRC_ETCPCONN_H_ */
