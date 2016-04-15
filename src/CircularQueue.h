/*
 * Copyright (c) 2016, All rights reserved.
 * See LICENSE.txt for full details. 
 * 
 *  Created:   24 Mar 2016
 *  File name: CircularQueue.h
 *  Description:
 *  <INSERT DESCRIPTION HERE> 
 */
#ifndef CIRCULARQUEUE_H_
#define CIRCULARQUEUE_H_

#include <stdbool.h>

#include "types.h"

/*
 * A circular buffer that acts as a queue.
 *
 * Sz = size of the queue
 * Rd = Read sequence no (64bit ~= infinity = 200 years at 1op/ns)
 * Wr = Write sequence no (64bit ~= infinity = 200 years at 1op/ns)
 *
 *                 \\\\\\\\\\\\\\\************
 *  0 ---------------------------------------------------- inf
 *                 ^              ^            ^
 *                 RD             WR           WR_Max = RD+Sz
 */


typedef struct __attribute__((packed)){
    //Make sure that the state is sized for a 64bit value. It wil be aligned this way as well for atomic access
    //Do not play with these if you are a user.
    i64 len;     //Length of data - this is a constant, and should not be altered
    void* buff;  //Place to get/put data - this is a constant and should not be altered
    bool valid;  //There is data inside this structure
} cqSlot_t;


typedef struct {

    i64 slotDataSize;
    i64 rdMin;
    i64 rdMax;
    i64 rdRng;
    i64 readable;
    i64 wrMin;
    i64 wrMax;
    i64 wrRng;
    i64 outstanding;
    volatile i64 rdSeq;
    volatile i64 wrSeq;

    //__itmes are "private"
    i64 __slotCountLog2;
    i64 __slotCount; //Users should not need to know this
    i64 __seqMask;
    i64 __slotSize;
    i8* __slots;
} cq_t;


/**
 * @brief Errors returned by the CQ structure
 */
typedef enum {
    cqENOERR = 0,   //!< cqENOERR       Success!
    cqENOMEM,       //!< cqENOMEM       Ran out of memory
    cqENOSLOT,      //!< cqENOSLOT      Ran out of slots, free a slot
    cqETRUNC,       //!< cqETRUNC       Truncated
    cqERANGE,       //!< cqERANGE       Out of range!
    cqEWRONGSLOT,   //!< cqEWRONGSLOT   This slot is not in the sate we expected
    cqEPANIC,       //!< cqEPANIC       Something bad has happened, user has taken too much memory!
    cqENULLPARAM,    //!< cqNULLPARAM    A parameter supplied was null and it shouldn't be!
    cqENOCHANGE,    //!< cqENOCHANGE    No change to the sequence number. Are you sure there's a contiguous slot?

    //THIS MUST BE LAST
    cqECOUNT,       //!< cqERCOUNT      Total number of error codes.
} cqError_t;


/**
 * @brief           Create a new circular queue structure
 * @param buffSize  The maximum size in bytes that is required for each data buffer in each slot.
 * @param slotCount The number of slots in this queue.
 * @return          On success a new a pointer to a new cq_t structure. On failure, NULL will be returned
 */
cq_t* cqNew(const i64 buffSize, const i64 slotCount);


// *************************************************************************************************************************
/**
 * @brief           Try to get an item anywhere in the operating range.
 * @param cq        The CQ structure that we're operating on
 * @param slot_o    If successful, slot_o variable will point to a valid CQ slot.
 * @param seqNum    The index to try
 * @return          ENOERROR - at slot_o is a valid pointer
 *                  ERANGE   - the sequence number given is out of range.
 */
cqError_t cqGet(const cq_t* const cq, cqSlot_t** const slot_o, const i64 seqNum);


/**
 * @brief           Push the write pointer forwards if possible. This will only advance if there is a contiguous sequence of
 *                  non-null items in the slots after the current write pointer.
 * @param cq        The CQ structure that we're operating on
 * @return          ENOERROR -  success
 *                  ENOCHANGE - no change to the write seq num
 */
cqError_t cqAdvWrSeq(cq_t* const cq);


/**
 * @brief           Push the read pointer forwards if possible. This will only advance if there is a contiguous sequence of
 *                  null items in the slots after the current read pointer.
 * @param cq        The CQ structure that we're operating on
 * @param rdSeqBef The read sequence number when called, you can compare this to cq_t.rdSeq to see how much has changed
 * @return          ENOERROR -  success
 *                  ENOCHANGE - no change to the read seq num
 */
cqError_t cqAdvRdSeq(cq_t* const cq);

// *************************************************************************************************************************


/**
 * Free memory resoruces associated with this CQ
 * @param cq
 */
void cqDelete(cq_t* const cq);

//Convert a cqError number into a text description

const char* cqError2Str(cqError_t const err);


// *************************************************************************************************************************
// Non-primitive functions, just thin wrappers around the previous things
cqError_t cqPush(cq_t* const cq, const void* __restrict data, i64* const len_io, i64 const seqNum);
cqError_t cqPull(cq_t* const cq, void* __restrict data, i64* const len_io, i64 const seqNum);

cqError_t cqGetNextWr(cq_t* const cq, cqSlot_t** const slot_o, i64* const seqNum_o);
cqError_t cqPushNext(cq_t* const cq, const void* __restrict data, i64* const len_io, i64* const slotIdx_o);
cqError_t cqGetNextRd(cq_t* const cq, cqSlot_t** const slot_o, i64* const seqNum_o);
cqError_t cqCommitSlot(cq_t* const cq, const i64 seqNum, const i64 len);

cqError_t cqGetNextRd(cq_t* const cq, cqSlot_t** const slot_o, i64* const seqNum_o);
cqError_t cqPullNext(cq_t* const cq, void* __restrict data, i64* const len_io, i64* const seqNum_o);
cqError_t cqReleaseSlot(cq_t* const cq, const i64 seqNum);
cqError_t cqGetRd(const cq_t* const cq, cqSlot_t** slot_o, const i64 seqNum);


#endif /* CIRCULARQUEUE_H_ */
