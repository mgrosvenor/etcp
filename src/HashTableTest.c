/*
 * Copyright (c) 2016, All rights reserved.
 * See LICENSE.txt for full details. 
 * 
 *  Created:   31 Mar 2016
 *  File name: HashTableTest.c
 *  Description:
 *  Some very basic sanity checks for the hash table data structure
 */
#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

#include "HashTable.h"

#define HT_ASSERT(p) do { if(!(p)) { fprintf(stdout, "Error in %s: failed assertion \""#p"\" on line %u\n", __FUNCTION__, __LINE__); result = 0; return result; } } while(0)

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
    htEntry_t* table;
} ht_t;


bool test1()
{
    bool result = true;
    ht_t* ht = htNew(16);
    HT_ASSERT( ht->tableEntriesLog2 == 16);
    HT_ASSERT( ht->tableEntries == 65536);
    HT_ASSERT( ht->table);
    htDelete(ht);

    return result;
}


bool test2()
{
    bool result = true;
    ht_t* ht = htNew(16);
    htKey_t key = { .keyLo = 1, .keyHi = 1 };
    i64 value = 3;
    htError_t  err = htAddNew(ht,&key,&value);
    HT_ASSERT(err == htENOEROR);

    htDelete(ht);

    return result;
}

bool test3()
{
    bool result = true;
    ht_t* ht = htNew(16);
    htKey_t key = { .keyLo = 1, .keyHi = 1 };
    i64 value = 3;
    htError_t  err = htAddNew(ht,&key,&value);
    HT_ASSERT(err == htENOEROR);

    i64* valueOut = NULL;
    err = htGet(ht,&key,(void**)&valueOut);
    HT_ASSERT(err == htENOEROR);

    HT_ASSERT( valueOut = &value);
    HT_ASSERT( *valueOut = value);

    htDelete(ht);

    return result;
}


bool test4()
{
    bool result = true;
    ht_t* ht = htNew(16);
    htKey_t key = { .keyLo = 1, .keyHi = 1 };
    i64 value = 3;
    htError_t  err = htAddNew(ht,&key,&value);
    HT_ASSERT(err == htENOEROR);

    i64* valueOut = NULL;
    err = htGet(ht,&key,(void**)&valueOut);
    HT_ASSERT(err == htENOEROR);

    HT_ASSERT( valueOut = &value);
    HT_ASSERT( *valueOut = value);

    htRem(ht,&key);


    htDelete(ht);

    return result;
}



int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    i64 test_pass = 0;
    printf("ETCP Data Structures: Hash Table Test 01: ");  printf("%s", (test_pass = test1()) ? "PASS\n" : "FAIL\n"); if(!test_pass) return 1;
    printf("ETCP Data Structures: Hash Table Test 02: ");  printf("%s", (test_pass = test2()) ? "PASS\n" : "FAIL\n"); if(!test_pass) return 1;
    printf("ETCP Data Structures: Hash Table Test 03: ");  printf("%s", (test_pass = test3()) ? "PASS\n" : "FAIL\n"); if(!test_pass) return 1;
    printf("ETCP Data Structures: Hash Table Test 04: ");  printf("%s", (test_pass = test4()) ? "PASS\n" : "FAIL\n"); if(!test_pass) return 1;
    return 0;
}
