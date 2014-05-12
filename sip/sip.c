//文件名: sip/sip.c
//
//描述: 这个文件实现SIP进程  
//
//创建日期: 2013年1月

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <strings.h>
#include <arpa/inet.h>
#include <signal.h>
#include <netdb.h>
#include <assert.h>
#include <sys/utsname.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>

#include "../common/constants.h"
#include "../common/pkt.h"
#include "../common/seg.h"
#include "../topology/topology.h"
#include "sip.h"
#include "nbrcosttable.h"
#include "dvtable.h"
#include "routingtable.h"

//SIP层等待这段时间让SIP路由协议建立路由路径. 
#define SIP_WAITTIME 60

/**************************************************************/
//声明全局变量
/**************************************************************/
int son_conn; 			//到重叠网络的连接
int stcp_conn;			//到STCP的连接
nbr_cost_entry_t* nct;			//邻居代价表
dv_t* dv;				//距离矢量表
pthread_mutex_t* dv_mutex;		//距离矢量表互斥量
routingtable_t* routingtable;		//路由表
pthread_mutex_t* routingtable_mutex;	//路由表互斥量



/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  update_routingtable
 *  Description:  
 * =====================================================================================
 */

void update_routingtable (){
    int nbr_num = topology_getNbrNum();
    int node_num = topology_getNodeNum();
    int i;

    for (i = 0; i < node_num; i++){
        if (dv->nodeID == dv->dvEntry[i].nodeID)
            continue;
        int mincost = INFINITE_COST;
        int nextNodeID = -1;
        int j;
        for (j = 0; j < nbr_num; j++){
            if (nct[j].cost + dvtable_getcost(dv, nct[j].nodeID, dv->dvEntry[i].nodeID) < mincost){
                mincost = nct[j].cost + dvtable_getcost(dv, nct[j].nodeID, dv->dvEntry[i].nodeID);
                nextNodeID = nct[j].nodeID;
            }
        }
        pthread_mutex_lock(dv_mutex);
        dv->dvEntry[i].cost = mincost;
        pthread_mutex_unlock(dv_mutex);
        pthread_mutex_lock(routingtable_mutex);
        routingtable_setnextnode(routingtable, dv->dvEntry[i].nodeID, nextNodeID);
        pthread_mutex_unlock(routingtable_mutex);
    }

}		/* -----  end of function update_routingtable  ----- */

/**************************************************************/
//实现SIP的函数
/**************************************************************/

//SIP进程使用这个函数连接到本地SON进程的端口SON_PORT.
//成功时返回连接描述符, 否则返回-1.
int connectToSON() { 
    int sockfd;
    struct sockaddr_in servaddr;
    if((sockfd = socket(AF_INET,SOCK_STREAM,0)) < 0){
        printf("Error in create the socket\n");
        return -1;
    }
    memset(&servaddr,0,sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    servaddr.sin_port = htons(SON_PORT);
    if(connect(sockfd,(struct sockaddr *)&servaddr, sizeof(servaddr)) < 0){
        printf("Error in connecting to the server\n");
        return -1;
    }
    return sockfd;
}

//这个线程每隔ROUTEUPDATE_INTERVAL时间发送路由更新报文.路由更新报文包含这个节点
//的距离矢量.广播是通过设置SIP报文头中的dest_nodeID为BROADCAST_NODEID,并通过son_sendpkt()发送报文来完成的.
void* routeupdate_daemon(void* arg) {
    sip_pkt_t pkt;
    pkt_routeupdate_t route_pkt;
    memset(&pkt,0,sizeof(pkt));
    int node_num = topology_getNodeNum();
    pkt.header.src_nodeID = topology_getMyNodeID();
    pkt.header.dest_nodeID = BROADCAST_NODEID;
    pkt.header.type = ROUTE_UPDATE;
    pkt.header.length = node_num * sizeof(routeupdate_entry_t) + sizeof(unsigned int);
    route_pkt.entryNum = node_num; 
    pthread_mutex_lock(dv_mutex);
    int i;
    for (i = 0; i < node_num; i++){
        route_pkt.entry[i].nodeID = dv[0].dvEntry[i].nodeID;
        route_pkt.entry[i].cost = dv[0].dvEntry[i].cost;
    }
    pthread_mutex_unlock(dv_mutex);
    memcpy(&pkt.data, &route_pkt, pkt.header.length);
    while(1){
        if(son_sendpkt(0,&pkt,son_conn) < 0)
            printf("Error in send route update pkt to son\n");
        sleep(ROUTEUPDATE_INTERVAL);
    }
}
//这个线程处理来自SON进程的进入报文. 它通过调用son_recvpkt()接收来自SON进程的报文.
//如果报文是SIP报文,并且目的节点就是本节点,就转发报文给STCP进程. 如果目的节点不是本节点,
//就根据路由表转发报文给下一跳.如果报文是路由更新报文,就更新距离矢量表和路由表.
void* pkthandler(void* arg) {
	sip_pkt_t pkt;

    while(1) {
        if (son_recvpkt(&pkt,son_conn)>0) {
            printf("Routing: received a packet from neighbor %d\n",pkt.header.src_nodeID);
            switch(pkt.header.type){
                case SIP:
                    {
                        if (pkt.header.dest_nodeID == topology_getMyNodeID()){
                            forwardsegToSTCP(stcp_conn, (seg_t *)pkt.data, pkt.header.src_nodeID); 
                        } else {
                            int nextnode = routingtable_getnextnode(routingtable, pkt.header.dest_nodeID);
                            if (nextnode == -1){
                                printf("no rule to forward pkt to %d, skip\n", pkt.header.dest_nodeID);
                                continue;
                            }
                            son_sendpkt(nextnode, &pkt, son_conn);
                        }
                        break;
                    }
                case ROUTE_UPDATE:
                    {
                        int nbr_num = topology_getNbrNum();
                        int node_num = topology_getNodeNum();
                        int i,j;
                        pthread_mutex_lock(dv_mutex);
                        for (i = 0; i < nbr_num; i++){
                            if (dv[i].nodeID == pkt.header.src_nodeID){
                                pkt_routeupdate_t *route_pkt = (pkt_routeupdate_t *)pkt.data;
                                for (j = 0; j < node_num; j++){
                                    dv[i].dvEntry[j].cost = route_pkt->entry[j].cost;
                                }
                                break;
                            }
                        }
                        pthread_mutex_unlock(dv_mutex);
                        update_routingtable();
                        break;
                    }
                default:
                    {
                        assert(false);
                        break;
                    }
            }
        }
        else
            printf("Error when recvpkt from son\n");
    }
    close(son_conn);
	son_conn = -1;
	pthread_exit(NULL);
}

//这个函数终止SIP进程, 当SIP进程收到信号SIGINT时会调用这个函数. 
//它关闭所有连接, 释放所有动态分配的内存.
void sip_stop() {
	close(son_conn);
    printf("Sip process is going to exit\n");
    exit(0);
}

//这个函数打开端口SIP_PORT并等待来自本地STCP进程的TCP连接.
//在连接建立后, 这个函数从STCP进程处持续接收包含段及其目的节点ID的sendseg_arg_t. 
//接收的段被封装进数据报(一个段在一个数据报中), 然后使用son_sendpkt发送该报文到下一跳. 下一跳节点ID提取自路由表.
//当本地STCP进程断开连接时, 这个函数等待下一个STCP进程的连接.
void waitSTCP() {
    int listenfd;
    socklen_t clilen;
    clilen = sizeof(struct sockaddr_in);
    struct sockaddr_in cliaddr,servaddr;

    if((listenfd = socket(AF_INET,SOCK_STREAM,0)) < 0){
        perror("Problem in creating the socket");
        exit(2);		
    }

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(SIP_PORT);

    bind(listenfd,(struct sockaddr*)&servaddr,sizeof(servaddr));
    listen(listenfd,8);

    stcp_conn = accept(listenfd,(struct sockaddr*)&cliaddr, &clilen);
    printf("STCP is connected\n");
    seg_t seg;
    int dest_nodeID;
    while(1){
        if(getsegToSend(stcp_conn,&seg,&dest_nodeID) == 1){
            int next_nodeID = routingtable_getnextnode(routingtable,dest_nodeID);
            sip_pkt_t pkt;
            pkt.header.dest_nodeID = dest_nodeID;
            pkt.header.src_nodeID = topology_getMyNodeID();
            pkt.header.length = strlen((char*)&seg);
            pkt.header.type = SIP;
            memcpy(pkt.data,&seg,sizeof(seg_t));
            son_sendpkt(next_nodeID,&pkt,son_conn);
        }
        else{
            printf("get seg from stcp failed\n");
            continue;
        }
    }
}

int main(int argc, char *argv[]) {
    printf("SIP layer is starting, pls wait...\n");

    //初始化全局变量
    nct = nbrcosttable_create();
    dv = dvtable_create();
    dv_mutex = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(dv_mutex,NULL);
    routingtable = routingtable_create();
    routingtable_mutex = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(routingtable_mutex,NULL);
    son_conn = -1;
    stcp_conn = -1;

    nbrcosttable_print(nct);
    dvtable_print(dv);
    routingtable_print(routingtable);

    //注册用于终止进程的信号句柄
    signal(SIGINT, sip_stop);

    //连接到本地SON进程 
    son_conn = connectToSON();
    if(son_conn<0) {
        printf("can't connect to SON process\n");
        exit(1);		
    }

    //启动线程处理来自SON进程的进入报文 
    pthread_t pkt_handler_thread; 
    pthread_create(&pkt_handler_thread,NULL,pkthandler,(void*)0);

    //启动路由更新线程 
    pthread_t routeupdate_thread;
    pthread_create(&routeupdate_thread,NULL,routeupdate_daemon,(void*)0);	

    printf("SIP layer is started...\n");
    printf("waiting for routes to be established\n");
    sleep(SIP_WAITTIME);
    routingtable_print(routingtable);

    //等待来自STCP进程的连接
    printf("waiting for connection from STCP process\n");
    waitSTCP(); 

}


