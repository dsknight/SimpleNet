#ifndef __IM_H__
#define __IM_H__
#include<stdint.h>
#include<pthread.h>
#include"list.h"
/*
the BMSG packet structure is as follows
<------ 4B -----><--2B--><--2B--><-----4B------->
+---------------+-------+-------+-------+-------+
|    B M S G    |service| status|   remaining   |
+-------+-------+-------+-------+-------+-------+
|                  source name                  |
+-------+-------+-------+-------+-------+-------+
|                destination name               |
+-------+-------+-------+-------+-------+-------+
|                                               |
:                    D A T A                    :
|                                               |
+-----------------------+-----------------------+

service
0:request for name
1:response for name
2:broadcast someone login
3:broadcast someone logout
4:request for msg
5:response for msg
6:server send name list to client

status
0:name exits
1:not exits
*/  

struct Biu_Packet
{
    char BMSG[4];//"BMSG"
    uint16_t service;
    uint16_t status;
    char remaining[4];
    char srcname[12];
    char dstname[12];
    char data[64]; 
};
typedef struct Biu_Packet Biu_Packet;


struct thread_node
{
	pthread_t tid;
	int sockfd;
	char name[12];
	struct ListHead list;
};
typedef struct thread_node thread_node;

struct name_node
{
	char name[12];
	struct ListHead list;
};
typedef struct name_node name_node;

void *process_requests(void *);

void *recv_messages(void *);

int is_name_exists(char*);
//void send_client_data(struct client_data *);
//void send_server_data(struct server_data *);
//
static inline void 
packet_init(struct Biu_Packet* packet)
{
	memset(packet,0,sizeof(Biu_Packet));
	strncpy(packet->BMSG,"BMSG",4);
}


#endif
