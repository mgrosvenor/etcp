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


cq_t* cqNew(const i64 buffSize, const i64 slotCount)
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
    result->_slots = calloc(slotCount, result->slotSize);
    if(!result->_slots){
        cqDelete(result);
        return NULL;
    }

    for(i64 i = 0; i < slotCount; i++){
        cqSlot_t* slot = (cqSlot_t*)(result->_slots + i * result->slotSize);
        slot->buff = (i8*)(slot + 1);
        slot->len  = result->slotSize;
    }

    return result;

}


cqError_t cqGetNextWr(cq_t* const cq, cqSlot_t** const slot_o, i64* const slotIdx_o)
{
    if(cq == NULL || slot_o == NULL || slotIdx_o == NULL){
        return cqNULLPARAM;
    }

    const i64 slotCount = cq->slotCount;
    const i64 idx = cq->_wrIdx + 1 < slotCount ? cq->_wrIdx + 1 : cq->_wrIdx + 1 - slotCount;
    cqSlot_t* slot = (cqSlot_t*)(cq->_slots +  idx * cq->slotSize);
    if(slot->__state != cqSTFREE){
        return cqENOSLOT;
    }

    __asm__ __volatile__ ("mfence");
    slot->__state = cqSTINUSEWR;
    __asm__ __volatile__ ("mfence"); //Atomically commit the new state

    cq->_wrIdx = idx + 1 < slotCount ? idx + 1 : idx + 1 - slotCount;
    cq->wrSlotsUsed++;
    //printf("Got a free slot at index %li, new index = %li\n", idx,cq->wrIdx);
    *slotIdx_o = idx;
    *slot_o = slot;
    return cqENOERR;
}

cqError_t cqGetWrIdx(cq_t* const cq, cqSlot_t** const slot_o, const i64 slotIdx)
{

    if(cq == NULL || slot_o == NULL){
        return cqNULLPARAM;
    }

    const i64 idx       = slotIdx;
    const i64 slotCount = cq->slotCount;

    if(idx > slotCount || idx < 0){
        return cqERANGE;
    }

    cqSlot_t* slot = (cqSlot_t*)(cq->_slots +  idx * cq->slotSize);
    if(slot->__state != cqSTFREE){
        return cqENOSLOT;
    }

    __asm__ __volatile__ ("mfence");
    slot->__state = cqSTINUSEWR;
    __asm__ __volatile__ ("mfence"); //Atomically commit the new state


    cq->wrSlotsUsed++;
    //printf("Got a free slot at index %li, new index = %li\n", idx,cq->wrIdx);
    *slot_o = slot;
    return cqENOERR;
}



cqError_t cqReleaseSlotWr(cq_t* const cq, const i64 slotIdx)
{
    if(cq == NULL){
        return cqNULLPARAM;
    }

    if(slotIdx < 0 || slotIdx > cq->slotCount){
        return cqERANGE;
    }

    cqSlot_t* slot = (cqSlot_t*)(cq->_slots +  slotIdx * cq->slotSize);
    if(slot->__state != cqSTINUSEWR){
        return cqEWRONGSLOT;
    }
    slot->len     = cq->slotDataSize;
    __asm__ __volatile__ ("mfence");
    slot->__state = cqSTFREE; //Atomically commite this state
    __asm__ __volatile__ ("mfence");

    //Set this to the current index, since it's now free, it's guaranteed to be writable in the future.
    cq->_wrIdx = slotIdx;
    cq->wrSlotsUsed--;

    return cqENOERR;
}

cqError_t cqPushNext(cq_t* const cq, const void* __restrict data, i64* const len_io)
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

    if(toCopy < len){
        return cqETRUNC;
    }

    return cqENOERR;
}


cqError_t cqPushIdx(cq_t* const cq, const void* __restrict data, i64* const len_io, const i64 idx)
{
    if(cq == NULL || data == NULL || len_io == NULL){
        return cqNULLPARAM;
    }


    cqSlot_t* slot = NULL;
    cqError_t err = cqGetWrIdx(cq, &slot, idx);
    if(err != cqENOERR){
        return err;
    }

    const i64 len = *len_io;
    const i64 toCopy = min(len, slot->len);
    *len_io = toCopy;
    memcpy(slot->buff,data,toCopy);

    if(toCopy < len){
        return cqETRUNC;
    }

    return cqENOERR;
}


cqError_t cqCommitSlot(cq_t* const cq, const i64 slotIdx, const i64 len)
{
    if(cq == NULL){
        return cqNULLPARAM;
    }

    if(slotIdx < 0 || slotIdx > cq->slotCount){
        return cqERANGE;
    }

    cqSlot_t* slot = (cqSlot_t*)(cq->_slots +  slotIdx * cq->slotSize);
    if(slot->__state != cqSTINUSEWR){
        return cqEWRONGSLOT;
    }

    if(len > slot->len){
        return cqEPANIC; //Crap, it's likely that internal data structures have been overwritten here
    }

    slot->len     = len;
    __asm__ __volatile__ ("mfence");
    slot->__state = cqSTREADY; //Attomically commit this state
    __asm__ __volatile__ ("mfence");

    cq->wrSlotsUsed--;
    return cqENOERR;
}



cqError_t cqGetNextRd(cq_t* const cq, cqSlot_t** const slot_o, i64* const slotIdx_o)
{
    if(cq == NULL || slot_o == NULL || slotIdx_o == NULL){
        return cqNULLPARAM;
    }


    const i64 slotCount = cq->slotCount;

    const i64 idx = cq->_rdIdx + 1 < slotCount ? cq->_rdIdx + 1 : cq->_rdIdx + 1 - slotCount;
    cqSlot_t* slot = (cqSlot_t*)(cq->_slots +  idx * cq->slotSize);
    if(slot->__state != cqSTREADY){
        return cqENOSLOT;
    }
    __asm__ __volatile__ ("mfence");
    slot->__state = cqSTINUSERD;
    __asm__ __volatile__ ("mfence");

    cq->_rdIdx = idx + 1 < slotCount ? idx + 1 : idx + 1 - slotCount;
    cq->rdSlotsUsed++;

    *slotIdx_o = idx;
    *slot_o = slot;
    return cqENOERR;

}

cqError_t cqReleaseSlotRd(cq_t* const cq, const i64 slotIdx)
{
    if(cq == NULL){
        return cqNULLPARAM;
    }

    if(slotIdx < 0 || slotIdx > cq->slotCount){
        return cqERANGE;
    }

    cqSlot_t* slot = (cqSlot_t*)(cq->_slots +  slotIdx * cq->slotSize);
    if(slot->__state != cqSTINUSERD){
        return cqEWRONGSLOT;
    }

    __asm__ __volatile__ ("mfence");
    slot->__state = cqSTFREE;
    __asm__ __volatile__ ("mfence");

    cq->rdSlotsUsed--;

    return cqENOERR;
}

cqError_t cqPullNext(cq_t* const cq, void* __restrict data, i64* const len_io, i64* const slotIdx_o)
{
    if(cq == NULL || data == NULL || len_io == NULL){
        return cqNULLPARAM;
    }

    cqSlot_t* slot = NULL;
    cqError_t err = cqGetNextRd(cq, &slot, slotIdx_o);
    if(err != cqENOERR){
        return err;
    }

    const i64 len = *len_io;
    const i64 toCopy = min(len, slot->len);
    *len_io = toCopy;
    memcpy(data,slot->buff,toCopy);

    if(toCopy < slot->len){
        return cqETRUNC;
    }

    return cqENOERR;

}

//Get a non empty slot
cqError_t cqGetSlotIdx(cq_t* const cq, cqSlot_t** const slot_o, const i64 slotIdx)
{

    if(cq == NULL || slot_o == NULL){
        return cqNULLPARAM;
    }

    const i64 idx       = slotIdx;
    const i64 slotCount = cq->slotCount;

    if(idx > slotCount || idx < 0){
        return cqERANGE;
    }

    cqSlot_t* slot = (cqSlot_t*)(cq->_slots +  idx * cq->slotSize);
    if(slot->__state == cqSTFREE){
        return cqEWRONGSLOT;
    }

    *slot_o = slot;
    return cqENOERR;
}



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
const char* cqError2Str(const cqError_t err)
{
    if(err >= cqECOUNT){
        return "Bad error number";
    }

    return errors[err];
}



