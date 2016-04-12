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

#include "CircularQueue.h"

#define CQ_ASSERT(p) do { if(!(p)) { fprintf(stdout, "Error in %s: failed assertion \""#p"\" on line %u\n", __FUNCTION__, __LINE__); result = 0; return result; } } while(0)

//Basic test allocate and free, should pass the valgrind and addresssanitizer checks
bool test1()
{
    bool result = true;
    cq_t* cq = cqNew(17,2);
    CQ_ASSERT(cq != NULL);
    cqDelete(cq);
    return result;
}


//Basic check of read buffer
bool test2()
{
    bool result = true;
    cq_t* cq = cqNew(17,2);

    cqSlot_t* slot = NULL;
    i64 seq = -1;
    cqError_t err = cqENOERR;
    err = cqGetNextWr(cq,&slot,&seq);
    CQ_ASSERT(err == cqENOERR);
    CQ_ASSERT(slot != NULL);
    CQ_ASSERT(seq == 0);
    CQ_ASSERT(cq->wrSeq == 0);

    cqDelete(cq);
    return result;
}

//Check that I can use buffers until they run out, then release them then reserve again.
bool test3()
{
    bool result = true;
    const i64 total = 4;
    cqError_t err = cqENOERR;
    cq_t* cq = cqNew(17,2);

    i64 wrSeqNum = 0;
    i64 rdSeqNum = 0;
    for(int test = 0; test < total; test++){
        for(int i = 0; i < total; i++, wrSeqNum++){
            cqSlot_t* slot = NULL;
            i64 seq = -1;
            err = cqGetNextWr(cq,&slot,&seq);
            CQ_ASSERT(err == cqENOERR);
            CQ_ASSERT(slot != NULL);
            CQ_ASSERT(cq->wrSeq == wrSeqNum );
            CQ_ASSERT(slot->valid == false);

            err = cqCommitSlot(cq,seq,slot->len);
            CQ_ASSERT(cq->wrSeq == wrSeqNum + 1);
            CQ_ASSERT(slot->valid == true);
        }

        cqSlot_t* slot = NULL;
        i64 seq = -1;
        err = cqGetNextWr(cq,&slot,&seq);
        CQ_ASSERT(err == cqENOSLOT);
        CQ_ASSERT(slot == NULL);
        CQ_ASSERT(seq == -1);
        CQ_ASSERT(cq->wrSeq == wrSeqNum );


        for(int i = 0; i  < total; i++, rdSeqNum++){
            err = cqReleaseSlot(cq,rdSeqNum);
            CQ_ASSERT(err == cqENOERR);
            CQ_ASSERT(cq->rdSeq == rdSeqNum + 1 );
        }
    }
    cqDelete(cq);
    return result;
}


bool test4()
{
    #define datalen 7
    bool result = true;
    const i64 total = 4;
    cqError_t err = cqENOERR;
    cq_t* cq = cqNew(datalen,2);
    const char data[datalen] = "123456";
    i64 len = datalen;

    for(int i = 0; i < total; i++){
        i64 idx = -1;
        err = cqPushNext(cq,(i8*)data,&len,&idx);
        CQ_ASSERT(err == cqENOERR);
        err = cqCommitSlot(cq,idx,len);
        CQ_ASSERT(cq->wrSeq == i +1 );
        CQ_ASSERT(err == cqENOERR);
    }

    cqSlot_t* slot = NULL;
    i64 idx = -1;
    err = cqGetNextWr(cq,&slot,&idx);
    CQ_ASSERT(err == cqENOSLOT);
    CQ_ASSERT(slot == NULL);
    CQ_ASSERT(idx == -1);

    int i = 0;
    for(; i < total; i++){
        err = cqReleaseSlot(cq,i);
        CQ_ASSERT(err == cqENOERR);
    }

    err = cqReleaseSlot(cq,i);
    CQ_ASSERT(err == cqEWRONGSLOT);

    cqDelete(cq);
    return result;
}


bool test5()
{
    #define datalen 7
    bool result = true;
    const i64 total = 4;
    cqError_t err = cqENOERR;
    cq_t* cq = cqNew(datalen,2);
    const char data[datalen] = "123456";
    i64 len = datalen;

    for(int i = 0; i < total; i++){
        i64 seq = -1;
        err = cqPushNext(cq,(i8*)data,&len,&seq);
        CQ_ASSERT(err == cqENOERR);
        err = cqCommitSlot(cq,seq,len);
        CQ_ASSERT(cq->wrSeq == i +1 );
        CQ_ASSERT(err == cqENOERR);
    }

    cqSlot_t* slot = NULL;
    i64 seq = -1;
    err = cqGetNextWr(cq,&slot,&seq);
    CQ_ASSERT(err == cqENOSLOT);
    CQ_ASSERT(slot == NULL);


    i8 readbuff[8] = {0};
    for(int i = 0; i < total; i++){
        err = cqPullNext(cq,readbuff,&len, &seq);
        CQ_ASSERT(err == cqENOERR);
        CQ_ASSERT(memcmp(readbuff, data,len) == 0);
        err = cqReleaseSlot(cq,seq);
        CQ_ASSERT(cq->rdSeq == i +1 );
    }

    seq = -1;
    err = cqPullNext(cq,readbuff,&len,&seq);
    CQ_ASSERT(err == cqENOSLOT);
    CQ_ASSERT(slot == NULL);

    cqDelete(cq);
    return result;
}


int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    i64 test_pass = 0;
    printf("ETCP Data Structures: Circular Queue Test 01: ");  printf("%s", (test_pass = test1()) ? "PASS\n" : "FAIL\n"); if(!test_pass) return 1;
    printf("ETCP Data Structures: Circular Queue Test 02: ");  printf("%s", (test_pass = test2()) ? "PASS\n" : "FAIL\n"); if(!test_pass) return 1;
    printf("ETCP Data Structures: Circular Queue Test 03: ");  printf("%s", (test_pass = test3()) ? "PASS\n" : "FAIL\n"); if(!test_pass) return 1;
    printf("ETCP Data Structures: Circular Queue Test 04: ");  printf("%s", (test_pass = test4()) ? "PASS\n" : "FAIL\n"); if(!test_pass) return 1;
    printf("ETCP Data Structures: Circular Queue Test 05: ");  printf("%s", (test_pass = test5()) ? "PASS\n" : "FAIL\n"); if(!test_pass) return 1;
    return 0;
}
