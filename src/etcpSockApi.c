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
#include "etcpState.h"



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
        etcpSrcsMap_t* la;
        etcpConn_t* conn;
    };
} etcpSocket_t;



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
    ht_t* const dstMap = state->dstMap;
    htKey_t dstKey = {.keyHi = srcAddr, .keyLo = srcPort }; //Flip the SRC/DST around so they match the listener side
    etcpSrcsMap_t* srcsMap = NULL;

    //First check if this destination is in our destinations map, if not, add a new sources map, based on this destination
    htError_t htErr = htGet(dstMap,&dstKey,(void**)&srcsMap);
    if(htErr == htENOTFOUND){
        srcsMap = srcsMapNew(0,0); //Listen window/buff size = 0 because we are not listening.
        if_unlikely(!srcsMap){
            WARN("Ran out of memory making new sources connections container\n");
            return etcpENOMEM;
        }

        htErr = htAddNew(dstMap,&dstKey,srcsMap);
        if(htErr != htENOEROR){
            ERR("Failed on hash table with error=%li\n", htErr);
            return etcpENOTCONN;
        }
    }

    //We have a sources map for this destination. Now make a new connection
    sock->type = ETCPSOCK_SR;
    sock->conn = etcpConnNew(windowSize,buffSize,srcAddr,srcPort, dstAddr,dstPort);
    if_unlikely(sock->conn == NULL){
        WARN("Ran out of memory trying to make a new connection\n");
        return etcpENOMEM;
    }

    //Now add the connection into the sources map
    ht_t* const srcsTable = srcsMap->table;
    htKey_t srcKey = {.keyHi = dstAddr, .keyLo = dstPort }; //Flip the SRC/DST around so they match the listener side    ;
    htErr = htAddNew(srcsTable,&srcKey,sock->conn);
    if_unlikely(htErr == htENOEROR){
        WARN("Trying to setup an exisiting connection\n");
        //We're already connected using the same source and destination ports!
        //Clean up the mess
        etcpConnDelete(sock->conn);
        return etcpEALREADY;
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

    etcpSrcsMap_t* const srcsMap = srcsMapNew(windowSize,buffSize);
    if_unlikely(!srcsMap){
        WARN("Ran out of memory making new sources connections container\n");
        return etcpENOMEM;
    }

    etcpState_t* const state = sock->etcpState;
    ht_t* const dstMap = state->dstMap;
    htKey_t dstKey = {.keyHi = dstAddr, .keyLo = dstPort };
    htError_t htErr = htAddNew(dstMap,&dstKey,srcsMap);
    if(htErr == htEALREADY){
        ERR("Trying to bind to an address that is already in use address=%li, port=%li\n", dstAddr, dstPort);
        //TODO could add a SO_REUSEADDR idea here, but that could get messy. Skip for now.
        return etcpEALREADY;
    }

    sock->type = ETCPSOCK_LA;
    sock->la   = srcsMap;

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

//    //Move the connection out of the listen queue and into the fast path -- this should happen at connection startup
//    ht_t* const srcTable = listenSock->la->srcTable;
//    const htKey_t srcKey = {.keyHi = conn->flowId.srcAddr, .keyLo = conn->flowId.srcPort };
//    htError_t htErr = htAddNew(srcTable,&srcKey,conn);
//    if(htErr == htEALREADY){
//        ERR("This connection is already connected\n");
//        return etcpEALREADY;
//    }

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
etcpError_t etcpSend(etcpSocket_t* const sock, const void* const toSendData, i64* const toSendLen_io)
{
    if_unlikely(sock->type != ETCPSOCK_SR){
        WARN("Wrong socket type, expected %li but got %li\n", ETCPSOCK_SR, sock->type);
        return etcpWRONGSOCK;
    }

    doEtcpUserTx(sock->conn,toSendData,toSendLen_io);

    //If TX is event triggered then do it now, this is the event!
    if(sock->etcpState->eventTriggeredTx){
        sock->etcpState->etcpTxTc(sock->etcpState->etcpTxTcState, sock->conn->datTxQ, sock->conn->ackTxQ, sock->conn->ackTxQ);
    }


    return etcpENOERR;
}


//Recv on an etcpSocket
etcpError_t etcpRecv(etcpSocket_t* const sock, void* const data, i64* const len_io)
{
    if_unlikely(sock->type != ETCPSOCK_SR){
        WARN("Wrong socket type, expected %li but got %li\n", ETCPSOCK_SR, sock->type);
        return etcpWRONGSOCK;
    }

    //If RX is event triggered then do it now, this is the event!
    if(sock->conn->state->eventTriggeredRx){
        doEtcpNetRx(sock->conn->state); //This is a generic RX function
    }

    const i64 maxAcks = sock->conn->state->etcpRxTc(sock->conn->state->etcpRxTcState, sock->conn->datRxQ, sock->conn->ackTxQ);
    generateAcks(sock->conn,maxAcks);

    doEtcpUserRx(sock->conn,data,len_io);

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
