#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/socket.h>
#include <string.h>
#include <stdbool.h>


#include "CircularQueue.h"
#include "types.h"



//Bit field describing the types of this message
enum {
    UNCO_ERR = 0x00, //Not a valid message, something wrong
    UNCO_CON = 0x01, //Start a new connection
    UNCO_FIN = 0x02, //Is the last packet on this connection
    UNCO_DAT = 0x04, //Contains valid data fields
    UNCO_ACK = 0x08, //Contains valid acknowledgement fields
    UNCO_DEN = 0x10, //Connection denied
};


typedef struct __attribute__((packed)){
    i64 start;
    i64 stop;
} sackField_t;


#define UNCO_MAX_SACK 5
typedef struct __attribute__((packed)){
    sackField_t sacks[UNCO_MAX_SACK];
} sack_t;

typedef struct __attribute__((packed)){
    i64 seqNum;
    i64 datLen;
} dat_t;


//Assumes a fast layer 2 network (10G plus), with reasonable latency. In this case, sending many more bits, is better than
//sending many more packets at higher latency
typedef struct __attribute__((packed)){
    i64 typeFlags;  //64 different message type flags, allows a message to be a CON, DAT, FIN and ACK all in 1.
    i64 src;        //Port, ip address, whatever
    i64 dst;        //Port, ip address, whatever
    i64 timeNs;     //Unix time in ns, used for RTT estimation
    sack_t sacks;   //All of the selective acknowledgements
    dat_t data;     //This MUST come last,dfata follows
} uncoMsgHead_t;

_Static_assert(sizeof(uncoMsgHead_t) == 128, "The UnCo mesage header is assumed to be 128 bytes");


typedef struct {
    bool connected;
    i64 src;
    i64 dst;
    i64 seq;
    cq_t* rdcq;
    cq_t* wrcq;
    cq_t* accq;
} uncoConn_t;


#define MAXCONNS 1024
typedef struct {
    uncoConn_t conns[MAXCONNS];
} uncoState_t;

uncoState_t uncoState;


typedef enum {
    ucENOERR,       //Success!
    ucENOMEM,       //Ran out of memory
    ucBADPKT,       //Bad packet, not enough bytes for a header
    ucEALREADY,     //Already connected!
    ucETOOMANY,     //Too many connections, we've run out!
} ucError_t;


void uncoConnDelete(uncoConn_t* uc)
{
    if(!uc){ return; }

    if(uc->accq){ cqDelete(uc->accq); }
    if(uc->wrcq){ cqDelete(uc->wrcq); }
    if(uc->rdcq){ cqDelete(uc->rdcq); }

}


ucError_t uncoConnNew(uncoConn_t* conn, i64 windowSize, i64 buffSize, i64 src, i64 dst, )
{
    if(!conn){ return ucENOMEM; }

    conn->rdcq = cqNew(buffSize,windowSize);
    if(!conn->rdcq){
        uncoConnDelete(conn);
        return ucENOMEM;
    }

    conn->wrcq = cqNew(buffSize,windowSize);
    if(!conn->wrcq){
        uncoConnDelete(conn);
        return ucENOMEM;
    }

    conn->src = src;
    conn->dst = dst;

    return ucENOERR;
}



ucError_t uncoOnConn(uncoMsgHead_t* msg)
{
    //First, check the connection doesn't yet exist
    for(i64 i = 0; i < MAXCONNS; i++){
        if(uncoState.conns[i].connected){
            if(uncoState.conns[i].src == msg->src && uncoState.conns[i].dst == msg->dst){
                return ucEALREADY;
            }
        }
    }

    //If we've got here this is a new connection
    i64 i = 0;
    for( ; i < MAXCONNS; i++){
        if(!uncoState.conns[i].connected){
            break;
        }
    }
    if(i >= MAXCONNS ){
        return ucETOOMANY;
    }

    uncoConn_t* conn = &uncoState.conns[i];
    uncoConnNew(conn);



    return ucENOERR;
}



ucError_t uncoOnPacket( const int8_t* packet, const i64 len)
{
    if(len < (i64)sizeof(uncoMsgHead_t)){
        return ucBADPKT; //Bad packet, not enough data in it
    }

    const uncoMsgHead_t* msg = (uncoMsgHead_t*) packet;

    if(msg->typeFlags & UNCO_CON){
        uncoOnConn(msg);
    }

    if(msg->typeFlags & UNCO_ACK){
        //Do acknowledgement processing here
    }

    if(msg->typeFlags & UNCO_DAT){
        //Do message processing here
    }

    if(msg->typeFlags & UNCO_FIN){
        //Do finish processing here
    }

    return ucENOERR;
}




static i64 connect_(int fd, const struct sockaddr *address,socklen_t address_len)
{

}

u64 unco_bind(int __fd, __CONST_SOCKADDR_ARG __addr, socklen_t __len)
{

}


u64 unco_listen(int __fd, int __n)
{

}

u64 unco_accept(int __fd, __SOCKADDR_ARG __addr, socklen_t *__restrict __addr_len)
{

}

u64 unco_sendto(int __fd, const void *__buf, size_t __n, int __flags)
{

}



u64 unco_send(int __fd, const void *__buf, size_t __n, int __flags)
{

}



u64 unco_recv(int __fd, void *__buf, size_t __n, int __flags)
{

}



void uncotpTestClient()
{
    //Open the connection


    //Write to the connection


    //Close the connection

}

void uncotpTestServer()
{

}




int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    printf("I love cheees\n");
    return 0;
}
