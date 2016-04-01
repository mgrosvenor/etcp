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
#include "utils.h"

//This structure contains all unique source address/port combination connections for a given destination address/port combination.
typedef struct etcpSrcConns_s etcpSrcConns_t;
typedef struct etcpSrcConns_s {

    //These are for new connections that happen when we're listening
    uint32_t listenWindowSize;
    uint32_t listenBuffSize;

    cq_t* listenQ; //Queue containing connections that have not yet been accepted

    ht_t* srcTable; //All unique src address/port combinations

} etcpSrcConns_t;


typedef enum {
    ETCPSOCK_UK, //Unknown, not yet bound/connected
    ETCPSOCK_LA, //For listening/accepting
    ETCPSOCK_SR, //For sending/receiving
} socketType_t;


struct etcpSocket_s;
typedef struct etcpSocket_s
{
    etcpState_t* etcpState; //Back reference to the etcp global state

    socketType_t type;
    union{
        etcpSrcConns_t* la;
        etcpConn_t* conn;
    };
} etcpSocket_t;



struct etcpState_s {

    void* ethHwState;
    ethHwTx_f ethHwTx;
    ethHwRx_f ethHwRx;

    ht_t* dstTable; //All unique dst address/port combinations
};




void connHTDelete(const htKey_t* const key, void* const value)
{
    DBG("Deleting conn for srcA=%li srcP=%li\n", key->keyHi, key->keyLo);
    etcpConn_t* const conn = (etcpConn_t* const)(value);
    etcpConnDelete(conn);
}


void srcConnsDelete(etcpSrcConns_t* const srcConns){
    if_unlikely(!srcConns){
        return;
    }

    if_likely(srcConns->listenQ != NULL){
        cqDelete(srcConns->listenQ);
    }

    if_likely(srcConns->srcTable != NULL){
        htDelete(srcConns->srcTable,connHTDelete);
    }
}


etcpSrcConns_t* srcConnsNew( const uint32_t listenWindowSize, const uint32_t listenBuffSize)
{
    etcpSrcConns_t* const srcConns = (etcpSrcConns_t* const )calloc(1,sizeof(etcpSrcConns_t));
    if_unlikely(!srcConns){
        return NULL;
    }

    srcConns->srcTable = htNew(SRC_TAB_MAX_LOG2);
    if_likely(!srcConns->srcTable){
        srcConnsDelete(srcConns);
        return NULL;
    }

    srcConns->listenWindowSize = listenWindowSize;
    srcConns->listenBuffSize   = listenBuffSize;

    return srcConns;

}


void srcConnsHTDelete(const htKey_t* const key, void* const value)
{
    DBG("HT deleting src cons for dstA=%li dstP=%li\n", key->keyHi, key->keyLo);
    etcpSrcConns_t* const srcConns = (etcpSrcConns_t* const)value;
    srcConnsDelete(srcConns);
}


void deleteEtcpState(etcpState_t* etcpState)
{
    if_unlikely(!etcpState){
        return;
    }

    if_likely(etcpState->dstTable != NULL){
        htDelete(etcpState->dstTable,srcConnsHTDelete);
    }

    free(etcpState);
}


etcpState_t* newEtcpState(void* const ethHwState, const ethHwTx_f ethHwTx, const ethHwRx_f ethHwRx)
{
    etcpState_t* etcpState = calloc(1,sizeof(etcpState_t));
    if_unlikely(!etcpState){
        return NULL;
    }

    etcpState->ethHwRx    = ethHwRx;
    etcpState->ethHwTx    = ethHwTx;
    etcpState->ethHwState = ethHwState;


    etcpState->dstTable = htNew(DST_TAB_MAX_LOG2);
    if_unlikely(!etcpState->dstTable){
        deleteEtcpState(etcpState);
        return NULL;
    }

    return etcpState;
}


//Make a new "socket" for either listening on or writing to
//A socket is a generic container that holds either a pair of connection read/write queues, or a pair of listening/accepted queues
etcpSocket_t* etcpSocketNew(etcpState_t* const etcpState)
{
    etcpSocket_t* const sock = (etcpSocket_t* const)calloc(1,sizeof(etcpSocket_t));
    if_unlikely(!sock){
        return NULL;
    }
    sock->etcpState = etcpState;
    sock->type      = ETCPSOCK_UK;
    return sock;

}

//Set the socket to have an outbound address. Etcp does not have a connection setup phase, so you can immediately send/recv directly on this socket
etcpError_t etcpConnect(etcpSocket_t* const sock, const uint32_t windowSize, const uint32_t buffSize, const uint64_t srcAddr, const uint32_t srcPort, const uint64_t dstAddr, const uint32_t dstPort)
{

    if(sock->type != ETCPSOCK_UK){
        WARN("Wrong socket type, expected %li but got %li\n", ETCPSOCK_UK, sock->type);
        return etcpWRONGSOCK;
    }

    etcpState_t* const state = sock->etcpState;
    ht_t* const dstTable = state->dstTable;
    htKey_t dstKey = {.keyHi = srcAddr, .keyLo = srcPort }; //Flip the SRC/DST around so they match the listener side
    void* srcConnsP = NULL;
    htError_t htErr = htGet(dstTable,&dstKey,&srcConnsP);
    if(htErr == htENOTFOUND){
        etcpSrcConns_t* const srcConns = srcConnsNew(0,0); //Listen window/buff size = 0 because we are not listening.
        if_unlikely(!srcConns){
            WARN("Ran out of memory making new sources connections container\n");
            return etcpENOMEM;
        }

        htErr = htAddNew(dstTable,&dstKey,srcConns);
        if(htErr != htENOEROR){
            ERR("Failed on hash table with error=%li\n", htErr);
            return etcpENOTCONN;
        }
        srcConnsP = srcConns;
    }
    etcpSrcConns_t* const srcConns = (etcpSrcConns_t* const)(srcConnsP);

    //By this stage we should have a valid srcConns structure, look into it id the destination address is already used
    ht_t* const srcTable = srcConns->srcTable;
    htKey_t srcKey = {.keyHi = dstAddr, .keyLo = dstPort }; //Flip the SRC/DST around so they match the listener side
    void* connP = NULL;
    htErr = htGet(srcTable,&srcKey,&connP);
    if_unlikely(htErr == htEALREADY){
        WARN("Trying to setup an exisiting connection\n");
        //We're already connected using the same source and destination ports!
        return etcpEALREADY;
    }

    sock->type = ETCPSOCK_SR;
    sock->conn = etcpConnNew(windowSize,buffSize,srcAddr,srcPort, dstAddr,dstPort);
    if_unlikely(sock->conn == NULL){
        WARN("Ran out of memory trying to make a new connection\n");
        return etcpENOMEM;
    }

    return etcpENOERR;
}

//Set the socket to have an inbound address.
etcpError_t etcpBind(etcpSocket_t* const sock, const uint32_t windowSize, const uint32_t buffSize, const uint64_t dstAddr, const uint32_t dstPort)
{

    if(sock->type != ETCPSOCK_UK){
        WARN("Wrong socket type, expected %li but got %li\n", ETCPSOCK_UK, sock->type);
        return etcpWRONGSOCK;
    }

    etcpSrcConns_t* const srcConns = srcConnsNew(windowSize,buffSize);
    if_unlikely(!srcConns){
        WARN("Ran out of memory making new sources connections container\n");
        return etcpENOMEM;
    }

    etcpState_t* const state = sock->etcpState;
    ht_t* const dstTable = state->dstTable;
    htKey_t dstKey = {.keyHi = dstAddr, .keyLo = dstPort };
    htError_t htErr = htAddNew(dstTable,&dstKey,srcConns);
    if(htErr != htEALREADY){
        ERR("Trying to bind to an address that is already in use address=%li, port=%li\n", dstAddr, dstPort);
        //TODO could add a reuse address idea here, but that could get messy. Skip for now.
        return etcpEALREADY;
    }

    sock->type = ETCPSOCK_LA;
    sock->la   = srcConns;

    return etcpENOERR;
}


//Tell the socket to start accepting connections. Backlog sets the length of the queue for unaccepted connections
//Maximum backlog 4 billion...
etcpError_t etcpListen(etcpSocket_t* const sock, uint32_t backlog)
{

    if_unlikely(sock->type != ETCPSOCK_LA){
        WARN("Wrong socket type, expected %li but got %li\n", ETCPSOCK_UK, sock->type);
        return etcpWRONGSOCK;
    }

    sock->la->listenQ = cqNew(sizeof(etcpConn_t*), backlog + 1);
    if_unlikely(!sock->la->listenQ){
        WARN("Ran out of memory trying to allocate new listener queue\n");
        return etcpENOMEM;
    }

    return etcpENOERR;
}


//Dequeue new connections from the listen queue
etcpError_t etcpAccept(etcpSocket_t* const listenSock, etcpSocket_t** const acceptSock_o)
{
    if_unlikely(listenSock->type != ETCPSOCK_LA){
        WARN("Wrong socket type, expected %li but got %li\n", ETCPSOCK_LA, listenSock->type);
        return etcpWRONGSOCK;
    }

    //if there is anything in the listen queue, then make a new socket and return it
    cq_t* listenQ = listenSock->la->listenQ;
    if_unlikely(listenQ == NULL){
        WARN("You need to do a listen on this socket before you can do an accept\n");
        return etcpWRONGSOCK; //
    }

    if_unlikely(listenQ->rdSlotsUsed == 0){
        return etcpETRYAGAIN; //Nothing here to be collected. Come back another time
    }

    etcpConn_t* conn;
    i64 len = sizeof(etcpConn_t*);
    i64 idx = -1;
    cqError_t err = cqPullNext(listenSock->la->listenQ,&conn,&len,&idx);
    if_unlikely(err != cqENOERR){
        DBG("Unexpected error on cq %s\n", cqError2Str(err));
        return etcpECQERR;
    }

    //By this point we have a valid value in conn. See if we can make a new send/receive socket for it
    etcpSocket_t* acceptSock = etcpSocketNew(listenSock->etcpState);
    if_unlikely(acceptSock == NULL){
        WARN("Ran out of memory making new socket\n");
        return etcpENOMEM;
    }

    acceptSock->type = ETCPSOCK_SR;
    acceptSock->conn = conn;
    *acceptSock_o = acceptSock;

    //Ok we're done with the entry in the cq
    err = cqReleaseSlotRd(listenQ,idx);
    if_unlikely(err != cqENOERR){
        DBG("Unexpected error on cq %s\n", cqError2Str(err));
        return etcpECQERR;
    }

    return etcpENOERR;
}


//Send on an etcpSocket
etcpError_t etcpSend(etcpSocket_t* const sock, const void* const toSendData, i64* const toSendLen_io, void* txStats)
{
    if_unlikely(sock->type != ETCPSOCK_SR){
        WARN("Wrong socket type, expected %li but got %li\n", ETCPSOCK_SR, sock->type);
        return etcpWRONGSOCK;
    }

    doEtcpUserTx(sock->conn,toSendData,toSendLen_io);
    (void)txStats;


    return etcpENOERR;
}


//Recv on an etcpSocket
etcpError_t etcpRecv(etcpSocket_t* const sock, void* const data, i64* const len_io, void* rxStats)
{
    if_unlikely(sock->type != ETCPSOCK_SR){
        WARN("Wrong socket type, expected %li but got %li\n", ETCPSOCK_SR, sock->type);
        return etcpWRONGSOCK;
    }

    doEtcpUserRx(sock->conn,data,len_io);
    (void)rxStats;


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
