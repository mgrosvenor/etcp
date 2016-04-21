/*
 * Copyright (c) 2016, All rights reserved.
 * See LICENSE.txt for full details. 
 * 
 *  Created:   29 Mar 2016
 *  File name: packets.h
 *  Description:
 *  <INSERT DESCRIPTION HERE> 
 */
#ifndef SRC_PACKETS_H_
#define SRC_PACKETS_H_

#include <linux/if_ether.h>

typedef struct __attribute__((packed)){
    //Note 1: Client and server times cannot be compared unless there is some kind of time synchronisation (eg PTP)
    //Note 2: Hardware cycle counts and software times cannot be compared unless there is time synchronisation and conversion
    //Note 3: To save space, some times are 32bits only, which assumes that time differences will be <4 seconds.
    //Note 4: Assumption 3 can be checked using swRxTimeNs - swTxTimeNs which is the absolute unix time.
    uint64_t swTxTimeNs;   //Client Unix time in ns, used for RTT time estimation
    uint64_t hwTxTimeNs;   //Client hardware used for network time estimation
    uint64_t hwRxTimeNs;   //Server hardware used for network time estimation
    uint64_t swRxTimeNs;   //Server Unix time in ns, used for host processing estimation
} etcpTime_t;

typedef struct __attribute__((packed)){
    uint32_t offset; //Offset from the base seq number, max 4 billion
    uint32_t count;  //Number of ack's in this range max, 4 billion
} etcpSackField_t;

_Static_assert(sizeof(etcpSackField_t) == 1 * sizeof(uint64_t) , "Don't let this grow too big, 8B is big enough!");

typedef struct __attribute__((packed)){
    i64 sackBaseSeq;
    uint64_t sackCount    : 8;  //Max 256 sack fields in a packet (256*32B = 8192B ~= 1 jumbo frame)
    uint64_t reserved     : 24; //Not in use right now
    uint64_t rxWindowSegs : 32; //Max 4B segment buffers in the rx window
    etcpTime_t timeFirst;
    etcpTime_t timeLast;
} etcpMsgSackHdr_t;

typedef struct __attribute__((packed)){
    uint64_t seqNum;
    uint32_t datLen;          //Max 4GB per message
    uint16_t txAttempts;      //Max 65K tx attenmpts.
    uint16_t noAck      :  1; //Tell the receiver not to generate an ack for this packet (ie 'UDP mode')
    uint16_t noRet      :  1; //The the recevier not to generate a return path

    //Keeping this state here beacuse I can. It could be in some kind of meta structure, but I have the bits here anyway
    uint16_t ackSent    :  1; //Has the ack for this packet been sent? Only pass the packet up to the user if it has.
    uint16_t staleDat   :  1; //Has this packet already been seen before. If so, don't give it back to the user
    uint16_t reserved   : 12; //Nothing here
} etcpMsgDatHdr_t;

//Assumes a fast layer 2 network (10G plus), with built in check summing and reasonable latency. In this case, sending
//more bits, is better than sending many more packets at higher latency. Keep this generic header pretty minimal, but use nice
//large types without too many range restrictions.
//Enum describing the types of etcp packets.
typedef enum {
    ETCP_ERR = 0x00, //Not a valid message, something wrong

    //Uses the the data message packet format
    //ETCP_FIN = 0x02, //Is the last packet on this connection
    ETCP_DAT = 0x03, //Just another data packet

    //Uses the acknowledgement packet format
    ETCP_ACK = 0x04, //Contains acknowledgement fields

} etcpMsgType_t;

typedef struct __attribute__((packed)){
    union{
        struct{
            //Top 48 bits
            uint64_t type      :8; //256 different message types for each protocol version. How could we run out...?
            uint64_t ver       :8;  //Protocol version. Max 255 versions
            uint64_t magic     :32;  //ASCII encoded "ETCP" -- To make hexdumps easier to parse

            //Bottom 16 bits
            uint64_t hwTxTs    :1; //Does this packet have a harware TX timestamp in it?
            uint64_t hwRxTs    :1; //Does this packet have a harware RX timestamp in it?
            uint64_t swTxTs    :1; //Does this packet have a software TX timestamp in it?
            uint64_t swRxTs    :1; //Does this packet have a software RX timestamp in it?

            //Metadata XXX HACK - this should really not be inline in the packet, but I have the space right now
            uint64_t reserved  :12;

        };
        struct{
            uint64_t fulltype  : 48; //A full type is the magic string, version number and message type in a single uint into make it easy to parse and compare
            uint64_t fullflags : 4;  //Full flags is a an int containing all flags for easy comparison
            uint64_t _reserved : 12; //Ignore this
        };
    };
    uint64_t srcPort;    //"port" on the tx side, max 4 billion ports
    uint64_t dstPort;    //"port" on the rx side, max 4 billion ports

    etcpTime_t ts; //Timing info for estimation
} etcpMsgHead_t;
_Static_assert(sizeof(etcpMsgHead_t) == 7 * sizeof(uint64_t) , "Don't let this grow too big, 48B is big enough!");

//This is arbitrarily set, to get a nice number of sacks, but make the packet not too big (~256B)
//TODO XXX reevaluate this later to see if the trade-off is ok. Should it be bigger or smaller or am I so awesome that I got
//it right first guess (unlikely...).
//Current sizes: 256B Max packet
//Ethernet overheads: Header 14, FCS, 4, VALN 2 = 20B
//ETCP header: 7 * 8B = 56B
//Ack header: 10 * 8B = 80B
//Sack field size: 8B
//Space = 256 - 20 - 56 - 80 = 108
//Sack count = 12 ranges per packet.
//This means that each sack packet can handle at most 12 dropped packets, or 4 billion recived packets.
#define ETCP_MAX_SACK_PKT (256LL)
#define ETCP_ETH_OVERHEAD (ETH_HLEN + ETH_FCS_LEN + 2)
#define ETCP_SACKHDR_OVERHEAD (sizeof(etcpMsgHead_t) + sizeof(etcpMsgSackHdr_t))
#define ETCP_MAX_SACKS ( (i64)((ETCP_MAX_SACK_PKT - ETCP_ETH_OVERHEAD - ETCP_SACKHDR_OVERHEAD) / (sizeof(etcpSackField_t))) )
_Static_assert(ETCP_MAX_SACKS >= 10 , "Make sure there is some reasonable number of sacks available");

#define ETCP_MAGIC 0x45544350ULL //"ETCP" in ASCII
#define ETCP_V1 0x1ULL
#define ETCP_V1_FULLHEAD(MSG) ((ETCP_MAGIC << 16) + (ETCP_V1 << 8) + (MSG << 0) )


typedef enum {
    ETCP_TX_RDY = 0,
    ETCP_TX_NOW = 1,
    ETCP_TX_DRP = 2
} txState_t;

typedef struct {
    txState_t txState;

    void* buffer;
    i64 buffSize; //Size of the buffer area to work in
    i64 msgSize;  //Size of the actual message in the buffer

    void* encapHdr;
    i64 encapHdrSize;

    etcpMsgHead_t* etcpHdr;
    i64 etcpHdrSize;

    union{
        etcpMsgDatHdr_t* etcpDatHdr;
        etcpMsgSackHdr_t* etcpSackHdr;
        void* etcpPayHdr;
    };
    union{
        i64 etcpDatHdrSize;
        i64 etcpSackHdrSize;
        i64 etcpPayHdrSize;
    };

    void* etcpPayload;
    i64 etcpPayloadSize;

} pBuff_t; //An ETCP packet buffer



#endif /* SRC_PACKETS_H_ */
