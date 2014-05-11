#include <stdlib.h>
#include <sys/socket.h>
#include <stdio.h>
#include <time.h>
#include <pthread.h>
#include "stcp_server.h"
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <assert.h>
#include <stdbool.h>
#include "../common/constants.h"
#include "debug.h"
#include "../topology/topology.h"


extern server_tcb_t *find_tcb(int);
extern void *timeout(void *);

static server_tcb_t *tcbTable[MAX_TRANSPORT_CONNECTIONS];
static int stcp_sock;

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
/*面向应用层的接口*/

//
//
//  我们在下面提供了每个函数调用的原型定义和细节说明, 但这些只是指导性的, 你完全可以根据自己的想法来设计代码.
//
//  注意: 当实现这些函数时, 你需要考虑FSM中所有可能的状态, 这可以使用switch语句来实现.
//
//  目标: 你的任务就是设计并实现下面的函数原型.
//

// stcp服务器初始化
//
// 这个函数初始化TCB表, 将所有条目标记为NULL. 它还针对重叠网络TCP套接字描述符conn初始化一个STCP层的全局变量,
// 该变量作为sip_sendseg和sip_recvseg的输入参数. 最后, 这个函数启动seghandler线程来处理进入的STCP段.
// 服务器只有一个seghandler.
//



void stcp_server_init(int conn) {
    memset(tcbTable,0,sizeof(server_tcb_t *)*MAX_TRANSPORT_CONNECTIONS);
    stcp_sock = conn;
    pthread_t tid;
    pthread_create(&tid,NULL,seghandler,NULL);
    return;
}

// 创建服务器套接字
//
// 这个函数查找服务器TCB表以找到第一个NULL条目, 然后使用malloc()为该条目创建一个新的TCB条目.
// 该TCB中的所有字段都被初始化, 例如, TCB state被设置为CLOSED, 服务器端口被设置为函数调用参数server_port. 
// TCB表中条目的索引应作为服务器的新套接字ID被这个函数返回, 它用于标识服务器的连接. 
// 如果TCB表中没有条目可用, 这个函数返回-1.

int stcp_server_sock(unsigned int server_port) {
    int sockfd = 0;
    while(tcbTable[sockfd] != NULL && sockfd < MAX_TRANSPORT_CONNECTIONS)
        sockfd++;
    if(sockfd >= MAX_TRANSPORT_CONNECTIONS)
        return -1;
    tcbTable[sockfd] = (server_tcb_t*)malloc(sizeof(server_tcb_t));
    tcbTable[sockfd]->server_portNum = server_port;
    tcbTable[sockfd]->server_nodeID = topology_getMyNodeID();
    tcbTable[sockfd]->state = CLOSED;
    tcbTable[sockfd]->recvBuf = (char*)malloc(RECV_BUFFERSIZE);
    tcbTable[sockfd]->usedBufLen = 0;
    tcbTable[sockfd]->expect_seqNum = 0;
    tcbTable[sockfd]->recvBufMutex = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
    tcbTable[sockfd]->next_seqNum = 0;
    tcbTable[sockfd]->sendBufHead = NULL;
    tcbTable[sockfd]->sendBufunSent = NULL;
    tcbTable[sockfd]->sendBufTail = NULL;
    tcbTable[sockfd]->unAck_segNum = 0;
    tcbTable[sockfd]->sendBufMutex = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(tcbTable[sockfd]->recvBufMutex, NULL);
    pthread_mutex_init(tcbTable[sockfd]->sendBufMutex, NULL);
    return sockfd;
}

// 接受来自STCP客户端的连接
//
// 这个函数使用sockfd获得TCB指针, 并将连接的state转换为LISTENING. 它然后进入忙等待(busy wait)直到TCB状态转换为CONNECTED 
// (当收到SYN时, seghandler会进行状态的转换). 该函数在一个无穷循环中等待TCB的state转换为CONNECTED,  
// 当发生了转换时, 该函数返回1. 你可以使用不同的方法来实现这种阻塞等待.
//

int stcp_server_accept(int sockfd) {
    server_tcb_t *currTcb = tcbTable[sockfd];
    if(currTcb == NULL){
        printf("int %s, wrong sockfd = %d\n",sockfd);
        return -1;
    }
    if(currTcb->state == CLOSED){
        currTcb->state = LISTENING;
        debug_printf("server %d now ready to accept\n", sockfd);
        while(currTcb->state != CONNECTED)
            sleep(1);
        return sockfd;
    }
    else{
        int retfd = stcp_server_sock(currTcb->server_portNum);
        tcbTable[retfd]->state = LISTENING;
        while(tcbTable[retfd]->state != CONNECTED)
            sleep(1);
        return retfd;
    }
}

int stcp_server_send(int sockfd, void* data, unsigned int length)
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


// 接收来自STCP客户端的数据
//
// 这个函数接收来自STCP客户端的数据. 你不需要在本实验中实现它.
//
int stcp_server_recv(int sockfd, void* buf, unsigned int length) {
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

// 关闭STCP服务器
//
// 这个函数调用free()释放TCB条目. 它将该条目标记为NULL, 成功时(即位于正确的状态)返回1,
// 失败时(即位于错误的状态)返回-1.
//

int stcp_server_close(int sockfd) {
    if(sockfd < 0 || tcbTable[sockfd] == NULL)
        return -1;
    server_tcb_t *pTcb = tcbTable[sockfd];
    free(pTcb->recvBuf);
    free(pTcb->recvBufMutex);
    free(pTcb->sendBufMutex);
    free(pTcb);
    tcbTable[sockfd] = NULL;
    return 1;
}

// 处理进入段的线程
//
// 这是由stcp_server_init()启动的线程. 它处理所有来自客户端的进入数据. seghandler被设计为一个调用sip_recvseg()的无穷循环, 
// 如果sip_recvseg()失败, 则说明重叠网络连接已关闭, 线程将终止. 根据STCP段到达时连接所处的状态, 可以采取不同的动作.
// 请查看服务端FSM以了解更多细节.
//

void *seghandler(void* arg) {
    pthread_detach(pthread_self());
    seg_t * pSeg = (seg_t *)malloc(sizeof(seg_t));
    int *client_nodeID = (int *)malloc(sizeof(int));
    while(sip_recvseg(stcp_sock,pSeg,client_nodeID)){
        debug_printf("get a pack, to deal with it, it's type is %d\n", pSeg->header.type);
        server_tcb_t *pTcb = find_tcb(pSeg->header.src_port, pSeg->header.dest_port, *client_nodeID);
        if(pTcb == NULL){
            printf("wrong port %d\n",pSeg->header.dest_port);
            break;
        }
        if(pSeg->header.type == SYN){
            debug_printf("now server state is %d\n", pTcb->state);
            switch(pTcb->state){
                case CLOSED:
                    break;
                case LISTENING:
                    {
                        pTcb->state = CONNECTED;
                        pTcb->client_portNum = pSeg->header.src_port;
                        pTcb->client_nodeID = *client_nodeID;
                        //modify the seg and send back the ack
                        pSeg->header.ack_num = pSeg->header.seq_num + 1;
                        pSeg->header.type = SYNACK;
                        pSeg->header.dest_port = pSeg->header.src_port;
                        pSeg->header.src_port = pTcb->server_portNum;
                        debug_printf("state turn to connected from listening, try send synack by %d\n", conn);
                        sip_sendseg(stcp_sock,pSeg,*client_nodeID);
                    }			
                    break;
                case CONNECTED:
                    {   
                        pTcb->state = CONNECTED;
                        pTcb->client_portNum = pSeg->header.src_port;
                        pSeg->header.ack_num = pSeg->header.seq_num + 1;
                        pSeg->header.type = SYNACK;
                        pSeg->header.dest_port = pSeg->header.src_port;
                        pSeg->header.src_port = pTcb->server_portNum;
                        debug_printf("state turn to connected from listening, try send synack by %d\n", conn);
                        sip_sendseg(stcp_sock,pSeg,*client_nodeID);
                    }
                    break;
                case CLOSEWAIT:
                    break;
                default:	
                    break;
            }
        }
        else if(pSeg->header.type == FIN){
            debug_printf("now server state is %d\n", pTcb->state);
            switch(pTcb->state){
                case CLOSED:
                    break;
                case LISTENING:
                    break;		
                case CONNECTED:
                    {
                        pTcb->state = CLOSEWAIT;
                        pSeg->header.ack_num = pSeg->header.seq_num + 1;
                        pSeg->header.type = FINACK;
                        pSeg->header.dest_port = pSeg->header.src_port;
                        pSeg->header.src_port = pTcb->server_portNum;
                        sip_sendseg(stcp_sock,pSeg,*client_nodeID);
                        pthread_t tid;
                        pthread_create(&tid,NULL,timeout,(void *)pTcb);
                        break;
                    }
                case CLOSEWAIT:
                    {
                        pSeg->header.ack_num = pSeg->header.seq_num + 1;
                        pSeg->header.type = FINACK;
                        pSeg->header.dest_port = pSeg->header.src_port;
                        pSeg->header.src_port = pTcb->server_portNum;
                        sip_sendseg(stcp_sock,pSeg,*client_nodeID);
                        break;
                    }
                default:break;	            
            }
        }

        else if(pSeg->header.type == DATA){
            switch(pTcb->state){
                case CLOSED:
                    {
                        printf("error occur: receive a data pack in state closed\n");
                        break;
                    }
                case LISTENING:
                    {
                        printf("error occur: receive a data pack in state listening\n");
                        break;
                    }
                case CONNECTED:
                    {
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
                        sip_sendseg(stcp_sock, &response, *client_nodeID);
                        break;
                    }
                case CLOSEWAIT:
                    {
                        printf("error occur: receive a data pack in state closewait\n");
                        break;
                    }
                default:
                    {
                        assert(false);
                        break;
                    }
            }
        }
        else if(pSeg->header.type == DATAACK){ 
            switch(pTcb->state){
                case CLOSED:
                    {
                        printf("error occur: receive a dataack pack in state closed\n");
                        break;
                    }
                case LISTENING:
                    {
                        printf("error occur: receive a dataack pack in state listening\n");
                        break;
                    }
                case CONNECTED:
                    {
                        server_tcb_t *item = pTcb;
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
                        break;
                    }
                case CLOSEWAIT:
                    {
                        printf("error occur: receive a dataack pack in state closewait\n");
                        break;
                    }
                default:
                    {
                        assert(false);
                        break;
                    }
            }
        }
        else{
            debug_printf("wrong segment type %d\n",pSeg->header.type);
        }
    }

    free(pSeg);	
    printf("now exit seghandler\n");
    return 0;
}

//find tcb找两次，第一次找两边端口号都匹配的；
//若没找到，再找只有目的端口号匹配的，此时即为监听套接字
server_tcb_t * find_tcb(int client_port int server_port int client_nodeID){
    int i = 0;
    while(i < MAX_TRANSPORT_CONNECTIONS){
        server_tcb_t *pTcb = tcbTable[i];
        if( pTcb != NULL && pTcb->client_portNum = client_port &&
            pTcb->server_portNum == server_port && pTcb->client_nodeID == client_nodeID){
            debug_printf("find item no.%d\n", i);
            return pTcb;
        }
        i++;
    }
    i = 0;
    while(i < MAX_TRANSPORT_CONNECTIONS){
        server_tcb_t *pTcb = tcbTable[i];
        if( pTcb != NULL && pTcb->client_portNum == 0 &&
            pTcb->server_portNum == server_port && pTcb->client_nodeID == client_nodeID){
            debug_printf("find item no.%d\n", i);
            return pTcb;
        }
        i++;
    }

    return NULL;

}

void *timeout(void *arg){
    pthread_detach(pthread_self());
    struct timeval tv,tv_curr;
    gettimeofday(&tv,NULL);
    while(1){
        gettimeofday(&tv_curr,NULL);
        int timeuse = tv_curr.tv_sec - tv.tv_sec;
        if(timeuse > 4){
            printf("CLOSE_WAIT_TIMEOUT\n");
            server_tcb_t *p = (server_tcb_t *)arg;
            if(p !=NULL)
                p -> state = CLOSED;
            return NULL;
        } 
        sleep(1);
    }
}

