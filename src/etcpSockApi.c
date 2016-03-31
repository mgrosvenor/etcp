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
#include "HashTable.h"
#include "debug.h"



//This structure contains all unique source address/port combination connections for a given destination address/port combination.
typedef struct etcpSrcConnTable_s etcpSrcConnTable_t;
typedef struct etcpSrcConnTable_s {


    uint32_t listenQMax; //Maximum length of listen queue before connections are auto rejected. If 0, the socket is not listening, 4 billion max
    uint32_t listenQCount; //Current length of the listen queue, 4 billion max
    etcpConn_t* listenQ; //Queue containing connections that have not yet been accepted

    ht_t* srcTable; //All unique src address/port combinations

} etcpSrcConnTable_t;


typedef enum {
    ETCPSOCK_UK, //Unknown, not yet bound/connected
    ETCPSOCK_LA, //For listening/accepting
    ETCPSCOK_SR, //For sending/receiving
} socketType_t;


struct etcpSocket_s;
typedef struct etcpSocket_s
{
    etcpState_t* etcpState; //Back reference to the etcp global state

    socketType_t type;
    union{
        etcpSrcConnTable_t* la;
        etcpConn_t* conn;
    };
} etcpSocket_t;



struct etcpState_s {

    void* ethHwState;
    ethHwTx_f ethHwTx;
    ethHwRx_f ethHwRx;

    ht_t* dstTable; //All unique dst address/port combinations
};


void deleteConn(const htKey_t* const key, void* const value)
{
    DBG("Deleting conn for srcA=%li srcP=%li\n", key->keyHi, key->keyLo);
    etcpConn_t* const conn = (etcpConn_t* const)(value);
    etcpConnDelete(conn);
}


void deleteSrcConns(const htKey_t* const key, void* const value)
{
    DBG("Deleting src cons for dstA=%li dstP=%li\n", key->keyHi, key->keyLo);
    const etcpSrcConnTable_t* const srcConTable = (const etcpSrcConnTable_t* const)(value);
    htDelete(srcConTable->srcConns,deleteConn);
}


void deleteEtcpState(etcpState_t* etcpState)
{
    if(!etcpState){
        return;
    }

    if(etcpState->dstTable){
        htDelete(etcpState->dstTable,deleteSrcConns);
    }

    free(etcpState);
}


#define DST_TAB_MAX_LOG2 (17) //2^17 = 128K dst Adrr/Port pairs, 1MB in memory
#define SRC_TAB_MAX_LOG2 (10) //210  = 1K src Adrr/Port pairs, 8kB in memory
etcpState_t* newEtcpState(void* const ethHwState, const ethHwTx_f ethHwTx, const ethHwRx_f ethHwRx)
{
    etcpState_t* etcpState = calloc(1,sizeof(etcpState_t));
    if(!etcpState){
        return NULL;
    }

    etcpState->ethHwRx    = ethHwRx;
    etcpState->ethHwTx    = ethHwTx;
    etcpState->ethHwState = ethHwState;


    etcpState->dstTable = htNew(DST_TAB_MAX_LOG2);
    if(!etcpState->dstTable){
        deleteEtcpState(etcpState);
        return NULL;
    }

    return etcpState;
}


//Make a new "socket" for either listening on or writing to
//A socket is a generic container that holds either a pair of connection read/write queues, or a pair of listening/accepted queues
etcpSocket_t* newEtcpSocket(etcpState_t* const etcpState)
{
    etcpSocket_t* const sock = (etcpSocket_t* const)calloc(1,sizeof(etcpSocket_t));
    if(!sock){
        return NULL;
    }
    sock->etcpState = etcpState;
    sock->type      = ETCPSOCK_UK;
    return sock;

}

//Set the socket to have an outbound address. Etcp does not have a connection setup phase, so you can immediately send/recv directly on this socket
etcpError_t etcpConnect(etcpSocket_t* const sock, const uint64_t windowSize, const uint64_t buffSize, const uint64_t srcAddr, const uint32_t srcPort, const uint64_t dstAddr, const uint32_t dstPort)
{
    etcpState_t* const state = sock->etcpState;
    ht_t* const dstTable = state->dstTable;
    htKey_t dstKey = {.keyHi = dstAddr, .keyLo = dstPort };
    void* srcTableP = NULL;
    htError_t htErr = htGet(dstTable,&dstKey,&srcTableP);
    if(htErr == htENOTFOUND){
        etcpSrcConnTable_t* const srcConns = (etcpSrcConnTable_t* const )calloc(1,sizeof(etcpSrcConnTable_t));
        if(!srcConns){
            return etcpENOMEM;
        }

        srcConns->srcTable = htNew(SRC_TAB_MAX_LOG2);
        if(!srcConns->srcTable){
            free(srcConns);
            return etcpENOMEM;
        }

        htErr = htAddNew(dstTable,&dstKey,srcConns);
    }
    etcpSrcConnTable_t* const srcConns = (etcpSrcConnTable_t* const)(srcTableP);

    //By this stage we should have a valid srcConns structure
    ht_t* const srcTable = srcConns->srcTable;
    htKey_t srcKey = {.keyHi = srcAddr, .keyLo = srcPort };
    void* connP = NULL;
    htErr = htAdd(srcTable,&srcKey,&connP);
    if(htErr == htEALREADY){
        //We're already connected using the same source and destination ports!
        return etcpEALREADY;
    }

    sock->type = ETCPSCOK_SR;
    sock->conn =

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
