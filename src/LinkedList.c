/*
 * Copyright (c) 2016, All rights reserved.
 * See LICENSE.txt for full details.
 *
 *  Created:   13 Apr 2016
 *  File name: LinkedList.c
 *  Description:
 *  <INSERT DESCRIPTION HERE>
 */


#include "LinkedList.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "utils.h"
#include "debug.h"


/**
 * @brief           Create a new linked list structure
 * @param buffSize  The maximum size in bytes that is required for each data buffer in each slot.
 * @return          On success a new a pointer to a new cq_t structure. On failure, NULL will be returned
 */
ll_t* llNew(const i64 buffSize)
{
   ll_t* result = calloc(1,sizeof(ll_t));
    if_unlikely(!result){
        return NULL;
    }

    result->slotDataSize    = buffSize;
    result->__slotSize      = buffSize + sizeof(llSlot_t);
    result->__slotSize      = (result->__slotSize + sizeof(uint64_t) - 1) & ~(sizeof(uint64_t) -1 ); //Round up nearest word size

    return result;
}


/**
 * @breif           Get the first item out of the linked list
 * @param ll        linked list we're working on
 * @param llSlot_o  If successful, the item will be here
 * @return
 */
llError_t llGetFirst(const ll_t* const ll, llSlot_t** llSlot_o)
{
    if_unlikely(ll == NULL || llSlot_o == NULL){
        return llENULLPARAM;
    }

    if_unlikely(ll->__head == NULL){
        return llENOSLOT;
    }

    *llSlot_o = ll->__head;

    return llENOERR;
}

/**
 *
 * @param ll
 * @param llSlot_io
 * @return
 */
llError_t llGetNext(const ll_t* const ll, llSlot_t** llSlot_io)
{
    if_unlikely(ll == NULL || llSlot_io == NULL){
        return llENULLPARAM;
    }

    const llSlot_t* const slot = *llSlot_io;

    if_unlikely(slot->__next == NULL){
        *llSlot_io = NULL;
        return llENOSLOT;
    }

    *llSlot_io = slot->__next;
    return llENOERR;

}

static inline llError_t newSlot(ll_t* const ll, const void* const data, i64* const len_io, llSlot_t** slot_o, const i64 seqNum)
{
    llSlot_t* const slot = calloc(1,ll->__slotSize); //This is stupid, should have a memory pool
    if(!slot){
        return llENOMEM;
    }

    const i64 len = *len_io;
    const i64 toCopy = MIN(len,ll->slotDataSize);
    slot->len  = toCopy;
    slot->buff = (void*)(slot + 1);
    slot->seqNum = seqNum;

    memcpy(slot->buff,data,toCopy);

    *len_io = toCopy;
    *slot_o = slot;

    return toCopy < len? llETRUNC : llENOERR;
}


/**
 * @brief       Push an item into the list, ordered by seqNum
 * @param ll
 * @param data
 * @param len
 * @param seqNum
 * @return
 */
//This is a bit shitty, it will do a (n) time lookup for each insert. Could make that log(n) if I used a tree, but a) this
//list shouldn't really grow to big (otherwise the network is very delayed and I getting lots of spurious RTOs) and b) if
//this is that's the case, then the packets will probably still come in order, leading to an unbalanced tree and a (n) time
//lookup unless I use some kind of fancy RB/2-4tree or something. Right now, the complexity is not worth it.
llError_t llPushSeqOrd(ll_t* const ll, const void* const data, i64* const len_io, const i64 seqNum)
{
    if_unlikely(ll == NULL || data == NULL || len_io == NULL || *len_io == 0){
        return llENULLPARAM;
    }

    //Look linearly through the structure for a space in the sequence number sequence
    llSlot_t* new  = NULL;
    llSlot_t* slot = NULL;

    llError_t err  = newSlot(ll,data,len_io, &new, seqNum);
    if_unlikely(err != llENOERR && err != llETRUNC){
        return err; //Can't continue at this point
    }
    llError_t successResult = err; //This could be ENOEROR or ETRUNC


    err = llGetFirst(ll,&slot);
    if_unlikely(err == llENOSLOT){
        //There's nothing here, no searching to do
        ll->__head = new;
        ll->slotCount++;
        return successResult;
    }

    //We have a head, check if this seqNum is less than or equalt to head, if so, insert it
    if_eqlikely(seqNum < slot->seqNum){
        new->__next = ll->__head;
        ll->__head = new;
        ll->slotCount++;
        return successResult;
    }


    while(1){

        if_unlikely(slot->__next == NULL){
            //We've run out of slots, this must be the biggest value
            slot->__next = new;
            ll->slotCount++;
            return successResult;
        }

        //Check if the new slot should go next
        if_eqlikely(seqNum < slot->__next->seqNum){
            //Do the insert
            new->__next = slot->__next;
            slot->__next = new;
            ll->slotCount++;
            return successResult;
        }

        err = llGetNext(ll,&slot);
        if_unlikely(err == llENOSLOT){
            //This should not happen, we've just checked above!
            FAT("Could not get next slot, but there appears to be one!\n");
            return llEPANIC;
        }

    }

    //Unreachable! Should never get here!
    FAT("Exited loop somehow!\n");
    return llEPANIC;

}


/**
 *
 * @param ll
 * @param llSlot
 * @return
 */
void llReleaseHead(ll_t* const ll)
{
    if_unlikely(ll == NULL){
        return;
    }

    ll->slotCount--;

    if_likely(ll->__head != NULL){
        llSlot_t* const tmp = ll->__head->__next;
        free(ll->__head);
        ll->__head = tmp;
    }
}

void llDelete(ll_t* const ll)
{
    if_unlikely(ll == NULL){
        return;
    }

    llSlot_t* slot = NULL;
    while(llGetFirst(ll,&slot) != llENOSLOT){
        llReleaseHead(ll);
    }

    free(ll);

}


static char* errors[llECOUNT+1] = {


    "Success! No error",                    //llENOERR
    "No memory available",                  //llENOMEM
    "Payload was truncated",                //llETRUNC
    "Value is out of range",                //llERANGE
    "PANIC! INTERNAL MEMROY OVERWRITTEN",   //llEPANIC
    "Null parameter supplied",              //llNULLPARAM
    "No change to sequence number",         //llNOCAHGNE
    "Wrong slot selected",                  //llENOSLOT,
};


//Convert a cqError number into a text description
const char* llError2Str(const llError_t err)
{
    if(err >= llECOUNT){
        return "Bad error number";
    }

    return errors[err];
}


