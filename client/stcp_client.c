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
//  �����������ṩ��ÿ���������õ�ԭ�Ͷ����ϸ��˵��, ����Щֻ��ָ���Ե�, ����ȫ���Ը����Լ����뷨����ƴ���.
//
//  ע��: ��ʵ����Щ����ʱ, ����Ҫ����FSM�����п��ܵ�״̬, �����ʹ��switch�����ʵ��.
//
//  Ŀ��: ������������Ʋ�ʵ������ĺ���ԭ��.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

// ���������ʼ��TCB��, ��������Ŀ���ΪNULL.  
// ��������ص�����TCP�׽���������conn��ʼ��һ��STCP���ȫ�ֱ���, �ñ�����Ϊsip_sendseg��sip_recvseg���������.
// ���, �����������seghandler�߳�����������STCP��. �ͻ���ֻ��һ��seghandler.
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


// ����������ҿͻ���TCB�����ҵ���һ��NULL��Ŀ, Ȼ��ʹ��malloc()Ϊ����Ŀ����һ���µ�TCB��Ŀ.
// ��TCB�е������ֶζ�����ʼ��. ����, TCB state������ΪCLOSED���ͻ��˶˿ڱ�����Ϊ�������ò���client_port. 
// TCB������Ŀ��������Ӧ��Ϊ�ͻ��˵����׽���ID�������������, �����ڱ�ʶ�ͻ��˵�����. 
// ���TCB����û����Ŀ����, �����������-1.
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


// ��������������ӷ�����. �����׽���ID�ͷ������Ķ˿ں���Ϊ�������. �׽���ID�����ҵ�TCB��Ŀ.  
// �����������TCB�ķ������˿ں�,  Ȼ��ʹ��sip_sendseg()����һ��SYN�θ�������.  
// �ڷ�����SYN��֮��, һ����ʱ��������. �����SYNSEG_TIMEOUTʱ��֮��û���յ�SYNACK, SYN �ν����ش�. 
// ����յ���, �ͷ���1. ����, ����ش�SYN�Ĵ�������SYN_MAX_RETRY, �ͽ�stateת����CLOSED, ������-1.
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


// �������ݸ�STCP������. �������ʹ���׽���ID�ҵ�TCB���е���Ŀ. 
// Ȼ����ʹ���ṩ�����ݴ���segBuf, �������ӵ����ͻ�����������. 
// ������ͻ������ڲ�������֮ǰΪ��, һ����Ϊsendbuf_timer���߳̾ͻ�����. 
// ÿ��SENDBUF_POLLING_INTERVALʱ���ѯ���ͻ������Լ���Ƿ��г�ʱ�¼�����.
// ��������ڳɹ�ʱ����1�����򷵻�-1. 
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

// ����������ڶϿ���������������. �����׽���ID��Ϊ�������. �׽���ID�����ҵ�TCB���е���Ŀ.  
// �����������FIN�θ�������. �ڷ���FIN֮��, state��ת����FINWAIT, ������һ����ʱ��.
// ��������ճ�ʱ֮ǰstateת����CLOSED, �����FINACK�ѱ��ɹ�����. ����, ����ھ���FIN_MAX_RETRY�γ���֮��,
// state��ȻΪFINWAIT, state��ת����CLOSED, ������-1.
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


// �����������free()�ͷ�TCB��Ŀ. ��������Ŀ���ΪNULL, �ɹ�ʱ(��λ����ȷ��״̬)����1,
// ʧ��ʱ(��λ�ڴ����״̬)����-1.
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


// ������stcp_client_init()�������߳�. �������������Է������Ľ����. 
// seghandler�����Ϊһ������sip_recvseg()������ѭ��. ���sip_recvseg()ʧ��, ��˵���ص����������ѹر�,
// �߳̽���ֹ. ����STCP�ε���ʱ����������״̬, ���Բ�ȡ��ͬ�Ķ���. ��鿴�ͻ���FSM���˽����ϸ��.
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

// ����̳߳�����ѯ���ͻ������Դ�����ʱ�¼�. ������ͻ������ǿ�, ��Ӧһֱ����.
// ���(��ǰʱ�� - ��һ���ѷ��͵�δ��ȷ�϶εķ���ʱ��) > DATA_TIMEOUT, �ͷ���һ�γ�ʱ�¼�.
// ����ʱ�¼�����ʱ, ���·��������ѷ��͵�δ��ȷ�϶�. �����ͻ�����Ϊ��ʱ, ����߳̽���ֹ.
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

