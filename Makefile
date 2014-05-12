CC 		= gcc
CFLAGS	= -Wall -pedantic -std=gnu99 -g -pthread -I./include

all: son/son sip/sip client/IM_client server/IM_server 

debug: CFLAGS = -Wall -pedantic -std=c99 -g -pthread -D DEBUG
debug: all

common/pkt.o: common/pkt.c common/pkt.h common/constants.h
	$(CC) $(CFLAGS) -c common/pkt.c -o common/pkt.o
topology/topology.o: topology/topology.c 
	$(CC) $(CFLAGS) -c topology/topology.c -o topology/topology.o
son/neighbortable.o: son/neighbortable.c
	$(CC) $(CFLAGS) -c son/neighbortable.c -o son/neighbortable.o
son/son: topology/topology.o common/pkt.o son/neighbortable.o son/son.c 
	$(CC) $(CFLAGS) -pthread son/son.c topology/topology.o common/pkt.o son/neighbortable.o -o son/son
sip/nbrcosttable.o: sip/nbrcosttable.c
	$(CC) $(CFLAGS) -c sip/nbrcosttable.c -o sip/nbrcosttable.o
sip/dvtable.o: sip/dvtable.c
	$(CC) $(CFLAGS) -c sip/dvtable.c -o sip/dvtable.o
sip/routingtable.o: sip/routingtable.c
	$(CC) $(CFLAGS) -c sip/routingtable.c -o sip/routingtable.o
sip/sip: common/pkt.o common/seg.o topology/topology.o sip/nbrcosttable.o sip/dvtable.o sip/routingtable.o sip/sip.c 
	$(CC) $(CFLAGS) -pthread sip/nbrcosttable.o sip/dvtable.o sip/routingtable.o common/pkt.o common/seg.o topology/topology.o sip/sip.c -o sip/sip 
client/IM_client: client/IM_client.c common/seg.o client/stcp_client.o topology/topology.o include/IM.h include/list.h
	$(CC) $(CFLAGS) client/IM_client.c common/seg.o client/stcp_client.o topology/topology.o -o client/IM_client -lncurses
server/IM_server: server/IM_server.c common/seg.o server/stcp_server.o topology/topology.o include/IM.h include/list.h
	$(CC) $(CFLAGS) server/IM_server.c common/seg.o server/stcp_server.o topology/topology.o -o server/IM_server
common/seg.o: common/seg.c common/seg.h
	$(CC) $(CFLAGS) -c common/seg.c -o common/seg.o
client/stcp_client.o: client/stcp_client.c client/stcp_client.h 
	$(CC) $(CFLAGS) -c client/stcp_client.c -o client/stcp_client.o
server/stcp_server.o: server/stcp_server.c server/stcp_server.h
	$(CC) $(CFLAGS) -c server/stcp_server.c -o server/stcp_server.o

OBJ = $(shell find -name "*.o")

clean:
	rm -rf $(OBJ)
	rm -rf son/son
	rm -rf sip/sip 
	rm -rf client/IM_client
	rm -rf server/IM_server



