/*
 * Copyright (c) 2016, All rights reserved.
 * See LICENSE.txt for full details. 
 * 
 *  Created:   31 Mar 2016
 *  File name: etcpApi.h
 *  Description:
 *  This file describes a socket like interface to ETCP.
 */
#ifndef SRC_ETCPAPI_H_
#define SRC_ETCPAPI_H_

#include "types.h"
#include "etcpState.h"
#include "etcpConn.h"

//Forward declarations to keep internals private
typedef struct etcpSocket_s etcpSocket_t;
typedef struct etcpState_s etcpState_t;

//Make a new "socket" for either listening on or writing to
//A socket is a generic container that holds either a pair of connection read/write queues, or an inbound connection queue
etcpSocket_t* etcpSocketNew(etcpState_t* const etcpState);

//Delete the socket and free all of its resources
void etcpSockeDelete(etcpSocket_t* const sock);

//Set the socket to have an outbound address. Etcp does not have a connection setup phase, so you can immediately send/recv directly on this socket
etcpError_t etcpConnect(etcpSocket_t* const sock, const uint32_t windowSize, const uint64_t buffSize, const uint64_t srcAddr, const uint64_t srcPort, const uint64_t dstAddr, const uint64_t dstPort, const bool doReturn, const i64 vlan, const i64 priority);

//Set the socket to have an inbound address.
etcpError_t etcpBind(etcpSocket_t* const sock, const uint32_t windowSize, const uint32_t buffSize, const uint64_t dstAddr, const uint32_t dstPort, const i64 vlan, const i64 priority);

//Tell the socket to start accepting connections. Backlog sets the length of the queue for unaccepted connections
etcpError_t etcpListen(etcpSocket_t* const sock, uint32_t backlog);

//Dequeue new connections from the listen queue
etcpError_t etcpAccept(etcpSocket_t* const listenSock, etcpSocket_t** const acceptSock_o);

//Send on an etcpSocket
etcpError_t etcpSend(etcpSocket_t* const sock, const void* const toSendData, i64* const toSendLen_io);

//Recv on an etcpSocket
etcpError_t etcpRecv(etcpSocket_t* const sock, void* const data, i64* const len_io);

//Close down the socket and free resources
void etcpClose(etcpSocket_t* const sock);


//Allows the protocol layer to interact with the sockets/mapping layer
etcpError_t addNewConn(etcpState_t* const state, etcpLAMap_t* const srcsMap, const etcpFlowId_t* const flowId, bool noRet, etcpConn_t** const connRecv_o, etcpConn_t** const connSend_o );

#endif /* SRC_ETCPAPI_H_ */
