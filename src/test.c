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
#define MIN(x,y) ( (x) < (y) ?  (x) : (y))


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
    etcpFlowId_t flowId;

    etcpConn_t* next; //For chaining in the hashtable

    cq_t* rxcq; //Queue for incoming packets
    cq_t* txcq; //Queue for outgoing packets
    i64 lastTx;
    cq_t* akcq; //Queue for outgoing acknowledgement packets

    i64 seqAck; //The current acknowledge sequence number
    i64 seqSnd; //The current send sequence number


    i64 retransTimeOut; //How long to wait before attempting a retransmit


    //XXX HACKS BELOW!
    int16_t vlan; //XXX HACK - this should be in some nice ethernet place, not here.
    uint8_t priority; //XXX HACK - this should be in some nice ethernet place, not here

    void* hwState;
    int (*hwXMit)(void* const hwState, const void* const data, const int len);
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
    etcpETRYAGAIN,    //There's nothing to see here, come back again
    etcpEFATAL,       //Something irrecoverably bad!
} etcpError_t;


static inline  void etcpConnDelete(etcpConn_t* const uc)
{
    unlikely(!uc){ return; }

    likely(uc->txcq != NULL){ cqDelete(uc->txcq); }
    likely(uc->rxcq != NULL){ cqDelete(uc->rxcq); }

    free(uc);

}


static inline int cmpFlowId(const etcpFlowId_t* __restrict lhs, const etcpFlowId_t* __restrict rhs)
{
    //THis is ok because flows are packed
    return memcmp(lhs,rhs, sizeof(etcpFlowId_t));
}


static inline  etcpConn_t* etcpConnNew(const i64 windowSize, const i64 buffSize, const etcpFlowId_t* flowId)
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


static inline etcpError_t etcpConnAdd( i64 windowSegs, i64 segSize, const etcpFlowId_t* const newFlowId)
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


static inline etcpConn_t* etcpConnGet(const etcpFlowId_t* const getFlowId)
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

//TODO XXX - currently unused!
//static inline void etcpConnDel(const etcpFlowId_t* const delFlowId)
//{
//    const i64 idx = getIdx(delFlowId);
//    etcpConn_t* conn = etcpState.conns[idx];
//    unlikely(conn == NULL){
//        return;
//    }
//
//    eqlikely(cmpFlowId(&conn->flowId, delFlowId) == 0){
//        etcpState.conns[idx] = conn->next;
//        etcpConnDelete(conn);
//    }
//
//    //Something already in the hash table, traverse to the end
//    etcpConn_t* prev = conn;
//    for(; conn->next; conn = conn->next){}{
//        eqlikely(cmpFlowId(&conn->flowId, delFlowId) == 0){
//            prev->next = conn->next;
//            etcpConnDelete(conn);
//        }
//        prev = conn;
//    }
//
//}

#define MAXSEGS 1024
#define MAXSEGSIZE (2048 - sizeof(etcpConn_t) - sizeof(cqSlot_t)) //Should bound the CQ slots to 1/2 a page
static inline etcpError_t etcpOnRxConn(const etcpFlowId_t* const flowId )
{
    etcpError_t err = etcpConnAdd(MAXSEGS, MAXSEGSIZE, flowId);
    unlikely(err != etcpENOERR){
        printf("Error adding connection, ignoring\n");
        return err;
    }

    return etcpENOERR;
}


static inline etcpError_t etcpOnRxDat(const etcpMsgHead_t* head, const i64 len, const etcpFlowId_t* const flowId)
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
    etcpConn_t* conn = etcpConnGet(flowId);
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


static inline  etcpError_t etcpDoAck(cq_t* const cq, const uint64_t seq, const etcpTime_t* const ackTime)
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



static inline  etcpError_t etcpOnRxAck(const etcpMsgHead_t* head, const i64 len, const etcpFlowId_t* const flowId)
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
    etcpConn_t* conn = etcpConnGet(flowId);
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




static inline  etcpError_t etcpOnRxPacket( const void* packet, const i64 len, i64 srcAddr, i64 dstAddr, i64 hwRxTimeNs)
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
                etcpOnRxConn(&flowId);
                /* no break */
        case ETCP_V1_FULLHEAD(ETCP_FIN): //XXX TODO, currently only the send side can disconnect...
        case ETCP_V1_FULLHEAD(ETCP_DAT):
            return etcpOnRxDat(head, len, &flowId);

        case ETCP_V1_FULLHEAD(ETCP_ACK):
            return etcpOnRxAck(head, len, &flowId);

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

#define ETH_P_ECTP 0x8888


//The expected transport for ETCP is Ethernet, but really it doesn't care. PCIE/IPV4/6 could work as well.
//This function codes the conversion from an Ethernet frame, supplied with a frame check sequence to the ETCP processor.
//It expects that an out-of-band hardware timestamp is also passed in.
static inline  etcpError_t etcpOnRxEthernetFrame( const void* const frame, const i64 len, i64 hwRxTimeNs)
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

       const void* packet = (void* const)(eHead + 1);
       i64 packetLen      = len - ETH_HLEN - ETH_FCS_LEN;

       likely(proto == ETH_P_ECTP ){
           return etcpOnRxPacket(packet,packetLen,srcAddr,dstAddr, hwRxTimeNs);
       }

       //This is a VLAN tagged packet we can handle these too
       likely(proto == ETH_P_8021Q){
           packet    = ((uint8_t*)packet + sizeof(eth8021qTCI_t));
           packetLen = len - sizeof(eth8021qTCI_t);
           return etcpOnRxPacket(packet,packetLen,srcAddr,dstAddr, hwRxTimeNs);
       }

       WARN("Unknown EtherType 0x%04x\n", proto);

       return etcpEBADPKT;

}


static inline etcpError_t etcpMkEthPkt(void* const buff, i64* const len_io, const uint64_t srcAddr, const uint64_t dstAddr, const i16 vlan, const uint8_t priority )
{
    const i64 len = *len_io;
    unlikely(len < ETH_ZLEN){
        ERR("Not enough bytes in Ethernet frame. Required %li but got %i\n", ETH_ZLEN, len );
        return etcpEFATAL;
    }

    struct ethhdr* const ethHdr = buff;

    memcpy(&ethHdr->h_dest, &dstAddr, ETH_ALEN);
    memcpy(&ethHdr->h_source, &srcAddr, ETH_ALEN);
    ethHdr->h_proto = htons(ETH_P_ECTP);

    eqlikely(vlan < 0){
        //No VLAN tag header, we're done!
        *len_io = ETH_HLEN;
        return etcpENOERR;
    }

    eth8021qTCI_t* vlanHdr = (eth8021qTCI_t*)(ethHdr + 1);
    vlanHdr->dei= 0;
    vlanHdr->vid = vlan;
    vlanHdr->pcp = priority;

    *len_io = ETH_HLEN + sizeof(eth8021qTCI_t);
    return etcpENOERR;


}

//Assumes ethernet packets, does in-place construction of a packet and puts it into the circular queue ready to send
static inline etcpError_t etcpEthSend(etcpConn_t* const conn, const void* const toSendData, i64* const toSendLen_io)
{
    const i64 toSendLen = *toSendLen_io;
    i64 bytesSent = 0;

    cq_t* const txcq = conn->txcq;
    while(bytesSent< toSendLen){
        cqSlot_t* slot = NULL;
        i64 slotIdx = 0;
        cqError_t cqErr = cqGetNextWr(txcq,&slot,&slotIdx);

        //We haven't send as much as we'd hoped, set the len_io and tell user to try again
        unlikely(cqErr == cqENOSLOT){
            *toSendLen_io = bytesSent;
            return etcpETRYAGAIN;
        }
        //Some other strange error. Shit.
        else unlikely(cqErr != cqENOERR){
            ERR("Error on circular queue: %s", cqError2Str(cqErr));
            return etcpECQERR;
        }

        //We got a slot, now format a packet into it
        i8* buff = slot->buff;
        i64 buffLen = slot->len;
        i64 ethLen = buffLen;
        etcpError_t etcpErr = etcpMkEthPkt(buff,&ethLen,conn->flowId.srcAddr, conn->flowId.dstAddr,conn->vlan, conn->priority);
        unlikely(etcpErr != etcpENOERR){
            WARN("Could not format Ethernet packet\n");
            return etcpErr;
        }
        buff += ethLen;
        buffLen -= ethLen;

        const i64 hdrsLen = sizeof(etcpMsgHead_t) + sizeof(etcpMsgDatHdr_t);
        unlikely(buffLen < hdrsLen + 1){ //Should be able to send at least 1 byte!
            ERR("Slot lengths are too small!");
            return etcpEFATAL;
        }
        const i64 datSpace = buffLen - hdrsLen;
        const i64 datLen   = MIN(datSpace,toSendLen);

        struct timespec ts = {0};
        clock_gettime(CLOCK_REALTIME,&ts);
        etcpMsgHead_t* const head = (etcpMsgHead_t* const)buff;
        head->fulltype      = ETCP_V1_FULLHEAD(ETCP_DAT);
        head->srcPort       = conn->flowId.srcPort;
        head->dstPort       = conn->flowId.dstPort;
        head->ts.swTxTimeNs = ts.tv_nsec * 1000 * 1000 * 1000 + ts.tv_nsec;

        etcpMsgDatHdr_t* const datHdr = (etcpMsgDatHdr_t* const)(head + 1);

        datHdr->datLen     = datLen;
        datHdr->seqNum     = conn->seqSnd;
        datHdr->txAttempts = 0;

        void* const msgDat = (void* const)(datHdr + 1);
        memcpy(msgDat,toSendData,datLen);

        //At this point, the packet is now ready to send!
        const i64 totalLen = ethLen + hdrsLen + datLen;
        cqErr = cqCommitSlot(conn->txcq,slotIdx, totalLen);
        unlikely(cqErr != cqENOERR){
            ERR("Error on circular queue: %s", cqError2Str(cqErr));
            return etcpECQERR;
        }

        bytesSent += datLen;

    }

    *toSendLen_io = bytesSent;
    return etcpENOERR;

}


//Traverse the send queues and check what can be sent.
//First look over the ack send queue, we send ACK packets as a priority. If the ackQueue is empty, then look over the send
//queue and check if anything has timed out.
etcpError_t doCqSend(etcpConn_t* const conn)
{
    //Sending the ack's is simple, just send and forget;
    cqSlot_t* slot = NULL;
    i64 slotIdx;
    while(cqGetNextRd(conn->akcq,&slot,&slotIdx) != cqENOSLOT){
        conn->hwXMit(conn->hwState,slot->buff, slot->len);
        cqError_t err = cqReleaseSlotRd(conn->akcq,slotIdx);
        unlikely(err != cqENOERR){
            ERR("CQ had an error: %s\n", cqError2Str(err));
            return etcpECQERR;
        }
    }

    struct timespec ts = {0};
    clock_gettime(CLOCK_REALTIME,&ts);
    const i64 timeNowNs = ts.tv_sec * 1000 * 1000 * 1000 + ts.tv_nsec;

    //Sending the TX packets is a little more complicated because they need to stay until we've received an ack.
    const i64 slotCount = conn->txcq->slotCount;
    for(i64 i = 0; i < slotCount; i++){

        conn->lastTx = conn->lastTx + i <  slotCount ? conn->lastTx + i : conn->lastTx + i - slotCount;
        const cqError_t err = cqGetSlotIdx(conn->akcq,&slot,conn->lastTx);
        eqlikely(err == cqEWRONGSLOT){
            //The slot is empty
            continue;
        }
        else unlikely(err != cqENOERR){
            ERR("Error getting slot: %s\n", cqError2Str(err));
            return etcpECQERR;
        }

        //We've now got a valid slot with a packet in it, grab the timestamp to see if it needs sending.
        const i64 skipEthHdrOffset          = conn->vlan < 0 ? ETH_HLEN : ETH_HLEN + sizeof(eth8021qTCI_t);
        const i8* const slotBuff            = slot->buff;
        const etcpMsgHead_t* const head     = (etcpMsgHead_t* const)(slotBuff + skipEthHdrOffset);
        const etcpMsgDatHdr_t* const datHdr = (etcpMsgDatHdr_t* const)(head +1);
        const i64 rtto                      = conn->retransTimeOut;
        const i64 swTxTimeNs                = head->ts.swRxTimeNs;
        const i64 txAttempts                = datHdr->txAttempts;
        const i64 transTimeNs               = swTxTimeNs + rtto * txAttempts;

        if(timeNowNs > transTimeNs){
            if(conn->hwXMit(conn->hwState,slot->buff, slot->len) < 0){
                return etcpETRYAGAIN;
            }
            //Do NOT release the slot here. The release will happen in the RX code when an ack comes through.
        }
    }

    return etcpENOERR;;
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
