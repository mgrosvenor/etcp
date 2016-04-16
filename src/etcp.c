/*
 * Copyright (c) 2016, All rights reserved.
 * See LICENSE.txt for full details. 
 * 
 *  Created:   30 Mar 2016
 *  File name: tecp.c
 *  Description:
 *  <INSERT DESCRIPTION HERE> 
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <assert.h>

#include <time.h>

#include <sys/socket.h>
#include <linux/if_ether.h>
#include <arpa/inet.h>

#include "CircularQueue.h"
#include "types.h"
#include "spooky_hash.h"
#include "debug.h"

#include "packets.h"
#include "utils.h"
#include "etcp.h"

#include "etcpState.h"
#include "etcpSockApi.h"


static inline etcpError_t etcpOnRxDat(etcpState_t* const state, const etcpMsgHead_t* head, const i64 len, const etcpFlowId_t* const flowId)
{
    DBG("Working on new data message with type = 0x%016x\n", head->type);

    const i64 minSizeDatHdr = sizeof(etcpMsgHead_t) + sizeof(etcpMsgDatHdr_t);
    if_unlikely(len < minSizeDatHdr){
        WARN("Not enough bytes to parse data header, required %li but got %li\n", minSizeDatHdr, len);
        return etcpEBADPKT; //Bad packet, not enough data in it
    }
    etcpMsgDatHdr_t* const datHdr = (etcpMsgDatHdr_t* const)(head + 1);
    DBG("Working on new data message with seq = 0x%016x\n", datHdr->seqNum);

    //Got a valid data header, more sanity checking
    const uint64_t datLen = len - minSizeDatHdr;
    if_unlikely(datLen != datHdr->datLen){
        WARN("Data length has unexpected value. Expected %li, but got %li\n",datLen, datHdr->datLen);
        return etcpEBADPKT;
    }
    DBG("Working on new data message with len = %li\n", datHdr->datLen);

    //Find the source map for this packet
    const htKey_t dstKey = {.keyHi = flowId->dstAddr, .keyLo = flowId->dstPort };
    etcpLAMap_t* srcsMap = NULL;
    htError_t htErr = htGet(state->dstMap,&dstKey,(void**)&srcsMap);
    if_unlikely(htErr == htENOTFOUND){
        DBG("Packet unexpected. No one listening to Add=%li, Port=%li\n", flowId->dstAddr, flowId->dstPort);
        return etcpEREJCONN;
    }
    else if_unlikely(htErr != htENOEROR){
        DBG("Unexpected hash table error: %s\n", htError2Str(htErr));
        return etcpEHTERR;
    }

    //Someone is listening to this destination, but is this the connection already established? Try to get the connection
    etcpConn_t* recvConn = NULL;
    etcpConn_t* sendConn = NULL;
    const htKey_t srcKey = { .keyHi = flowId->srcAddr, .keyLo = flowId->srcPort };
    htErr = htGet(srcsMap->table,&srcKey,(void**)&recvConn);
    if(htErr == htENOTFOUND){
        etcpError_t err = addNewConn(state, srcsMap, flowId, datHdr->noRet, &recvConn, &sendConn);
        if_unlikely(err != etcpENOERR){
            DBG("Error trying to add new connection\n");
            return err;
        }
    }

    //By this point, the connection structure should be properly populated one way or antoher
    const i64 seqPkt        = datHdr->seqNum;
    const i64 seqMin        = recvConn->rxQ->rdMin; //The very minimum sequence number that we will consider
    const i64 seqMax        = recvConn->rxQ->wrMax; //One greater than the biggest seq we can handle
    DBG("seqPkt = %li, seqMin= %li, seqMax = %li\n",
        seqPkt,
        seqMin,
        seqMax
    );

    //When we receive a packet, it should be between seqMin and seqMax
    //-- if seq >= seqMax, it is beyond the end of the rx window, ignore it, the packet will be sent again
    //-- if seq < seqMin, it has already been ack'd, the ack must have got lost, send another ack
    if_unlikely(seqPkt >= seqMax){
        WARN("Ignoring packet, seqPkt %li > %li seqMax, packet will not fit in window\n", seqPkt, seqMax);
        return etcpERANGE;
    }

    if_unlikely(seqPkt < seqMin){
        WARN("Stale packet, seqPkt %li < %li seqMin, packet has already been ack'd\n", seqPkt, seqMin);

        if_eqlikely(datHdr->noAck){
            //This packet does not want an ack, and it's stale, so just ignore it
            return etcpECQERR;;
        }

        //Packet does want an ack. We can't ignore these because the send side might be waiting for a lost ack, which will
        //hold up new TX's
        //Note: This is a slow-path, a ordered push to a LL O(n) + malloc() is more costly a push to the CQ O(c).

        i64 toCopy    = len; //By now this value has been checked to be right
        i64 toCopyTmp = toCopy;
        datHdr->staleDat = 1; //Mark the packet as stale so we don't try to deliver again to user
        llError_t err = llPushSeqOrd(recvConn->staleQ,head,&toCopyTmp,seqPkt);
        if_unlikely(err == llETRUNC){
            WARN("Payload (%liB) is too big for slot (%liB), truncating\n", toCopy, toCopyTmp );
        }
        else if_unlikely(err != llENOERR){
            WARN("Error inserting into linked-list: %s", llError2Str(err));
            return etcpECQERR;
        }

        return etcpENOERR;

    }

    i64 toCopy    = len; //By now this value has been checked to be right
    i64 toCopyTmp = toCopy;
    cqError_t err = cqPush(recvConn->rxQ,head,&toCopyTmp,seqPkt);

    if_unlikely(err == cqENOSLOT){
        WARN("Unexpected state, packet not enough space in queue\n");
        return etcpETRYAGAIN; //We've run out of slots to put packets into, drop the packet. But can this actually happen?
    }
    else if_unlikely(err == cqETRUNC){
        WARN("Payload (%liB) is too big for slot (%liB), truncating\n", toCopy, toCopyTmp );
    }
    else if_unlikely(err != cqENOERR){
        WARN("Error inserting into Circular Queue: %s", cqError2Str(err));
        return etcpECQERR;
    }

    cqCommitSlot(recvConn->rxQ,seqPkt,toCopyTmp);

    return etcpENOERR;
}

static inline  etcpError_t etcpProcessAck(cq_t* const cq, const uint64_t seq, const etcpTime_t* const ackTime, const etcpTime_t* const datFirstTime, const etcpTime_t* const datLastTime)
{
    DBG("Ack'ing packet with seq=%li\n", seq);
    cqSlot_t* slot = NULL;
    const cqError_t err = cqGetRd(cq,&slot,seq);
    if_unlikely(err == cqEWRONGSLOT){
        WARN("Got an (duplicate) ACK for a packet that's gone.\n");
        return etcpENOERR;
    }
    else if_unlikely(err != cqENOERR){
        WARN("Error getting value from Circular Queue: %s", cqError2Str(err));
        return etcpECQERR;
    }

    const pBuff_t* pbuff = slot->buff;
    const etcpMsgHead_t* const head = pbuff->etcpHdr;
    const etcpMsgDatHdr_t* const datHdr = pbuff->etcpDatHdr;
    if_unlikely(seq != datHdr->seqNum){
        WARN("Got an ACK for a packet that's gone.\n");
        return etcpENOERR;
    }
    //Successful ack! -- Do timing stats here
    DBG("Successful ack for seq %li\n", seq);

    //TODO XXX can do stats here.
    const i64 totalRttTime     = ackTime->swRxTimeNs - head->ts.swTxTimeNs; //Total round trip for the sack vs dat
//    const i64 remoteProcessing = ackTime->swTxTimeNs - datFirstTime->swRxTimeNs; //Time between the first data packet RX and the ack TX
//    //Not supported without NIC help, assume this is constant on both sides
//    //const i64 remoteHwTime     = ackTime->hwTxTimeNs - ackTime->hwRxTimeNs; //Time in hardware on the remote side
//    const i64 localHwTxTime    = head->ts.hwTxTimeNs - head->ts.swTxTimeNs; //Time in TX hardware on the local side
//    const i64 localHwRxTime    = ackTime->swRxTimeNs - ackTime->hwRxTimeNs; //Time in RX hardware on the local side
//    const i64 localHwTime      = localHwTxTime + localHwRxTime;
//    const i64 remoteHwTime     = localHwTime;
//    const i64 networkTime      = totalRttTime - remoteHwTime - remoteProcessing - localHwTime;
    (void)datLastTime;//Not needed right now
    (void)datFirstTime;
//    DBG("TIMING STATS:\n");
//    DBG("-------------------------------------\n");
    DBG("Total RTT:           %lins (%lius, %lims, %lis)\n", totalRttTime, totalRttTime / 1000, totalRttTime / 1000/1000, totalRttTime / 1000/1000/1000);
//    DBG("Remote Processing:   %lins (%lius, %lims, %lis)\n", remoteProcessing, remoteProcessing/ 1000, remoteProcessing / 1000/1000, remoteProcessing / 1000/1000/1000);
//    DBG("Local HW TX:         %lins (%lius, %lims, %lis)\n", localHwTxTime, localHwTxTime / 1000, localHwTxTime / 1000/1000, localHwTxTime / 1000/1000/1000 );
//    DBG("Local HW RX:         %lins (%lius, %lims, %lis)\n", localHwRxTime, localHwRxTime / 1000, localHwRxTime / 1000/1000, localHwRxTime / 1000/1000/1000 );
//    DBG("Local HW:            %lins (%lius, %lims, %lis)\n", localHwTime, localHwTime/1000, localHwTime / 1000/1000, localHwTime / 1000/1000/1000);
//    DBG("Remote HW (guess)    %lins (%lius, %lims, %lis)\n", remoteHwTime, remoteHwTime/1000, remoteHwTime/ 1000/1000, remoteHwTime / 1000/1000/1000);
//    DBG("Network time:        %lins (%lius, %lims, %lis)\n", networkTime, networkTime/1000, networkTime/1000/1000, networkTime / 1000/1000/1000);
//    DBG("-------------------------------------\n");

    //Packet is now ack'd, we can release this slot and use it for another TX
    const cqError_t cqErr = cqReleaseSlot(cq,seq);
    if_unlikely(cqErr != cqENOERR){
        ERR("Unexpected cq error: %s\n", cqError2Str(cqErr));
        return etcpECQERR;
    }
    return etcpENOERR;
}



static inline  etcpError_t etcpOnRxAck(etcpState_t* const state, const etcpMsgHead_t* head, const i64 msgLen, const etcpFlowId_t* const flowId)
{
    DBG("Working on new ack message\n");

    const i64 minSizeSackHdr = sizeof(etcpMsgHead_t) + sizeof(etcpMsgSackHdr_t);
    if_unlikely(msgLen < minSizeSackHdr){
        WARN("Not enough bytes to parse sack header, required %li but got %li\n", minSizeSackHdr, msgLen);
        return etcpEBADPKT; //Bad packet, not enough data in it
    }

    //Got a valid sack header, more sanity checking
    const etcpMsgSackHdr_t* const sackHdr = (const etcpMsgSackHdr_t* const)(head + 1);
    const i64 sackLen = msgLen - minSizeSackHdr;
    if_unlikely(sackLen != sackHdr->sackCount * sizeof(etcpSackField_t)){
        WARN("Sack length has unexpected value. Expected %li, but got %li\n",sackLen, sackHdr->sackCount * sizeof(etcpSackField_t));
        return etcpEBADPKT;
    }
    etcpSackField_t* const sackFields = (etcpSackField_t* const)(sackHdr + 1);

    //Find the source map for this packet
    const htKey_t dstKey = {.keyHi = flowId->srcAddr, .keyLo = flowId->srcPort }; //Since this is an ack, we swap src / dst
    etcpLAMap_t* srcsMap = NULL;
    htError_t htErr = htGet(state->dstMap,&dstKey,(void**)&srcsMap);
    if_unlikely(htErr == htENOTFOUND){
        DBG("Ack unexpected. No one listening to destination addr=%li, port=%li\n", flowId->srcAddr, flowId->srcPort);
        return etcpEREJCONN;
    }
    else if_unlikely(htErr != htENOEROR){
        DBG("Hash table error: %s\n", htError2Str(htErr));
        return etcpEHTERR;
    }

    //Someone is listening to this destination, but is there also someone listening to the source?
    etcpConn_t* conn = NULL;
    const htKey_t srcKey = { .keyHi = flowId->dstAddr, .keyLo = flowId->dstPort }; //Flip these for an ack packet
    htErr = htGet(srcsMap->table,&srcKey,(void**)&conn);
    if_unlikely(htErr == htENOTFOUND){
        DBG("Ack unexpected. No one listening to source addr=%li, port=%li\n", flowId->dstAddr, flowId->dstPort);
        return etcpEREJCONN;
    }
    else if_unlikely(htErr != htENOEROR){
        DBG("Hash table error: %s\n", htError2Str(htErr));
        return etcpEHTERR;
    }

    //By now we have located the connection structure for this ack packet
    //Try to put the sack into the AckRxQ so that the Tranmission Control function can use it as an input
    i64 slotIdx = -1;
    i64 len = sackLen;
    cqPushNext(conn->rxQ,sackHdr, &len,&slotIdx); //No error checking it's ok if this fails.
    if(len < sackLen){
        WARN("Truncated SACK packet into ackRxQ\n");
    }
    cqCommitSlot(conn->rxQ,slotIdx,len);


    //Process the acks and apply to TX packets waiting.
    const uint64_t sackBaseSeq = sackHdr->sackBaseSeq;
    for(i64 sackIdx = 0; sackIdx < sackHdr->sackCount; sackIdx++){
        const uint16_t ackOffset = sackFields[sackIdx].offset;
        const uint16_t ackCount  = sackFields[sackIdx].count;
        DBG("Working on ACKs between %li and %li\n", sackBaseSeq + ackOffset, sackBaseSeq + ackOffset + ackCount);
        for(uint16_t ackIdx = 0; ackIdx < ackCount; ackIdx++){
            const uint64_t ackSeq = sackBaseSeq + ackOffset + ackIdx;
            etcpProcessAck(conn->txQ,ackSeq,&head->ts, &sackHdr->timeFirst, &sackHdr->timeLast);
        }
    }

    return etcpENOERR;
}




static inline  etcpError_t etcpOnRxPacket(etcpState_t* const state, const void* packet, const i64 len, i64 srcAddr, i64 dstAddr, i64 hwRxTimeNs)
{
    //First sanity check the packet
    const i64 minSizeHdr = sizeof(etcpMsgHead_t);
    if_unlikely(len < minSizeHdr){
        WARN("Not enough bytes to parse ETCP header\n");
        return etcpEBADPKT; //Bad packet, not enough data in it
    }
    etcpMsgHead_t* const head = (etcpMsgHead_t* const)packet;

    //Get a timestamp as soon as we know we have a place to put it.
    struct timespec ts = {0};
    clock_gettime(CLOCK_REALTIME,&ts);
    head->ts.hwRxTimeNs = hwRxTimeNs;
    head->ts.swRxTimeNs  = ts.tv_sec * 1000 * 1000 * 1000 + ts.tv_nsec;
    head->hwRxTs = 1;
    head->swRxTs = 1;

    //Do this in a common place since everyone needs it
    const etcpFlowId_t flowId = {
        .srcAddr = srcAddr,
        .dstAddr = dstAddr,
        .srcPort = head->srcPort,
        .dstPort = head->dstPort,
    };

    //Now we can process the message
    switch(head->fulltype){
//        case ETCP_V1_FULLHEAD(ETCP_FIN): //XXX TODO, currently only the send side can disconnect...
        case ETCP_V1_FULLHEAD(ETCP_DAT):
            return etcpOnRxDat(state, head, len, &flowId);

        case ETCP_V1_FULLHEAD(ETCP_ACK):
            return etcpOnRxAck(state, head, len, &flowId);

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
//This function codes the assumes an Ethernet frame, supplied with a frame check sequence to the ETCP processor.
//It expects that an out-of-band hardware timestamp is also passed in.
static inline  etcpError_t etcpOnRxEthernetFrame(etcpState_t* const state, const void* const frame, const i64 len, i64 hwRxTimeNs)
{
    const i64 minSizeEHdr = ETH_HLEN + ETH_FCS_LEN;
       if_unlikely(len < minSizeEHdr){
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
       i64 etcpPacketLen      = len - ETH_HLEN - ETH_FCS_LEN;

       if_likely(proto == ETH_P_ECTP ){
           return etcpOnRxPacket(state, packet,etcpPacketLen,srcAddr,dstAddr, hwRxTimeNs);
       }

       //This is a VLAN tagged packet we can handle these too
       if_likely(proto == ETH_P_8021Q){
           packet        = ((uint8_t*)packet + sizeof(eth8021qTCI_t));
           etcpPacketLen = len - sizeof(eth8021qTCI_t);
           return etcpOnRxPacket(state, packet,etcpPacketLen,srcAddr,dstAddr, hwRxTimeNs);
       }

       WARN("Unknown EtherType 0x%04x\n", proto);

       return etcpEBADPKT;

}


static inline etcpError_t etcpMkEthPkt(void* const buff, i64* const len_io, const uint64_t srcAddr, const uint64_t dstAddr, const i16 vlan, const uint8_t priority )
{
    DBG("Making ethernet packet with source address=0x%016lx, dstAddr=0x%016lx, vlan=%i, priority=%i\n",
            srcAddr,
            dstAddr,
            vlan,
            priority);

    const i64 len = *len_io;
    if_unlikely(len < ETH_ZLEN){
        ERR("Not enough bytes in Ethernet frame. Required %li but got %i\n", ETH_ZLEN, len );
        return etcpEFATAL;
    }

    struct ethhdr* const ethHdr = buff;

    memcpy(&ethHdr->h_dest, &dstAddr, ETH_ALEN);
    memcpy(&ethHdr->h_source, &srcAddr, ETH_ALEN);
    ethHdr->h_proto = htons(ETH_P_ECTP);

    if_eqlikely(vlan < 0){
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

//Put a sack packet into a buffer for transmit
static inline etcpError_t pushSackEthPacket(etcpConn_t* const conn, const i8* const sackHdrAndData, const i64 sackCount)
{
    cqSlot_t* slot = NULL;
    i64 seqNum;
    cqError_t cqErr = cqGetNextWr(conn->txQ, &slot,&seqNum);
    if_unlikely(cqErr == cqENOSLOT){
        DBG("Ran out of ACK queue slots\n");
        return etcpETRYAGAIN;
    }
    else if_unlikely(cqErr != cqENOERR){
        ERR("Error on circular queue: %s", cqError2Str(cqErr));
        return etcpECQERR;
    }

    //We got a slot, now check it's big enough then format a packet into it
    pBuff_t * pBuff = slot->buff;
    pBuff->buffer = (i8*)slot->buff + sizeof(pBuff_t);
    pBuff->buffSize = slot->len - sizeof(pBuff_t);

    i8* buff = pBuff->buffer;
    i64 buffLen = pBuff->buffSize;
    const i64 sackHdrAndDatSize = sizeof(etcpMsgSackHdr_t) + sizeof(etcpSackField_t) * sackCount;
    const i64 ethOverhead       = conn->vlan < 0 ? ETH_HLEN : ETH_HLEN + 2; //Include vlan space if needed!
    const i64 ethEtcpSackPktSize = ethOverhead + sizeof(etcpMsgHead_t) + sackHdrAndDatSize;
    if_unlikely(buffLen < ethEtcpSackPktSize){
        ERR("Slot length is too small for sack packet need %li but only got %li!",ethEtcpSackPktSize, buffLen );
    }

    //NB: Reverse the srcAddr and dstAddr so that the packet goes back to where it came
    i64 ethLen = buffLen;
    etcpError_t etcpErr = etcpMkEthPkt(buff,&ethLen, conn->flowId.dstAddr, conn->flowId.srcAddr, conn->vlan, conn->priority);
    if_unlikely(etcpErr != etcpENOERR){
        WARN("Could not format Ethernet packet\n");
        return etcpErr;
    }
    pBuff->encapHdr = buff;
    pBuff->encapHdrSize = ethLen;
    buff += ethLen;
    pBuff->msgSize = pBuff->encapHdrSize;

    etcpMsgHead_t* const head = (etcpMsgHead_t* const)buff;
    pBuff->etcpHdr      = head;
    pBuff->etcpHdrSize  = sizeof(etcpMsgHead_t);
    pBuff->msgSize     += pBuff->etcpHdrSize;
    head->fulltype      = ETCP_V1_FULLHEAD(ETCP_ACK);
    //Reverse the source and destination ports so that the packet goest back to where it came from
    head->srcPort       = conn->flowId.dstPort;
    head->dstPort       = conn->flowId.srcPort;
    buff                += sizeof(etcpMsgHead_t);

    pBuff->etcpSackHdr      = (etcpMsgSackHdr_t*)buff;
    pBuff->etcpSackHdrSize  = sizeof(etcpMsgSackHdr_t);
    pBuff->msgSize         += pBuff->etcpSackHdrSize;

    pBuff->etcpPayload  = buff + sizeof(etcpMsgSackHdr_t);
    pBuff->etcpPayloadSize = sizeof(etcpSackField_t) * sackCount;
    pBuff->msgSize         += pBuff->etcpPayloadSize;

    memcpy(buff,sackHdrAndData,sackHdrAndDatSize);

    cqErr = cqCommitSlot(conn->txQ,seqNum,ethEtcpSackPktSize);
    if_unlikely(cqErr != cqENOERR){
        ERR("Unexpected error on CQ: %s\n", cqError2Str(cqErr));
        return etcpECQERR;
    }
    assert(pBuff->msgSize == ethEtcpSackPktSize);

    return etcpENOERR;
}



//Traverse the receive queues and generate ack's
etcpError_t generateStaleAcks(etcpConn_t* const conn, const i64 maxAckPackets, const i64 maxSlots)
{
    if_eqlikely(maxAckPackets <= 0){
        return etcpENOERR; //Don't bother trying if you don't want me to!
    }

    if_unlikely(conn->staleQ->slotCount == 0){
        return etcpENOERR; //There's nothing to do here
    }

    i64 fieldIdx          = 0;
    bool fieldInProgress  = false;
    i64  expectSeqNum       = 0;
    i64 unsentAcks        = 0;
    const i64 tmpBuffSize = sizeof(etcpMsgSackHdr_t) + sizeof(etcpSackField_t) * ETCP_MAX_SACKS;
    assert(sizeof(tmpBuffSize) <=  256);
    i8 tmpBuff[tmpBuffSize];
    memset(tmpBuff,0,tmpBuffSize);

    etcpMsgSackHdr_t* const sackHdr  = (etcpMsgSackHdr_t* const)(tmpBuff + 0 );
    etcpSackField_t* const sackFields = ( etcpSackField_t* const)(tmpBuff + sizeof(etcpMsgSackHdr_t));

    //Iterate through the rx packets to build up sack ranges
    i64 completeAckPackets = 0;
    for(i64 i = 0; i < maxSlots; i++){

        //We collected enough sack fields to make a whole packet and send it
        if_unlikely(fieldIdx >= ETCP_MAX_SACKS){
            //sackHdr->rxWindowSegs = 0;
            etcpError_t err = pushSackEthPacket(conn,tmpBuff,ETCP_MAX_SACKS);
            if_unlikely(err == etcpETRYAGAIN){
                WARN("Ran out of slots for sending acks, come back again\n");
                return err;
            }
            else if_unlikely(err != etcpENOERR){
                ERR("Unexpected error making ack packet\n");
                return err;
            }

            //Reset the sackStructure to make a new one
            memset(tmpBuff,0,tmpBuffSize);
            fieldIdx        = 0;
            fieldInProgress = false;
            unsentAcks      = 0;

            if(completeAckPackets >= maxAckPackets){
                return etcpENOERR;
            }
        }


        llSlot_t* slot = NULL;
        const llError_t err = llGetFirst(conn->staleQ,&slot);
        if_unlikely(err != llENOERR){
            break;
        }
        
        //The next sequence we get should be either equalt to the expected sequence
        if_unlikely(i == 0){
            expectSeqNum = slot->seqNum;
        }
        const i64 seqNum = slot->seqNum;

        DBG("Now looking at seq %i\n", seqNum);

        if_unlikely(seqNum < expectSeqNum){
            if(seqNum == expectSeqNum -1){
                WARN("Duplicate entries for sequence number %li in stale queue, igoring this one\n", seqNum);
                llReleaseHead(conn->staleQ); //We're done with this packet, throw away
                continue;

            }

            FAT("This should not happen, sequence numbers have gone backwards from %li to %li. This means there's been an ordering violation in the stale queue\n", seqNum, expectSeqNum);
            return etcpEFATAL;
        }

        //There is a break in the sequence number series. Start/finish a sack field
        if_eqlikely(seqNum > expectSeqNum){
            //The slot is empty --
            if_likely(fieldInProgress){
                fieldIdx++;
                fieldInProgress = false;
                expectSeqNum = seqNum; //Jump the expected value up to the new value
            }
        }

        //At this point we have a valid packet
        const i8* const slotBuff            = slot->buff;
        const etcpMsgHead_t* const head     = (etcpMsgHead_t* const)(slotBuff);
        etcpMsgDatHdr_t* const datHdr       = (etcpMsgDatHdr_t* const)(head +1);

        //Start a new field
        if_unlikely(!fieldInProgress){
            if_unlikely(fieldIdx == 0){
                sackHdr->timeFirst = head->ts;
                sackHdr->sackBaseSeq = seqNum;
            }

            sackFields[fieldIdx].offset = datHdr->seqNum - sackHdr->sackBaseSeq;
            sackFields[fieldIdx].count = 0;
        }
        unsentAcks++;
        sackFields[fieldIdx].count++;
        sackHdr->timeLast = head->ts;
        datHdr->ackSent = 1;
        expectSeqNum++;
    }

    //Push the last sack out
    if(unsentAcks > 0){
        sackHdr->sackCount = fieldIdx+1;
        etcpError_t err = pushSackEthPacket(conn,tmpBuff,fieldIdx+1);
        if_unlikely(err == etcpETRYAGAIN){
            WARN("Ran out of slots for sending acks, come back again\n");
            return err;
        }
        else if_unlikely(err != etcpENOERR){
            ERR("Unexpected error making ack packet\n");
            return err;
        }
    }
    return etcpENOERR;

}



//Traverse the receive queues and generate ack's
etcpError_t generateAcks(etcpConn_t* const conn, const i64 maxAckPackets, const i64 maxSlots)
{
    if_eqlikely(maxAckPackets <= 0){
        return etcpENOERR; //Don't bother trying if you don't want me to!
    }


    if_unlikely(conn->rxQ->readable == 0){
        return etcpENOERR; //There's nothing to do here
    }

    i64 fieldIdx          = 0;
    bool fieldInProgress  = false;
    i64 unsentAcks        = 0;
    const i64 tmpBuffSize = sizeof(etcpMsgSackHdr_t) + sizeof(etcpSackField_t) * ETCP_MAX_SACKS;
    assert(sizeof(tmpBuffSize) <=  256);
    i8 tmpBuff[tmpBuffSize];
    memset(tmpBuff,0,tmpBuffSize);

    etcpMsgSackHdr_t* const sackHdr  = (etcpMsgSackHdr_t* const)(tmpBuff + 0 );
    etcpSackField_t* const sackFields = ( etcpSackField_t* const)(tmpBuff + sizeof(etcpMsgSackHdr_t));

    //Iterate through the rx packets to build up sack ranges
    i64 completeAckPackets = 0;
    for(i64 i = conn->rxQ->rdMin; i < conn->rxQ->rdMax && i < conn->rxQ->rdMin + maxSlots; i++){

        //We collected enough sack fields to make a whole packet and send it
        if_unlikely(fieldIdx >= ETCP_MAX_SACKS){
            //sackHdr->rxWindowSegs = 0;
            etcpError_t err = pushSackEthPacket(conn,tmpBuff,ETCP_MAX_SACKS);
            if_unlikely(err == etcpETRYAGAIN){
                WARN("Ran out of slots for sending acks, come back again\n");
                return err;
            }
            else if_unlikely(err != etcpENOERR){
                ERR("Unexpected error making ack packet\n");
                return err;
            }

            //Account for the now sent messages by updating the seqAck value, only do so up to the edge of the first field, this
            //is the limit that will be receivable until new packets come.
            conn->seqAck = conn->seqAck + sackFields[0].offset + sackFields[0].count;

            //Reset the sackStructure to make a new one
            memset(tmpBuff,0,tmpBuffSize);
            fieldIdx        = 0;
            fieldInProgress = false;
            unsentAcks      = 0;

            if(completeAckPackets >= maxAckPackets){
                return etcpENOERR;
            }
        }


        DBG("Now looking at seq %i\n", i);
        cqSlot_t* slot = NULL;
        const cqError_t err = cqGetRd(conn->rxQ,&slot,i);

        //This slot is empty, so stop making the field and start a new one
        if_eqlikely(err == cqEWRONGSLOT){
            //The slot is empty --
            if_likely(fieldInProgress){
                fieldIdx++;
                fieldInProgress = false;
            }
            continue;
        }
        else if_unlikely(err != cqENOERR){
            ERR("Error getting slot: %s\n", cqError2Str(err));
            return etcpECQERR;
        }

        //At this point we have a valid packet
        const i8* const slotBuff            = slot->buff;
        const etcpMsgHead_t* const head     = (etcpMsgHead_t* const)(slotBuff);
        etcpMsgDatHdr_t* const datHdr       = (etcpMsgDatHdr_t* const)(head +1);

        if_eqlikely(datHdr->noAck){
            //This packet does not want an ack
            if_likely(fieldInProgress){
                fieldIdx++;
                fieldInProgress = false;
            }
            cqCommitSlot(conn->rxQ,i,slot->len); //We're done with this packet, it can be RX'd now
            continue;
        }

        //Start a new field
        if_unlikely(!fieldInProgress){
            if_unlikely(fieldIdx == 0){ //If we're starting a new sack packet, we need some extra fields
                sackHdr->timeFirst    = head->ts;
                sackHdr->sackBaseSeq  = conn->seqAck;
            }

            sackFields[fieldIdx].offset = datHdr->seqNum - conn->seqAck;
            sackFields[fieldIdx].count = 0;
        }
        unsentAcks++;
        sackFields[fieldIdx].count++;
        sackHdr->timeLast = head->ts;
        datHdr->ackSent = 1;
    }

    //Push the last sack out
    if(unsentAcks > 0){
        sackHdr->sackCount = fieldIdx+1;
        etcpError_t err = pushSackEthPacket(conn,tmpBuff,fieldIdx+1);
        if_unlikely(err == etcpETRYAGAIN){
            WARN("Ran out of slots for sending acks, come back again\n");
            return err;
        }
        else if_unlikely(err != etcpENOERR){
            ERR("Unexpected error making ack packet\n");
            return err;
        }

        //Account for the now sent messages by updating the seqAck value, only do so up to the edge of the first field, this
        //is the limit that will be receivable until new packets come.
        conn->seqAck = conn->seqAck + sackFields[0].offset + sackFields[0].count;
    }
    return etcpENOERR;

}


etcpError_t doEtcpNetTx(cq_t* const cq, const etcpState_t* const state, const i64 maxSlots )
{
    cqSlot_t* slot = NULL;


    for(i64 i = cq->rdMin; i < cq->rdMax && i < cq->rdMin + maxSlots; i++){
        const cqError_t err = cqGetRd(cq,&slot,i);
        DBG("Sending packet %li\n", i);
        if_eqlikely(err == cqEWRONGSLOT){
            //The slot is empty
            continue;
        }
        else if_unlikely(err != cqENOERR){
            ERR("Error getting slot: %s\n", cqError2Str(err));
            return etcpECQERR;
        }

        //We've now got a valid slot with a packet in it, grab it and see if the TC has decided it should be sent?
        pBuff_t* const pBuff = slot->buff;

        if_eqlikely(pBuff->txState == ETCP_TX_DRP ){
            //We're told to drop the packet. Release it and continue
            DBG("Dropping packet %li\n", i);
            cqReleaseSlot(cq,i);
            continue;
        }
        else if_unlikely(pBuff->txState != ETCP_TX_NOW ){
            continue; //Not ready to send this packet now.
        }

        switch(pBuff->etcpHdr->type){
            case ETCP_DAT:{
                if_likely(pBuff->etcpDatHdr->txAttempts == 0){
                    //Only put the timestamp in on the first time we send a data packet so we know how long it spends RTT incl
                    //in the txq.
                    //Would be nice to have an extra timestamp slot so that the local queueing time could be accounted for as well.
                    struct timespec ts = {0};
                    clock_gettime(CLOCK_REALTIME,&ts);
                    const i64 timeNowNs = ts.tv_sec * 1000 * 1000 * 1000 + ts.tv_nsec;
                    pBuff->etcpHdr->ts.swTxTimeNs = timeNowNs;
                    pBuff->etcpHdr->swTxTs        = 1;
                }
                break;
            }
            case ETCP_ACK:{
                struct timespec ts = {0};
                clock_gettime(CLOCK_REALTIME,&ts);
                const i64 timeNowNs = ts.tv_sec * 1000 * 1000 * 1000 + ts.tv_nsec;
                pBuff->etcpHdr->ts.swTxTimeNs = timeNowNs;
                pBuff->etcpHdr->swTxTs        = 1;
                break;
            }
            default:{
                ERR("Unkown packet type! %i\n",pBuff->etcpHdr->type );
                return etcpEFATAL;
            }
        }

        //before the packet is sent, make it ready to send again in the future just in case something goes wrong
        pBuff->txState = ETCP_TX_RDY;

        uint64_t hwTxTimeNs = 0;
        const pBuff_t* const pbuff = slot->buff;
        if_unlikely(state->ethHwTx(state->ethHwState, pBuff->buffer, pbuff->msgSize, &hwTxTimeNs) < 0){
            return etcpETRYAGAIN;
        }

        DBG("Sent packet %li\n", i);

        switch(pBuff->etcpHdr->type){
            case ETCP_DAT:{
                //The exanics do not yet support inline HW tx timestamping, but we can kind of fake it here
                //XXX HACK - not sure what a generic way to do this is?
                if_likely(pBuff->etcpDatHdr->txAttempts == 0){
                    pBuff->etcpHdr->ts.hwTxTimeNs = hwTxTimeNs;
                    pBuff->etcpHdr->hwTxTs        = 1;
                }
                pBuff->etcpDatHdr->txAttempts++; //Keep this around for next time.
                if_eqlikely(pBuff->etcpDatHdr->noAck){
                    //We're done with the packet, not expecting an ack, so drop it now
                    cqReleaseSlot(cq,i);
                }
                //Otherwise, we need to wait for the packet to be ack'd
                break;
            }
            case ETCP_ACK:{
                cqReleaseSlot(cq,i);
                DBG("Released packet %li\n", i);
                break;
            }
            default:{
                ERR("Unkown packet type! %i\n",pBuff->etcpHdr->type );
                return etcpEFATAL;
            }

        }

    }

    return etcpENOERR;
}






//Returns the number of packets received
i64 doEtcpNetRx(etcpState_t* state)
{

    i64 result = 0;
    i8 frameBuff[MAX_FRAME] = {0}; //This is crap, should have a frame pool, from which pointers can be taken and then
                                   //inserted into the CQ structure rather than copying. This would make the whole
                                   //stack 0/1-copy.
    uint64_t hwRxTimeNs = 0;
    i64 rxLen = state->ethHwRx(state->ethHwState,frameBuff,MAX_FRAME, &hwRxTimeNs);
    for(; rxLen > 0; rxLen = state->ethHwRx(state->ethHwState,frameBuff,MAX_FRAME, &hwRxTimeNs)){

        etcpError_t err = etcpOnRxEthernetFrame(state, frameBuff,rxLen, hwRxTimeNs);
        result++;
        if_unlikely(err == etcpETRYAGAIN){
            WARN("Ring is full\n");
            break;
        }
    }

    if(rxLen < 0){
        WARN("Rx error %li\n", rxLen);
    }

    return result;

}


//This is a user facing function
etcpError_t doEtcpUserRx(etcpConn_t* const conn, void* __restrict data, i64* const len_io)
{
    while(1){
        i64 seqNum = -1;
        cqSlot_t* slot;
        cqError_t cqErr = cqGetNextRd(conn->rxQ,&slot,&seqNum);
        if_unlikely(cqErr == cqENOSLOT){
            //Nothing here. Give up
            return etcpETRYAGAIN;
        }
        else if_unlikely(cqErr != cqENOERR){
            ERR("Error on circular buffer: %s\n", cqError2Str(cqErr));
            return etcpECQERR;
        }

        const i64 msgHdrs = sizeof(etcpMsgHead_t) + sizeof(etcpMsgDatHdr_t);
        if_unlikely(slot->len < msgHdrs){
            ERR("Packet too small for headers %li < %li\n", slot->len, msgHdrs );
            cqReleaseSlot(conn->rxQ,seqNum);
            return etcpEBADPKT;
        }

        const etcpMsgHead_t* const head     = (etcpMsgHead_t* const)(slot->buff);
        const etcpMsgDatHdr_t* const datHdr = (etcpMsgDatHdr_t* const)(head +1);
        if_unlikely(slot->len - msgHdrs < datHdr->datLen){
            ERR("Packet too small for payload, required %li but got %li", datHdr->datLen,slot->len - msgHdrs );
            return etcpEBADPKT;
        }


        //We have a valid packet, but it might be stale, or not yet ack'd
        if(!datHdr->ackSent){
            return etcpETRYAGAIN; //Ack has not yet been made for this, cannot give over to the user until it has
        }

        const i8* dat = (i8*)(datHdr + 1);

        //Looks ok, give the data over to the user
        memcpy(data,dat,MIN(datHdr->datLen,*len_io));

        cqReleaseSlot(conn->rxQ,seqNum);
        if(datHdr->staleDat){
            continue; //The packet is stale, so release it, but get another one
        }

        //We've copied a valid packet and released it, the user can have it now
        return etcpENOERR;
    }

    //Unreachable!
    return etcpEFATAL;
}

//Assumes ethernet packets, does in-place construction of a packet and puts it into the circular queue ready to send
//This is a user facing function
etcpError_t doEtcpUserTx(etcpConn_t* const conn, const void* const toSendData, i64* const toSendLen_io)

{
    DBG("Doing user tx, with %li bytes to send\n", *toSendLen_io);
    const i64 toSendLen = *toSendLen_io;
    i64 bytesSent = 0;

    cq_t* const txcq = conn->txQ;
    while(bytesSent< toSendLen){
        DBG("Bytes sent so far = %lli\n", bytesSent);
        cqSlot_t* slot = NULL;
        i64 seqNum = 0;
        cqError_t cqErr = cqGetNextWr(txcq,&slot,&seqNum);

        //We haven't sent as much as we'd hoped, set the len_io and tell user to try again
        if_unlikely(cqErr == cqENOSLOT){
            DBG("Ran out of CQ slots\n");
            *toSendLen_io = bytesSent;
            return etcpETRYAGAIN;
        }
        //Some other strange error. Shit.
        else if_unlikely(cqErr != cqENOERR){
            ERR("Error on circular queue: %s", cqError2Str(cqErr));
            return etcpECQERR;
        }
        DBG("Got a new CQ slot\n");

        //We got a slot, now format a pBuff into it
        pBuff_t* pBuff  = slot->buff;
        pBuff->buffer   = pBuff + 1;
        pBuff->buffSize = slot->len - sizeof(pBuff);
        pBuff->msgSize  = 0;

        i8* buff    = pBuff->buffer;
        i64 buffLen = pBuff->buffSize;
        i64 ethLen  = buffLen;
        //XXX HACK - This should be externalised and happen after the ETCP packet formatting to allow multiple carrier transports
        etcpError_t etcpErr = etcpMkEthPkt(buff,&ethLen,conn->flowId.srcAddr, conn->flowId.dstAddr,conn->vlan, conn->priority);
        if_unlikely(etcpErr != etcpENOERR){
            WARN("Could not format Ethernet packet\n");
            return etcpErr;
        }
        pBuff->encapHdr     = buff;
        pBuff->encapHdrSize = ethLen;
        buff               += ethLen;
        pBuff->msgSize     += ethLen;
        buffLen            -= ethLen;

        const i64 hdrsLen = sizeof(etcpMsgHead_t) + sizeof(etcpMsgDatHdr_t);
        if_unlikely(buffLen < hdrsLen + 1){ //Should be able to send at least 1 byte!
            ERR("Slot lengths are too small!");
            return etcpEFATAL;
        }
        const i64 datSpace = buffLen - hdrsLen;
        const i64 datLen   = MIN(datSpace,toSendLen);
        pBuff->msgSize    += hdrsLen;

        struct timespec ts = {0};
        clock_gettime(CLOCK_REALTIME,&ts);
        etcpMsgHead_t* const head = (etcpMsgHead_t* const)buff;
        pBuff->etcpHdr = head;
        pBuff->etcpHdrSize = sizeof(etcpMsgHead_t);

        head->fulltype      = ETCP_V1_FULLHEAD(ETCP_DAT);
        head->srcPort       = conn->flowId.srcPort;
        head->dstPort       = conn->flowId.dstPort;
        head->ts.swTxTimeNs = ts.tv_sec * 1000 * 1000 * 1000 + ts.tv_nsec;

        etcpMsgDatHdr_t* const datHdr = (etcpMsgDatHdr_t* const)(head + 1);
        pBuff->etcpDatHdr     = datHdr;
        pBuff->etcpDatHdrSize = sizeof(etcpMsgDatHdr_t);


        datHdr->datLen     = datLen;
        pBuff->msgSize    += datLen;
        datHdr->seqNum     = conn->seqSnd;
        datHdr->txAttempts = 0;

        void* const msgDat = (void* const)(datHdr + 1);
        pBuff->etcpPayload = msgDat;
        pBuff->etcpPayloadSize = sizeof(datLen);

        DBG("Copying %li payload to data\n", datLen);
        memcpy(msgDat,toSendData,datLen);

        pBuff->txState = ETCP_TX_RDY; //Packet is ready to be sent, subject to Transmission Control.

        //At this point, the packet is now ready to send!
        const i64 totalLen = ethLen + hdrsLen + datLen;
        cqErr = cqCommitSlot(conn->txQ,seqNum, totalLen);
        if_unlikely(cqErr != cqENOERR){
            ERR("Error on circular queue: %s", cqError2Str(cqErr));
            return etcpECQERR;
        }

        bytesSent += datLen;
        conn->seqSnd++;

    }

    *toSendLen_io = bytesSent;
    return etcpENOERR;

}



