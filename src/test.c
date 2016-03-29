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

#include "packets.h"



typedef struct  __attribute__((packed)) {
    i32 srcPort;
    i32 dstPort;
    i64 srcAddr;
    i64 dstAddr;
} etcpFlowIdConnect_t;

typedef struct  __attribute__((packed)){
    i32 dstPort;
    i32 srcPort;
    i64 dstAddr;
    i64 srcAddr;
} etcpFlowId_t;



typedef struct etcpConn etcpConn_t;
struct etcpConn {
    bool connected;

    etcpConn_t* next; //For chaining in the hashtable

    cq_t* rxcq; //Queue for incoming packets
    cq_t* txcq; //Queue for outgoing packets

    i64 seqAck; //The current acknowledge sequence number
    etcpFlowId_t flowId;
};



#define MAXCONNSPOW 10ULL //(2^10 = 1024 buckets)
#define MAXCONNS (1 << MAXCONNSPOW)
typedef struct {
    etcpConn_t* conns[MAXCONNS];
} etcpState_t;

etcpState_t etcpState = {0};


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
} etcpError_t;


void etcpConnDelete(etcpConn_t* uc)
{
    if(!uc){ return; }

    if(uc->txcq){ cqDelete(uc->txcq); }
    if(uc->rxcq){ cqDelete(uc->rxcq); }

    free(uc);

}


int cmpFlowId(const etcpFlowId_t* __restrict lhs, const etcpFlowId_t* __restrict rhs)
{
    return memcmp(lhs,rhs, sizeof(etcpFlowId_t));
}


etcpConn_t* etcpConnNew(const i64 windowSize, const i64 buffSize, const etcpFlowId_t* flowId)
{
    etcpConn_t* conn = calloc(1, sizeof(etcpConn_t));
    if(!conn){ return NULL; }

    conn->rxcq = cqNew(buffSize,windowSize);
    if(!conn->rxcq){
        etcpConnDelete(conn);
        return NULL;
    }

    conn->txcq = cqNew(buffSize,windowSize);
    if(!conn->txcq){
        etcpConnDelete(conn);
        return NULL;
    }

    conn->flowId = *flowId;

    return conn;
}


static inline i64 getIdx(const etcpFlowId_t* const flowId)
{
    i64 hash64 = spooky_Hash64(flowId, sizeof(etcpFlowId_t), 0xB16B00B1E5FULL);
    return (hash64 * 11400714819323198549ul) >> (64 - MAXCONNSPOW);
}


etcpError_t etcpOnConnAdd( i64 windowSegs, i64 segSize, const etcpFlowId_t* const newFlowId)
{
    const i64 idx = getIdx(newFlowId);
    etcpConn_t* conn = etcpState.conns[idx];
    if(conn){
        if(cmpFlowId(&conn->flowId, newFlowId) == 0){
            return etcpEALREADY;
        }

        //Something already in the hash table, traverse to the end
        for(; conn->next; conn = conn->next){
            if(cmpFlowId(&conn->flowId, newFlowId) == 0){
                return etcpEALREADY;
            }
        }

        conn->next = etcpConnNew(windowSegs, segSize, newFlowId);
    }
    else{
        etcpState.conns[idx] = etcpConnNew(windowSegs, segSize, newFlowId);
    }

    return etcpENOERR;
}


etcpConn_t* etcpOnConnGet(const etcpFlowId_t* const getFlowId)
{
    const i64 idx = getIdx(getFlowId);
    etcpConn_t* conn = etcpState.conns[idx];
    if(!conn){
        return NULL;
    }


    if(cmpFlowId(&conn->flowId, getFlowId) == 0){
               return conn;
    }

    //Something already in the hash table, traverse to the end
    for(; conn->next; conn = conn->next){
        if(cmpFlowId(&conn->flowId, getFlowId) == 0){
            return conn;
        }
    }

    return NULL;
}


void etcpOnConnDel(const etcpFlowId_t* const delFlowId)
{
    const i64 idx = getIdx(delFlowId);
    etcpConn_t* conn = etcpState.conns[idx];
    if(!conn){
        return;
    }

    if(cmpFlowId(&conn->flowId, delFlowId) == 0){
        etcpState.conns[idx] = conn->next;
        etcpConnDelete(conn);
    }

    //Something already in the hash table, traverse to the end
    etcpConn_t* prev = conn;
    for(; conn->next; conn = conn->next){}{
        if(cmpFlowId(&conn->flowId, delFlowId) == 0){
            prev->next = conn->next;
            etcpConnDelete(conn);
        }
        prev = conn;
    }

}

#define MAXSEGS 1024
#define MAXSEGSIZE (2048 - sizeof(etcpConn_t) - sizeof(cqSlot_t)) //Should bound the CQ slots to 1/2 a page
etcpError_t etcpOnConn(const etcpFlowId_t* const flowId )
{
    etcpError_t err = etcpOnConnAdd(MAXSEGS, MAXSEGSIZE, flowId);
    if(err != etcpENOERR){
        printf("Error adding connection, ignoring\n");
        return err;
    }

    return etcpENOERR;
}


etcpError_t etcpOnDat(const etcpMsgHead_t* msg, const etcpFlowId_t* const flowId)
{
    DBG("Working on new data message\n");
    etcpConn_t* conn = etcpOnConnGet(flowId);
    if(!conn){
        printf("Error data packet for invalid connection\n");
        return etcpENOTCONN;
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
        return etcpERANGE;
    }

    if(seqPkt > seqMax){
        WARN("Ignoring packet, seqPkt %li > %li seqMax, packet will not fit in window\n", seqPkt, seqMax);
        return etcpERANGE;
    }

    i64 toCopy = msg->data.datLen + sizeof(etcpMsgHead_t);
    i64 toCopyTmp = toCopy;
    cqError_t err = cqPushIdx(conn->rxcq,msg,&toCopyTmp,seqIdx);
    if(err != cqENOERR){
        if(err == cqETRUNC){
            WARN("Payload (%liB) is too big for slot (%liB), truncating\n", toCopy, toCopyTmp );
        }
        else{
            WARN("Error inserting into Circular Queue: %s", cqError2Str(err));
            return etcpECQERR;
        }
    }

    return etcpENOERR;
}


etcpError_t etcpDoAck(cq_t* const cq, const uint64_t seq, const etcpMsgTime_t* const time)
{
    const uint64_t idx = seq % cq->slotCount;
    DBG("Ack'ing packet with seq=%li and idx=%li\n", seq,idx);
    cqSlot_t* slot = NULL;
    cqError_t err = cqGetSlotIdx(cq,&slot,idx);
    if(err == cqEWRONGSLOT){
        WARN("Got an ACK for a packet that's gone.\n");
        return etcpENOERR;
    }
    else if(err != cqENOERR){
        WARN("Error getting value from Circular Queue: %s", cqError2Str(err));
        return etcpECQERR;
    }

    etcpMsgHead_t* msg = slot->buff;
    if(seq != msg->data.seqNum){
        WARN("Got an ACK for a packet that's gone.\n");
        return etcpENOERR;
    }
    //Successful ack! -- Do timing stats here
    DBG("Successful ack for seq %li at index=%li\n", seq, idx);
    //TODO XXX can do stats here.
    (void)time;

    //Packet is now ack'd, we can release this slot and use it for another TX
    cqReleaseSlotRd(cq,idx);
    return etcpENOERR;
}



etcpError_t etcpOnAck(const etcpMsgHead_t* msg, const etcpFlowId_t* const flowId)
{
    DBG("Working on new data message\n");
    etcpConn_t* conn = etcpOnConnGet(flowId);
    if(!conn){
        WARN("Trying to ACK a packet on a flow that doesn't exist?\n");
        return etcpENOTCONN; //Trying to ACK a packet on a flow that doesn't exist
    }

    const uint64_t sackBaseSeq = msg->acks.sackBaseSeq;
    //Process the acks
    for(i64 sackIdx = 0; sackIdx < msg->acks.sackCount; sackIdx++){
        const uint16_t ackOffset = msg->acks.sacks[sackIdx].offset;
        const uint16_t ackCount = msg->acks.sacks[sackIdx].count;
        DBG("Working on ACKs between %li and %li\n", sackBaseSeq + ackOffset, sackBaseSeq + ackOffset + ackCount);
        for(uint16_t ackIdx = 0; ackIdx < ackCount; ackIdx++){
            const uint64_t ackSeq = sackBaseSeq + ackOffset + ackIdx;
            etcpDoAck(conn->txcq,ackSeq,&msg->timing);
        }
    }

    return etcpENOERR;

}




etcpError_t etcpOnPacket( const void* const packet, const i64 len, i64 srcAddr, i64 dstAddr)
{
    //First sanity check the packet
    const i64 minSizeHdr = sizeof(etcpMsgHead_t);
    if(len < minSizeHdr){
        WARN("Not enough bytes to parse header\n");
        return etcpEBADPKT; //Bad packet, not enough data in it
    }
    const etcpMsgHead_t* head = (etcpMsgHead_t*)packet;

    etcpFlowId_t flowId = {
        .srcAddr = srcAddr,
        .dstAddr = dstAddr,
        .srcPort = head->srcPort,
        .dstPort = head->dstPort,
    };

    if()

    //Now we can process it. The ordering here is specific, it is possible for a single packet to be a CON, ACT, DATA and FIN
    //in one.
    switch(head->type);
    if(msg->typeFlags & UNCO_CON){
        etcpOnConn(&flowId);
    }

    if(msg->typeFlags & UNCO_ACK){
        etcpOnAck(msg, &flowId);
    }

    if(msg->typeFlags & UNCO_DAT){
        etcpOnDat(msg, &flowId);
    }

    if(msg->typeFlags & UNCO_FIN){
        //Do cleanup here. This is a bit of pain because we need to wait for any outstanding DAT packets to come in before we release resources
        WARN("FIN is not implemented\n");
    }

    return etcpENOERR;
}


//
//
//static i64 connect_(int fd, const struct sockaddr *address,socklen_t address_len)
//{
//
//}
//
//u64 etcp_bind(int __fd, __CONST_SOCKADDR_ARG __addr, socklen_t __len)
//{
//
//}
//
//
//u64 etcp_listen(int __fd, int __n)
//{
//
//}
//
//u64 etcp_accept(int __fd, __SOCKADDR_ARG __addr, socklen_t *__restrict __addr_len)
//{
//
//}
//
//u64 etcp_sendto(int __fd, const void *__buf, size_t __n, int __flags)
//{
//
//}
//
//
//
//u64 etcp_send(int __fd, const void *__buf, size_t __n, int __flags)
//{
//
//}
//
//
//
//u64 etcp_recv(int __fd, void *__buf, size_t __n, int __flags)
//{
//
//}



void etcptpTestClient()
{
    //Open the connection


    //Write to the connection


    //Close the connection

}

void etcptpTestServer()
{

}




int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    printf("I love cheees\n");
    return 0;
}
