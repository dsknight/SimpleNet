//文件名: son/son.c
//
//描述: 这个文件实现SON进程 
//SON进程首先连接到所有邻居, 然后启动listen_to_neighbor线程, 每个该线程持续接收来自一个邻居的进入报文, 并将该报文转发给SIP进程. 
//然后SON进程等待来自SIP进程的连接. 在与SIP进程建立连接之后, SON进程持续接收来自SIP进程的sendpkt_arg_t结构, 并将接收到的报文发送到重叠网络中. 
//
//创建日期: 2013年

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <strings.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>
#include <sys/utsname.h>
#include <assert.h>
#include <stdbool.h>

#include "../common/constants.h"
#include "../common/pkt.h"
#include "son.h"
#include "../topology/topology.h"
#include "neighbortable.h"

//你应该在这个时间段内启动所有重叠网络节点上的SON进程
#define SON_START_DELAY 10

/**************************************************************/
//声明全局变量
/**************************************************************/

//将邻居表声明为一个全局变量 
nbr_entry_t* nt; 
//将与SIP进程之间的TCP连接声明为一个全局变量
int sip_conn; 

static bool exist_id(nbr_entry_t *nt, int id){
    int nbr_num = topology_getNbrNum();
    int i;

    for(i = 0; i < nbr_num; i++){
        if (nt[i].nodeID == id)
            return true;
    }

    return false;
}

static int getConnByID(int id){
    int nbr_num = topology_getNbrNum();
    int i ;

    for(i = 0; i < nbr_num; i++){
        if(nt[i].nodeID == id)
            return nt[i].conn;
    }

    return -1;
}

/**************************************************************/
//实现重叠网络函数
/**************************************************************/

// 这个线程打开TCP端口CONNECTION_PORT, 等待节点ID比自己大的所有邻居的进入连接,
// 在所有进入连接都建立后, 这个线程终止. 
void* waitNbrs(void* arg) {
    int nbr_num = topology_getNbrNum();
    int bigger_node_num = 0;
    int i;
    int local_id = topology_getMyNodeID();

    for(i = 0; i < nbr_num; i++){
        if (nt[i].nodeID > local_id)
            bigger_node_num++;
    }

    struct sockaddr_in servaddr, cliaddr;
    socklen_t cliaddr_len;
    int listenfd, connfd;

    listenfd = socket(AF_INET, SOCK_STREAM, 0);

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(CONNECTION_PORT);

    bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr));
    listen(listenfd, 20);

    for(;bigger_node_num > 0;){
        cliaddr_len = sizeof(cliaddr);
        connfd = accept(listenfd, (struct sockaddr *)&cliaddr, &cliaddr_len);
        printf("receive a connection from %d\n", topology_getNodeIDfromip(&cliaddr.sin_addr));
        int client_nodeid = topology_getNodeIDfromip((&cliaddr.sin_addr));
        if (client_nodeid > local_id && exist_id(nt, client_nodeid) == true){
            if (nt_addconn(nt, client_nodeid, connfd) == -1)
                printf("error when addconn %d\n", client_nodeid);
            bigger_node_num--;
        } else {
            printf("not wanted, close\n");
            close(connfd);
        }
    }

    return 0;
}

// 这个函数连接到节点ID比自己小的所有邻居.
// 在所有外出连接都建立后, 返回1, 否则返回-1.
int connectNbrs() {
    int nbr_num = topology_getNbrNum();
    int local_id = topology_getMyNodeID();
    int i;

    for (i = 0; i < nbr_num; i++){
        if (nt[i].nodeID < local_id){
            struct sockaddr_in servaddr;
            int sockfd = socket(AF_INET, SOCK_STREAM, 0);
            bzero(&servaddr, sizeof(servaddr));
            servaddr.sin_family = AF_INET;
            servaddr.sin_port = htons(CONNECTION_PORT);
            servaddr.sin_addr.s_addr = nt[i].nodeIP;
            if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0){
                printf("error when connect %d\n", nt[i].nodeID);
                return -1;
            }
            else {
                if (nt_addconn(nt, nt[i].nodeID, sockfd) == -1) {
                    printf("error when addconn %d\n", nt[i].nodeID);
                    close(sockfd);
                    return -1;
                }
            }
        }
    }

    return 1;
}

//每个listen_to_neighbor线程持续接收来自一个邻居的报文. 它将接收到的报文转发给SIP进程.
//所有的listen_to_neighbor线程都是在到邻居的TCP连接全部建立之后启动的. 
void* listen_to_neighbor(void* arg) {
    nbr_entry_t currNbr = nt[*(int *)arg];
    int conn = currNbr.conn;
    sip_pkt_t pkt;
    while(1){
        if (recvpkt(&pkt,conn) == 1){
            printf("recv a pkt from nbr %d\n",currNbr.nodeID);
            if(forwardpktToSIP(&pkt,sip_conn) < 0)
                printf("Error in send pkt to SIP\n");
        } else {
            printf("Error when try to recvpkt from neighbor\n");
            printf("connect lost, exit\n");
            close(nt[*(int *)arg].conn);
            nt[*(int *)arg].conn = -1;
            break;
        }
    }
    printf("Error in recvpkt\n");
    return 0;
}

//这个函数打开TCP端口SON_PORT, 等待来自本地SIP进程的进入连接. 
//在本地SIP进程连接之后, 这个函数持续接收来自SIP进程的sendpkt_arg_t结构, 并将报文发送到重叠网络中的下一跳. 
//如果下一跳的节点ID为BROADCAST_NODEID, 报文应发送到所有邻居节点.
void waitSIP() {
    struct sockaddr_in servaddr, cliaddr;
    socklen_t cliaddr_len;
    int listenfd;

    listenfd = socket(AF_INET, SOCK_STREAM, 0);

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(SON_PORT);

    bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr));
    listen(listenfd, 20);

    sip_conn = accept(listenfd, (struct sockaddr *)&cliaddr, &cliaddr_len);

    //recv pkt from sip and send
    sip_pkt_t pkt;
    int nextNode, conn;
    while(1){
        if(getpktToSend(&pkt,&nextNode,sip_conn) == 1){
            if (pkt.header.type == SIP){
                if((conn = getConnByID(nextNode)) >= 0){
                    if(sendpkt(&pkt,conn) < 0)
                        printf("Error in send pkt to Nbr\n");
                } else {
                    printf("Wrong Node id nextNode = %d\n",nextNode);
                }
            } else if (pkt.header.type == ROUTE_UPDATE){
                int nbr_num = topology_getNbrNum();
                int i;
                for (i = 0; i < nbr_num; i++){
                    if (nt[i].conn >= 0 && sendpkt(&pkt, nt[i].conn) < 0){
                        printf("Error in send route update pkt to Nbr\n");
                    }
                }
            }
        }
    }
    printf("Error in getpktToSend\n");
}


//这个函数停止重叠网络, 当接收到信号SIGINT时, 该函数被调用.
//它关闭所有的连接, 释放所有动态分配的内存.
void son_stop() {
    int nbr_num = topology_getNbrNum();
    int i;

    for(i = 0; i < nbr_num; i++){
        if (nt[i].conn >= 0)
            close(nt[i].conn);
    }
    close(sip_conn);

    free(nt);
    printf("son exit\n");
    exit(0);
}

int main() {
    //启动重叠网络初始化工作
    printf("Overlay network: Node %d initializing...\n",topology_getMyNodeID());	

    //创建一个邻居表
    nt = nt_create();
    //将sip_conn初始化为-1, 即还未与SIP进程连接
    sip_conn = -1;

    //注册一个信号句柄, 用于终止进程
    signal(SIGINT, son_stop);

    //打印所有邻居
    int nbrNum = topology_getNbrNum();
    int i;
    for(i=0;i<nbrNum;i++) {
        printf("Overlay network: neighbor %d:%d\n",i+1,nt[i].nodeID);
    }

    //启动waitNbrs线程, 等待节点ID比自己大的所有邻居的进入连接
    pthread_t waitNbrs_thread;
    pthread_create(&waitNbrs_thread,NULL,waitNbrs,(void*)0);

    //等待其他节点启动
    sleep(SON_START_DELAY);

    //连接到节点ID比自己小的所有邻居
    connectNbrs();

    //等待waitNbrs线程返回
    pthread_join(waitNbrs_thread,NULL);	

    //此时, 所有与邻居之间的连接都建立好了
    
    for (i = 0; i < nbrNum; i++){
        printf("id:%d, socket:%d\n", nt[i].nodeID, nt[i].conn);
    }

    //创建线程监听所有邻居
    for(i=0;i<nbrNum;i++) {
        int* idx = (int*)malloc(sizeof(int));
        *idx = i;
        pthread_t nbr_listen_thread;
        pthread_create(&nbr_listen_thread,NULL,listen_to_neighbor,(void*)idx);
    }
    printf("Overlay network: node initialized...\n");
    printf("Overlay network: waiting for connection from SIP process...\n");

    //等待来自SIP进程的连接
    waitSIP();
}
