#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <assert.h>
#include "stcp_client.h"
#include "debug.h"
#include "../topology/topology.h"

client_tcb_t *tcb_table[MAX_TRANSPORT_CONNECTIONS];

int stcp_sock;
static pthread_t seghandler_t, segBuf_timer_tid;

static inline unsigned int get_current_time(){
    struct timeval now;
    gettimeofday(&now, NULL);
    return now.tv_sec;
}

static void add_to_buffer(client_tcb_t *item, void *data, unsigned int length){
    segBuf_t *pSegBuf = (segBuf_t *)malloc(sizeof(segBuf_t));
    memset(pSegBuf,0,sizeof(segBuf_t));
    pSegBuf->seg.header.src_port = item->client_portNum;
    pSegBuf->seg.header.dest_port = item->server_portNum;
    pSegBuf->seg.header.length = length;
    pSegBuf->seg.header.seq_num = item->next_seqNum;
    pSegBuf->seg.header.type = DATA;
    memcpy(pSegBuf->seg.data,data,length); 
    //fill next and time of this segBUf
    pSegBuf->next = NULL;
    //modify the TCB item,insert the segBuf to tail
    item->next_seqNum += length;
    pthread_mutex_lock(item->sendBufMutex);
    if(item->sendBufHead == NULL){//init head,tail and start segBuf_timer()
        usleep(SENDBUF_POLLING_INTERVAL/1000);//in case the timer have not exit
        item->sendBufHead = pSegBuf;
        item->sendBufTail = pSegBuf;
        item->sendBufunSent = pSegBuf;
        pthread_create(&segBuf_timer_tid,NULL,sendBuf_timer,(void*)item);
    }
    else{
        item->sendBufTail->next = pSegBuf;
        item->sendBufTail = pSegBuf;
        if (item->sendBufunSent == NULL)
            item->sendBufunSent = item->sendBufTail;
    }
    pthread_mutex_unlock(item->sendBufMutex);
}

//
//  我们在下面提供了每个函数调用的原型定义和细节说明, 但这些只是指导性的, 你完全可以根据自己的想法来设计代码.
//
//  注意: 当实现这些函数时, 你需要考虑FSM中所有可能的状态, 这可以使用switch语句来实现.
//
//  目标: 你的任务就是设计并实现下面的函数原型.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

// 这个函数初始化TCB表, 将所有条目标记为NULL.  
// 它还针对重叠网络TCP套接字描述符conn初始化一个STCP层的全局变量, 该变量作为sip_sendseg和sip_recvseg的输入参数.
// 最后, 这个函数启动seghandler线程来处理进入的STCP段. 客户端只有一个seghandler.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//

void stcp_client_init(int conn) {
    for (int i = 0; i < MAX_TRANSPORT_CONNECTIONS; i++){
        tcb_table[i] = NULL;
    }
    stcp_sock = conn;
    pthread_create(&seghandler_t, NULL, seghandler, NULL);
    return;
}


// 这个函数查找客户端TCB表以找到第一个NULL条目, 然后使用malloc()为该条目创建一个新的TCB条目.
// 该TCB中的所有字段都被初始化. 例如, TCB state被设置为CLOSED，客户端端口被设置为函数调用参数client_port. 
// TCB表中条目的索引号应作为客户端的新套接字ID被这个函数返回, 它用于标识客户端的连接. 
// 如果TCB表中没有条目可用, 这个函数返回-1.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
int stcp_client_sock(unsigned int client_port) {
    for (int i = 0; i < MAX_TRANSPORT_CONNECTIONS; i++){
        if (tcb_table[i] == NULL){
            tcb_table[i] = (client_tcb_t *)malloc(sizeof(client_tcb_t));
            client_tcb_t *item = tcb_table[i];
            // init tcb item value
            item->state = CLOSED;
            item->client_portNum = client_port;
            item->client_nodeID = topology_getMyNodeID();
            item->next_seqNum = 0;//data seq start from 0
            item->sendBufMutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
            item->sendBufHead = NULL;
            item->sendBufunSent = NULL;
            item->sendBufTail = NULL;
            item->unAck_segNum = 0;
            item->recvBuf = (char *)malloc(RECV_BUFFERSIZE);
            item->recvBufMutex = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
            item->usedBufLen = 0;
            item->expect_seqNum = 0;
            pthread_mutex_init(item->sendBufMutex,NULL);
            pthread_mutex_init(item->recvBufMutex,NULL);
            return i;
        }
    }
    return -1;
}


// 这个函数用于连接服务器. 它以套接字ID和服务器的端口号作为输入参数. 套接字ID用于找到TCB条目.  
// 这个函数设置TCB的服务器端口号,  然后使用sip_sendseg()发送一个SYN段给服务器.  
// 在发送了SYN段之后, 一个定时器被启动. 如果在SYNSEG_TIMEOUT时间之内没有收到SYNACK, SYN 段将被重传. 
// 如果收到了, 就返回1. 否则, 如果重传SYN的次数大于SYN_MAX_RETRY, 就将state转换到CLOSED, 并返回-1.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
int stcp_client_connect(int sockfd, unsigned int server_port, int nodeID) {
    client_tcb_t *item = tcb_table[sockfd];
    if (item == NULL){
        printf("in %s : sockfd invalid!\n", __func__);
        return -1;
    }
    item->server_portNum = server_port;
    item->server_nodeID = nodeID;
    seg_t syn_seg;
    memset(&syn_seg, 0, sizeof(syn_seg));
    syn_seg.header.type = SYN;
    syn_seg.header.src_port = item->client_portNum;
    syn_seg.header.dest_port = item->server_portNum;
    syn_seg.header.seq_num = sockfd;

    switch(item->state){
        case CLOSED:
            {
                item->state = SYNSENT;
                int try_times = 0;
                sip_sendseg(stcp_sock, &syn_seg, nodeID);
                printf("send syn\n");
                sleep(1);
                while(item->state != CONNECTED && try_times <= SYN_MAX_RETRY){
                    try_times++;
                    debug_printf("send a pack with port %d\n", syn_seg.header.dest_port);
                    sip_sendseg(stcp_sock, &syn_seg, nodeID);
                    printf("timeout, send syn again\n");
                    sleep(1);
                }
                if (item->state == CONNECTED){
                    printf("now stcp connected\n");
                    return 1;
                } else {
                    item->state = CLOSED;
                    printf("stcp connect error\n");
                    return -1;
                }
                break;
            }
        case SYNSENT:
            {
                printf("error occur: call connect when in synsent state\n");
                return -1;
                break;
            }
        case CONNECTED:
            {
                printf("error occur: has connected\n");
                return -1;
                break;
            }
        case FINWAIT:
            {
                printf("error occur: call connect when in finwait state\n");
                return -1;
                break;
            }
    }

    return -1;

}


// 发送数据给STCP服务器. 这个函数使用套接字ID找到TCB表中的条目. 
// 然后它使用提供的数据创建segBuf, 将它附加到发送缓冲区链表中. 
// 如果发送缓冲区在插入数据之前为空, 一个名为sendbuf_timer的线程就会启动. 
// 每隔SENDBUF_POLLING_INTERVAL时间查询发送缓冲区以检查是否有超时事件发生.
// 这个函数在成功时返回1，否则返回-1. 
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
int stcp_client_send(int sockfd, void* data, unsigned int length)
{
    client_tcb_t *item = tcb_table[sockfd];
    if (item == NULL){
        printf("invalid sockfd:%d\n", sockfd);
        return -1;
    }
    if(item->state != CONNECTED){
        printf("can not send data,because the TCB state = %d\n",item->state);
        return -1;
    }

    if (length <= MAX_SEG_LEN)
        add_to_buffer(item, data, length);
    else {
        while(length > MAX_SEG_LEN){
            add_to_buffer(item, data, MAX_SEG_LEN);
            length -= MAX_SEG_LEN;
            data += MAX_SEG_LEN;
        }
        if (length > 0)
            add_to_buffer(item, data, length);
    }

    //send the segment
    pthread_mutex_lock(item->sendBufMutex);
    while(item->unAck_segNum < GBN_WINDOW && item->sendBufunSent != NULL){
        item->sendBufunSent->sentTime = get_current_time();
        sip_sendseg(stcp_sock, &item->sendBufunSent->seg, tcb_table[sockfd]->server_nodeID);
        item->unAck_segNum++;
        item->sendBufunSent = item->sendBufunSent->next;
    }
    pthread_mutex_unlock(item->sendBufMutex);

    return 1;
}


int stcp_client_recv(int sockfd, void* buf, unsigned int length) {
    server_tcb_t *currTcb = tcbTable[sockfd];
    if (currTcb == NULL){
        printf("invalid sockfd\n");
        return -1;
    }
    if (currTcb->state != CONNECTED){
        printf("stcp not connect, cannot recv data\n");
        return -1;
    }
    
    while(currTcb->usedBufLen < length){
        sleep(1);
    }
    pthread_mutex_lock(currTcb->recvBufMutex);
    memcpy(buf, currTcb->recvBuf, length);
    for (int i = 0; i < currTcb->usedBufLen - length; i++)
        currTcb->recvBuf[i] = currTcb->recvBuf[i + length];
    currTcb->usedBufLen -= length;
    pthread_mutex_unlock(currTcb->recvBufMutex);
    return 1;
}

// 这个函数用于断开到服务器的连接. 它以套接字ID作为输入参数. 套接字ID用于找到TCB表中的条目.  
// 这个函数发送FIN段给服务器. 在发送FIN之后, state将转换到FINWAIT, 并启动一个定时器.
// 如果在最终超时之前state转换到CLOSED, 则表明FINACK已被成功接收. 否则, 如果在经过FIN_MAX_RETRY次尝试之后,
// state仍然为FINWAIT, state将转换到CLOSED, 并返回-1.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
int stcp_client_disconnect(int sockfd) {
    client_tcb_t *item = tcb_table[sockfd];
    if (item == NULL){
        printf("in %s : sockfd invalid!\n", __func__);
        return -1;
    }
    seg_t fin_seg;
    memset(&fin_seg, 0, sizeof(fin_seg));
    fin_seg.header.type = FIN;
    fin_seg.header.src_port = item->client_portNum;
    fin_seg.header.dest_port = item->server_portNum;
    fin_seg.header.seq_num = sockfd;
    
    switch(item->state){
        case CLOSED:
            {
                printf("error occur: call disconnect when in closed state\n");
                return -1;
                break;
            }
        case SYNSENT:
            {
                printf("error occur: call disconnect when in synsent state\n");
                return -1;
                break;
            }
        case CONNECTED:
            {
                item->state = FINWAIT;
                int try_times = 0;
                sip_sendseg(stcp_sock, &fin_seg, tcb_table[sockfd]->server_nodeID);
                printf("send fin\n");
                sleep(1);
                while(item->state != CLOSED && try_times < FIN_MAX_RETRY){
                    try_times++;
                    sip_sendseg(stcp_sock, &fin_seg, tcb_table[sockfd]->server_nodeID);
                    printf("timeout, send fin again\n");
                    sleep(1);
                }

                if (item->state == CLOSED){
                    printf("now stcp closed\n");
                    return 1;
                } else {
                    printf("timeout, finally close connect\n");
                    item->state = CLOSED;
                    return -1;
                }
            }
        case FINWAIT:
            {
                printf("error occur: call disconnect when in finwait state\n");
                return -1;
                break;
            }
    }


    return 0;
}


// 这个函数调用free()释放TCB条目. 它将该条目标记为NULL, 成功时(即位于正确的状态)返回1,
// 失败时(即位于错误的状态)返回-1.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//

int stcp_client_close(int sockfd) {
    client_tcb_t *item = tcb_table[sockfd];
    if (item == NULL){
        printf("no such item! sockfd: %d\n", sockfd);
        return -1;
    } else {
        printf("free tcb item %d memory successfully\n", sockfd);
        free(item->sendBufMutex);
        free(item->recvBufMutex);
        free(item->recvBuf);
        free(item);
        tcb_table[sockfd] = NULL;
        return 1;
    }
}


// 这是由stcp_client_init()启动的线程. 它处理所有来自服务器的进入段. 
// seghandler被设计为一个调用sip_recvseg()的无穷循环. 如果sip_recvseg()失败, 则说明重叠网络连接已关闭,
// 线程将终止. 根据STCP段到达时连接所处的状态, 可以采取不同的动作. 请查看客户端FSM以了解更多细节.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//

void *seghandler(void* arg) {
    seg_t recv_seg;
    int *server_nodeID = (int *)malloc(sizeof(int));
    while(sip_recvseg(stcp_sock, &recv_seg, server_nodeID) > 0){
        client_tcb_t *item = NULL;
        for(int i = 0; i < MAX_TRANSPORT_CONNECTIONS; i++){
            if (tcb_table[i] != NULL && tcb_table[i]->client_portNum == recv_seg.header.dest_port){
                item = tcb_table[i];
                break;
            }
        }
        if (item == NULL){
            printf("receive a pack without corresponding socket\n");
            continue;
        }

        switch(item->state){
            case CLOSED:
                {
                    printf("a closed socket receive a pack\n");
                    break;
                }
            case SYNSENT:
                {
                    if (recv_seg.header.type == SYNACK){
                        printf("receive synack, turn to connected state\n");
                        item->state = CONNECTED;
                    } else {
                        printf("receice an useless pack when in synsent state\n");
                    }
                    break;
                }
            case CONNECTED:
                {
                    debug_printf("get a pack in connected state,type is %d\n",recv_seg.header.type);
                    if(recv_seg.header.type == DATAACK){
                        segBuf_t *pSegBuf = item->sendBufHead;
                        //delete the segBuf whose seq_num < ack_num
                        pthread_mutex_lock(item->sendBufMutex);
                        int seqNum = pSegBuf->seg.header.seq_num;
                        while(seqNum < recv_seg.header.ack_num){
                            if(item->sendBufHead == item->sendBufTail){//only one segBuf
                                free(pSegBuf);
                                pSegBuf = item->sendBufHead = item->sendBufTail = NULL;
                                item->unAck_segNum = 0;
                                break;
                            }
                            else{
                                item->sendBufHead = item->sendBufHead->next;
                                free(pSegBuf);
                                pSegBuf = item->sendBufHead;
                                item->unAck_segNum --;
                            }
                            seqNum = pSegBuf->seg.header.seq_num;
                        }
                        pthread_mutex_unlock(item->sendBufMutex);
                        //send the unsent segBuf
                        pthread_mutex_lock(item->sendBufMutex);
                        while(item->sendBufunSent != NULL && item->unAck_segNum < GBN_WINDOW){
                            pSegBuf = item->sendBufunSent;
                            if(pSegBuf == NULL){
                                pthread_mutex_unlock(item->sendBufMutex);
                                break;
                            }
                            pSegBuf->sentTime = get_current_time();
                            sip_sendseg(stcp_sock,&pSegBuf->seg,*server_nodeID);
                            printf("in : %s, seq_num : %d has been sent\n", __func__,pSegBuf->seg.header.seq_num);
                            item->sendBufunSent = item->sendBufunSent->next;
                            item->unAck_segNum++;
                        }
                        pthread_mutex_unlock(item->sendBufMutex);
                    }
                    else if(recv_seg.header.type == DATA){
                        if (pSeg->header.seq_num == pTcb->expect_seqNum){
                            //push data to buffer
                            pTcb->expect_seqNum += pSeg->header.length;
                            assert(pTcb->usedBufLen + pSeg->header.length < BUFFERSIZE);
                            pthread_mutex_lock(pTcb->recvBufMutex);
                            char *buf_tail = pTcb->recvBuf + pTcb->usedBufLen;
                            for (int i = 0; i < pSeg->header.length; i++){
                                buf_tail[i] = pSeg->data[i];
                            }
                            pTcb->usedBufLen += pSeg->header.length;
                            pthread_mutex_unlock(pTcb->recvBufMutex);
                        }
                        seg_t response;
                        response.header.type = DATAACK;
                        response.header.dest_port = pSeg->header.src_port;
                        response.header.src_port = pSeg->header.dest_port;
                        response.header.ack_num = pTcb->expect_seqNum;
                        response.header.length = 0;
                        sip_sendseg(stcp_sock, &response, *server_nodeID);
                    }           
                    break;
                }
            case FINWAIT:
                {
                    if (recv_seg.header.type == FINACK){
                        printf("receive finack, turn to closed state\n");
                        item->state = CLOSED;
                    } else {
                        printf("receive an useless pack when in synsent state\n");
                    }
                    break;
                }
        }
    }
    return 0;
}

// 这个线程持续轮询发送缓冲区以触发超时事件. 如果发送缓冲区非空, 它应一直运行.
// 如果(当前时间 - 第一个已发送但未被确认段的发送时间) > DATA_TIMEOUT, 就发生一次超时事件.
// 当超时事件发生时, 重新发送所有已发送但未被确认段. 当发送缓冲区为空时, 这个线程将终止.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
void* sendBuf_timer(void* clienttcb)
{
    client_tcb_t * currTcb = (client_tcb_t *)clienttcb;
    int sockfd = 0;
    for(;sockfd < MAX_TRANSPORT_CONNECTIONS; sockfd++){
        if(tcb_table[sockfd] != NULL && tcb_table[sockfd] == currTcb)
            break;
    }
    if(sockfd >= MAX_TRANSPORT_CONNECTIONS){
        printf("in:%s,Error! Can not find the TCB\n",__func__);
        return NULL;
    }
    while(1){
        segBuf_t *pSegBuf = currTcb->sendBufHead;
        if(pSegBuf == NULL){
            printf("segBuf now is empty,timer terminated\n");
            return NULL;
        }
        if(currTcb->state != CONNECTED){
            printf("in : %s, timer terminated because currTcb->state = %d\n",__func__,currTcb->state);
            return NULL;
        }
        int now = get_current_time();
        pthread_mutex_lock(currTcb->sendBufMutex);
        if(now - pSegBuf->sentTime > 1){//time out
            for(int i = 0; i < currTcb->unAck_segNum; i++){
                assert(pSegBuf != NULL);
                pSegBuf->sentTime = get_current_time();
                sip_sendseg(stcp_sock,&pSegBuf->seg, currTcb->server_nodeID);
                printf("in : %s, seq_num : %d has been resent\n",__func__,pSegBuf->seg.header.seq_num);
                pSegBuf = pSegBuf->next;
            }
        }
        pthread_mutex_unlock(currTcb->sendBufMutex);
        usleep(SENDBUF_POLLING_INTERVAL/1000);
    }
    return NULL;
}

