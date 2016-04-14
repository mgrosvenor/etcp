/*
 * Copyright (c) 2016, All rights reserved.
 * See LICENSE.txt for full details. 
 * 
 *  Created:   24 Mar 2016
 *  File name: CircularQueueTest.c
 *  Description:
 *  Some very basic sanity checks for the queue structure
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "LinkedList.h"

#define LL_ASSERT(p) do { if(!(p)) { fprintf(stdout, "Error in %s: failed assertion \""#p"\" on line %u\n", __FUNCTION__, __LINE__); result = 0; return result; } } while(0)

//Basic test allocate and free, should pass the valgrind and addresssanitizer checks
bool test1()
{
    bool result = true;
    ll_t* ll = llNew(17);
    LL_ASSERT(ll != NULL);
    llDelete(ll);
    return result;
}


//Basic check of insert and free
bool test2()
{
    #define datalen 7
    i64 len = datalen;
    bool result = true;
    llError_t err = llENOERR;
    ll_t* ll = llNew(datalen);
    const char data[datalen] = "123456";


    err = llPushSeqOrd(ll,data,&len,5432);
    LL_ASSERT(len == datalen);
    LL_ASSERT(err == llENOERR);
    LL_ASSERT(ll->__head);
    LL_ASSERT(ll->slotCount == 1);

    llReleaseHead(ll);
    LL_ASSERT(ll->slotCount == 0);


    llDelete(ll);
    return result;
}

bool test3()
{
#define datalen 7
    i64 len = datalen;
    bool result = true;
    llError_t err = llENOERR;
    ll_t* ll = llNew(datalen);
    const char data[datalen] = "123456";


    for(int i = 0; i < 10; i++){
        err = llPushSeqOrd(ll,data,&len,5432);
        LL_ASSERT(err == llENOERR);
        LL_ASSERT(ll->slotCount == i + 1);
    }

    for(int i = 0; i < 10; i++){
        LL_ASSERT(ll->slotCount == 10-i);
        llReleaseHead(ll);
    }

    llDelete(ll);
    return result;

}

bool test4()
{
#define datalen 7
    i64 len = datalen;
    bool result = true;
    llError_t err = llENOERR;
    ll_t* ll = llNew(datalen);
    const char data[datalen] = "123456";
    i64 keys[6]  = {7,3,5,9,1,2};
    i64 keysR[6][6] = {
            {7, 0, 0, 0, 0, 0 },
            {3, 7, 0, 0, 0, 0 },
            {3, 5, 7, 0, 0, 0 },
            {3, 5, 7, 9, 0, 0 },
            {1, 3, 5, 7, 9, 0 },
            {1, 2, 3, 5, 7, 9 }
    };


    for(int i = 0; i < 6; i++){
        err = llPushSeqOrd(ll,data,&len,keys[i]);
        LL_ASSERT(err == llENOERR);
        LL_ASSERT(ll->slotCount == i + 1);
        llSlot_t* slot = NULL;
        err = llGetFirst(ll,&slot);
        LL_ASSERT(err == llENOERR);
        LL_ASSERT(slot->seqNum = keysR[i][0]);
        for(int j = 1; j <= i; j++ ){
            err = llGetNext(ll,&slot);
            LL_ASSERT(err == llENOERR);
            LL_ASSERT(slot->seqNum = keysR[i][j]);
        }
    }

    for(int i = 0; i < 6; i++){
        LL_ASSERT(ll->slotCount == 6-i);
        llReleaseHead(ll);
    }

    llDelete(ll);
    return result;
}
//
//
//bool test5()
//{
//    #define datalen 7
//    bool result = true;
//    const i64 total = 4;
//    llError_t err = llENOERR;
//    ll_t* ll = llNew(datalen,2);
//    const char data[datalen] = "123456";
//    i64 len = datalen;
//
//    for(int i = 0; i < total; i++){
//        i64 seq = -1;
//        err = llPushNext(ll,(i8*)data,&len,&seq);
//        ll_ASSERT(err == llENOERR);
//        err = llCommitSlot(ll,seq,len);
//        ll_ASSERT(ll->wrSeq == i +1 );
//        ll_ASSERT(err == llENOERR);
//    }
//
//    llSlot_t* slot = NULL;
//    i64 seq = -1;
//    err = llGetNextWr(ll,&slot,&seq);
//    ll_ASSERT(err == llENOSLOT);
//    ll_ASSERT(slot == NULL);
//
//
//    i8 readbuff[8] = {0};
//    for(int i = 0; i < total; i++){
//        err = llPullNext(ll,readbuff,&len, &seq);
//        ll_ASSERT(err == llENOERR);
//        ll_ASSERT(memcmp(readbuff, data,len) == 0);
//        err = llReleaseSlot(ll,seq);
//        ll_ASSERT(ll->rdSeq == i +1 );
//    }
//
//    seq = -1;
//    err = llPullNext(ll,readbuff,&len,&seq);
//    ll_ASSERT(err == llENOSLOT);
//    ll_ASSERT(slot == NULL);
//
//    llDelete(ll);
//    return result;
//}


int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    i64 test_pass = 0;
    printf("ETCP Data Structures: Circular Queue Test 01: ");  printf("%s", (test_pass = test1()) ? "PASS\n" : "FAIL\n"); if(!test_pass) return 1;
    printf("ETCP Data Structures: Circular Queue Test 02: ");  printf("%s", (test_pass = test2()) ? "PASS\n" : "FAIL\n"); if(!test_pass) return 1;
    printf("ETCP Data Structures: Circular Queue Test 03: ");  printf("%s", (test_pass = test3()) ? "PASS\n" : "FAIL\n"); if(!test_pass) return 1;
    printf("ETCP Data Structures: Circular Queue Test 04: ");  printf("%s", (test_pass = test4()) ? "PASS\n" : "FAIL\n"); if(!test_pass) return 1;
//    printf("ETCP Data Structures: Circular Queue Test 05: ");  printf("%s", (test_pass = test5()) ? "PASS\n" : "FAIL\n"); if(!test_pass) return 1;
    return 0;
}
