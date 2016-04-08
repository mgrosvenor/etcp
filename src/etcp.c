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

    const i64 rxQSlotCount  = recvConn->rxQ->slotCount;
    const i64 seqPkt        = datHdr->seqNum;
    const i64 seqMin        = recvConn->seqAck;
    const i64 seqMax        = recvConn->seqAck + rxQSlotCount;
    const i64 seqIdx        = seqPkt % rxQSlotCount;
    DBG("SlotCount = %li, seqPkt = %li, seqMin= %li, seqMax = %li, seqIdx = %li\n",
        rxQSlotCount,
        seqPkt,
        seqMin,
        seqMax,
        seqIdx
    );

    //When we receive a packet, it should be between seqMin and seqMax
    //-- if seq > seqMax, it is beyond the end of the rx window, ignore it, the packet will be sent again
    //-- if seq < seqMin, it has already been ack'd, the ack must have got lost, send another ack
    if_unlikely(seqPkt > seqMax){
        WARN("Ignoring packet, seqPkt %li > %li seqMax, packet will not fit in window\n", seqPkt, seqMax);
        return etcpERANGE;
    }

    if_unlikely(seqPkt < seqMin){
        WARN("Packet, seqPkt %li < %li seqMin, packet has already been ack'd\n", seqPkt, seqMin);
        datHdr->staleDat = 1;
    }

    i64 toCopy    = len; //By now this value has been checked to be right
    i64 toCopyTmp = toCopy;
    cqError_t err = cqPushIdx(recvConn->rxQ,head,&toCopyTmp,seqIdx);
    if_unlikely(err == cqENOSLOT){
        return etcpETRYAGAIN; //We've run out of slots to put packets into, drop this packet
    }
    else if_unlikely(err == cqETRUNC){
        WARN("Payload (%liB) is too big for slot (%liB), truncating\n", toCopy, toCopyTmp );
    }
    else if_unlikely(err != cqENOERR){
        WARN("Error inserting into Circular Queue: %s", cqError2Str(err));
        return etcpECQERR;
    }

    cqCommitSlot(recvConn->rxQ,seqIdx,toCopyTmp);

    return etcpENOERR;
}

static inline  etcpError_t etcpProcessAck(cq_t* const cq, const uint64_t seq, const etcpTime_t* const ackTime)
{
    const uint64_t idx = seq % cq->slotCount;
    DBG("Ack'ing packet with seq=%li and idx=%li\n", seq,idx);
    cqSlot_t* slot = NULL;
    const cqError_t err = cqGetSlotIdx(cq,&slot,idx);
    if_unlikely(err == cqEWRONGSLOT){
        WARN("Got an ACK for a packet that's gone.\n");
        return etcpENOERR;
    }
    else if_unlikely(err != cqENOERR){
        WARN("Error getting value from Circular Queue: %s", cqError2Str(err));
        return etcpECQERR;
    }

    const etcpMsgHead_t* const head = slot->buff;
    const etcpMsgDatHdr_t* const datHdr = (const etcpMsgDatHdr_t* const)(head + 1);
    if_unlikely(seq != datHdr->seqNum){
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
    const cqError_t cqErr = cqReleaseSlotRd(cq,idx);
    if_unlikely(cqErr != cqENOERR){
        ERR("Unexpected cq error: %s\n", cqError2Str(err));
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
    const htKey_t srcKey = { .keyHi = flowId->srcAddr, .keyLo = flowId->srcPort };
    htErr = htGet(srcsMap->table,&srcKey,(void**)&conn);
    if_unlikely(htErr == htENOTFOUND){
        DBG("Ack unexpected. No one listening to source addr=%li, port=%li\n", flowId->srcAddr, flowId->srcPort);
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
            etcpProcessAck(conn->txQ,ackSeq,&head->ts);
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
    const i64 len = *len_io;
    if_unlikely(len < ETH_ZLEN){
        ERR("Not enough bytes in Ethernet frame. Required %li but got %i\n", ETH_ZLEN, len );
        return etcpEFATAL;
    }

    struct ethhdr* const ethHdr = buff;

    memcpy(&ethHdr->h_dest, &dstAddr, ETH_ALEN);
    memcpy(&ethHdr->h_source, &srcAddr, ETH_ALEN);
    ethHdr->h_proto = htons(ETH_P_ECTP);

    DBG("Ethernet header:\n");
    hexdump(ethHdr,ETH_HLEN);

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
    i64 slotIdx;
    cqError_t cqErr = cqGetNextWr(conn->txQ, &slot,&slotIdx);
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
    const i64 ethEtcpSackPktSize = ETCP_ETH_OVERHEAD + sizeof(etcpMsgHead_t) + sackHdrAndDatSize;
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


    struct timespec ts = {0};
    clock_gettime(CLOCK_REALTIME,&ts);
    etcpMsgHead_t* const head = (etcpMsgHead_t* const)buff;
    pBuff->etcpHdr      = head;
    pBuff->etcpHdrSize  = sizeof(etcpMsgHead_t);
    head->fulltype      = ETCP_V1_FULLHEAD(ETCP_ACK);
    //Reverse the source and destination ports so that the packet goest back to where it came from
    head->srcPort       = conn->flowId.dstPort;
    head->dstPort       = conn->flowId.srcPort;
    head->ts.swTxTimeNs = ts.tv_nsec * 1000 * 1000 * 1000 + ts.tv_nsec;
    buff                += sizeof(etcpMsgHead_t);

    pBuff->etcpSackHdr      = (etcpMsgSackHdr_t*)buff;
    pBuff->etcpSackHdrSize  = sizeof(etcpMsgSackHdr_t);

    pBuff->etcpPayload  = buff + sizeof(etcpMsgSackHdr_t);
    pBuff->etcpPayloadSize = sizeof(etcpSackField_t) * sackCount;

    memcpy(buff,sackHdrAndData,sackHdrAndDatSize);

    cqErr = cqCommitSlot(conn->txQ,slotIdx,ethEtcpSackPktSize);
    if_unlikely(cqErr != cqENOERR){
        ERR("Unexpected error on CQ: %s\n", cqError2Str(cqErr));
        return etcpECQERR;
    }

    return etcpENOERR;
}


//Traverse the receive queues and generate ack's
etcpError_t generateAcks(etcpConn_t* const conn, const i64 maxAckPackets, const i64 maxSlots)
{
    if(maxAckPackets <= 0){
        return etcpENOERR; //Don't bother trying if you don't want me to!
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
    const i64 slotCount = conn->rxQ->slotCount;
    i64 slotIdx = conn->seqAck;
    i64 completeAckPackets = 0;
    for(i64 i = 0; i < slotCount && i < maxSlots; i++, slotIdx = slotIdx + 1 < slotCount ? slotIdx +1 : slotIdx + 1 - slotCount){

        //We collected enough sack fields to make a whole packet and send it
        if_unlikely(fieldIdx >= ETCP_MAX_SACKS){
            sackHdr->rxWindowSegs = conn->rxQ->slotCount - conn->rxQ->slotsUsed; //Get the most up-to-date value
            sackHdr->sackBaseSeq  = conn->seqAck;
            etcpError_t err = pushSackEthPacket(conn,tmpBuff,ETCP_MAX_SACKS);
            if_unlikely(err == etcpETRYAGAIN){
                WARN("Ran out of slots for sending acks, come back again\n");
                return err;
            }
            else if_unlikely(err != etcpENOERR){
                ERR("Unexpected error making ack packet\n");
                return err;
            }

            //Account for the now sent messages by updating the seqAck value
            conn->seqAck = conn->seqAck + sackFields[fieldIdx].offset + sackFields[fieldIdx].count - 1;

            //Reset the sackStructure to make a new one
            memset(tmpBuff,0,tmpBuffSize);
            fieldIdx        = 0;
            fieldInProgress = false;
            unsentAcks      = 0;

            if(completeAckPackets >= maxAckPackets){
                return etcpENOERR;
            }
        }


        DBG("Now looking at slot %i\n", slotIdx);
        cqSlot_t* slot = NULL;
        const cqError_t err = cqGetSlotIdx(conn->rxQ,&slot,slotIdx);

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

        //We've now got a valid slot with a packet in it, grab the timestamp to see if it needs sending.
        //const i64 skipEthHdrOffset          = conn->vlan < 0 ? ETH_HLEN : ETH_HLEN + sizeof(eth8021qTCI_t);
        const i8* const slotBuff            = slot->buff;
        //const etcpMsgHead_t* const head     = (etcpMsgHead_t* const)(slotBuff + skipEthHdrOffset);
        const etcpMsgHead_t* const head     = (etcpMsgHead_t* const)(slotBuff);
        etcpMsgDatHdr_t* const datHdr       = (etcpMsgDatHdr_t* const)(head +1);

        if_eqlikely(datHdr->noAck){
            //This packet does not want an ack
            if_likely(fieldInProgress){
                fieldIdx++;
                fieldInProgress = false;
            }
            continue;
        }

        //Start a new field
        if_unlikely(!fieldInProgress){
            if_unlikely(fieldIdx == 0){
                sackHdr->timeFirst = head->ts;
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
        const i64 sackHdrAndDatSize = sizeof(etcpMsgSackHdr_t) + sizeof(etcpSackField_t) * (fieldIdx+1);
        etcpError_t err = pushSackEthPacket(conn,tmpBuff,sackHdrAndDatSize);
        if_unlikely(err == etcpETRYAGAIN){
            WARN("Ran out of slots for sending acks, come back again\n");
            return err;
        }
        else if_unlikely(err != etcpENOERR){
            ERR("Unexpected error making ack packet\n");
            return err;
        }

        //Account for the now sent messages by updating the seqAck value
        conn->seqAck = conn->seqAck + sackFields[fieldIdx].offset + sackFields[fieldIdx].count - 1;
    }
    return etcpENOERR;

}


etcpError_t doEtcpNetTx(cq_t* const cq, i64* const lastTxIdx_io, const etcpState_t* const state, const i64 maxSlots )
{
    cqSlot_t* slot = NULL;
    i64 lastTxIdx  = *lastTxIdx_io;
    i64 hwTxCount  = 0;

    const i64 slotCount = cq->slotCount;
    for(i64 i = 0; i < slotCount && i < maxSlots; i++){
        const cqError_t err = cqGetSlotIdx(cq,&slot,lastTxIdx);
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
            cqReleaseSlotRd(cq,lastTxIdx);
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

        uint64_t hwTxTimeNs = 0;
        const pBuff_t* const pbuff = slot->buff;
        if_unlikely(state->ethHwTx(state->ethHwState, pBuff->buffer, pbuff->msgSize, &hwTxTimeNs) < 0){
            return etcpETRYAGAIN;
        }
        hwTxCount++;

        //The exanics do not yet support inline HW tx timestamping, but we can kind of fake it here
        //XXX HACK - not sure what a generic way to do this is?
        if_likely(pBuff->etcpDatHdr->txAttempts == 0){
            pBuff->etcpHdr->ts.hwTxTimeNs = hwTxTimeNs;
            pBuff->etcpHdr->hwTxTs        = 1;
        }

        pBuff->etcpDatHdr->txAttempts++; //Keep this around for next time.

        switch(pBuff->etcpHdr->type){
            case ETCP_DAT:{
                if_eqlikely(pBuff->etcpDatHdr->noAck){
                    //We're done with the packet, not expecting an ack, so drop it now
                    cqReleaseSlotRd(cq,lastTxIdx);
                }
                //Otherwise, we need to wait for the packet to be ack'd
                break;
            }
            case ETCP_ACK:{
                cqReleaseSlotRd(cq,lastTxIdx);
                break;
            }
            default:{
                ERR("Unkown packet type! %i\n",pBuff->etcpHdr->type );
                return etcpEFATAL;
            }

        }

        //Inciment the last sent index so next time we come back we start at a resonable place?
        //TODO XXX, could probably make this a part of the TC call
        lastTxIdx = lastTxIdx + 1 <  slotCount ? lastTxIdx + 1 : lastTxIdx + 1 - slotCount;
    }

    return etcpENOERR;
}







void doEtcpNetRx(etcpState_t* state)
{

    i8 frameBuff[MAX_FRAME] = {0};
    uint64_t hwRxTimeNs = 0;
    i64 rxLen = state->ethHwRx(state->ethHwState,frameBuff,MAX_FRAME, &hwRxTimeNs);
    for(; rxLen > 0; rxLen = state->ethHwRx(state->ethHwState,frameBuff,MAX_FRAME, &hwRxTimeNs)){
        etcpError_t err = etcpOnRxEthernetFrame(state, frameBuff,rxLen, hwRxTimeNs);
        if_unlikely(err == etcpETRYAGAIN){
            WARN("Ring is full\n");
            break;
        }
    }

    if(rxLen < 0){
        WARN("Rx error %li\n", rxLen);
    }

}


//This is a user facing function
etcpError_t doEtcpUserRx(etcpConn_t* const conn, void* __restrict data, i64* const len_io)
{
    while(1){
        i64 slotIdx = -1;
        cqSlot_t* slot;
        cqError_t cqErr = cqGetNextRd(conn->rxQ,&slot,&slotIdx);
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
            cqReleaseSlotRd(conn->rxQ,slotIdx);
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

        cqReleaseSlotRd(conn->rxQ,slotIdx);
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
    const i64 toSendLen = *toSendLen_io;
    i64 bytesSent = 0;

    cq_t* const txcq = conn->txQ;
    while(bytesSent< toSendLen){
        cqSlot_t* slot = NULL;
        i64 slotIdx = 0;
        cqError_t cqErr = cqGetNextWr(txcq,&slot,&slotIdx);

        //We haven't sent as much as we'd hoped, set the len_io and tell user to try again
        if_unlikely(cqErr == cqENOSLOT){
            *toSendLen_io = bytesSent;
            return etcpETRYAGAIN;
        }
        //Some other strange error. Shit.
        else if_unlikely(cqErr != cqENOERR){
            ERR("Error on circular queue: %s", cqError2Str(cqErr));
            return etcpECQERR;
        }

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
        head->ts.swTxTimeNs = ts.tv_nsec * 1000 * 1000 * 1000 + ts.tv_nsec;

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

        memcpy(msgDat,toSendData,datLen);

        pBuff->txState = ETCP_TX_RDY; //Packet is ready to be sent, subject to Transmission Control.

        //At this point, the packet is now ready to send!
        const i64 totalLen = ethLen + hdrsLen + datLen;
        cqErr = cqCommitSlot(conn->txQ,slotIdx, totalLen);
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



