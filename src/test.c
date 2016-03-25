#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/socket.h>
#include <string.h>
#include <stdbool.h>
#include <linux/if_ether.h>

#include "CircularQueue.h"
#include "types.h"
#include "spooky_hash.h"
#include "debug.h"



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
    uint16_t start; //At most 65K segments per window
    uint16_t stop;
} sackField_t;


#define UNCO_MAX_SACK 9 //This number is determined by the max size of the header/fcs (128B) minus all of the other fields
typedef struct __attribute__((packed)){
    i64 sackBase;
    i64 sackCount;
    i64 rxWindow;
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
    dat_t data;     //This MUST come last,data follows
} uncoMsgHead_t;

_Static_assert(sizeof(uncoMsgHead_t) == 128 - ETH_HLEN - 2 - ETH_FCS_LEN, "The UnCo mesage header is assumed to be 128 bytes including 20B of ethernet header + VLAN");

typedef struct uncoConn uncoConn_t;

struct uncoConn {
    bool connected;
    i64 src;    //Identifiers for this flow
    i64 dst;    //Identifiers for this flow
    uncoConn_t* next; //For chaining in the hashtable

    cq_t* rxcq; //Queue for incoming packets
    cq_t* txcq; //Queue for outgoing packets

    i64 seqAck; //The current acknowledge sequence number



};


#define MAXCONNSPOW 10ULL //(2^10 = 1024 buckets)
#define MAXCONNS (1 << MAXCONNSPOW)
typedef struct {
    uncoConn_t* conns[MAXCONNS];
} uncoState_t;

uncoState_t uncoState = {0};


typedef enum {
    ucENOERR,       //Success!
    ucENOMEM,       //Ran out of memory
    ucBADPKT,       //Bad packet, not enough bytes for a header
    ucEALREADY,     //Already connected!
    ucETOOMANY,     //Too many connections, we've run out!
    ucNOTCONN,      //Not connected to anything
    ucECQERR,       //Some issue with a Circular Queue
    ucERANGE,       //Out of range
    ucETOOBIG,      //The payload is too big for this buffer
} ucError_t;


void uncoConnDelete(uncoConn_t* uc)
{
    if(!uc){ return; }

    if(uc->txcq){ cqDelete(uc->txcq); }
    if(uc->rxcq){ cqDelete(uc->rxcq); }

    free(uc);

}


uncoConn_t* uncoConnNew(i64 windowSize, i64 buffSize, i64 src, i64 dst )
{
    uncoConn_t* conn = calloc(1, sizeof(uncoConn_t));
    if(!conn){ return NULL; }

    conn->rxcq = cqNew(buffSize,windowSize);
    if(!conn->rxcq){
        uncoConnDelete(conn);
        return NULL;
    }

    conn->txcq = cqNew(buffSize,windowSize);
    if(!conn->txcq){
        uncoConnDelete(conn);
        return NULL;
    }

    conn->src = src;
    conn->dst = dst;

    return conn;
}


static inline i64 getHashKey(const uncoMsgHead_t* msg)
{
    i64 hash64 = spooky_Hash64(&msg->src,sizeof(msg->src) * 2, 0xB16B00B1E5FULL);
    return (hash64 * 11400714819323198549ul) >> (64 - MAXCONNSPOW);
}


ucError_t uncoOnConnAdd(const uncoMsgHead_t* msg, i64 windowSegs, i64 segSize)
{
    const i64 idx = getHashKey(msg);
    uncoConn_t* conn = uncoState.conns[idx];
    if(conn){
        if(conn->src == msg->src && conn->dst == msg->dst){
            return ucEALREADY;
        }

        //Something already in the hash table, traverse to the end
        for(; conn->next; conn = conn->next){
            if(conn->src == msg->src && conn->dst == msg->dst){
                return ucEALREADY;
            }
        }

        conn->next = uncoConnNew(windowSegs, segSize, msg->src, msg->dst);
    }
    else{
        conn = uncoConnNew(windowSegs, segSize, msg->src, msg->dst);
        uncoState.conns[idx] = conn;
    }

    return ucENOERR;
}


uncoConn_t* uncoOnConnGet(const uncoMsgHead_t* msg)
{
    const i64 idx = getHashKey(msg);
    uncoConn_t* conn = uncoState.conns[idx];
    if(!conn){
        return NULL;
    }

    if(conn->src == msg->src && conn->dst == msg->dst){
        return conn;
    }

    //Something already in the hash table, traverse to the end
    for(; conn->next; conn = conn->next){}{
        if(conn->src == msg->src && conn->dst == msg->dst){
            return conn;
        }
    }

    return NULL;
}


void uncoOnConnDel(uncoMsgHead_t* msg)
{
    const i64 idx = getHashKey(msg);
    uncoConn_t* conn = uncoState.conns[idx];
    if(!conn){
        return;
    }

    if(conn->src == msg->src && conn->dst == msg->dst){
        uncoState.conns[idx] = conn->next;
        uncoConnDelete(conn);
    }

    //Something already in the hash table, traverse to the end
    uncoConn_t* prev = conn;
    for(; conn->next; conn = conn->next){}{
        if(conn->src == msg->src && conn->dst == msg->dst){
            prev->next = conn->next;
            uncoConnDelete(conn);
        }
        prev = conn;
    }

}

#define MAXSEGS 1024
#define MAXSEGSIZE (2048 - sizeof(uncoConn_t) - sizeof(cqSlot_t)) //Should bound the CQ slots to 1/2 a page
ucError_t uncoOnConn(const uncoMsgHead_t* msg)
{
    ucError_t err = uncoOnConnAdd(msg,MAXSEGS, MAXSEGSIZE);
    if(err != ucENOERR){
        printf("Error adding connection, ignoring\n");
        return err;
    }

    return ucENOERR;
}


ucError_t uncoOnDat(const uncoMsgHead_t* msg)
{
    DBG("Working on new data message\n");
    uncoConn_t* conn = uncoOnConnGet(msg);
    if(!conn){
        printf("Error data packet for invalid connection\n");
        return ucNOTCONN;
    }

    const i64 rxQSlotCount  = conn->rxcq->slotCount;
    const i64 seqPkt        = msg->data.seqNum;
    const i64 seqMin        = conn->seqAck;
    const i64 seqMax        = conn->seqAck + rxQSlotCount;
    const i64 seqIdx        = seqPkt % rxQSlotCount;
    DBG("SlotCount = %li, seqPkt = %li, seqMin= %li, seqMax = %li, seqIdx = %li\n",
        rxQSlotCount,
        seqPkt,
        seqMin,
        seqMax,
        seqIdx
    );

    //When we receive a packet, it must be between seqMin and seqMax
    //-- if seq < seqMin, it has already been ack'd
    //-- if seq >= seqMax, it is beyond the end of the rx window
    if(seqPkt < seqMin){
        WARN("Ignoring packet, seqPkt %li < %li seqMin, packet has already been ack'd\n", seqPkt, seqMin);
        return ucERANGE;
    }

    if(seqPkt > seqMax){
        WARN("Ignoring packet, seqPkt %li > %li seqMax, packet will not fit in window\n", seqPkt, seqMax);
        return ucERANGE;
    }

    i64 toCopy = msg->data.datLen + sizeof(uncoMsgHead_t);
    i64 toCopyTmp = toCopy;
    cqError_t err = cqPushIdx(conn->rxcq,msg,&toCopyTmp,seqIdx);
    if(err != cqENOERR){
        if(err == cqETRUNC){
            WARN("Payload (%liB) is too big for slot (%liB), truncating\n", toCopy, toCopyTmp );
        }
        else{
            WARN("Error inserting into Circular Queue: %s", cqError2Str(err));
            return ucECQERR;
        }
    }

    return ucENOERR;
}

ucError_t uncoOnDat(const uncoMsgHead_t* msg)
{
    DBG("Working on new data message\n");
    uncoConn_t* conn = uncoOnConnGet(msg);
    if(!conn){




ucError_t uncoOnPacket( const void* const packet, const i64 len)
{
    //First sanity check the packet
    const i64 minSizeHdr = sizeof(uncoMsgHead_t);
    if(len < minSizeHdr){
        WARN("Not enough bytes to parse header\n");
        return ucBADPKT; //Bad packet, not enough data in it
    }
    const uncoMsgHead_t* msg = (uncoMsgHead_t*)packet;

    const i64 minSizeDat = minSizeHdr + msg->data.datLen;
    if(len < minSizeDat){
        WARN("Not enough bytes to parse data\n");
        return ucBADPKT; //Bad packet, not enough data in it
    }

    //Now we can process it. The ordering here is specific, it is possible for a single packet to be a CON, ACT, DATA and FIN
    //in one.
    if(msg->typeFlags & UNCO_CON){
        uncoOnConn(msg);
    }

    if(msg->typeFlags & UNCO_ACK){
        uncoOnAck(msg);
    }

    if(msg->typeFlags & UNCO_DAT){
        uncoOnDat(msg);
    }

    if(msg->typeFlags & UNCO_FIN){

    }

    return ucENOERR;
}


//
//
//static i64 connect_(int fd, const struct sockaddr *address,socklen_t address_len)
//{
//
//}
//
//u64 unco_bind(int __fd, __CONST_SOCKADDR_ARG __addr, socklen_t __len)
//{
//
//}
//
//
//u64 unco_listen(int __fd, int __n)
//{
//
//}
//
//u64 unco_accept(int __fd, __SOCKADDR_ARG __addr, socklen_t *__restrict __addr_len)
//{
//
//}
//
//u64 unco_sendto(int __fd, const void *__buf, size_t __n, int __flags)
//{
//
//}
//
//
//
//u64 unco_send(int __fd, const void *__buf, size_t __n, int __flags)
//{
//
//}
//
//
//
//u64 unco_recv(int __fd, void *__buf, size_t __n, int __flags)
//{
//
//}



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
