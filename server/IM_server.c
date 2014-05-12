#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<string.h>
#include<pthread.h>
#include"stcp_server.h"
#include"../include/IM.h"

#define SERV_PORT 6666
#define LISTENQ 16
#define MAX_SIZE 1024

static ListHead thread_head;
static pthread_mutex_t tnode_mutex;

int connectToSIP() {
    struct sockaddr_in servaddr;
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &servaddr.sin_addr);
    servaddr.sin_port = htons(SIP_PORT);
    if (connect(sock_fd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0){
        printf("error when connect to sip\n");
        return -1;
    }
    return sock_fd;
}

//这个函数断开到本地SIP进程的TCP连接. 
void disconnectToSIP(int sip_conn) {
    close(sip_conn);	
}

int main()
{
    //init stcp
	//用于丢包率的随机数种子
	srand(time(NULL));

	//连接到SIP进程并获得TCP套接字描述符
	int sip_conn = connectToSIP();
	if(sip_conn<0) {
		printf("can not connect to the local SIP process\n");
	}

	//初始化STCP服务器
	stcp_server_init(sip_conn);


	printf("\033[1H\033[2J");
	int listenfd,connfd;
	
	//initialize two list heads and mutex
	list_init(&thread_head);
	pthread_mutex_init(&tnode_mutex,NULL);

	//printf("$$$$$$$thread_head:%d$$$$$$$$\n",(int)&thread_head);
	if((listenfd = stcp_server_sock(SERV_PORT)) < 0)
	{
		perror("Problem in creating the socket\n");
		exit(1);
	}

	while(1)
	{
		connfd = stcp_server_accept(listenfd);
		thread_node *tnode = (thread_node *)malloc(sizeof(thread_node));
		memset(tnode,0,sizeof(thread_node));
		tnode->sockfd = connfd;
		list_add_before(&thread_head,&tnode->list);
		printf("\nReceived request...\n");
		int rc = pthread_create(&tnode->tid,NULL,process_requests,(void*)tnode);//tid problem
		if(rc)
		{
			printf("Error;return code from pthread_create() is %d\n",rc);
			exit(-1);
		}
	}


	//remember to deltele link list
}

void *process_requests(void *tnode)//thread funtion,every thread deals with a connection
{
	thread_node *my_tnode = (thread_node*)tnode;
	printf("now is in thread %d\n",(int)my_tnode->tid);
	int recv_bytes,connfd =	my_tnode->sockfd;
	struct Biu_Packet *recv_packet = (Biu_Packet*)malloc(sizeof(Biu_Packet));
	struct Biu_Packet *send_packet = (Biu_Packet*)malloc(sizeof(Biu_Packet));
	while((recv_bytes = stcp_server_recv(connfd,(char*)recv_packet,sizeof(Biu_Packet))) > 0)
	{
		printf("\nPacket received from a client,service = %d\n",recv_packet->service);
		if(recv_packet->service == 0){//request for valid name
			printf("============ a client request for name %s==============\n",recv_packet->data);
			packet_init(send_packet);
			send_packet->service = 1;
			send_packet->status = is_name_exists(recv_packet->data) ? 0 : 1;
			strncpy(send_packet->data,recv_packet->data,64);
			stcp_server_send(connfd,(char*)send_packet,sizeof(Biu_Packet));
			printf("========== response for name %s has been sent==========\n",recv_packet->data);
			if(send_packet->status){//if the name is valid
				pthread_mutex_lock(&tnode_mutex);
				strncpy(my_tnode->name,recv_packet->data,12);
                               	//send login information to other clients
				send_packet->service = 2;
				ListHead *ptr;
				list_foreach(ptr,&thread_head){
					if(ptr == &my_tnode->list)
						continue;//do not send login msg to himself
					stcp_server_send(list_entry(ptr,thread_node,list)->sockfd,(char*)send_packet,sizeof(Biu_Packet));

				}

				//send name list to this cilent
				packet_init(send_packet);
				send_packet->service = 6;
				int cnt = 0;
				list_foreach(ptr,&thread_head){
					strncpy(send_packet->data + 12*cnt,list_entry(ptr,thread_node,list)->name,12);
					cnt = (cnt+1) % 5;
					if(cnt == 0){
						stcp_server_send(connfd,(char*)send_packet,sizeof(Biu_Packet));
						memset(&send_packet->data,0,64);
					}
				}
				pthread_mutex_unlock(&tnode_mutex);
				if(strlen(send_packet->data) != 0){//every 5 name send a packet,now send the remainings
					stcp_server_send(connfd,(char*)send_packet,sizeof(Biu_Packet));
				}
				printf("=====name %s is valid and name list has been sent to him======\n",recv_packet->data);


			}
			
		}
		else if(recv_packet->service == 4){//request for sending msg	
			printf("============ name %s request for sending msg to %s==============\n",recv_packet->srcname,recv_packet->dstname);
			memcpy(send_packet,recv_packet,sizeof(Biu_Packet));
			send_packet->service = 5;
			send_packet->status = 1;
			ListHead *ptr;
			if(strcmp(recv_packet->dstname,"BiuBiuBiu") == 0){
				pthread_mutex_lock(&tnode_mutex);
				list_foreach(ptr,&thread_head){
					stcp_server_send(list_entry(ptr,thread_node,list)->sockfd,(char*)send_packet,sizeof(Biu_Packet));
				}
				pthread_mutex_unlock(&tnode_mutex);
			}
			else{
				if(!is_name_exists(recv_packet->dstname)){
					send_packet->status = 0;
					stcp_server_send(connfd,(char*)send_packet,sizeof(Biu_Packet));	
				}
				else{
					int sockfd;
					pthread_mutex_lock(&tnode_mutex);
					list_foreach(ptr,&thread_head){
						if(strcmp(list_entry(ptr,thread_node,list)->name,recv_packet->dstname) == 0){
							sockfd = list_entry(ptr,thread_node,list)->sockfd;
							break;
						}
					}	
					pthread_mutex_unlock(&tnode_mutex);
					stcp_server_send(sockfd,(char*)send_packet,sizeof(Biu_Packet));
					stcp_server_send(connfd,(char*)send_packet,sizeof(Biu_Packet));//send the response to both side
				}
			}

			printf("============ msg '%s' has been sent==============\n",recv_packet->data);
		}
			
	}

	if(recv_bytes < 0)
		printf("Read error\n");

	//broadcast the client logout
	packet_init(send_packet);
	send_packet->service = 3;
	pthread_mutex_lock(&tnode_mutex);
	strcpy(send_packet->data,my_tnode->name);
	ListHead *ptr;
	list_foreach(ptr,&thread_head){
		if(ptr == &my_tnode->list)
			continue;
		stcp_server_send(list_entry(ptr,thread_node,list)->sockfd,(char*)send_packet,sizeof(Biu_Packet));
	}

	printf("thread %d is going to exit!\n",(int)my_tnode->tid);
	//delete thread node
	list_del(&my_tnode->list);
	free(my_tnode);
	pthread_mutex_unlock(&tnode_mutex);
	//free and exit
	stcp_server_close(connfd);
	free(recv_packet);
	free(send_packet);
	pthread_exit(NULL);
}

int is_name_exists(char *name)
{
	ListHead *ptr;
	pthread_mutex_lock(&tnode_mutex);
	list_foreach(ptr,&thread_head)
	{
		if(strcmp(list_entry(ptr,thread_node,list)->name,name) == 0){
			pthread_mutex_unlock(&tnode_mutex);
			return 1;
		}
	}
	pthread_mutex_unlock(&tnode_mutex);
	return 0;
}















