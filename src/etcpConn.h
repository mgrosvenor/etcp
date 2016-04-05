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

    cq_t* datRxQ; //Queue for incoming packets
    cq_t* datTxQ; //Queue for outgoing packets
    i64 lastDatTxIdx;

    cq_t* ackTxQ; //Queue for outgoing acknowledgement packets
    i64 lastAckTxIdx;

    cq_t* ackRxQ; //Queue for outgoing acknowledgement packets

    i64 seqAck; //The current acknowledge sequence number
    i64 seqSnd; //The current send sequence number

    i64 retransTimeOut; //How long to wait before attempting a retransmit

    //XXX HACKS BELOW!
    int16_t vlan; //XXX HACK - this should be in some nice ethernet place, not here.
    uint8_t priority; //XXX HACK - this should be in some nice ethernet place, not here

};

etcpConn_t* etcpConnNew(const i64 windowSize, const i32 buffSize, const uint32_t srcAddr, const uint32_t srcPort, const uint64_t dstAddr, const uint32_t dstPort);
void etcpConnDelete(etcpConn_t* const conn);

#endif /* SRC_ETCPCONN_H_ */
