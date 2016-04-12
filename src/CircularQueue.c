/*
 * Copyright (c) 2016, All rights reserved.
 * See LICENSE.txt for full details.
 *
 *  Created:   24 Mar 2016
 *  File name: CircularQueue.c
 *  Description:
 *  <INSERT DESCRIPTION HERE>
 */

#include "CircularQueue.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "utils.h"

//Slot count must be a power of 2 for performance reasons (to avoid a modulus on the critical path)
cq_t* cqNew(const i64 buffSize, const i64 slotCountLog2)
{
    if(buffSize < 0 || slotCountLog2 < 0){
        return cqERANGE;
    }

    cq_t* result = calloc(1,sizeof(cq_t));
    if(!result){
        return NULL;
    }

    result->__slotCouuntLog2 = slotCountLog2;
    result->slotCount        = 1 << slotCountLog2;
    result->__seqMask        = result->slotCount - 1;
    result->slotDataSize     = buffSize;
    result->__slotSize       = buffSize + sizeof(cqSlot_t);
    result->__slotSize       = (result->__slotSize + sizeof(uint64_t) - 1) & ~(sizeof(uint64_t) -1 ); //Round up nearest word size

    DBG("Allocating %li slots of size %li for %liB with mask=%016x\n", slotCount, result->slotSize, slotCount * result->slotSize);
    result->__slots = calloc(result->slotCount, result->__slotSize);
    if(!result->__slots){
        cqDelete(result);
        return NULL;
    }

    for(i64 i = 0; i < result->slotCount; i++){
        cqSlot_t* slot = (cqSlot_t*)(result->__slots + i * result->__slotSize);
        slot->buff = (i8*)(slot + 1);
        slot->len  = result->slotDataSize;
    }

    return result;

}

cqError_t cqGet(cq_t* const cq, cqSlot_t** const slot_o, const i64 seqNum)
{
    if_unlikely(cq == NULL){
        return cqENULLPARAM;
    }

    if_unlikely(slot_o == NULL){
        return cqENULLPARAM;
    }

    if_unlikely(seqNum < cq->rdSeq){
        return cqERANGE;
    }

    if_unlikely(seqNum >= cq->rdSeq + cq->slotCount ){
        return cqERANGE;
    }

    //Passed all the checks, this seqNumber is in the right range, turn it into a slot index.
    const i64 idx = ( seqNum & cq->__seqMask);

    DBG("Getting item at index =%li (seq=%li)\n", )
    *slot_o = cq->__slots + idx;




}


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



void cqDelete(cq_t* const cq)
{
    if(!cq){
        return;
    }

    if(cq->_slots){
        free(cq->_slots);
    }

    free(cq);
}

static char* errors[cqECOUNT] = {
    "Success! No error",                    //cqENOERR
    "No memory available",                  //cqENOMEM
    "No slots available",                   //cqENOSLOT
    "Payload was truncated",                //cqETRUNC
    "Value is out of range",                //cqERANGE
    "Wrong slot selected",                  //cqEWRONGSLOT
    "PANIC! INTERNAL MEMROY OVERWRITTEN",   //cqEPANIC
    "Null paramter supplied"                //cqNULLPARAM

};


//Convert a cqError number into a text description
const char* cqError2Str(const cqError_t err)
{
    if(err >= cqECOUNT){
        return "Bad error number";
    }

    return errors[err];
}



