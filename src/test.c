#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

#include <exanic/exanic.h>
#include <exanic/fifo_rx.h>
#include <exanic/fifo_tx.h>
#include <exanic/time.h>



#include "src/etcpSockApi.h"
#include "src/debug.h"



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
    exanic_t* dev;
    exanic_rx_t* rxBuff;
    exanic_tx_t* txBuff;
} exaNicState_t;
exaNicState_t nicState;


typedef struct {
    i64 foo;
} tcState_t;
tcState_t tcState;

int etcptpTestClient()
{
    //Open the connection
    etcpSocket_t* sock = etcpSocketNew(etcpState);
    etcpConnect(sock,16,2048,0x000001,0x00000F, 0x0000002, 0x00000E, true);

    //Write to the connection
    i64 len = 128;
    i8 dat[len];
    for(int i = 0; i < len; i++){
        dat[i] = 0xAA + i;
    }
    etcpSend(sock,dat,&len);

    //Close the connection
    etcpClose(sock);

    return 0;
}

int etcptpTestServer()
{
    //Open a socket and bind it
    etcpSocket_t* sock = etcpSocketNew(etcpState);
    etcpBind(sock,16,2048,0x000002,0x00000E);

    //Tell the socket to list
    etcpListen(sock,8);

    etcpSocket_t* accSock = NULL;
    etcpError_t accErr = etcpETRYAGAIN;
    for( accErr = etcpETRYAGAIN; accErr == etcpETRYAGAIN; accErr = etcpAccept(sock,&accSock)){
        sleep(1);
    }
    if(accErr != etcpENOERR){
        ERR("Something borke on accept!\n");
        return -1;
    }

    i8 data[128] = {0};
    i64 len = -1;
    etcpError_t recvErr = etcpETRYAGAIN;
    for(recvErr = etcpETRYAGAIN; recvErr == etcpETRYAGAIN; recvErr = etcpRecv(sock,&data,&len)){
        sleep(1);
    }

    if(recvErr != etcpENOERR){
        ERR("Something borke on accept!\n");
        return -1;
    }


    DBG("Success!\n");
    for(int i = 0; i < 128; i++){
        printf("%i 0x%02x\n", i, data[i]);
    }

    //Close the connection
    etcpClose(sock);

    return 0;

}


//Returns: >0 this is the number of acknowledgement packets that can be generated. <=0 no ack packets will be generated
int64_t etcpRxTc(void* const rxTcState, const cq_t* const datRxQ, const cq_t* const ackTxQ )
{
    DBG("RX TC Called!\n");
    (void)rxTcState;
    (void)datRxQ;
    (void)ackTxQ;
    return 1;
}

void etcpTxTc(void* const txTcState, const cq_t* const datTxQ, cq_t* ackTxQ, const cq_t* ackRxQ, bool* const ackFirst, i64* const maxAck_o, i64* const maxDat_o )
{
    (void)txTcState;
    (void)datTxQ;
    (void)ackTxQ;
    (void)ackRxQ;
    (void)ackFirst;
    (void)maxAck_o;
    (void)maxDat_o;
    DBG("TX TC Called!\n");
}



//The ETCP internal state expects to be provided with hardware send and receive operations, these typedefs spell them out
//A generic wrapper around the "hardware" tx layer
//Returns: >0, number of bytes transmitted =0, no send capacity, try again, <0 hw specific error code
static int64_t exanicTx(void* const hwState, const void* const data, const int64_t len, uint64_t* const hwTxTimeNs )
{
    exaNicState_t* const exaNicState = hwState;

    ssize_t result = exanic_transmit_frame(exaNicState->txBuff,(const char*)data, len);
    const uint32_t txTimeCyc = exanic_get_tx_timestamp(exaNicState->txBuff);
    *hwTxTimeNs = exanic_timestamp_to_counter(exaNicState->dev, txTimeCyc);
    return result;
}


//Returns: >0, number of bytes received, =0, nothing available right now, <0 hw specific error code
static int64_t exanicRx(void* const hwState, void* const data, const int64_t len, uint64_t* const hwRxTimeNs )
{
    exaNicState_t* const exaNicState = hwState;
    uint32_t rxTimeCyc = -1;
    ssize_t result = exanic_receive_frame(exaNicState->rxBuff, (char*)data, len, &rxTimeCyc);
    *hwRxTimeNs = exanic_timestamp_to_counter(exaNicState->dev, rxTimeCyc);
    return result;
}



static inline void exanicInit(exaNicState_t* const nicState, const char* exanicDev, const int exanicPort)
{
    nicState->dev = exanic_acquire_handle(exanicDev);
    if(!nicState->dev ){
        ERR("Could not get handle for device %s\n", exanicDev);
        return;
    }


    const int rxBuffNo = 0;
    nicState->rxBuff = exanic_acquire_rx_buffer(nicState->dev, exanicPort, rxBuffNo);
    if(!nicState->rxBuff ){
        ERR("Could not get handle for rx buffer %s:%i:%i\n", exanicDev, exanicPort, rxBuffNo);
        return;
    }


    const int txBuffNo = 0;
    const int reqMult = 4;
    nicState->txBuff = exanic_acquire_tx_buffer(nicState->dev, exanicPort, reqMult * 4096);
    if(!nicState->dev ){
        ERR("Could not get handle for tx buffer %s:%i:%i\n", exanicDev, exanicPort, txBuffNo);
        return;
    }

}


int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    if(argc < 4){
        printf("Usage test [client|server] exanic_device exanic_port");
        return -1;
    }

    if(argv[1][0] == 's'){
        return etcptpTestServer();
    }
    else if(argv[1][0] == 'c'){
        return etcptpTestClient();
    }

    const char* const exanicDev = argv[2];
    const int exanicPort = strtol(argv[3],NULL,10);

    exanicInit(&nicState, exanicDev, exanicPort);
    etcpState = etcpStateNew(&nicState,exanicTx,exanicRx,etcpTxTc,&tcState,true,etcpRxTc,&tcState,true);


    return -1;

}
