/*
 * Copyright (c) 2015, All rights reserved.
 * See LICENSE.txt for full details. 
 * 
 *  Created:    Jul 30, 2015
 *  File name:  debug.h
 *  Description:
 *  <INSERT DESCRIPTION HERE> 
 */
#ifndef DEBUG_H_
#define DEBUG_H_

#include "types.h"
#include <stdbool.h>

typedef enum {
    DBGMODE_DBG,
    DBGMODE_ERR,
    DBGMODE_WARN,
    DBGMODE_FAT,
} ch_dbg_mode_e;


i64 _debug_out_(
        bool info,
        ch_dbg_mode_e mode,
        i64 line_num,
        const char* filename,
        const char* function,
        const char* format, ... );

#define FAT( /*format, args*/...)  _fat_helper(__VA_ARGS__, "")
#define _fat_helper(format, ...) _debug_out_(true, DBGMODE_FAT, __LINE__, __FILE__, __FUNCTION__, format, __VA_ARGS__ )
#define ERR( /*format, args*/...)  _err_helper(__VA_ARGS__, "")
#define _err_helper(format, ...) _debug_out_(true, DBGMODE_ERR, __LINE__, __FILE__, __FUNCTION__, format, __VA_ARGS__ )
#define ERR2( /*format, args*/...)  _err_helper(__VA_ARGS__, "")
#define camio_err_helper2(format, ...) _debug_out_(false, DBGMODE_ERR, __LINE__, __FILE__, __FUNCTION__, format, __VA_ARGS__ )
#define WARN( /*format, args*/...)  _debug_helper3(__VA_ARGS__, "")
#define _debug_helper3(format, ...) _debug_out_(true,DBGMODE_WARN,__LINE__, __FILE__, __FUNCTION__, format, __VA_ARGS__ )

#ifndef NDEBUG
    #define DBG( /*format, args*/...)  _debug_helper(__VA_ARGS__, "")
    #define _debug_helper(format, ...) _debug_out_(true,DBGMODE_DBG,__LINE__, __FILE__, __FUNCTION__, format, __VA_ARGS__ )
    #define DBG2( /*format, args*/...)  _debug_helper2(__VA_ARGS__, "")
    #define _debug_helper2(format, ...) _debug_out_(false,DBGMODE_DBG,__LINE__, __FILE__, __FUNCTION__, format, __VA_ARGS__ )
#else
    #define DBG( /*format, args*/...)
    #define DBG2( /*format, args*/...)
#endif

void hexdump(const void *data, int size);


#endif /* DEBUG_H_ */
