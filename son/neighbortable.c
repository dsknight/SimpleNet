//文件名: son/neighbortable.c
//
//描述: 这个文件实现用于邻居表的API
//
//创建日期: 2013年
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "neighbortable.h"
#include "../topology/topology.h"

//这个函数首先动态创建一个邻居表. 然后解析文件topology/topology.dat, 填充所有条目中的nodeID和nodeIP字段, 将conn字段初始化为-1.
//返回创建的邻居表.
nbr_entry_t* nt_create()
{
    int nbr_num = topology_getNbrNum();
    int *nbr_list = topology_getNbrArray(); 

    nbr_entry_t *nbr_entry_list = (nbr_entry_t *)malloc(nbr_num * sizeof(nbr_entry_t));
    nbr_entry_t *p = nbr_entry_list;

    int i;
    for (i = 0; i < nbr_num; i++){
        p->nodeID = nbr_list[i];
        p->conn = -1;
        char ip[20];
        sprintf(ip, "114.212.190.%d", p->nodeID);
        inet_aton(ip, (struct in_addr *)&p->nodeIP);
        p++;
    }

    free(nbr_list);

    return nbr_entry_list;
}

//这个函数删除一个邻居表. 它关闭所有连接, 释放所有动态分配的内存.
void nt_destroy(nbr_entry_t* nt)
{
    int nbr_num = topology_getNbrNum();
    int i;

    for (i = 0; i < nbr_num; i++){
        close(nt[i].conn);
    }

    free(nt);

    return;
}

//这个函数为邻居表中指定的邻居节点条目分配一个TCP连接. 如果分配成功, 返回1, 否则返回-1.
int nt_addconn(nbr_entry_t* nt, int nodeID, int conn)
{
    int nbr_num = topology_getNbrNum();
    int i = 0;
    
    for (i = 0; i < nbr_num; i++){
        if (nt[i].nodeID == nodeID){
            nt[i].conn = conn;
            return 1;
        }
    }
    return -1;
}
