/*
 * Copyright (c) 2016, All rights reserved.
 * See LICENSE.txt for full details. 
 * 
 *  Created:   31 Mar 2016
 *  File name: HashTable.h
 *  Description:
 *  <INSERT DESCRIPTION HERE> 
 */
#ifndef SRC_HASHTABLE_H_
#define SRC_HASHTABLE_H_

#include <stdint.h>
#include <stddef.h>

#include "types.h"

typedef struct __attribute__((packed)) htKey_s {
    uint64_t keyLo;
    uint64_t keyHi;
} htKey_t;

typedef struct ht_s ht_t;

typedef enum {
    htENOEROR = 0,   //Things went well!
    htENOMEM,    //Not enough memory to do this
    htEALREADY,  //This key already exists
    htENOTFOUND, //The key does not exist
    htECOUNT     //Must come last
} htError_t;


/**
 * Make a new hashtable with (1 << tableEntriesLog2) entries in it
 * @param tableEntriesLog2
 * @return
 */
ht_t* htNew( uint64_t tableEntriesLog2);

/**
 * Put a new key/value into the HT, return an error if it already exists
 * @param ht
 * @param key
 * @param keySize
 * @param valueSize
 * @param value
 * @return
 */
htError_t htAddNew(ht_t* const ht, const htKey_t* const key, void* value  );

/**
 * Get a value from the table given the key
 * @param ht
 * @param key
 * @param value_o
 * @return
 */
htError_t htGet(ht_t* const ht, const htKey_t* const key, void** const value_o  );

/**
 * Remove a key from the table
 * @param ht
 * @param key
 */
void htRem(ht_t* const ht, const htKey_t* const key );


const char* htError2Str(const htError_t err);

/**
 * Free all resources associated with the hashtable
 * @param ht
 */
//deleteCB is a callback which (if not null) will be called just before deleting each key entry. This gives the user the
//option to cleanup their own code.
typedef void (*deleteCb_f)(const htKey_t* const key,  void* const value);
void htDelete(ht_t* const ht, deleteCb_f deleteCb);

#endif /* SRC_HASHTABLE_H_ */


