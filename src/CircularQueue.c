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
    result->slotSize     = (result->slotSize + sizeof(uint64_t)) & ~(sizeof(uint64_t)); //Round up nearest word size
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
    const i64 slotCount = cq->slotCount;

    for(i64 i = 0; i < slotCount; i++){
        const i64 idx = (cq->rdIdx + i) % slotCount;
        cqSlot_t* slot = (cqSlot_t*)(cq->slots +  idx * cq->slotSize);
        if(slot->__state == cqSTFREE){
            slot->__state = cqSTINUSEWR;
            cq->rdIdx = i;
            *slotIdx_o = i;
            *slot_o = slot;
            return cqENOERR;
        }
    }

    return cqENOSLOT;
}


cqError_t cqCommitSlot(cq_t* cq, i64 slotIdx, i64 len)
{
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
    cq->rdIdx++;
    cq->rdIdx %= cq->slotCount;

    __asm__ __volatile__ ("mfence");
    slot->__state = cqSTREADY; //This must happen last! At this point, it is committed!
    return cqENOERR;
}


cqError_t cQPushNext(cq_t* cq, i8* data, i64* len_io)
{
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
    const i64 slotCount = cq->slotCount;

    for(i64 i = 0; i < slotCount; i++){
        const i64 idx = (cq->rdIdx + i) % slotCount;
        cqSlot_t* slot = (cqSlot_t*)(cq->slots +  idx * cq->slotSize);
        if(slot->__state == cqSTREADY){
            slot->__state = cqSTINUSERD;
            cq->wrIdx = i;
            *slotIdx_o = i;
            *slot_o = slot;
            return cqENOERR;
        }
    }

    return cqENOSLOT;
}

cqError_t cqReleaseSlot(const cq_t* cq, i64 slotIdx)
{
    if(slotIdx < 0 || slotIdx > cq->slotCount){
            return cqERANGE;
        }

        cqSlot_t* slot = (cqSlot_t*)(cq->slots +  slotIdx * cq->slotSize);
        if(slot->__state != cqSTINUSERD){
            return cqEWRONGSLOT;
        }

        slot->len     = cq->slotDataSize;
        cq->wrIdx++;
        cq->wrIdx %= cq->slotCount;

        __asm__ __volatile__ ("mfence");
        slot->__state = cqSTFREE; //This must happen last! At this point, it is committed!
        return cqENOERR;
}

cqError_t cqPullNext(const cq_t* cq, i8* data, i64* len_io)
{
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

    cqReleaseSlot(cq,idx);

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

char erros[cqECOUNT] = {
    "Success! No error",
    "No memory available",
    "No slots available",
    "Payload was truncated",
};


//Convert a cqError number into a text description
const char* cqError2Str(cqError_t err)
{

}



