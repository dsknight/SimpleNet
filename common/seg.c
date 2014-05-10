//
// 文件名: seg.c

// 描述: 这个文件包含用于发送和接收STCP段的接口sip_sendseg() and sip_rcvseg(), 及其支持函数的实现. 
//
// 创建日期: 2013年
//

#include "seg.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <errno.h>
#include "debug.h"

static int readn( int fd, char *bp, size_t len)
{
    int cnt;
    int rc;

    cnt = len;
    while ( cnt > 0 )
    {
        rc = recv( fd, bp, cnt, 0 );
        if ( rc < 0 )               /* read error? */
        {
            if ( errno == EINTR )   /* interrupted? */
                continue;           /* restart the read */
            return -1;              /* return error */
        }
        if ( rc == 0 )              /* EOF? */
            return len - cnt;       /* return short count */
        bp += rc;
        cnt -= rc;
    }
    return len;
}
//
//
//  用于客户端和服务器的SIP API 
//  =======================================
//
//  我们在下面提供了每个函数调用的原型定义和细节说明, 但这些只是指导性的, 你完全可以根据自己的想法来设计代码.
//
//  注意: sip_sendseg()和sip_recvseg()是由网络层提供的服务, 即SIP提供给STCP.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//

// 通过重叠网络(在本实验中，是一个TCP连接)发送STCP段. 因为TCP以字节流形式发送数据, 
// 为了通过重叠网络TCP连接发送STCP段, 你需要在传输STCP段时，在它的开头和结尾加上分隔符. 
// 即首先发送表明一个段开始的特殊字符"!&"; 然后发送seg_t; 最后发送表明一个段结束的特殊字符"!#".  
// 成功时返回1, 失败时返回-1. sip_sendseg()首先使用send()发送两个字符, 然后使用send()发送seg_t,
// 最后使用send()发送表明段结束的两个字符.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
int sip_sendseg(int connection, seg_t* segPtr, int dest_nodeID)
{
    segPtr->header.checksum = checksum(segPtr);
    sendseg_arg_t sendSeg;
    sendSeg.nodeID = dest_nodeID;
    sendSeg.seg = *segPtr;
    debug_printf("send form socket:%d\n", connection);
    if(send(connection,"!&",2,0) > 0)
        if(send(connection,&sendSeg,sizeof(sendseg_arg_t),0) > 0)
            if(send(connection,"!#",2,0) > 0)
                return 1;
    return -1;
}

// 通过重叠网络(在本实验中，是一个TCP连接)接收STCP段. 我们建议你使用recv()一次接收一个字节.
// 你需要查找"!&", 然后是seg_t, 最后是"!#". 这实际上需要你实现一个搜索的FSM, 可以考虑使用如下所示的FSM.
// SEGSTART1 -- 起点 
// SEGSTART2 -- 接收到'!', 期待'&' 
// SEGRECV -- 接收到'&', 开始接收数据
// SEGSTOP1 -- 接收到'!', 期待'#'以结束数据的接收
// 这里的假设是"!&"和"!#"不会出现在段的数据部分(虽然相当受限, 但实现会简单很多).
// 你应该以字符的方式一次读取一个字节, 将数据部分拷贝到缓冲区中返回给调用者.
//
// 注意: 还有一种处理方式可以允许"!&"和"!#"出现在段首部或段的数据部分. 具体处理方式是首先确保读取到!&，然后
// 直接读取定长的STCP段首部, 不考虑其中的特殊字符, 然后按照首部中的长度读取段数据, 最后确保以!#结尾.
//
// 注意: 在你剖析了一个STCP段之后,  你需要调用seglost()来模拟网络中数据包的丢失. 
// 在sip_recvseg()的下面是seglost(seg_t* segment)的代码.
//
// 一个段有PKT_LOST_RATE/2的可能性丢失, 或PKT_LOST_RATE/2的可能性有着错误的校验和.
// 如果数据包丢失了, 就返回1, 否则返回0. 
// 即使段没有丢失, 它也有PKT_LOST_RATE/2的可能性有着错误的校验和.
// 我们在段中反转一个随机比特来创建错误的校验和.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// 

int sip_recvseg(int sip_conn, seg_t* segPtr, int *src_nodeID)
{
    int state = 0;
    char ch;
    
    debug_printf("try to get a seg from sip_conn = %d\n",sip_conn);
    while(readn(sip_conn,&ch,1) > 0){
        switch(state){
            case 0 : 
                {
                    if(ch == '!')
                        state = 1;
                    break;
                }
            case 1 :
                {
                    if(ch == '&'){
                        state = 2;
                        if(readn(sip_conn,(void *)src_nodeID,sizeof(int)) < 0){
                            printf("error occurs when readn in %s\n",__func__);
                            return -1;
                        }
                        if(readn(sip_conn,(void *)segPtr,sizeof(seg_t)) < 0){
                            printf("error occurs when readn in %s\n",__func__);
                            return -1;
                        }
                    }
                    else 
                        state = 0;
                    break;
                }
            case 2 :
                {
                    if(ch == '!')
                        state = 3;
                    else
                        return -1;
                    break;
                }
            case 3 :
                {
                    if(ch == '#'){
                        if (seglost(segPtr) == 1){
                            printf("we lost a pack\n");
                            continue;
                        }
                        if (checkchecksum(segPtr) == -1){
                            printf("checksum error, abandom the pack\n");
                            continue;
                        }
                        return 1;
                    }
                    else
                        return -1;
                }
        }
    }   
    return -1;
}


//SIP进程使用这个函数接收来自STCP进程的包含段及其目的节点ID的sendseg_arg_t结构.
//参数stcp_conn是在STCP进程和SIP进程之间连接的TCP描述符.
//如果成功接收到sendseg_arg_t就返回1, 否则返回-1.
int getsegToSend(int stcp_conn, seg_t* segPtr, int *dest_nodeID)
{
    int state = 0;
    char ch;

    while(readn(stcp_conn,&ch,1) > 0){
        switch(state){
            case 0 : 
                {
                    if(ch == '!')
                        state = 1;
                    break;
                }
            case 1 :
                {
                    if(ch == '&'){
                        state = 2;
                        if(readn(stcp_conn,(void *)dest_nodeID,sizeof(int)) < 0){
                            printf("error occurs when readn in %s\n",__func__);
                            return -1;
                        }
                        if(readn(stcp_conn,(void *)segPtr,sizeof(seg_t)) < 0){
                            printf("error occurs when readn in %s\n",__func__);
                            return -1;
                        }
                    }
                    else 
                        state = 0;
                    break;
                }
            case 2 :
                {
                    if(ch == '!')
                        state = 3;
                    else
                        return -1;
                    break;
                }
            case 3 :
                {
                    if(ch == '#')
                        return 1;
                    else
                        return -1;
                }
        }
    }   
    return -1;
}

//SIP进程使用这个函数发送包含段及其源节点ID的sendseg_arg_t结构给STCP进程.
//参数stcp_conn是STCP进程和SIP进程之间连接的TCP描述符.
//如果sendseg_arg_t被成功发送就返回1, 否则返回-1.
int forwardsegToSTCP(int stcp_conn, seg_t* segPtr, int src_nodeID)
{
    sendseg_arg_t sendSeg;
    sendSeg.nodeID = src_nodeID;
    sendSeg.seg = *segPtr;
    if(send(stcp_conn,"!&",2,0) > 0)
        if(send(stcp_conn,&sendSeg,sizeof(sendseg_arg_t),0) > 0)
            if(send(stcp_conn,"!#",2,0) > 0)
                return 1;
    return -1;
}

// 一个段有PKT_LOST_RATE/2的可能性丢失, 或PKT_LOST_RATE/2的可能性有着错误的校验和.
// 如果数据包丢失了, 就返回1, 否则返回0. 
// 即使段没有丢失, 它也有PKT_LOST_RATE/2的可能性有着错误的校验和.
// 我们在段中反转一个随机比特来创建错误的校验和.
int seglost(seg_t* segPtr) {
    int random = rand()%100;
    if(random<PKT_LOSS_RATE*100) {
        //50%可能性丢失段
        if(rand()%2==0) {
            printf("seg lost!!!\n");
            return 1;
        }
        //50%可能性是错误的校验和
        else {
            //获取数据长度
            int len = sizeof(stcp_hdr_t)+segPtr->header.length;
            //获取要反转的随机位
            int errorbit = rand()%(len*8);
            //反转该比特
            char* temp = (char*)segPtr;
            temp = temp + errorbit/8;
            *temp = *temp^(1<<(errorbit%8));
            return 0;
        }
    }
    return 0;
}

//这个函数计算指定段的校验和.
//校验和覆盖段首部和段数据. 你应该首先将段首部中的校验和字段清零, 
//如果数据长度为奇数, 添加一个全零的字节来计算校验和.
//校验和计算使用1的补码.
unsigned short checksum(seg_t* segment)
{
    segment->header.checksum = 0x0;
    int count = sizeof(seg_t);
    char *addr = (char *)segment;
    unsigned long sum = 0;
    while(count > 1){
        sum += *(unsigned short*)addr;
        addr += 2;
        count -= 2;
    }
    //add remaining byte
    if(count > 0)
        sum += *(unsigned char*)addr;
    while(sum >> 16)
        sum = (sum & 0xffff) + (sum >> 16);
    return ~sum;
}

//这个函数检查段中的校验和, 正确时返回1, 错误时返回-1
int checkchecksum(seg_t* segment)
{
    int count = sizeof(seg_t);
    char *addr = (char *)segment;
    unsigned long sum = 0;
    while(count > 1){
        sum += *(unsigned short*)addr;
        addr += 2;
        count -= 2;
    }
    //add remaining byte
    if(count > 0)
        sum += *(unsigned char*)addr;
    while(sum >> 16)
        sum = (sum & 0xffff) + (sum >> 16);
    if(sum != 0xffff)
        return -1;
    else
        return 1;
}
