/*
 * Copyright (c) 2016, All rights reserved.
 * See LICENSE.txt for full details. 
 * 
 *  Created:   31 Mar 2016
 *  File name: HashTable.c
 *  Description:
 *  A really simple hash table implementation for doing 128bit address lookups
 */

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "types.h"
#include "utils.h"
#include "spooky_hash.h"
#include "HashTable.h"

typedef struct htEntry_s htEntry_t;
typedef struct htEntry_s {
    htEntry_t* next; //Next entry in the linked list
    htKey_t key;
    void* value; //Pointer to the value this is not internally maintained! Dangling pointers warning....
} htEntry_t;

typedef struct ht_s {
    uint64_t entrySize;
    uint64_t tableEntriesLog2;
    uint64_t tableEntries;
    htEntry_t** table;
} ht_t;


/**
 * Make a new hashtable with (1 << tableEntriesLog2) entries in it
 * @param tableEntriesLog2
 * @return
 */
ht_t* htNew( uint64_t tableEntriesLog2)
{
    ht_t* const ht = (ht_t* const)calloc(1,sizeof(ht_t));
    if_unlikely(ht == NULL){
        //Not enough memory for the structure
        return NULL;
    }

    ht->tableEntriesLog2 = tableEntriesLog2;
    ht->tableEntries     = (1 << tableEntriesLog2);
    ht->table            = (htEntry_t**)calloc(ht->tableEntries, sizeof(htEntry_t*));
    if(ht->table == NULL){
        //Not enough memory
        free(ht);
        return NULL;
    }

    return ht;
}



/**
 * Take a hash of key and convert it to power of 2 bit wide index.
 * @param ht        The hashtable container structure
 * @param key       The key to convert
 * @param keySize   The size of the key in bytes
 * @return          A value of no more than (1 << ht->tableEntriesLog2) in size
 */
static inline uint64_t getIdx(const ht_t* const ht, const htKey_t* const key)
{
    uint64_t hash64 = spooky_Hash64(key, sizeof(htKey_t), 0xB16B00B1E5FULL);
    return (hash64 * 11400714819323198549UL) >> (64 - (ht->tableEntriesLog2));
}

/**
 * Put a new key/value into the HT, return an error if it already exists
 * @param ht
 * @param key
 * @param keySize
 * @param valueSize
 * @param value
 * @return
 */
htError_t htAddNew(ht_t* const ht, const htKey_t* const key, void* value  )
{
    //Convert the key to an index
    const i64 idx = getIdx(ht, key);
    assert(idx >= 0);
    assert(idx <= (i64)ht->tableEntries);

    //Jump to the index
    htEntry_t* entry = ht->table[idx];

    //Traverse the linked list
    if_eqlikely(entry == NULL){
        //The linked list is currently empty
        htEntry_t* const newEntry = (htEntry_t* const)calloc(1,sizeof(htEntry_t));
        if_unlikely(newEntry == NULL){
            return htENOMEM;
        }

        ht->table[idx]       = newEntry;
        ht->table[idx]->key   = *key;
        ht->table[idx]->value = value;
        return htENOEROR;

    }

    //The linked is is not empty, check that the key is not already in it
    if_unlikely(memcmp(key, &entry->key, sizeof(htKey_t)) == 0){
        //The key is already in the table!
        return htEALREADY;
    }

    //Keep checking and traversing
    for(; entry->next; entry = entry->next){
        if_unlikely(memcmp(key, &entry->key, sizeof(htKey_t)) == 0){
            return htEALREADY;
        }
    }

    //Reached the end of the linked list without finding the key. Winner, add it
    htEntry_t* const newEntry = (htEntry_t* const)calloc(1,sizeof(htEntry_t));
    if_unlikely(newEntry == NULL){
        return htENOMEM;
    }

    entry->next       = newEntry;
    entry->next->key   = *key;
    entry->next->value = value;
    return htENOEROR;
}



/**
 * Get a value from the table given the key
 * @param ht
 * @param key
 * @param value_o
 * @return
 */
htError_t htGet(ht_t* const ht, const htKey_t* const key, void** const value_o  )
{
    //Convert the key to an index
    const i64 idx = getIdx(ht, key);
    assert(idx >= 0);
    assert(idx <= (i64)ht->tableEntries);

    //Jump to the index
    htEntry_t* entry = ht->table[idx];

    //Traverse the linked list
    if_unlikely(entry == NULL){
        *value_o = NULL;
        return htENOTFOUND;
    }

    //The linked is is not empty, check that the key is not already in it
    if_eqlikely(memcmp(key, &entry->key, sizeof(htKey_t)) == 0){
        *value_o = entry->value;
        return htENOEROR;
    }

    //Keep checking and traversing
    for(; entry->next; entry = entry->next){
        if_eqlikely(memcmp(key, &entry->key, sizeof(htKey_t)) == 0){
            *value_o = entry->value;
            return htENOEROR;
        }
    }

    //Reached the end of the linked list without finding the key.
    printf("Not found entry!\n");
    *value_o = NULL;
    return htENOTFOUND;
}


/**
 * Remove a key from the table
 * @param ht
 * @param key
 */
void htRem(ht_t* const ht, const htKey_t* const key )
{
    //Convert the key to an index
    const i64 idx = getIdx(ht, key);
    assert(idx >= 0);
    assert(idx <= (i64)ht->tableEntries);


    //Jump to the index
    htEntry_t* entry = ht->table[idx];

    //Traverse the linked list
    if_unlikely(entry == NULL){
        return;
    }

    //The linked is is not empty, check that the key is not already in it
    if_eqlikely(memcmp(key, &entry->key, sizeof(htKey_t)) == 0){
        ht->table[idx] = entry->next; //Reassign the table entry
        free(entry); //Free the old entry
        return;
    }

    //Keep checking and traversing
    htEntry_t* prev = entry;
    for(; entry->next; entry = entry->next){
        if_eqlikely(memcmp(key, &entry->key, sizeof(htKey_t)) == 0){
            prev->next = entry->next; //Reassign the links
            free(entry); //Free the old entry
            return;
        }
    }

    //Reached the end of the linked list without finding the key.
}

/**
 * Free all resources associated with the hashtable
 * @param ht
 */
void htDelete(ht_t* const ht, deleteCb_f deleteCb)
{
    //Look at each table entry
    for(uint64_t i = 0; i < ht->tableEntries; i++){
        //Free each linked list entry
        htEntry_t* entry = ht->table[i];
        if_eqlikely(entry == NULL){
            continue; //Nothing more to do here
        }

        //Free all list elements
        for(htEntry_t* entryNext = entry->next; entry != NULL; entry = entryNext ){
            entryNext = entry->next;
            if_likely(deleteCb != NULL){
                deleteCb(&entry->key, entry->value);
            }
            free(entry);
        }
    }

    free(ht->table);

    free(ht);
}



static char* errors[htECOUNT] = {
    "Success! No error",
    "No memory available",
    "Key is already in the table",
    "Key was not found in table",
};

//Convert a cqError number into a text description
const char* htError2Str(const htError_t err)
{
    if(err >= htECOUNT){
        return "Bad error number";
    }

    return errors[err];
}

