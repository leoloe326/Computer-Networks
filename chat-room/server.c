#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/types.h>
#include "configure.h"
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <time.h>

#define MAXDATASIZE 582
#define FWD_FIELDS 2

struct client_record{
	char name[17];
	int sd;
	int allocated;
};
struct client_record records[100];

void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}


int get_message_data(uint32_t recv_buf[MAXDATASIZE/4], char payloadRecvd[512]){
	union msghdr_t msghdr;
    union attrhdr_t attrhdr;
    union attrpayload_t context[MAXDATASIZE/4];
	//attributes for the recieved messgage
	int type,length;
	//attributes for the recieved Header
	int typeHdr,lengthHdr,remains;
	int i,j=0;
	char temp[512];

    printf("recv_buf size is: %d\n",sizeof recv_buf);
	msghdr.bitstream = ntohl(recv_buf[0]);
	//printf("network bitstream %d: %08x\n",0,recv_buf[0]);
	type = msghdr.msgfield.type;
	length = msghdr.msgfield.length;
	attrhdr.bitstream = ntohl(recv_buf[1]);
	printf("network bitstream %d: %08x\n",1,recv_buf[1]);
	if( type != attrhdr.attrfield.attrtype){
		printf("Header and message type do not match, Header type :%d", type);
		return 0;
	}
    memset(&context,0,sizeof context);
    /*
    for(i=1;i<(attrhdr.attrfield.size)/4;i++){
        message[i].bitstream = ntohl(bitstream[i]);
    }
*/

	for(i=4; i< attrhdr.attrfield.size;i+=4){
		context[j].bitstream = ntohl(recv_buf[j+2]);
		strcat(payloadRecvd, context[j].payload);
		j++;
//		printf("network bitstream %d: %08x\n",2+j,recv_buf[j+2]);
	//	printf("bitstream: %08x\n",context[j].bitstream);

	}

	//memset(&recv_buf, 0, MAXDATASIZE);// Free buffer for the next message to be recieved
	printf("Receive Buffer reset\n");
	return type;
}

int userExists(char username[17]){
	int i;
	for(i=0; i< 100; i++){
		if((records[i].allocated==1) ){
			if(strcmp(username,records[i].name) == 0){
				//printf("user exists \n");
				return 1;
			}
		}
	}
return 0;
}

int addUser(char username[16], int sd){
	int i;
	for(i=0; i< 100; i++){
		if(records[i].allocated != 1){
            memset(&records[i],0, sizeof records);
			strcpy(records[i].name, username);
			records[i].sd = sd;
			records[i].allocated = 1;
			printf("username received %s \n",records[i].name);
			return 1;
		}
	}
	return 0;
}

char *getUsername(int sd){
	int i;
	for(i=0; i< 100; i++){
		if(records[i].sd == sd && records[i].allocated == 1)
			return records[i].name;
	}
	//if it comes here then username not found
	printf("User with this name is not present");
	return NULL;
}

int socketExists(int sd){
    int i;
    for(i=0;i<100;i++){
        if(records[i].allocated==1){
            if(sd==records[i].sd)
                return 1;
        }
    }
    return 0;
}

int onRecieve(int type, char payloadRecvd[512], int fd, int max_fd, int sd, fd_set mainFdSet){
	int i;
	char name[16];
	char message[512];
	//check type of message
	//check if it is one of the configured types only otherwise discard
	//if any other than join..check if client is known or unknown
	//if join add the username to list
	//check availabilty of username
	if(type == JOIN){
		strcpy(name, payloadRecvd);
		//check username already exists
		if(userExists(name)){
			//if yes do not add it and print..user already exists
			printf("This user already exists, Please try some other username");
			return -1;
		}else{		//if not add to list
			printf("new user added %d\n", addUser(name,fd));
		}
	}else if(type == SEND){
		strcpy(message, payloadRecvd);
		strcpy(name, getUsername(fd));
		//iterate list and check if username exists
		if(userExists(name)){
			printf("Send method called; Triggering forward now\n");
			triggerForward(name,message, fd, max_fd, sd, mainFdSet);
		}else{
			printf("The user with this username has not joined yet. Please join first\n");
			return -1;
		}
	}else{
		printf("Undefined message type\n");
		return -1;
	}
	return 1;
}


int triggerForward(char username[16],char message[512], int fd, int max_fd, int sd, fd_set mainFdSet){
	//send this message to everyone except the user given
	//calculate recv_buf

	union msghdr_t msghdr;
    union attrhdr_t attrhdr1;
	union attrhdr_t attrhdr2;

    union attrpayload_t attrpayload1[4];
    union attrpayload_t attrpayload2[32];

    uint32_t bitstream[MAXDATASIZE];
    char *nameptr=username;
    char *messageptr=message;

	int i,j,k,temp,remains,attr_length;

	for(i=0; i<= max_fd; i++){
		if(FD_ISSET(i, &mainFdSet)){
			if( i != sd && i != fd){
   //                 if(socketExists(i)){
                printf("start forwarding, socket %d.\n",i);
				attr_length = 8+strlen(username)+strlen(message);
				msghdr.msgfield.vrsn = 3;
				msghdr.msgfield.type = FWD;
				msghdr.msgfield.length = attr_length + 4;
				bitstream[0]=htonl(msghdr.bitstream);

				attrhdr1.attrfield.attrtype = USERNAME;
				attrhdr1.attrfield.size = 4+strlen(username);
				bitstream[1]=htonl(attrhdr1.bitstream);

				j=0;
				temp = 0;
				memset(&attrpayload1,0,sizeof attrpayload1);
				nameptr = username;
                for(k=0;k<4;k++){
                    if(j<strlen(username)){
                        remains = (strlen(username)-j)>4?4:(strlen(username)-j);
                        strncpy(attrpayload1[k].payload,nameptr,remains);
                        //printf("payload %d is %08x: \n",k+1,attrpayload1[k].bitstream);
                        bitstream[k+temp+2] = htonl(attrpayload1[k].bitstream);
                        nameptr+=4;
                        j+=4;
                    }
                }
                temp = k;

				attrhdr2.attrfield.attrtype = MESSAGE;
				attrhdr2.attrfield.size = 4+strlen(message);
				bitstream[temp+2]=htonl(attrhdr2.bitstream);

                j=0;
                messageptr=message;
                memset(&attrpayload2,0,sizeof attrpayload2);
                for(k=0;k<128;k++){
                if(j<strlen(message)){
                    //remains = (strlen(sd_buf)-j)>4?4:(strlen(sd_buf)-j);
                   // printf("length of sending message is: %d\n",strlen(message));
                    strncpy(attrpayload2[k].payload,messageptr,4);
                    //printf("context %d is: %08x \n",k+1,attrpayload2[k].bitstream);
                    bitstream[k+temp+3] = htonl(attrpayload2[k].bitstream);
                    messageptr+=4;
                    j+=4;
                    }
                }
				if(send(i, &bitstream, sizeof bitstream, 0) == -1)
                    perror("forward failure\n");
                else
//                for(k=0;k<MAXDATASIZE/4;++k)
  //                  printf("bitstream %d is %08x\n",k,bitstream[k]);
                memset(&bitstream,0,sizeof(bitstream));
                //    }

			}
		}
	}
	return 0;
}

int main(int argc, char *argv[]){
	int sd, new_sd;
	struct addrinfo hints,*serverInfo, *ptr;
	int rv;
	struct sockaddr_storage remote_addr;
	char ip[INET_ADDRSTRLEN];
	fd_set mainFdSet; // master set for all fds
	fd_set temp_fds;	// for traversing as it changes
	int max_fd, fd;
	uint32_t recv_buf[250];
	char payloadRecvd[512];
	socklen_t sin_size;
	//attributes for the recieved messgage
	int type, bytes;
	int BACKLOG;
	struct timeval timeout;
	timeout.tv_sec = 3;
	timeout.tv_usec = 0;
//data types
//1 for saving accepted type of messages
//2 for saving each connection infor with username and socket id ; can be either just username list or need to be socket id vs username?
//variable for number of clients
	if(argc != 4){
		printf("pass the arguments as : server_IP, server_port, Max_clients");
	return -1;
	}
	BACKLOG = atoi(argv[3]);

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET;
	hints.ai_flags = AI_PASSIVE;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = 0;
	if((rv = getaddrinfo(argv[1], argv[2], &hints, &serverInfo)) == -1){
		fprintf(stderr, "getaddrinfo failed: %s\n", gai_strerror(rv));
		return 1;
	}
	for(ptr = serverInfo; ptr != NULL; ptr = ptr->ai_next){
		if((sd = socket(ptr->ai_family, ptr->ai_socktype,0)) < 0 ){
			perror("socket");
			continue;
		}
		if( bind(sd, ptr->ai_addr, ptr->ai_addrlen) < 0 ){
			perror("in socket bind");
			continue;
		}
		break;
	}

	if(ptr == NULL){
		fprintf(stderr," : server failed to bind \n");
		return 2;
	}
	freeaddrinfo(serverInfo);

	if( listen(sd, BACKLOG) == -1){
		perror("failed to listen ");
		return 3;
	}

	printf("Server all set; Waiting for connections \n");
	//initialise file desciptors
	FD_SET(sd, &mainFdSet);
	max_fd = sd;

	while(1){

        memset(payloadRecvd,0,512);
		temp_fds = mainFdSet;
		if(select(max_fd+1, &temp_fds,NULL,NULL,&timeout) == -1){
			perror("in select");
			return 4;
		}
		for(fd=0; fd<= max_fd; fd++){
			if(FD_ISSET(fd, &temp_fds)){
				if(fd == sd){
					//if connection made from what you just listened, it is a new connection thus do accept and add
					sin_size = sizeof remote_addr;
					if((new_sd = accept(sd, (struct sockaddr *) &remote_addr, &sin_size)) == -1){
						perror("inside accept");
					}else{
						if(new_sd > max_fd)
							max_fd = new_sd;
						FD_SET(new_sd, &mainFdSet);
						inet_ntop(remote_addr.ss_family, get_in_addr(( struct sockaddr *) &remote_addr), ip, sizeof ip);
						printf("got connection from client with socket: %d\n", new_sd);
					}
				}else{
					//fetching actual data from the client
					memset(&recv_buf, 0, MAXDATASIZE);
					if((bytes = (recv(fd,&recv_buf,MAXDATASIZE-1,0))) <= 0){
						if(bytes == 0){
							printf("connection got closed: %d , freeing up resources \n", fd);
							cleanup(fd);
						}else{
							perror("in recieve");
						}
						close(fd);
						FD_CLR(fd, &mainFdSet);
					}else{
						type = get_message_data(recv_buf, payloadRecvd);

						//printf("receive buffer 0 is %d\n",recv_buf[0]);
						//printf("Message type number is %d\n",type);
                        //printf("got data %d \n", fd);
						if(onRecieve(type, payloadRecvd, fd, max_fd, sd, mainFdSet) == 1)
							printf("Message received successfully \n");
						else
							printf("some error occurred while receiving message \n");
					}

				}
			}
		}
	}
	return 1;
}



int cleanup(int sd){
	//check if some client has been disconnected...if yes then
    //cleanup resources like: close socket, make username available
	int i;
	for(i=0; i < sizeof records ; i++){
		if (sd == records[i].sd){
			printf("got to cleanup socket %d \n", sd);
			records[i].allocated = 0;
			close(sd);
			return 1;
		}
	}
return 0;
}
