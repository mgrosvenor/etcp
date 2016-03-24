/*
 * Copyright (c) 2016, All rights reserved.
 * See LICENSE.txt for full details. 
 * 
 *  Created:   24 Mar 2016
 *  File name: CircularQueueTest.c
 *  Description:
 *  <INSERT DESCRIPTION HERE> 
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include <stdint.h>

#include "CircularQueue.h"

#define CQ_ASSERT(p) do { if(!(p)) { fprintf(stdout, "Error in %s: failed assertion \""#p"\" on line %u\n", __FUNCTION__, __LINE__); result = 0; } } while(0)

//Basic test allocate and free, should pass the valgrind and addresssanitizer checks
bool test1()
{
    bool result = true;
    cq_t* cq = cqNew(17,5);
    CQ_ASSERT(cq != NULL);
    cqDelete(cq);
    return result;
}


//Basic check of read buffer
bool test2()
{
    bool result = true;
    cq_t* cq = cqNew(17,5);

    cqSlot_t* slot = NULL;
    i64 idx = -1;
    cqError_t err = cqENOERR;
    err = cqGetNextWr(cq,&slot,&idx);
    CQ_ASSERT(err == cqENOERR);
    CQ_ASSERT(slot != NULL);
    CQ_ASSERT(idx == 0);
    CQ_ASSERT(cq->wrIdx == 1);

    err = cqReleaseSlotWr(cq,idx);
    CQ_ASSERT(err == cqENOERR);
    CQ_ASSERT(cq->wrIdx == 0);

    cqDelete(cq);
    return result;
}

//Check that I can reserve read buffers until they run out, then release them then reserve again.
bool test3()
{
    bool result = true;
    const i64 total = 5;
    cqError_t err = cqENOERR;
    cq_t* cq = cqNew(17,total);

    for(int test = 0; test < 5; test++){
        for(int i = 0; i < total; i++){
            cqSlot_t* slot = NULL;
            i64 idx = -1;
            err = cqGetNextWr(cq,&slot,&idx);
            CQ_ASSERT(err == cqENOERR);
            CQ_ASSERT(slot != NULL);
            CQ_ASSERT(idx == i);
            CQ_ASSERT(cq->wrIdx == i +1 || cq->wrIdx == 0 );
        }

        cqSlot_t* slot = NULL;
        i64 idx = -1;
        err = cqGetNextWr(cq,&slot,&idx);
        CQ_ASSERT(err == cqENOSLOT);
        CQ_ASSERT(slot == NULL);
        CQ_ASSERT(idx == -1);
        CQ_ASSERT(cq->wrIdx == 0 );


        for(int i = total - 1; i >= 0; i--){
            err = cqReleaseSlotWr(cq,i);
            CQ_ASSERT(err == cqENOERR);
            CQ_ASSERT(cq->wrIdx == i );
        }
    }
    cqDelete(cq);
    return result;
}


bool test4()
{
    #define datalen 7
    bool result = true;
    const i64 total = 5;
    cqError_t err = cqENOERR;
    cq_t* cq = cqNew(datalen,total);
    const char data[datalen] = "123456";
    i64 len = datalen;

    for(int i = 0; i < total; i++){
        err = cQPushNext(cq,(i8*)data,&len);
        CQ_ASSERT(err == cqENOERR);
        CQ_ASSERT(cq->wrIdx == i +1 || cq->wrIdx == 0 );
    }

    cqSlot_t* slot = NULL;
    i64 idx = -1;
    err = cqGetNextWr(cq,&slot,&idx);
    CQ_ASSERT(err == cqENOSLOT);
    CQ_ASSERT(slot == NULL);
    CQ_ASSERT(idx == -1);
    CQ_ASSERT(cq->wrIdx == 0 );


    for(int i = total - 1; i >= 0; i--){
        err = cqReleaseSlotWr(cq,i);
        CQ_ASSERT(err == cqEWRONGSLOT);
    }

    cqDelete(cq);
    return result;
}


bool test5()
{
    #define datalen 7
    bool result = true;
    const i64 total = 5;
    cqError_t err = cqENOERR;
    cq_t* cq = cqNew(datalen,total);
    const char data[datalen] = "123456";
    i64 len = datalen;

    for(int i = 0; i < total; i++){
        err = cQPushNext(cq,(i8*)data,&len);
        CQ_ASSERT(err == cqENOERR);
        CQ_ASSERT(cq->wrIdx == i +1 || cq->wrIdx == 0 );
    }

    cqSlot_t* slot = NULL;
    i64 idx = -1;
    err = cqGetNextWr(cq,&slot,&idx);
    CQ_ASSERT(err == cqENOSLOT);
    CQ_ASSERT(slot == NULL);
    CQ_ASSERT(idx == -1);
    CQ_ASSERT(cq->wrIdx == 0 );


    i8 readbuff[8];
    for(int i = 0; i < total; i++){
        err = cqPullNext(cq,(i8*)readbuff,&len);
        CQ_ASSERT(err == cqENOERR);
        CQ_ASSERT(cq->rdIdx == i +1 || cq->rdIdx == 0 );
    }

    err = cqPullNext(cq,(i8*)readbuff,&len);
    CQ_ASSERT(err == cqENOSLOT);
    CQ_ASSERT(slot == NULL);
    CQ_ASSERT(idx == -1);
    CQ_ASSERT(cq->wrIdx == 0 );



    cqDelete(cq);
    return result;
}


int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    i64 test_pass = 0;
    printf("CH Data Structures: Array Test 01: ");  printf("%s", (test_pass = test1()) ? "PASS\n" : "FAIL\n"); if(!test_pass) return 1;
    printf("CH Data Structures: Array Test 02: ");  printf("%s", (test_pass = test2()) ? "PASS\n" : "FAIL\n"); if(!test_pass) return 1;
    printf("CH Data Structures: Array Test 03: ");  printf("%s", (test_pass = test3()) ? "PASS\n" : "FAIL\n"); if(!test_pass) return 1;
    printf("CH Data Structures: Array Test 04: ");  printf("%s", (test_pass = test4()) ? "PASS\n" : "FAIL\n"); if(!test_pass) return 1;
    printf("CH Data Structures: Array Test 05: ");  printf("%s", (test_pass = test5()) ? "PASS\n" : "FAIL\n"); if(!test_pass) return 1;
    return 0;
}
