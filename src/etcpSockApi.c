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


typedef struct etcpSocket_s
{
    i64 listenMax; //Maximum length of listen queue before connections are auto rejected. If 0, the socket is not listening
    i64 listenCount; //Current length of the listen queue
    etcpState_t* etcpState; //Back reference to the etcp global state
    etcpConn_t* conns; //1 or more connections to listen / send / rx on

} etcpSocket_t;


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
