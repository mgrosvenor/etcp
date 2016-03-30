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

typedef enum {
    etcpENOERR,       //Success!
    etcpENOMEM,       //Ran out of memory
    etcpEBADPKT,      //Bad packet, not enough bytes for a header
    etcpEALREADY,     //Already connected!
    etcpETOOMANY,     //Too many connections, we've run out!
    etcpENOTCONN,     //Not connected to anything
    etcpECQERR,       //Some issue with a Circular Queue
    etcpERANGE,       //Out of range
    etcpETOOBIG,      //The payload is too big for this buffer
    etcpETRYAGAIN,    //There's nothing to see here, come back again
    etcpEFATAL,       //Something irrecoverably bad happened! Stop the world!
} etcpError_t;

//Forward declaration to hide the internals from users
struct etcpState_s;
typedef struct etcpState_s etcpState_t;

struct etcpConn_s;
typedef struct etcpConn_s etcpConn_t;


//Returns:
//>0, number of bytes transmitted
//>=0, error
typedef int64_t (*ethHwTx_f)(void* const hwState, const void* const data, const int64_t len, int64_t* const hwTxTimeNs );

//Returns:
//>0, number of bytes received
// 0, nothing available right now
//-1, error
typedef int64_t (*ethHwRx_f)(void* const hwState, const void* const data, const int64_t len, int64_t* const hwRxTimeNs );

//Setup the ETCP internal state
etcpState_t* newEtcpState(void* ethHwState, const ethHwTx_f ethHwTx, const ethHwRx_f ethHwRx, const i64 maxConnsLog2);

//Setup a new outbound connection
etcpConn_t* etcpConnect(etcpState_t* etcpState, const i64 windowSize, const i64 buffSize, const i64 srcAddr, const i32 srcPort, const i64 dstAddr, const i32 dstPort);

//Setup an new inbound connection
etcpConn_t* etcpListen(etcpState_t* etcpState, const i64 windowSize, const i64 buffSize, const i64 srcAddr, const i64 dstAddr, const i32 dstPort);

etcpError_t doEtcpUserTx(etcpConn_t* const conn, const i64 connId, const void* const toSendData, i64* const toSendLen_io);
etcpError_t doEtcpUserRx(etcpConn_t* const conn, const i64 connId, void* __restrict data, i64* const len_io);

etcpError_t doEtcpNetTx(etcpConn_t* const conn, i64 connId);
void doEtcpNetRx(etcpConn_t* const conn);

#endif /* SRC_ETCP_H_ */
