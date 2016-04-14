/*
 * Copyright (c) 2016, All rights reserved.
 * See LICENSE.txt for full details.
 *
 *  Created:   13 Apr 2016
 *  File name: LinkedList.c
 *  Description:
 *  <INSERT DESCRIPTION HERE>
 */
#ifndef LINKEDLIST_H_
#define LINKEDLIST_H_

#include <stdbool.h>

#include "types.h"

typedef struct llSlot_s llSlot_t;

//Using the "slot" nomenclature from the CQ structure to keep things consistent
struct llSlot_s {
    i64 len;     //Length of data - this is a constant, and should not be altered
    void* buff;  //Place to get/put data - this is a constant and should not be altered
    i64 seqNum;

    llSlot_t* __next; //Next item in the list
} ;


//Using the "slot" nomenclature from the CQ structure to keep things consistent
typedef struct {

    i64 slotDataSize;
    i64 slotCount;


    // "__" means "private"
    i64 __slotSize;
    llSlot_t* __head;


} ll_t;


/**
 * @brief Errors returned by the CQ structure
 */
typedef enum {
    llENOERR = 0,
    llENOMEM,
    llETRUNC,
    llERANGE,
    llEPANIC,
    llENULLPARAM,
    llENOSLOT,

    //THIS MUST BE LAST
    llECOUNT,       //!< llERCOUNT      Total number of error codes.
} llError_t;



/**
 * @brief           Create a new linked list structure
 * @param buffSize  The maximum size in bytes that is required for each data buffer in each slot.
 * @return          On success a new a pointer to a new cq_t structure. On failure, NULL will be returned
 */
ll_t* llNew(const i64 buffSize);


/**
 * @breif           Get the first item out of the linked list
 * @param ll        linked list we're working on
 * @param llSlot_o  If successful, the item will be here
 * @return
 */
llError_t llGetFirst(const ll_t* const ll, llSlot_t** llSlot_o);

/**
 *
 * @param ll
 * @param llSlot_io
 * @return
 */
llError_t llGetNext(const ll_t* const ll, llSlot_t** llSlot_io);


/**
 * @brief       Push an item into the list, ordered by seqNum
 * @param ll
 * @param data
 * @param len
 * @param seqNum
 * @return
 */
llError_t llPushSeqOrd(ll_t* const ll, const void* const data, i64* const len_io, const i64 seqNum);


/**
 *
 * @param ll
 * @param llSlot
 * @return
 */
void llReleaseHead(ll_t* const ll);


void llDelete(ll_t* const ll);


const char* llError2Str(llError_t const err);

#endif /*LINKEDLIST_H_*/
