/*
 * Copyright (c) 2016, All rights reserved.
 * See LICENSE.txt for full details. 
 * 
 *  Created:   1 Apr 2016
 *  File name: etcpConn.c
 *  Description:
 *  <INSERT DESCRIPTION HERE> 
 */


#include <stddef.h>
#include <stdlib.h>

#include "utils.h"
#include "debug.h"
#include "etcpConn.h"


void etcpConnDelete(etcpConn_t* const conn)
{
    if_unlikely(!conn){ return; }

    if_likely(conn->datTxQ != NULL){ cqDelete(conn->datTxQ); }
    if_likely(conn->datRxQ != NULL){ cqDelete(conn->datRxQ); }
    if_likely(conn->ackTxQ != NULL){ cqDelete(conn->ackTxQ); }
    if_likely(conn->ackRxQ != NULL){ cqDelete(conn->ackRxQ); }

    free(conn);

}


etcpConn_t* etcpConnNew(etcpState_t* const state, const i64 windowSize, const i32 buffSize, const uint32_t srcAddr, const uint32_t srcPort, const uint64_t dstAddr, const uint32_t dstPort, const i64 vlan, const i64 priority)
{
    etcpConn_t* conn = calloc(1, sizeof(etcpConn_t));
    if_unlikely(!conn){ return NULL; }

    conn->datRxQ = cqNew(buffSize,windowSize);
    if_unlikely(conn->datRxQ == NULL){
        etcpConnDelete(conn);
        return NULL;
    }

    conn->datTxQ = cqNew(buffSize,windowSize);
    if_unlikely(conn->datTxQ == NULL){
        etcpConnDelete(conn);
        return NULL;
    }

    conn->ackRxQ = cqNew(buffSize,windowSize);
    if_unlikely(conn->ackRxQ == NULL){
        etcpConnDelete(conn);
        return NULL;
    }

    conn->ackTxQ = cqNew(buffSize,windowSize);
    if_unlikely(conn->ackTxQ == NULL){
        etcpConnDelete(conn);
        return NULL;
    }

    conn->flowId.srcAddr = srcAddr;
    conn->flowId.srcPort = srcPort;
    conn->flowId.dstAddr = dstAddr;
    conn->flowId.dstPort = dstPort;

    conn->state = state;

    conn->vlan     = vlan;
    conn->priority = priority;

    return conn;
}
