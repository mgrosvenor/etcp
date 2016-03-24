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

#include "types.h"


typedef enum {
    cqSTFREE = 0,
    cqSTINUSEWR,
    cqSTINUSERD,
    cqSTREADY,
} cqSlotState_t;


typedef struct __attribute__((packed)){
    //Make sure that the state is sized for a 64bit value. It wil be aligned this way as well for atomic access
    //Do not play with these if you are a user.
    union {
        volatile cqSlotState_t __state;
        volatile uint64_t ___state;
    };


    i64 len;   //Length of buffer
    i8* buff;  //Place to get/put data
} cqSlot_t;


typedef struct {
    i64 slotSize;
    i64 slotDataSize;
    i64 slotCount;
    i64 rdIdx;
    i64 wrIdx;
    i8* slots;
} cq_t;


/**
 * @brief Errors returned by the CQ structure
 */
typedef enum {
    cqENOERR,       //!< cqENOERR       Success!
    cqENOMEM,       //!< cqENOMEM       Ran out of memory
    cqENOSLOT,      //!< cqENOSLOT      Ran out of slots, free a slot
    cqETRUNC,       //!< cqETRUNC       Truncated
    cqERANGE,       //!< cqERANGE       Out of range!
    cqEWRONGSLOT,   //!< cqEWRONGSLOT   This slot is not in the sate we expected
    cqEPANIC,       //!< cqEPANIC       Something bad has happened, user has taken too much memory!
    cqECOUNT,       //!< cqERCOUNT      Total number of error codes.
} cqError_t;


/**
 * @brief           Create a new circular queue structure
 * @param buffSize  The maximum size in bytes that is required for each data buffer in each slot.
 * @param slotCount The number of slots in this queue.
 * @return          On success a new a pointer to a new cq_t structure. On failure, NULL will be returned
 */
cq_t* cqNew(i64 buffSize, i64 slotCount);


/**
 * @brief           Try to get the next slot in the CQ for writing to. Once done, call CommitSlot.
 * @param cq        The CQ structure that we're operating on
 * @param slot_o    If successful, slot_o variable will point to a valid CQ slot.
 * @param slotIdx_o If successful, slotIdx_o will point to the index of the slot returned in slot_o, use this for CommitSlot.
 * @return          A cqError code this may be ENOERR, which indicates that slot_o is a valid pointer or ENOSLOT which
 *                  indicates that there are no free slots.
 */
cqError_t cqGetNextWr(cq_t* cq, cqSlot_t** slot_o, i64* slotIdx_o);


/**
 * @brief           Tell the CQ that the slot is ready to be read. After this, no more changes can be made
 * @param cq        The CQ structure we're operating on
 * @param slot      The slot to commit to the data to.
 * @return
 */
cqError_t cqCommitSlot(cq_t* cq, i64 slotIdx, i64 len);

/**
 * @brief           Tell the CQ that the slot is empty.
 * @param cq        The CQ structure we're operating on
 * @param slot      The slot to commit to the data to.
 * @return          ESUCCESS
 */
cqError_t cqReleaseSlotWr(cq_t* cq, i64 slotIdx);


/**
 * @brief       Equivalent to GetNext, memcopy, CommitSlot.
 * @param cq    The CQ structure that we're operating on
 * @param data  Data that you wish to have copied into the slot
 * @param len   Length of the data, this must be less than slot size. If it is too big, ETRUNC will be returned and len will
 *              will be set to the number of bytes actually copied
 * @return
 */
cqError_t cQPushNext(cq_t* cq, i8* data, i64* len);



/**
 * @brief           Try to get the next slot in the CQ for reading from. Once done, call ReleaseSlot.
 * @param cq        The CQ structure that we're operating on
 * @param slot_o    If successful, slot_o variable will point to a valid CQ slot.
 * @param slotIdx_o If successful, slotIfx_o variable will point to the index of the slot returned, use this for ReleaseSlot
 * @return          A cqError code this may be ENOERR, which indicates that slot_o is a valid pointer or ENOSLOT which
 *                  indicates that there are no slots to read.
 */
cqError_t cqGetNextRd(cq_t* cq, cqSlot_t** slot_o, i64* slotIdx_o);


/**
 * @brief           Tell the CQ that the slot is empty.
 * @param cq        The CQ structure we're operating on
 * @param slot      The slot to commit to the data to.
 * @return          ESUCCESS
 */
cqError_t cqReleaseSlotRd(cq_t* cq, i64 slotIdx);


/**
 * @brief       Equivalent to GetNextRd, memcopy, ReleaseSlot.
 * @param cq    The CQ structure that we're operating on
 * @param data  Place where you want to have data copied out of the slot
 * @param len   Length of the data area,
 * @return      ESUCCESS, ETURNC
 */
cqError_t cqPullNext(cq_t* cq, i8* data, i64* len_io);



/**
 * Free memory resoruces associated with this CQ
 * @param cq
 */
void cqDelete(cq_t* cq);

//Convert a cqError number into a text description
const char* cqError2Str(cqError_t err);


#endif /* CIRCULARQUEUE_H_ */