/*
 * Copyright (c) 2016, All rights reserved.
 * See LICENSE.txt for full details. 
 * 
 *  Created:   31 Mar 2016
 *  File name: HashTableTest.c
 *  Description:
 *  <INSERT DESCRIPTION HERE> 
 */
#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

#include "HashTable.h"

#define HT_ASSERT(p) do { if(!(p)) { fprintf(stdout, "Error in %s: failed assertion \""#p"\" on line %u\n", __FUNCTION__, __LINE__); result = 0; } } while(0)

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


int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    i64 test_pass = 0;
    printf("CH Data Structures: Array Test 01: ");  printf("%s", (test_pass = test1()) ? "PASS\n" : "FAIL\n"); if(!test_pass) return 1;
//    printf("CH Data Structures: Array Test 02: ");  printf("%s", (test_pass = test2()) ? "PASS\n" : "FAIL\n"); if(!test_pass) return 1;
//    printf("CH Data Structures: Array Test 03: ");  printf("%s", (test_pass = test3()) ? "PASS\n" : "FAIL\n"); if(!test_pass) return 1;
//    printf("CH Data Structures: Array Test 04: ");  printf("%s", (test_pass = test4()) ? "PASS\n" : "FAIL\n"); if(!test_pass) return 1;
//    printf("CH Data Structures: Array Test 05: ");  printf("%s", (test_pass = test5()) ? "PASS\n" : "FAIL\n"); if(!test_pass) return 1;
    return 0;
}
