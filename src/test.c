#include <stdio.h>
#include "etcpSockApi.h"


//
//
//static i64 connect_(int fd, const struct sockaddr *address,socklen_t address_len)
//{
//
//}
//
//u64 etcp_bind(int __fd, __CONST_SOCKADDR_ARG __addr, socklen_t __len)
//{
//
//}
//
//
//u64 etcp_listen(int __fd, int __n)
//{
//
//}
//
//u64 etcp_accept(int __fd, __SOCKADDR_ARG __addr, socklen_t *__restrict __addr_len)
//{
//
//}
//
//u64 etcp_sendto(int __fd, const void *__buf, size_t __n, int __flags)
//{
//
//}
//
//
//
//u64 etcp_send(int __fd, const void *__buf, size_t __n, int __flags)
//{
//
//}
//
//
//
//u64 etcp_recv(int __fd, void *__buf, size_t __n, int __flags)
//{
//
//}
//u64 etcp_close(int __fd)
//{
//
//}

static etcpState_t* etcpState = NULL;

typedef struct  {
    int64_t foo;
} exaNicState_t;

exaNicState_t nicState;

int etcptpTestClient()
{
    //Open the connection
    etcpSocket_t* sock = etcpSocketNew(etcpState);
    etcpConnect(sock,16,2048,0x000001,0x00000F, 0x0000002, 0x00000E, true);

    //Write to the connection
    i8 dat[128] = {0};
    for(int i = 0; i < 128; i++){
        dat[i] = 0xAA + i;
    }
    etcpSend(sock,dat,128);

    //Close the connection
    etcpClose(sock);
}

int etcptpTestServer()
{
    //Open a socket and bind it
    etcpSocket_t* sock = etcpSocketNew(etcpState);
    etcpBind(sock,16,2048,0x000002,0x00000E);

    //Tell the socket to list
    etcpListen(sock,8);

    etcpSocket_t* accSock = NULL;
    for(etcpError_t accErr = etcpETRYAGAIN; accErr == etcpETRYAGAIN; accErr = etcpAccept(sock,&accSock)){
        sleep(1);
    }
    if(accErr != etcpENOERR){

    }


    i8 data

    for(etcpError_t accErr = etcpETRYAGAIN; accErr == etcpETRYAGAIN; accErr = etcpAccept(sock,&accSock)){
    for(int i = 0; i < 128; i++){
        dat[i] = 0xAA + i;
    }



}



typedef int64_t (*etcpRxTc_f)(void* const rxTcState, const cq_t* const datRxQ, const cq_t* const ackTxQ );

typedef void (*etcpTxTc_f)(void* const txTcState, const cq_t* const datTxQ, cq_t* ackTxQ, const cq_t* ackRxQ, bool* const ackFirst, i64* const maxAck_o, i64* const maxDat_o );



//The ETCP internal state expects to be provided with hardware send and receive operations, these typedefs spell them out
//A generic wrapper around the "hardware" tx layer
//Returns: >0, number of bytes transmitted =0, no send capacity, try again, <0 hw specific error code
int64_t exanicTx(void* const hwState, const void* const data, const int64_t len, uint64_t* const hwTxTimeNs )
{
    return 0;
}


//Returns: >0, number of bytes received, =0, nothing available right now, <0 hw specific error code
int64_t exanicRx(void* const hwState, const void* const data, const int64_t len, uint64_t* const hwRxTimeNs )
{
    return 0;
}






int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    if(argc < 2){
        printf("Usage test [client|server]");
        return -1;
    }

    etcpState = etcpStateNew(&nicState,exanicTx,exanicRx);

    if(argv[1][0] == "s"){
        return etcptpTestServer();
    }
    else if(argv[1][0] == "c"){
        return etcptpTestClient();
    }

    return -1;

}
