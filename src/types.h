/*
 * Copyright (c) 2016, All rights reserved.
 * See LICENSE.txt for full details. 
 * 
 *  Created:   24 Mar 2016
 *  File name: types.h
 *  Description:
 *  <INSERT DESCRIPTION HERE> 
 */


#ifndef SRC_TYPES_H_
#define SRC_TYPES_H_

#include <stdint.h>

//Reduce typing a little, the unsigned ints are purposely not defined, these should be used sparingly, only when necessary
typedef int8_t  i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;


typedef enum {
    etcpENOERR,       //Success!
    etcpENOMEM,       //Ran out of memory
    etcpEBADPKT,      //Bad packet, not enough bytes for a header
    etcpEALREADY,     //Already connected!
    etcpETOOMANY,     //Too many connections, we've run out!
    etcpENOTCONN,     //Not connected to anything
    etcpECQERR,       //Some issue with a Circular Queue
    etcpEHTERR,       //Some issue with a Hash Table
    etcpERANGE,       //Out of range
    etcpETOOBIG,      //The payload is too big for this buffer
    etcpETRYAGAIN,    //There's nothing to see here, come back again
    etcpEWRONGSOCK,   //Operation on the wrong socket type
    etcpEFATAL,       //Something irrecoverably bad happened! Stop the world!
    etcpEREJCONN,     //Rejecting this connection
} etcpError_t;


#endif /* SRC_TYPES_H_ */
