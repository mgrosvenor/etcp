#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>


#include <time.h>

#include <sys/socket.h>
#include <linux/if_ether.h>
#include <arpa/inet.h>

#include "CircularQueue.h"
#include "types.h"
#include "spooky_hash.h"
#include "debug.h"

#include "packets.h"


#define likely(x)       if(__builtin_expect((x),1))
#define unlikely(x)     if(__builtin_expect((x),0))
#define eqlikely(x)     if(x)


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
    unlikely(!uc){ return; }

    likely(uc->txcq != NULL){ cqDelete(uc->txcq); }
    likely(uc->rxcq != NULL){ cqDelete(uc->rxcq); }

    free(uc);

}


int cmpFlowId(const etcpFlowId_t* __restrict lhs, const etcpFlowId_t* __restrict rhs)
{
    //THis is ok because flows are packed
    return memcmp(lhs,rhs, sizeof(etcpFlowId_t));
}


etcpConn_t* etcpConnNew(const i64 windowSize, const i64 buffSize, const etcpFlowId_t* flowId)
{
    etcpConn_t* conn = calloc(1, sizeof(etcpConn_t));
    unlikely(!conn){ return NULL; }

    conn->rxcq = cqNew(buffSize,windowSize);
    unlikely(conn->rxcq == NULL){
        etcpConnDelete(conn);
        return NULL;
    }

    conn->txcq = cqNew(buffSize,windowSize);
    unlikely(conn->txcq == NULL){
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
    eqlikely(conn != NULL){
        unlikely(cmpFlowId(&conn->flowId, newFlowId) == 0){
            return etcpEALREADY;
        }

        //Something already in the hash table, traverse to the end
        for(; conn->next; conn = conn->next){
            unlikely(cmpFlowId(&conn->flowId, newFlowId) == 0){
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
    unlikely(conn == NULL){
        return NULL;
    }

    eqlikely(cmpFlowId(&conn->flowId, getFlowId) == 0){
               return conn;
    }

    //Something already in the hash table, traverse to the end
    for(; conn->next; conn = conn->next){
        eqlikely(cmpFlowId(&conn->flowId, getFlowId) == 0){
            return conn;
        }
    }

    return NULL;
}


void etcpOnConnDel(const etcpFlowId_t* const delFlowId)
{
    const i64 idx = getIdx(delFlowId);
    etcpConn_t* conn = etcpState.conns[idx];
    unlikely(conn == NULL){
        return;
    }

    eqlikely(cmpFlowId(&conn->flowId, delFlowId) == 0){
        etcpState.conns[idx] = conn->next;
        etcpConnDelete(conn);
    }

    //Something already in the hash table, traverse to the end
    etcpConn_t* prev = conn;
    for(; conn->next; conn = conn->next){}{
        eqlikely(cmpFlowId(&conn->flowId, delFlowId) == 0){
            prev->next = conn->next;
            etcpConnDelete(conn);
        }
        prev = conn;
    }

}

#define MAXSEGS 1024
#define MAXSEGSIZE (2048 - sizeof(etcpConn_t) - sizeof(cqSlot_t)) //Should bound the CQ slots to 1/2 a page
static inline etcpError_t etcpOnConn(const etcpFlowId_t* const flowId )
{
    etcpError_t err = etcpOnConnAdd(MAXSEGS, MAXSEGSIZE, flowId);
    unlikely(err != etcpENOERR){
        printf("Error adding connection, ignoring\n");
        return err;
    }

    return etcpENOERR;
}


static inline etcpError_t etcpOnDat(const etcpMsgHead_t* head, const i64 len, const etcpFlowId_t* const flowId)
{
    DBG("Working on new data message with type = 0x%016x\n", head->type);

    const i64 minSizeDatHdr = sizeof(etcpMsgHead_t) + sizeof(etcpMsgDatHdr_t);
    unlikely(len < minSizeDatHdr){
        WARN("Not enough bytes to parse data header, required %li but got %li\n", minSizeDatHdr, len);
        return etcpEBADPKT; //Bad packet, not enough data in it
    }
    const etcpMsgDatHdr_t* const datHdr = (const etcpMsgDatHdr_t* const)(head + 1);
    DBG("Working on new data message with seq = 0x%016x\n", datHdr->seqNum);

    //Got a valid data header, more sanity checking
    const uint64_t datLen = len - minSizeDatHdr;
    unlikely(datLen != datHdr->datLen){
        WARN("Data length has unexpected value. Expected %li, but got %li\n",datLen, datHdr->datLen);
        return etcpEBADPKT;
    }
    DBG("Working on new data message with len = 0x%016x\n", datHdr->datLen);

    //Find the connection for this packet
    etcpConn_t* conn = etcpOnConnGet(flowId);
    unlikely(!conn){
        WARN("Error data packet for invalid connection\n");
        return etcpENOTCONN;
    }

    const i64 rxQSlotCount  = conn->rxcq->slotCount;
    const i64 seqPkt        = datHdr->seqNum;
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
    unlikely(seqPkt < seqMin){
        WARN("Ignoring packet, seqPkt %li < %li seqMin, packet has already been ack'd\n", seqPkt, seqMin);
        return etcpERANGE;
    }

    unlikely(seqPkt > seqMax){
        WARN("Ignoring packet, seqPkt %li > %li seqMax, packet will not fit in window\n", seqPkt, seqMax);
        return etcpERANGE;
    }

    i64 toCopy    = len; //By now this value has been checked to be right
    i64 toCopyTmp = toCopy;
    cqError_t err = cqPushIdx(conn->rxcq,head,&toCopyTmp,seqIdx);
    unlikely(err != cqENOERR){
        eqlikely(err == cqETRUNC){
            WARN("Payload (%liB) is too big for slot (%liB), truncating\n", toCopy, toCopyTmp );
        }
        else{
            WARN("Error inserting into Circular Queue: %s", cqError2Str(err));
            return etcpECQERR;
        }
    }

    return etcpENOERR;
}


etcpError_t etcpDoAck(cq_t* const cq, const uint64_t seq, const etcpTime_t* const ackTime)
{
    const uint64_t idx = seq % cq->slotCount;
    DBG("Ack'ing packet with seq=%li and idx=%li\n", seq,idx);
    cqSlot_t* slot = NULL;
    cqError_t err = cqGetSlotIdx(cq,&slot,idx);
    unlikely(err == cqEWRONGSLOT){
        WARN("Got an ACK for a packet that's gone.\n");
        return etcpENOERR;
    }
    else unlikely(err != cqENOERR){
        WARN("Error getting value from Circular Queue: %s", cqError2Str(err));
        return etcpECQERR;
    }

    const etcpMsgHead_t* const head = slot->buff;
    const etcpMsgDatHdr_t* const datHdr = (const etcpMsgDatHdr_t* const)(head + 1);
    unlikely(seq != datHdr->seqNum){
        WARN("Got an ACK for a packet that's gone.\n");
        return etcpENOERR;
    }
    //Successful ack! -- Do timing stats here
    DBG("Successful ack for seq %li at index=%li\n", seq, idx);
    //TODO XXX can do stats here.
    const i64 totalRttTime     = ackTime->swTxTimeNs - head->ts.swTxTimeNs; //Total round trip
    const i64 remoteProcessing = ackTime->swTxTimeNs - ackTime->swRxTimeNs; //Time in software on the remote side
    //Not supported without NIC help, assume this is constant on both sides
    //const i64 remoteHwTime     = ackTime->hwTxTimeNs - ackTime->hwRxTimeNs; //Time in hardware on the remote side
    const i64 localHwTime      = ackTime->hwRxTimeNs - head->ts.hwTxTimeNs; //TIme in hardware on the local side
    const i64 remoteHwTime     = localHwTime;
    const i64 networkTime      = totalRttTime - remoteHwTime - remoteProcessing - localHwTime;
    DBG("Packet spent %lins on the wire\n", networkTime);

    //Packet is now ack'd, we can release this slot and use it for another TX
    cqReleaseSlotRd(cq,idx);
    return etcpENOERR;
}



etcpError_t etcpOnAck(const etcpMsgHead_t* head, const i64 len, const etcpFlowId_t* const flowId)
{
    DBG("Working on new ack message\n");

    const i64 minSizeSackHdr = sizeof(etcpMsgHead_t) + sizeof(etcpMsgSackHdr_t);
    unlikely(len < minSizeSackHdr){
        WARN("Not enough bytes to parse sack header, required %li but got %li\n", minSizeSackHdr, len);
        return etcpEBADPKT; //Bad packet, not enough data in it
    }

    //Got a valid data header, more sanity checking
    const etcpMsgSackHdr_t* const sackHdr = (const etcpMsgSackHdr_t* const)(head + 1);
    const uint64_t sackLen = len - minSizeSackHdr;
    unlikely(sackLen != sackHdr->sackCount * sizeof(etcpSackField_t)){
        WARN("Sack length has unexpected value. Expected %li, but got %li\n",sackLen, sackHdr->sackCount * sizeof(etcpSackField_t));
        return etcpEBADPKT;
    }
    etcpSackField_t* const sackFields = (etcpSackField_t* const)(sackHdr + 1);

    //Find the connection for this packet
    etcpConn_t* conn = etcpOnConnGet(flowId);
    unlikely(!conn){
        WARN("Trying to ACK a packet on a flow that doesn't exist?\n");
        return etcpENOTCONN; //Trying to ACK a packet on a flow that doesn't exist
    }

    //Process the acks
    const uint64_t sackBaseSeq = sackHdr->sackBaseSeq;
    for(i64 sackIdx = 0; sackIdx < sackHdr->sackCount; sackIdx++){
        const uint16_t ackOffset = sackFields[sackIdx].offset;
        const uint16_t ackCount  = sackFields[sackIdx].count;
        DBG("Working on ACKs between %li and %li\n", sackBaseSeq + ackOffset, sackBaseSeq + ackOffset + ackCount);
        for(uint16_t ackIdx = 0; ackIdx < ackCount; ackIdx++){
            const uint64_t ackSeq = sackBaseSeq + ackOffset + ackIdx;
            etcpDoAck(conn->txcq,ackSeq,&head->ts);
        }
    }

    return etcpENOERR;
}




etcpError_t etcpOnPacket( const void* packet, const i64 len, i64 srcAddr, i64 dstAddr, i64 hwRxTimeNs)
{
    //First sanity check the packet
    const i64 minSizeHdr = sizeof(etcpMsgHead_t);
    unlikely(len < minSizeHdr){
        WARN("Not enough bytes to parse ETCP header\n");
        return etcpEBADPKT; //Bad packet, not enough data in it
    }
    etcpMsgHead_t* const head = (etcpMsgHead_t* const)packet;

    //Get a timestamp as soon as we know we have a place to put it.
    struct timespec ts = {0};
    clock_gettime(CLOCK_REALTIME,&ts);
    head->ts.hwRxTimeNs = hwRxTimeNs;
    head->ts.swRxTimeNs  = ts.tv_sec * 1000 * 1000 * 1000 + ts.tv_sec;

    //Do this in a common place since everyone needs it
    const etcpFlowId_t flowId = {
        .srcAddr = srcAddr,
        .dstAddr = dstAddr,
        .srcPort = head->srcPort,
        .dstPort = head->dstPort,
    };

    //Now we can process the message
    switch(head->fulltype){
        case ETCP_V1_FULLHEAD(ETCP_CON):
                etcpOnConn(&flowId);
                /* no break */
        case ETCP_V1_FULLHEAD(ETCP_FIN):
        case ETCP_V1_FULLHEAD(ETCP_DAT):
            return etcpOnDat(head, len, &flowId);

        case ETCP_V1_FULLHEAD(ETCP_DEN):
        case ETCP_V1_FULLHEAD(ETCP_ACK):
            return etcpOnAck(head, len, &flowId);

        default:
            WARN("Bad header, unrecognised type msg_magic=%li (should be %li), version=%i (should be=%i), type=%li\n",
                    head->magic, ETCP_MAGIC, head->ver, ETCP_V1, head->type);
            return etcpEBADPKT; //Bad packet, not enough data in it
    }
    return etcpENOERR;

}


typedef struct __attribute__((packed)){
    uint16_t pcp: 3;
    uint16_t dei: 1;
    uint16_t vid: 12;
} eth8021qTCI_t;


//The expected transport for ETCP is Ethernet, but really it doesn't care. PCIE/IPV4/6 could work as well.
//This function codes the conversion from an Ethernet frame, supplied with a frame check sequence to the ETCP processor.
//It expects that an out-of-band hardware timestamp is also passed in.
etcpError_t etcpOnEthernetFrame( const void* const frame, const i64 len, i64 hwRxTimeNs)
{
    const i64 minSizeEHdr = sizeof(ETH_HLEN + ETH_FCS_LEN);
       unlikely(len < minSizeEHdr){
           WARN("Not enough bytes to parse Ethernet header, expected at least %li but got %li\n", minSizeEHdr, len);
           return etcpEBADPKT; //Bad packet, not enough data in it
       }
       struct ethhdr* const eHead = (struct ethhdr* const) frame ;
       uint64_t dstAddr = 0;
       memcpy(&dstAddr, eHead->h_dest, ETH_ALEN);
       uint64_t srcAddr = 0;
       memcpy(&srcAddr, eHead->h_source, ETH_ALEN);
       uint16_t proto = ntohs(eHead->h_proto);
       const void*  packet = (void* const)(eHead + 1);

       likely(proto == 0x8888 ){
           return etcpOnPacket(packet,len - ETH_HLEN,srcAddr,dstAddr, hwRxTimeNs);
       }

       //This is a vlan tagged packet we can handle these too
       likely(proto == ETH_P_8021Q){
           packet = ((uint8_t*)packet + sizeof(eth8021qTCI_t));
       }

       WARN("Unknown EtherType 0x%04x\n", proto);

       return etcpEBADPKT;

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
//u64 etcp_close(int __fd)
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
