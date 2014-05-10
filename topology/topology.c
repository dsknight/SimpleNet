//文件名: topology/topology.c
//
//描述: 这个文件实现一些用于解析拓扑文件的辅助函数 
//
//创建日期: 2013年

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include "topology.h"
#include "../common/constants.h"

struct topology_t{
    char            node1[32];
    int             id1;
    char            node2[32];
    int             id2;
    unsigned int    cost;
};

struct topology_t   topology_table[10];
bool                scanned = false;
int                 table_num = 0;
int                 host_id = -1;

static void topology_scan(){
    FILE *fp = fopen("../topology/topology.dat", "r");
    assert(fp != NULL);
    table_num = 0;
    scanned = true;
    struct topology_t *tp = &topology_table[table_num];
    while (fscanf(fp, "%s %s %u\n", tp->node1, tp->node2, &tp->cost) != EOF){
        tp->id1 = topology_getNodeIDfromname(tp->node1);
        tp->id2 = topology_getNodeIDfromname(tp->node2);
        assert(tp->id1 >= 0 && tp->id1 <= 255 && tp->id2 >= 0 && tp->id2 <= 255);
        tp++;
        table_num++;
    }

    fclose(fp);
}

//这个函数返回指定主机的节点ID.
//节点ID是节点IP地址最后8位表示的整数.
//例如, 一个节点的IP地址为202.119.32.12, 它的节点ID就是12.
//如果不能获取节点ID, 返回-1.
int topology_getNodeIDfromname(char* hostname) 
{
    struct hostent *hptr;
    if ((hptr = gethostbyname(hostname)) == NULL){
        return -1;
    }

    return (int)(((unsigned char *)*hptr->h_addr_list)[3]);
}

//这个函数返回指定的IP地址的节点ID.
//如果不能获取节点ID, 返回-1.
int topology_getNodeIDfromip(struct in_addr* addr)
{
    return (addr->s_addr >> 24) & 0xff;
}

//这个函数返回本机的节点ID
//如果不能获取本机的节点ID, 返回-1.
int topology_getMyNodeID()
{
    if (host_id == -1) {
        /*FILE *fp;
        char hostname[80];
        struct in_addr addr;

        fp = popen("hostname -i", "r");
        if (fp == NULL){
            printf("can't get NodeID\n");
            return -1;
        }

        if (fgets(hostname, sizeof(hostname) - 1, fp) == NULL){
            printf("can't get NodeID\n");
            return -1;
        }

        if (inet_aton(hostname, &addr) == 0){
            printf("error ip address\n");
            return -1;
        }

        pclose(fp);
        host_id = (addr.s_addr >> 24) & 0xff;
        return host_id;*/
        char host[20];
        if (gethostname(host, 20) == -1){
            int tmp = errno;
            printf("%s\n", strerror(tmp));
            return -1;
        }
        host_id = topology_getNodeIDfromname(host);
        return host_id;
    } else 
        return host_id;
}

//这个函数解析保存在文件topology.dat中的拓扑信息.
//返回邻居数.
int topology_getNbrNum()
{
    if (scanned == false)
        topology_scan();
    
    int local_id = topology_getMyNodeID();
    int i;
    int nbrnum = 0;
    for (i = 0; i < table_num; i++){
        if (topology_table[i].id1 == local_id || topology_table[i].id2 == local_id)
            nbrnum++;
    }

    return nbrnum;
}

//这个函数解析保存在文件topology.dat中的拓扑信息.
//返回重叠网络中的总节点数.
int topology_getNodeNum()
{
    if (scanned == false)
        topology_scan();
    
    int hash[256];
    int nodesum = 0;
    int i;
    memset((void *)hash, 0, sizeof(hash));
    
    for (i = 0; i < table_num; i++){
        int id1 = topology_table[i].id1;
        int id2 = topology_table[i].id2;
        if (hash[id1] == 0){
            hash[id1]++;
            nodesum++;
        }
        if (hash[id2] == 0){
            hash[id2]++;
            nodesum++;
        }
    }

    return nodesum;
}

//这个函数解析保存在文件topology.dat中的拓扑信息.
//返回一个动态分配的数组, 它包含重叠网络中所有节点的ID. 
//it return an array end by -1
int* topology_getNodeArray()
{
    if (scanned == false)
        topology_scan();
    
    int *id_list = (int *)malloc(sizeof(int) * 20);
    int i;
    for (i = 0; i < 20; i++)
        id_list[i] = -1;

    for (i = 0; i < table_num; i++){
        int id1 = topology_table[i].id1;
        int id2 = topology_table[i].id2;
        int *p = id_list;
        while(*p != -1){
            if (*p == id1)
                break;
            p++;
        }
        *p = id1;
        p = id_list;
        while(*p != -1){
            if (*p == id2)
                break;
            p++;
        }
        *p = id2;
    }

    return id_list;
}

//这个函数解析保存在文件topology.dat中的拓扑信息.
//返回一个动态分配的数组, 它包含所有邻居的节点ID.  
int* topology_getNbrArray()
{
    if (scanned == false)
        topology_scan();
    
    int *id_list = (int *)malloc(sizeof(int) * 20);
    int i;
    for (i = 0; i < 20; i++)
        id_list[i] = -1;

    int local_id = topology_getMyNodeID();

    for (i = 0; i < table_num; i++){
        int neighbor;
        if (topology_table[i].id1 == local_id)
            neighbor = topology_table[i].id2;
        else if (topology_table[i].id2 == local_id)
            neighbor = topology_table[i].id1;
        else
            continue;

        int *p = id_list;
        while(*p != -1){
            if (*p == neighbor)
                break;
            p++;
        }
        *p = neighbor;
    }

    return id_list;
}

//这个函数解析保存在文件topology.dat中的拓扑信息.
//返回指定两个节点之间的直接链路代价. 
//如果指定两个节点之间没有直接链路, 返回INFINITE_COST.
unsigned int topology_getCost(int fromNodeID, int toNodeID)
{
    if (scanned == false)
        topology_scan();
    if (fromNodeID == toNodeID)
        return 0;

    int i;
    for (i = 0; i < table_num; i++){
        if (fromNodeID == topology_table[i].id1 && toNodeID == topology_table[i].id2)
            return topology_table[i].cost;
        if (fromNodeID == topology_table[i].id2 && toNodeID == topology_table[i].id1)
            return topology_table[i].cost;
    }

    return INFINITE_COST;
}
