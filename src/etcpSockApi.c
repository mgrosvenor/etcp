/*
 * Copyright (c) 2016, All rights reserved.
 * See LICENSE.txt for full details. 
 * 
 *  Created:   31 Mar 2016
 *  File name: etcpSockApi.c
 *  Description:
 *  <INSERT DESCRIPTION HERE> 
 */

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

#include "etcpSockApi.h"
#include "etcp.h"


//This structure contains all unique source address/port combination connections for a given destination address/port combination.
typedef struct etcpSrcConnTable_s etcpSrcConnTable_t;
typedef struct etcpSrcConnTable_s {
    uint64_t dstAddr;
    uint64_t dstPort;

    etcpState_t* etcpState; //Back reference to the etcp global state

    i64 listenQMax; //Maximum length of listen queue before connections are auto rejected. If 0, the socket is not listening
    i64 listenQCount; //Current length of the listen queue
    etcpConn_t* listenQ; //Queue containing connections that have not yet been accepted

    i64 connsHTMaxLog2;
    i64 connsHTMMax;
    etcpConn_t** connsHashTable; //Queue containing all connections

    etcpSrcConnTable_t* next; //For chaining elements in the dstConTable hash table

} etcpSrcConnTable_t;


typedef struct etcpDstConnTable_s{
    i64 connTablesHTMaxLog2;
    i64 connTablesHTMMax;
    etcpSrcConnTable_t** connTablesHT; //Queue containing all connections
} etcpDstConnTable_s;


struct etcpState_s {

    void* ethHwState;
    ethHwTx_f ethHwTx;
    ethHwRx_f ethHwRx;


};


void deleteEtcpState(etcpState_t* etcpState)
{
    if(!etcpState){
        return;
    }

//    if(etcpState->connsHashTable){
//        free(etcpState->connsHashTable);
//    }

    free(etcpState);
}


etcpState_t* newEtcpState(void* const ethHwState, const ethHwTx_f ethHwTx, const ethHwRx_f ethHwRx, const uint64_t maxConnsLog2)
{
    etcpState_t* etcpState = calloc(1,sizeof(etcpState_t));
    if(!etcpState){
        return NULL;
    }

//    etcpState->maxConnsLog2 = maxConnsLog2;
//    etcpState->maxConns     = 1 << maxConnsLog2;
//
//    etcpState->connsHashTable = (etcpConn_t**)calloc(etcpState->maxConns, sizeof(etcpConn_t*));
//    if(!etcpState->connsHashTable){
//        deleteEtcpState(etcpState);
//        return NULL;
//    }

    etcpState->ethHwRx    = ethHwRx;
    etcpState->ethHwTx    = ethHwTx;
    etcpState->ethHwState = ethHwState;

    //This is a dumb way to do this but should be sufficient for the moment
    etcpState->portDirectory = calloc()

    return etcpState;

}


//Make a new "socket" for either listening on or writing to
//A socket is a generic container that holds either a pair of connection read/write queues, or an inbound connection queue
etcpSocket_t* newEtcpSocket(etcpState_t* const etcpState)
{
    etcpSocket_t* const sock = (etcpSocket_t* const)calloc(1,sizeof(etcpSocket_t));
    if(!sock){
        return NULL;
    }
    sock->etcpState = etcpState;
    return sock;

}

//Set the socket to have an outbound address. Etcp does not have a connection setup phase, so you can immediately send/recv directly on this socket
etcpError_t etcpConnect(etcpSocket_t* const sock, const uint64_t windowSize, const uint64_t buffSize, const uint64_t srcAddr, const uint32_t srcPort, const uint64_t dstAddr, const uint32_t dstPort)
{
    etcpState_t* const state = sock->etcpState;
    state->connsHashTable

    return etcpENOERR;
}

//Set the socket to have an inbound address.
etcpError_t etcpBind(etcpSocket_t* const sock, const uint64_t windowSize, const uint64_t buffSize, const uint64_t srcAddr, const uint32_t srcPort, const uint64_t dstAddr, const uint32_t dstPort)
{
    return etcpENOERR;
}


//Tell the socket to start accepting connections. Backlog sets the length of the queue for unaccepted connections
etcpError_t etcpListen(etcpSocket_t* const sock, uint64_t backlog)
{
    return etcpENOERR;
}


//Dequeue new connections from the listen queue
etcpError_t etcpAccept(etcpSocket_t* const listenSock, etcpSocket_t* const acceptSock)
{
    return etcpENOERR;
}


//Send on an etcpSocket
etcpError_t etcpSend(etcpSocket_t* const sock, const void* const toSendData, i64* const toSendLen_io, void* txStats)
{
    return etcpENOERR;
}


//Recv on an etcpSocket
etcpError_t etcpRecv(etcpSocket_t* const sock, void* const data, i64* const len_io, void* rxStats)
{
    return etcpENOERR;
}

//Close down the socket and free resources
void etcpClose(etcpSocket_t* const sock)
{
    if(!sock){
        return;
    }

    //Should do some shutdown things here, send a FIN message, clear out the buffers etc.
    //...

    free(sock);
}
