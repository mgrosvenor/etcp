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

//Forward declaration to hide the internals from users
struct etcpState_s;
typedef struct etcpState_s etcpState_t;

struct etcpConn_s;
typedef struct etcpConn_s etcpConn_t;


//The ETCP internal state expects to be provided with hardware send and receive operations, these typedefs spell them out
//A generic wrapper around the "hardware" tx layer
//Returns: >0, number of bytes transmitted =0, no send capacity, try again, <0 hw specific error code
typedef int64_t (*ethHwTx_f)(void* const hwState, const void* const data, const uint64_t len, uint64_t* const hwTxTimeNs );
//Returns: >0, number of bytes received, =0, nothing available right now, <0 hw specific error code
typedef int64_t (*ethHwRx_f)(void* const hwState, const void* const data, const uint64_t len, uint64_t* const hwRxTimeNs );

//Setup the ETCP internal state
etcpState_t* newEtcpState(void* const ethHwState, const ethHwTx_f ethHwTx, const ethHwRx_f ethHwRx, const uint64_t maxConnsLog2);

etcpError_t doEtcpUserTx(etcpConn_t* const conn, const i64 connId, const void* const toSendData, i64* const toSendLen_io);
etcpError_t doEtcpUserRx(etcpConn_t* const conn, const i64 connId, void* __restrict data, i64* const len_io);

etcpError_t doEtcpNetTx(etcpConn_t* const conn, i64 connId);
void doEtcpNetRx(etcpConn_t* const conn);



#endif /* SRC_ETCP_H_ */
