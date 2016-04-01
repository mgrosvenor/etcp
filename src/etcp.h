/*
 * Copyright (c) 2016, All rights reserved.
 * See LICENSE.txt for full details. 
 * 
 *  Created:   30 Mar 2016
 *  File name: etcp.h
 *  Description:
 *  <INSERT DESCRIPTION HERE> 
 */
#ifndef SRC_ETCP_H_
#define SRC_ETCP_H_

#include "types.h"
#include "CircularQueue.h"

typedef struct etcpConn_s etcpConn_t;

etcpConn_t* etcpConnNew(const i64 windowSize, const i32 buffSize, const uint32_t srcAddr, const uint32_t srcPort, const uint64_t dstAddr, const uint32_t dstPort);
void etcpConnDelete(etcpConn_t* const conn);

etcpError_t doEtcpUserTx(etcpConn_t* const conn, const void* const toSendData, i64* const toSendLen_io);
etcpError_t doEtcpUserRx(etcpConn_t* const conn, void* __restrict data, i64* const len_io);

etcpError_t doEtcpNetTx(etcpConn_t* const conn, i64 connId);
void doEtcpNetRx(etcpConn_t* const conn);



#endif /* SRC_ETCP_H_ */
