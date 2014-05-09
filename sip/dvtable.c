
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../common/constants.h"
#include "../topology/topology.h"
#include "dvtable.h"

//这个函数动态创建距离矢量表.
//距离矢量表包含n+1个条目, 其中n是这个节点的邻居数,剩下1个是这个节点本身.
//距离矢量表中的每个条目是一个dv_t结构,它包含一个源节点ID和一个有N个dv_entry_t结构的数组, 其中N是重叠网络中节点总数.
//每个dv_entry_t包含一个目的节点地址和从该源节点到该目的节点的链路代价.
//距离矢量表也在这个函数中初始化.从这个节点到其邻居的链路代价使用提取自topology.dat文件中的直接链路代价初始化.
//其他链路代价被初始化为INFINITE_COST.
//该函数返回动态创建的距离矢量表.
dv_t* dvtable_create()
{
    int node_num = topology_getNodeNum();
    int nbr_num = topology_getNbrNum();
    dv_t *dv_list = (dv_t *)malloc(sizeof(dv_t) * (nbr_num + 1));
    int *nbr_list = topology_getNbrArray();
    int *node_list = topology_getNodeArray();
    int i,j;

    dv_list[0].nodeID = topology_getMyNodeID();
    dv_list[0].dvEntry = (dv_entry_t *)malloc(sizeof(dv_entry_t) * node_num);
    for(j = 0; j < node_num; j++){
        dv_list[0].dvEntry[j].nodeID = node_list[j];
        dv_list[0].dvEntry[j].cost = topology_getCost(dv_list[0].nodeID, node_list[j]);
    }
    for(i = 1; i <= nbr_num; i++){
        dv_list[i].nodeID = nbr_list[i - 1];
        dv_list[i].dvEntry = (dv_entry_t *)malloc(sizeof(dv_entry_t) * node_num);
        for(j = 0; j < node_num; j++){
            dv_list[i].dvEntry[j].nodeID = node_list[j];
            dv_list[i].dvEntry[j].cost = INFINITE_COST;
        }
    }

    free(nbr_list);
    free(node_list);
    return dv_list;
}

//这个函数删除距离矢量表.
//它释放所有为距离矢量表动态分配的内存.
void dvtable_destroy(dv_t* dvtable)
{
    int nbr_num = topology_getNbrNum();
    int i;
    for (i = 0; i <= nbr_num; i++){
        free(dvtable[i].dvEntry);
    }
    free(dvtable);
    return;
}

//这个函数设置距离矢量表中2个节点之间的链路代价.
//如果这2个节点在表中发现了,并且链路代价也被成功设置了,就返回1,否则返回-1.
int dvtable_setcost(dv_t* dvtable,int fromNodeID,int toNodeID, unsigned int cost)
{
    int node_num = topology_getNodeNum();
    int nbr_num = topology_getNbrNum();
    int i,j;

    for (i = 0; i <= nbr_num; i++){
        if (dvtable[i].nodeID == fromNodeID){
            for (j = 0; j < node_num; j++){
                if (dvtable[i].dvEntry[j].nodeID == toNodeID){
                    dvtable[i].dvEntry[j].cost = cost;
                    return 1;
                }
            }
        }
    }

    return -1;
}

//这个函数返回距离矢量表中2个节点之间的链路代价.
//如果这2个节点在表中发现了,就返回链路代价,否则返回INFINITE_COST.
unsigned int dvtable_getcost(dv_t* dvtable, int fromNodeID, int toNodeID)
{
    int node_num = topology_getNodeNum();
    int nbr_num = topology_getNbrNum();
    int i,j;
   
    for (i = 0; i <= nbr_num; i++){
        if (dvtable[i].nodeID == fromNodeID){
            for (j = 0; j < node_num; j++){
                if (dvtable[i].dvEntry[j].nodeID == toNodeID){
                    return dvtable[i].dvEntry[j].cost;
                }
            }
        }
    }
        
    return INFINITE_COST;
}

//这个函数打印距离矢量表的内容.
void dvtable_print(dv_t* dvtable)
{
    int node_num = topology_getNodeNum();
    int nbr_num = topology_getNbrNum();
    int i,j;

    for (i = 0; i <= nbr_num; i++){
        printf("node_%d: ", dvtable[i].nodeID);
        for (j = 0; j < node_num; j++){
            printf("node_%d~>%u ", dvtable[i].dvEntry[j].nodeID, dvtable[i].dvEntry[j].cost);
        }
        printf("\n");
    }

    return;
}
