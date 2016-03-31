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
    htENOEROR,   //Things went well!
    htENOMEM,    //Not enough memory to do this
    htEALREADY,  //This key already exists
    htENOTFOUND, //The key does not exist
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


/**
 * Free all resources associated with the hashtable
 * @param ht
 */
void htDelete(ht_t* const ht);

#endif /* SRC_HASHTABLE_H_ */


