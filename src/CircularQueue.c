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

static inline i64 min(i64 lhs, i64 rhs)
{
    return lhs < rhs ? lhs : rhs;
}


cq_t* cqNew(i64 buffSize, i64 slotCount)
{
    cq_t* result = calloc(1,sizeof(cq_t));
    if(!result){
        return NULL;
    }

    result->slotCount    = slotCount;
    result->slotDataSize = buffSize;
    result->slotSize     = buffSize + sizeof(cqSlot_t);
    result->slotSize     = (result->slotSize + sizeof(uint64_t) - 1) & ~(sizeof(uint64_t) -1 ); //Round up nearest word size

    //printf("Allocating %li slots of size %li for %liB\n", slotCount, result->slotSize, slotCount * result->slotSize);
    result->slots        = calloc(slotCount, result->slotSize);
    if(!result->slots){
        cqDelete(result);
        return NULL;
    }

    for(i64 i = 0; i < slotCount; i++){
        cqSlot_t* slot = (cqSlot_t*)(result->slots + i * result->slotSize);
        slot->buff = (i8*)(slot + 1);
        slot->len  = result->slotSize;
    }

    return result;

}


cqError_t cqGetNextWr(cq_t* cq, cqSlot_t** slot_o, i64* slotIdx_o)
{
    if(cq == NULL || slot_o == NULL || slotIdx_o == NULL){
        return cqNULLPARAM;
    }

    const i64 slotCount = cq->slotCount;
    const i64 idx = cq->wrIdx + 1 < slotCount ? cq->wrIdx + 1 : cq->wrIdx + 1 - slotCount;
    cqSlot_t* slot = (cqSlot_t*)(cq->slots +  idx * cq->slotSize);
    if(slot->__state != cqSTFREE){
        return cqENOSLOT;
    }

    slot->__state = cqSTINUSEWR;
    cq->wrIdx = idx + 1 < slotCount ? idx + 1 : idx + 1 - slotCount;
    //printf("Got a free slot at index %li, new index = %li\n", idx,cq->wrIdx);
    *slotIdx_o = idx;
    *slot_o = slot;
    return cqENOERR;
}

cqError_t cqGetNextWrIdx(cq_t* cq, cqSlot_t** slot_o, i64 slotIdx)
{

    if(cq == NULL || slot_o == NULL){
        return cqNULLPARAM;
    }

    const i64 idx       = slotIdx;
    const i64 slotCount = cq->slotCount;

    if(idx > slotCount || idx < 0){
        return cqERANGE;
    }

    cqSlot_t* slot = (cqSlot_t*)(cq->slots +  idx * cq->slotSize);
    if(slot->__state != cqSTFREE){
        return cqENOSLOT;
    }

    slot->__state = cqSTINUSEWR;
    //printf("Got a free slot at index %li, new index = %li\n", idx,cq->wrIdx);
    *slot_o = slot;
    return cqENOERR;
}


cqError_t cqCommitSlot(cq_t* cq, i64 slotIdx, i64 len)
{
    if(cq == NULL){
        return cqNULLPARAM;
    }

    if(slotIdx < 0 || slotIdx > cq->slotCount){
        return cqERANGE;
    }

    cqSlot_t* slot = (cqSlot_t*)(cq->slots +  slotIdx * cq->slotSize);
    if(slot->__state != cqSTINUSEWR){
        return cqEWRONGSLOT;
    }

    if(len > slot->len){
        return cqEPANIC; //Crap, it's likely that internal data structures have been overwritten here
    }

    slot->len     = len;

    __asm__ __volatile__ ("mfence");
    slot->__state = cqSTREADY; //This must happen last! At this point, it is committed!
    return cqENOERR;
}

cqError_t cqReleaseSlotWr(cq_t* cq, i64 slotIdx)
{
    if(cq == NULL){
        return cqNULLPARAM;
    }

    if(slotIdx < 0 || slotIdx > cq->slotCount){
        return cqERANGE;
    }

    cqSlot_t* slot = (cqSlot_t*)(cq->slots +  slotIdx * cq->slotSize);
    if(slot->__state != cqSTINUSEWR){
        return cqEWRONGSLOT;
    }
    slot->len     = cq->slotDataSize;
    //Set this to the current index, since it's now free, it's guaranteed to be writable in the future.
    cq->wrIdx     = slotIdx;

    __asm__ __volatile__ ("mfence");
    slot->__state = cqSTFREE; //This must happen last! At this point, it is committed!
    return cqENOERR;
}


cqError_t cQPushNext(cq_t* cq, i8* data, i64* len_io)
{
    if(cq == NULL || data == NULL || len_io == NULL){
        return cqNULLPARAM;
    }


    cqSlot_t* slot = NULL;
    i64 idx = -1;
    cqError_t err = cqGetNextWr(cq, &slot, &idx);
    if(err != cqENOERR){
        return err;
    }

    const i64 len = *len_io;
    const i64 toCopy = min(len, slot->len);
    *len_io = toCopy;
    memcpy(slot->buff,data,toCopy);

    cqCommitSlot(cq,idx,toCopy);

    if(toCopy < len){
        return cqETRUNC;
    }

    return cqENOERR;
}


cqError_t cqGetNextRd(cq_t* cq, cqSlot_t** slot_o, i64* slotIdx_o)
{
    if(cq == NULL || slot_o == NULL || slotIdx_o == NULL){
        return cqNULLPARAM;
    }


    const i64 slotCount = cq->slotCount;

    const i64 idx = cq->rdIdx + 1 < slotCount ? cq->rdIdx + 1 : cq->rdIdx + 1 - slotCount;
    cqSlot_t* slot = (cqSlot_t*)(cq->slots +  idx * cq->slotSize);
    if(slot->__state != cqSTREADY){
        return cqENOSLOT;
    }

    slot->__state = cqSTINUSERD;
    cq->rdIdx = idx + 1 < slotCount ? idx + 1 : idx + 1 - slotCount;
    *slotIdx_o = idx;
    *slot_o = slot;
    return cqENOERR;

}

cqError_t cqReleaseSlotRd(cq_t* cq, i64 slotIdx)
{
    if(cq == NULL){
        return cqNULLPARAM;
    }

    if(slotIdx < 0 || slotIdx > cq->slotCount){
        return cqERANGE;
    }

    cqSlot_t* slot = (cqSlot_t*)(cq->slots +  slotIdx * cq->slotSize);
    if(slot->__state != cqSTINUSERD){
        return cqEWRONGSLOT;
    }
    slot->len     = cq->slotDataSize;

    __asm__ __volatile__ ("mfence");
    slot->__state = cqSTFREE; //This must happen last! At this point, it is committed!
    return cqENOERR;
}

cqError_t cqPullNext(cq_t* cq, i8* data, i64* len_io)
{
    if(cq == NULL || data == NULL || len_io == NULL){
        return cqNULLPARAM;
    }


    cqSlot_t* slot = NULL;
    i64 idx = -1;
    cqError_t err = cqGetNextRd(cq, &slot, &idx);
    if(err != cqENOERR){
        return err;
    }

    const i64 len = *len_io;
    const i64 toCopy = min(len, slot->len);
    *len_io = toCopy;
    memcpy(data,slot->buff,toCopy);

    cqReleaseSlotRd(cq,idx);

    if(toCopy < slot->len){
        return cqETRUNC;
    }

    return cqENOERR;

}

void cqDelete(cq_t* cq)
{
    if(!cq){
        return;
    }

    if(cq->slots){
        free(cq->slots);
    }

    free(cq);
}


char* errors[cqECOUNT] = {
    "Success! No error",
    "No memory available",
    "No slots available",
    "Payload was truncated",
    "Value is out of range",
    "Wrong slot selected",
    "PANIC! INTERNAL MEMROY OVERWRITTEN",
    "Null paramter supplied"

};


//Convert a cqError number into a text description
const char* cqError2Str(cqError_t err)
{
    if(err >= cqECOUNT){
        return "Bad error number";
    }

    return errors[err];
}



