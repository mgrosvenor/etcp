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
#include "etcpState.h"
#include "etcpConn.h"

etcpError_t doEtcpUserTx(etcpConn_t* const conn, const void* const toSendData, i64* const toSendLen_io);
etcpError_t doEtcpUserRx(etcpConn_t* const conn, void* __restrict data, i64* const len_io);

etcpError_t doEtcpNetTx(etcpConn_t* const conn, const i64 ackFirst, const i64 maxAckSlots, const i64 maxDatSlots);
void doEtcpNetRx(etcpState_t* state);
etcpError_t generateAcks(etcpConn_t* const conn, const i64 maxAckPackets, const i64 maxSlots);


#endif /* SRC_ETCP_H_ */
