/*
 * Copyright (c) 2016, All rights reserved.
 * See LICENSE.txt for full details. 
 * 
 *  Created:   1 Apr 2016
 *  File name: etcpState.c
 *  Description:
 *  <INSERT DESCRIPTION HERE> 
 */

#include <stdlib.h>
#include <stddef.h>

#include "etcpState.h"
#include "etcpConn.h"
#include "utils.h"
#include "debug.h"

void connHTDelete(const htKey_t* const key, void* const value)
{
    DBG("Deleting conn for srcA=%li srcP=%li\n", key->keyHi, key->keyLo);
    etcpConn_t* const conn = (etcpConn_t* const)(value);
    etcpConnDelete(conn);
}


void srcsMapDelete(etcpLAMap_t* const srcConns)
{
    if_unlikely(!srcConns){
        return;
    }

    if_likely(srcConns->listenQ != NULL){
        cqDelete(srcConns->listenQ);
    }

    if_likely(srcConns->table != NULL){
        htDelete(srcConns->table,connHTDelete);
    }
}


etcpLAMap_t* srcsMapNew( const uint32_t listenWindowSize, const uint32_t listenBuffSize)
{
    etcpLAMap_t* const srcConns = (etcpLAMap_t* const )calloc(1,sizeof(etcpLAMap_t));
    if_unlikely(!srcConns){
        return NULL;
    }

    srcConns->table = htNew(SRC_TAB_MAX_LOG2);
    if_likely(!srcConns->table){
        srcsMapDelete(srcConns);
        return NULL;
    }

    srcConns->listenWindowSize = listenWindowSize;
    srcConns->listenBuffSize   = listenBuffSize;

    return srcConns;

}


void srcConnsHTDelete(const htKey_t* const key, void* const value)
{
    DBG("HT deleting src cons for dstA=%li dstP=%li\n", key->keyHi, key->keyLo);
    etcpLAMap_t* const srcConns = (etcpLAMap_t* const)value;
    srcsMapDelete(srcConns);
}


void deleteEtcpState(etcpState_t* etcpState)
{
    if_unlikely(!etcpState){
        return;
    }

    if_likely(etcpState->dstMap != NULL){
        htDelete(etcpState->dstMap,srcConnsHTDelete);
    }

    free(etcpState);
}


etcpState_t* etcpStateNew(void* const ethHwState, const ethHwTx_f ethHwTx, const ethHwRx_f ethHwRx)
{
    etcpState_t* etcpState = calloc(1,sizeof(etcpState_t));
    if_unlikely(!etcpState){
        return NULL;
    }

    etcpState->ethHwRx    = ethHwRx;
    etcpState->ethHwTx    = ethHwTx;
    etcpState->ethHwState = ethHwState;


    etcpState->dstMap = htNew(DST_TAB_MAX_LOG2);
    if_unlikely(!etcpState->dstMap){
        deleteEtcpState(etcpState);
        return NULL;
    }

    return etcpState;
}
