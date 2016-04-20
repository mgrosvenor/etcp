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
#include "debug.h"

//Slot count must be a power of 2 for performance reasons (to avoid a modulus on the critical path)
cq_t* cqNew(const i64 buffSize, const i64 slotCountLog2)
{
    if(buffSize < 0 || slotCountLog2 < 0){
        return NULL;
    }

    cq_t* result = calloc(1,sizeof(cq_t));
    if(!result){
        return NULL;
    }

    result->__slotCountLog2 = slotCountLog2;
    result->__slotCount       = 1 << slotCountLog2;
    result->__seqMask       = result->__slotCount - 1;
    result->slotDataSize    = buffSize;
    result->__slotSize      = buffSize + sizeof(cqSlot_t);
    result->__slotSize      = (result->__slotSize + sizeof(uint64_t) - 1) & ~(sizeof(uint64_t) -1 ); //Round up nearest word size

    //DBG("Allocating %li slots of size %li for %liB with mask=%016x\n",  result->__slotCount  , result->__slotSize,  result->__slotCount * result->__slotSize, result->__seqMask);
    result->__slots = calloc(result->__slotCount, result->__slotSize);
    if(!result->__slots){
        cqDelete(result);
        return NULL;
    }

    for(i64 i = 0; i < result->__slotCount; i++){
        cqSlot_t* slot = (cqSlot_t*)(result->__slots + i * result->__slotSize);
        slot->buff = (i8*)(slot + 1);
        slot->len  = result->slotDataSize;
    }


    result->rdMin = 0;
    result->rdMax = 0;
    result->rdSeq = 0;
    result->wrSeq = 0; //Write pointer has been advanced
    result->wrMin = 0;
    result->wrMax = result->rdSeq + result->__slotCount;
    result->rdMax = result->wrMin; //Pushing write forwards means there's more to read
    result->available = result->__slotCount;

    return result;

}

cqError_t cqGet(const cq_t* const cq, cqSlot_t** slot_o, const i64 seqNum)
{
    if_unlikely(cq == NULL){
        return cqENULLPARAM;
    }

    if_unlikely(slot_o == NULL){
        return cqENULLPARAM;
    }

    if_unlikely(seqNum < cq->rdSeq){
        return cqERANGELO;
    }

    if_unlikely(seqNum >= cq->rdSeq + cq->__slotCount ){
        return cqERANGEHI;
    }

    //Passed all the checks, this seqNumber is in the right range, turn it into a slot index.
    const i64 idx = ( seqNum & cq->__seqMask);

    //DBG("Getting item at index =%li (seq=%li)\n", idx, seqNum);
    *slot_o = (cqSlot_t*)(cq->__slots + idx * cq->__slotSize);

    return cqENOERR;

}

cqError_t cqAdvWrSeq(cq_t* const cq)
{
    if_unlikely(cq == NULL){
        return cqENULLPARAM;
    }

    cqSlot_t* slot = NULL;
    cqError_t err = cqENOERR;
    i64 seqNum = cq->wrSeq;

    //DBG("Trying to advance wrSeq stating at %li\n", seqNum);

    for(;; seqNum++){
        err = cqGet(cq, &slot, seqNum);
        if_eqlikely(err == cqERANGEHI){ //Is it a valid slot?
            //No. Can't advance the write pointer, it's overlapping with read pointer
            break;
        }
        else if_unlikely(err != cqENOERR){ //Is it a valid slot?
          FAT("Unexpected error: %s\n", cqError2Str(err) );
          return cqEPANIC;
        }

        //Is the slot empty?
        if(!slot->valid){
            break; //Can't advance the write pointer, slot is still empty
        }
    }

    //DBG("New wrSeq %li\n", seqNum);

    if_eqlikely(seqNum == cq->wrSeq){
        return cqENOCHANGE;
    }

    cq->wrSeq = seqNum; //Write pointer has been advanced
    cq->wrMin = seqNum;
    cq->wrMax = cq->rdSeq + cq->__slotCount;
    cq->rdMax = cq->wrMin; //Pushing write forwards means there's more to read
    cq->outstanding--;
    cq->readable++;
    cq->available--;

    return cqENOERR;
}


cqError_t cqAdvRdSeq(cq_t* const cq)
{
    if_unlikely(cq == NULL){
        return cqENULLPARAM;
    }

    cqSlot_t* slot = NULL;
    cqError_t err = cqENOERR;
    i64 seqNum = cq->rdSeq;

    //DBG("Trying to advance rdSeq stating at %li\n", seqNum);

    for(;; seqNum++){
        //Can't advance the read pointer beyond the write pointer
        if_unlikely( seqNum >= cq->wrSeq){
            break;
        }

        err = cqGet(cq, &slot, seqNum);
        if_eqlikely(err == cqERANGEHI){ //Is it a valid slot?
            FAT("This should never happen. Panic!\n");
            return cqEPANIC;
        }
        else if_unlikely(err != cqENOERR){ //Is it a valid slot?
            FAT("Unexpected error: %s\n", cqError2Str(err) );
            return cqEPANIC;
        }

        //Is the slot full?
        if_unlikely(slot->valid){
            break; //Can't advance the read pointer, slot is full
        }
    }



    if_eqlikely(seqNum == cq->rdSeq){
        //DBG("Done with no change\n");
        return cqENOCHANGE;
    }

    //DBG("Done, new rdSeq %li\n", seqNum);

    cq->rdSeq = seqNum; //Write pointer has been advanced
    cq->rdMin = seqNum;
    cq->rdMax = cq->wrMin + 1;
    cq->wrMax = cq->rdMin + cq->__slotCount; //... but it does advance this
    cq->wrRng = cq->wrMax - cq->wrMin; //Maximum writable capacity
    cq->rdRng = cq->rdMax - cq->rdMin; //Maximum readable capacity
    cq->readable--;
    cq->available++;

    return cqENOERR;
}


void cqDelete(cq_t* const cq)
{
    if(!cq){
        return;
    }

    if(cq->__slots){
        free(cq->__slots);
    }

    free(cq);
}



static char* errors[cqECOUNT] = {
    "Success! No error",                    //cqENOERR
    "No memory available",                  //cqENOMEM
    "No slots available",                   //cqENOSLOT
    "Payload was truncated",                //cqETRUNC
    "Value is out of range. Too high",      //cqERANGE
    "Value is out of range. Too low",       //cqERANGE
    "Wrong slot selected",                  //cqEWRONGSLOT
    "PANIC! INTERNAL MEMROY OVERWRITTEN",   //cqEPANIC
    "Null parameter supplied",              //cqNULLPARAM
    "No change to sequence number",         //cqNOCAHGNE
};


//Convert a cqError number into a text description
const char* cqError2Str(const cqError_t err)
{
    if(err >= cqECOUNT){
        return "Bad error number";
    }

    return errors[err];
}

//**************************************************************************************************************************
// non primitive functions

cqError_t cqPush(cq_t* const cq, const void* __restrict data, i64* const len_io, i64 const seqNum)
{
    if(cq == NULL || data == NULL || len_io == NULL){
        return cqENULLPARAM;
    }

    cqSlot_t* slot = NULL;
    cqError_t err = cqGet(cq,&slot,seqNum);
    if_unlikely(err == cqERANGEHI){
        return cqENOSLOT;
    }
    else if_unlikely(err != cqENOERR){ //Is it a valid slot?
        FAT("Unexpected error: %s\n", cqError2Str(err) );
        return cqEPANIC;
    }

    if(slot->valid){
        return cqENOSLOT;
    }


    const i64 len = *len_io;
    const i64 toCopy = MIN(len, slot->len);
    *len_io = toCopy;
    memcpy(slot->buff,data,toCopy);

    if(toCopy < len){
        return cqETRUNC;
    }

    return cqENOERR;
}

cqError_t cqPull(cq_t* const cq, void* __restrict data, i64* const len_io, i64 const seqNum)
{
    if(cq == NULL || data == NULL || len_io == NULL){
        return cqENULLPARAM;
    }

    cqSlot_t* slot = NULL;
    cqError_t err = cqGet(cq,&slot,seqNum);
    if_unlikely(err == cqERANGEHI){
        return cqENOSLOT;
    }
    else if_unlikely(err != cqENOERR){ //Is it a valid slot?
        FAT("Unexpected error: %s\n", cqError2Str(err) );
        return cqEPANIC;
    }

    if(!slot->valid){
        return cqENOSLOT;
    }

    const i64 len = *len_io;
    const i64 toCopy = MIN(len, slot->len);
    *len_io = toCopy;
    memcpy(data,slot->buff,toCopy);

    if(toCopy > slot->len){
        return cqETRUNC;
    }

    return cqENOERR;

}

cqError_t cqCommitSlot(cq_t* const cq, const i64 seqNum, const i64 len)
{
    if(cq == NULL){
        return cqENULLPARAM;
    }

    cqSlot_t* slot = NULL;
    cqError_t err = cqGet(cq,&slot,seqNum);
    if_unlikely(err != cqENOERR){
        return err;
    }

    if(len > slot->len){
        FAT(" it's likely that internal data structures have been overwritten here\n");
        return cqEPANIC; //Crap, it's likely that internal data structures have been overwritten here
    }

    if_unlikely(slot->valid){
        return cqEWRONGSLOT;
    }


    slot->len = len;
    slot->valid = true;
    cq->outstanding++;

    //Now try to advance the write pointer as much as possilbe
    return cqAdvWrSeq(cq);
}




cqError_t cqGetNextWr(cq_t* const cq, cqSlot_t** const slot_o, i64* const seqNum_o)
{
    if_unlikely(cq == NULL || slot_o == NULL){
        return cqENULLPARAM;
    }

    cqError_t err = cqGet(cq,slot_o,cq->wrSeq);
    if_unlikely(err == cqERANGEHI){
        return cqENOSLOT;
    }
    else if_unlikely(err != cqENOERR){ //Is it a valid slot?
      FAT("Unexpected error: %s\n", cqError2Str(err) );
      return cqEPANIC;
    }

    const cqSlot_t* slot = *slot_o;
    if(slot->valid){
        return cqENOSLOT;
    }

    *seqNum_o = cq->wrSeq;
    return err;
}


cqError_t cqPushNext(cq_t* const cq, const void* __restrict data, i64* const len_io, i64* const seqNum_o)
{
    *seqNum_o = cq->wrSeq;
   return cqPush(cq,data,len_io,cq->wrSeq);
}


cqError_t cqGetNextRd(cq_t* const cq, cqSlot_t** const slot_o, i64* const seqNum_o)
{
    if_unlikely(cq == NULL || slot_o == NULL){
        return cqENULLPARAM;
    }

    cqError_t err = cqGet(cq,slot_o,cq->rdSeq);
    if_unlikely(err == cqERANGEHI){
        return cqENOSLOT;
    }
    else if_unlikely(err != cqENOERR){ //Is it a valid slot?
        FAT("Unexpected error: %s\n", cqError2Str(err) );
        return cqEPANIC;
    }

    const cqSlot_t* slot = *slot_o;
    if(!slot->valid){
        return cqENOSLOT;
    }


    *seqNum_o = cq->rdSeq;
    return err;
}

cqError_t cqPullNext(cq_t* const cq, void* __restrict data, i64* const len_io, i64* const seqNum_o)
{
    *seqNum_o = cq->rdSeq;
     return cqPull(cq,data,len_io,cq->rdSeq);
}


cqError_t cqReleaseSlot(cq_t* const cq, const i64 seqNum)
{
    cqSlot_t* slot = NULL;
    cqError_t err = cqGet(cq,&slot,seqNum);
    if_unlikely(err != cqENOERR){
        return err;
    }

    if_unlikely(!slot->valid){
        return cqEWRONGSLOT;
    }

    slot->len = cq->__slotSize;
    slot->valid = false;

    //Now try to move the read pointer as far forward as possible
   return cqAdvRdSeq(cq);
}

//This is the same as the "cqGet" function, but it also checks that the slot is readable (ie, has valid data in it)
cqError_t cqGetRd(const cq_t* const cq, cqSlot_t** slot_o, const i64 seqNum)
{
    cqError_t err = cqGet(cq,slot_o,seqNum);
    if_unlikely(err != cqENOERR){
        return err;
    }

    const cqSlot_t* const slot = *slot_o;
    if_unlikely(!slot->valid){
        return cqEWRONGSLOT;
    }

    return cqENOERR;

}
