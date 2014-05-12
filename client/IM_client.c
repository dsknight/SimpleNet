#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<curses.h>
#include<unistd.h>
#include "stcp_client.h"
#include"../include/IM.h"
#include"../common/constants.h"
#include"../topology/topology.h"

#define MAX_SIZE 1024
#define SERV_PORT 6666
#define CLIENT_PORT 5555

static ListHead name_head;
static pthread_mutex_t nnode_mutex;
static WINDOW *title_win,*display_win,*input_win,*name_win,*display_box,*input_box,*name_box;
static char nick_name[MAX_SIZE];

//这个函数连接到本地SIP进程的端口SIP_PORT. 如果TCP连接失败, 返回-1. 连接成功, 返回TCP套接字描述符, STCP将使用该描述符发送段.
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

void window_init(){
	//init boxes,boxes have boader,contain window
	display_box = subwin(stdscr,LINES-5,COLS-15,1,0);
	input_box = subwin(stdscr,4,COLS,LINES-4,0);
	name_box = subwin(stdscr,LINES-5,15,1,COLS-15);
	//init windows
	title_win = subwin(stdscr,1,COLS,0,0);
	display_win = newwin(LINES-7,COLS-17,2,1);
	input_win = newwin(2,COLS-2,LINES-3,1);
	name_win = newwin(LINES-7,13,2,COLS-14);
	//set box boaders
	box(display_box,ACS_VLINE,ACS_HLINE);
	box(input_box,ACS_VLINE,ACS_HLINE);
	box(name_box,ACS_VLINE,ACS_HLINE);
	//set window scroll ok
	scrollok(display_win,1);
	scrollok(input_win,1);
	scrollok(name_win,1);
}

int char_at(char* str, int len, char ch){
	int i = 0;
	while(++i < len)
		if(str[i] == ch)
			return i;
	return -1;
}

int name_valid(char * name){
	if(strcmp("BiuBiuBiu",name) == 0 )
		return 1;
	ListHead *ptr;
	pthread_mutex_lock(&nnode_mutex);
	list_foreach(ptr,&name_head){
		char *tmp = list_entry(ptr,name_node,list)->name; 
		if(strcmp(tmp,name) == 0 && strncmp(tmp,nick_name,strlen(nick_name)-1) != 0){
			pthread_mutex_unlock(&nnode_mutex);
			return 1;
		}
	}
	pthread_mutex_unlock(&nnode_mutex);
	return 0;

}

int main()
{
    //init stcp
    //用于丢包率的随机数种子
	srand(time(NULL));

	//连接到SIP进程并获得TCP套接字描述符	
	int sip_conn = connectToSIP();
	if(sip_conn<0) {
		printf("fail to connect to the local SIP process\n");
		exit(1);
	}

	//初始化stcp客户端
	stcp_client_init(sip_conn);
	sleep(1);


	//show basic information
	printf("\033[1H\033[2J");
	printf("Welcome to BiuBiuBiu IM system!\nYou need a nick name (4-10 characters, not BiuBiuBiu, no $):");
	fgets(nick_name,MAX_SIZE,stdin);
	while(strlen(nick_name) > 11 || strlen(nick_name) < 5 || strncmp(nick_name,"BiuBiuBiu",9) == 0 
			|| char_at(nick_name,strlen(nick_name),'$') >= 0){//BiuBiuBiu is not valid name
		printf("Your nick name is out of range!\nre-enter your nick name (4-10 characters, not BiuBiuBiu, no $): ");
		fgets(nick_name,MAX_SIZE,stdin);
	}

	int sockfd;

	// socket init
	if((sockfd = stcp_client_sock(CLIENT_PORT)) <0)
	{
		perror("Problem in connecting to the server!\n");
		exit(1);
	}


	if(stcp_client_connect(sockfd,SERV_PORT,topology_getNodeIDfromname("csnetlab_4")) <0)
	{
		perror("Problem in connecting to the server!\n");
		exit(2);
	}

	//request for valid nick name
	struct Biu_Packet *send_packet = (struct Biu_Packet*)malloc(sizeof(Biu_Packet));
	packet_init(send_packet);
	send_packet->service = 0;
	strncpy(send_packet->data,nick_name,strlen(nick_name)-1);
	stcp_client_send(sockfd,(char*)send_packet,sizeof(Biu_Packet));

	struct Biu_Packet *recv_packet = (struct Biu_Packet*)malloc(sizeof(Biu_Packet));
	if(stcp_client_recv(sockfd,(char*)(recv_packet),sizeof(Biu_Packet)) < 0)
	{
		perror("The server terminated prematurely\n");
		exit(3);
	}

	while(!recv_packet->status && recv_packet->service == 1)
	{
		printf("Your nick name is already exists in the IM system!\n");
		printf("Please re-enter your nick name (4-10 characters, not BiuBiuBiu, no $): ");
		fgets(nick_name,MAX_SIZE,stdin);
		while(strlen(nick_name) > 11 || strlen(nick_name) < 5 || strncmp(nick_name,"BiuBiuBiu",9) == 0 
			|| char_at(nick_name,strlen(nick_name),'$') >= 0){
		printf("Your nick name is out of range!\nre-enter your nick name (4-10 characters, not BiuBiuBiu, no $): ");
		fgets(nick_name,MAX_SIZE,stdin);
		}
		memset(&send_packet->data,0,64);
		strncpy(send_packet->data,nick_name,strlen(nick_name)-1);
		stcp_client_send(sockfd,(char*)send_packet,sizeof(Biu_Packet));
		if(stcp_client_recv(sockfd,(char*)(recv_packet),sizeof(Biu_Packet)) < 0)
		{
			perror("The server terminated prematurely\n");
			exit(3);
		}
	}

	//nick name is valid and begin 
	printf("\033[1H\033[2J");
	initscr();
	window_init();
	wprintw(title_win,"Hi %s ! Welcome to BiuBiuBiu chatting room! #(exit) $(show name list)\n",send_packet->data);
	refresh();
	list_init(&name_head);
	pthread_mutex_init(&nnode_mutex,NULL);

	pthread_t tid;
	pthread_create(&tid,NULL,recv_messages,(void*)sockfd);

	//get input and send msgs
	while(1){
		char content[MAX_SIZE];
		wgetnstr(input_win,content,MAX_SIZE);
		while(strlen(content) > 60 || strlen(content) == 0) {
			if(strlen(content) > 60)
				wprintw(display_win,"BiuBiuBiu : Your content should be no more than 60 characters!\n");
			else
				wprintw(display_win,"BiuBiuBiu : you sent nothing\n");
			wrefresh(display_win);
			wgetnstr(input_win,content,MAX_SIZE);
		}
		//content is valid and process
		if(strlen(content) == 1){
			if(content[0] == '#'){
                if(stcp_client_disconnect(sockfd)<0) {
                    printf("fail to disconnect from stcp server\n");
                    exit(1);
                }
                if(stcp_client_close(sockfd)<0) {
                    printf("fail to close stcp client\n");
                    exit(1);
                }
                endwin();
				exit(0);
			}
			else if(content[0] == '$'){
				wclear(name_win);
				wprintw(name_win,"name list:\n");
				ListHead *ptr;
				list_foreach(ptr,&name_head){
					wprintw(name_win,"%s\n",list_entry(ptr,name_node,list)->name);
				}
				wrefresh(name_win);
			}
		}
		else{//send msg
			int divide = char_at(content,11,'$');
			if(divide <= 0){// send to all
				packet_init(send_packet);
				strncpy(send_packet->srcname,nick_name,strlen(nick_name)-1);
				strncpy(send_packet->dstname,"BiuBiuBiu",12);
				strncpy(send_packet->data,content,64);
				send_packet->service = 4;
				stcp_client_send(sockfd,(char*)send_packet,sizeof(Biu_Packet));
			}
			else{//send to one

				packet_init(send_packet);
				strncpy(send_packet->srcname,nick_name,strlen(nick_name)-1);
				strncpy(send_packet->dstname,content,divide);
				if(!name_valid(send_packet->dstname)){
					wprintw(display_win,"BiuBiuBiu to you : Error! %s is not a valid name!\n",send_packet->dstname);
					wrefresh(display_win);
					continue;
				}
				strncpy(send_packet->data,content+divide+1,64);
				send_packet->service = 4;
				stcp_client_send(sockfd,(char*)send_packet,sizeof(Biu_Packet));
			}
		}

	}

	free(send_packet);
	free(recv_packet);
    disconnectToSIP(sip_conn);
	exit(0);
}

void *recv_messages(void *sockfd)
{
	Biu_Packet packet;
	while(stcp_client_recv((int)sockfd,(char*)&packet,sizeof(Biu_Packet)) > 0)
	{
		if(packet.service <= 1 || packet.service == 4 || packet.service > 6){
			printf("service error:%d\n",packet.service);
			exit(4);
		}
		else if(packet.service == 6){//names start every 12B in data
			int i = 0;
			pthread_mutex_lock(&nnode_mutex);
			while(strlen(packet.data + 12*i)){
				name_node *nnode = (name_node*)malloc(sizeof(name_node));
				strncpy(nnode->name,packet.data + 12*i,12);
				list_add_before(&name_head,&nnode->list);
				i++;
			}
			pthread_mutex_unlock(&nnode_mutex);
		}
		else if(packet.service == 5){
			if(packet.status == 0){
				wprintw(display_win,"BiuBiuBiu to you : %s is offline\n",packet.dstname);
				wrefresh(display_win);
			}
			else{
				if(strcmp(packet.dstname,"BiuBiuBiu") == 0){
					wprintw(display_win,"%s to All :\n%s\n",packet.srcname,packet.data);
					wrefresh(display_win);
				}
				else if(strncmp(packet.srcname,nick_name,strlen(nick_name)-1) == 0){
					wprintw(display_win,"You to %s :\n%s\n",packet.dstname,packet.data);
					wrefresh(display_win);
				}
				else{
					wprintw(display_win,"%s to you :\n%s\n",packet.srcname,packet.data);
					wrefresh(display_win);
				}
			}
		}
		else if(packet.service == 3){//data store the name who logout
			wprintw(display_win,"BiuBiuBiu to All : %s logout!\n",packet.data);
			wrefresh(display_win);
			ListHead *ptr;
			pthread_mutex_lock(&nnode_mutex);
			list_foreach(ptr,&name_head){
				name_node *tmp = list_entry(ptr,name_node,list);
				if(strcmp(tmp->name,packet.data) == 0){
					list_del(ptr);
					free(tmp);
					break;
				}
			}
			pthread_mutex_unlock(&nnode_mutex);

		}
		else if(packet.service == 2){//data store the name who login
			wprintw(display_win,"BiuBiuBiu to All : %s login!\n",packet.data);
			wrefresh(display_win);
			name_node *new_name = (name_node*)malloc(sizeof(name_node));
			strncpy(new_name->name,packet.data,12);
			pthread_mutex_lock(&nnode_mutex);
			list_add_before(&name_head,&new_name->list);
			pthread_mutex_unlock(&nnode_mutex);
		}
		else{
			printf("service error : %d\n",packet.service);
			exit(4);
		}

	}


	pthread_exit(NULL);
}









