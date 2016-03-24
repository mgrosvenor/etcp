#include <stdio.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/socket.h>

#include "CircularQueue.h"
#include "types.h"



//Bit field describing the types of this message
enum {
    UNCO_ERR = 0x00, //Not a valid message, something wrong
    UNCO_CON = 0x01, //Start a new connection
    UNCO_FIN = 0x02, //Is the last packet on this connection
    UNCO_DAT = 0x04, //Contains valid data fields
    UNCO_ACK = 0x08, //Contains valid acknowledgement fields
    UNCO_DEN = 0x10, //Connection denied
};


typedef struct __attribute__((packed)){
    i64 start;
    i64 stop;
} sackField_t;


#define UNCO_MAX_SACK 5
typedef struct __attribute__((packed)){
    sackField_t sacks[UNCO_MAX_SACK];
} sack_t;

typedef struct __attribute__((packed)){
    i64 seqNum;
    i64 datLen;
} dat_t;


//Assumes a fast layer 2 network (10G plus), with reasonable latency. In this case, sending many more bits, is better than
//sending many more packets at higher latency
typedef struct __attribute__((packed)){
    i64 typeFlags;  //64 different message type flags, allows a message to be a CON, DAT, FIN and ACK all in 1.
    i64 src;        //Port, ip address, whatever
    i64 dst;        //Port, ip address, whatever
    i64 timeNs;     //Unix time in ns, used for RTT estimation
    sack_t sacks;   //All of the selective acknowledgements
    dat_t data;     //This MUST come last,dfata follows
} uncoMsgHead_t;

_Static_assert(sizeof(uncoMsgHead_t) == 128, "The UnCo mesage header is assumed to be 128 bytes");


typedef struct {
    i64 seq;

} unco_t;


static i64 handle_packet(unco_t* unco, const int8_t* packet, const i64 len)
{
    if(len < sizeof(uncoMsgHead_t)){
        return -1; //Bad packet, not enough data in it
    }

    uncoMsgHead_t* msg =

}




static i64 connect_(int fd, const struct sockaddr *address,socklen_t address_len)
{

}

u64 unco_bind(int __fd, __CONST_SOCKADDR_ARG __addr, socklen_t __len)
{

}


u64 unco_listen(int __fd, int __n)
{

}

u64 unco_accept(int __fd, __SOCKADDR_ARG __addr, socklen_t *__restrict __addr_len)
{

}

u64 unco_sendto(int __fd, const void *__buf, size_t __n, int __flags)
{

}



u64 unco_send(int __fd, const void *__buf, size_t __n, int __flags)
{

}



u64 unco_recv(int __fd, void *__buf, size_t __n, int __flags)
{

}



void uncotpTestClient()
{
    //Open the connection


    //Write to the connection


    //Close the connection

}

void uncotpTestServer()
{

}




int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    printf("I love cheees\n");
    return 0;
}
