/*************************************
*  This is a client program for SBCP
*  Written by: Leo
*  Date: 09/17/2014
*  Version:1.0
***************************************
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <semaphore.h>
#include <pthread.h>
#include <arpa/inet.h>

#include "configure.h"

#define PORT "3490" // the port client will be connecting to
#define MAXDATASIZE 520
#define HEADERLENGTH 4

//struct SBCP_msg message;
int sockfd;

void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int send_message(){
    char sd_buf[512];
    char *temptr;
    //struct SBCP_msg message_s;
	//sem_t *flag;
    int success,i,j,attr_length,remains;
    union msghdr_t msghdr;
    union attrhdr_t attrhdr;;
    union attrpayload_t context[32];
    uint32_t bitstream[MAXDATASIZE];
    pthread_mutex_t m1;

    pthread_mutex_init(&m1,NULL);
	/*if ((flag = sem_open("flag",O_CREAT,0644,0)) == SEM_FAILED)
	fprintf(stderr,"Failure to create semaphore flag1");
	*/
    success = 1;
    memset(&bitstream,0,sizeof(bitstream));
	while(1){
        //Flag to indicate whether to send new message or resend message
        if(success == 0){
            if(pthread_mutex_trylock(&m1)==-1) continue;
            if (send(sockfd,&bitstream,attr_length+4,0)==-1){
            pthread_mutex_unlock(&m1);
            success = 0;
			fprintf(stderr,"Failure to send in the client host!\n");
			continue;
            }
            pthread_mutex_unlock(&m1);
            success = 1;
            continue;
        }
        else{
            printf("Please input the message:\n");
            gets(sd_buf);
            printf("%s",sd_buf);
            if(strlen(sd_buf)>512) fprintf(stderr,"Exceed maximum message length!\n");
            attr_length = 4+strlen(sd_buf);
            msghdr.msgfield.vrsn = 3;
            msghdr.msgfield.type = SEND;
            msghdr.msgfield.length = attr_length + 4;
            bitstream[0]=htonl(msghdr.bitstream);

            attrhdr.attrfield.attrtype = MESSAGE;
            attrhdr.attrfield.size = attr_length;
            bitstream[1]=htonl(attrhdr.bitstream);

            /*for(i=2;i<32;i++){
                strncpy(&context[i-2].bitstream,sd_buf,4);
                bitstream[i] = htonl(context[i-2].bitstream);
            }*/

            j=0;
            temptr=sd_buf;
            memset(&context,0,sizeof context);
            for(i=0;i<MAXDATASIZE/4-2;i++){
            if(j<strlen(sd_buf)){
                //remains = (strlen(sd_buf)-j)>4?4:(strlen(sd_buf)-j);
                //printf("length of sending message is: %d\n",strlen(sd_buf));
                strncpy(context[i].payload,temptr,4);
                //printf("context %d is: %08x \n",i+1,context[i].bitstream);
                bitstream[i+2] = htonl(context[i].bitstream);
                temptr+=4;
                j+=4;
            }
       }
        }
        if(pthread_mutex_trylock(&m1)==-1){
            success = 0;
            continue;
        }
        //The socket is free now
        if(attr_length%4==0){
            if (send(sockfd,&bitstream,attr_length+4,0)==-1){
            pthread_mutex_unlock(&m1);
            success = 0;
			fprintf(stderr,"Failure to send in the client host!\n");
			continue;
            }
        }
        else{
            if (send(sockfd,&bitstream,attr_length-attr_length%4+8,0)==-1){
            pthread_mutex_unlock(&m1);
            success = 0;
			fprintf(stderr,"Failure to send in the client host!\n");
			continue;
            }
        }

		pthread_mutex_unlock(&m1);

		memset(&bitstream,0,sizeof(bitstream));

		success = 1;
		printf(" sent successfully!\n");
		continue;
	}
    return -1;
}

int recv_message(){
    //unsigned char recv_buf[MAXDATASIZE];
    int msg_length,length;
    int counter = 0;
    int index = 0;
	int i,j;

    union msghdr_t msghdr;
    union attrhdr_t attrhdr;
    union attrpayload_t context[32];
    uint32_t bitstream[MAXDATASIZE/4];

    unsigned char username[17];
    unsigned char payloadRecvd[513];
    //unsigned char temp[513];

    pthread_mutex_t m1;
    pthread_mutex_init(&m1,NULL);
	while(1){
	    pthread_mutex_lock(&m1);                                    //Lock the semaphore when the socket is occupied by recving process
	    //printf("listen to socket fd: %d\n",sockfd);
		if((length=recv(sockfd,&bitstream,MAXDATASIZE,0)) < 0){
            pthread_mutex_unlock(&m1);
            fprintf(stderr,"Failure to receive in the client host!\n");
            continue;
		}
		else if(length == 0) continue;
        else
//Processing the packet received
        {

            msghdr.bitstream = ntohl(bitstream[0]);

            msg_length = msghdr.msgfield.length;

            memset(&attrhdr,0,sizeof attrhdr);
            memset(&username,0,strlen(username));
            memset(&payloadRecvd,0,strlen(payloadRecvd));
            memset(&context,0,sizeof context);
            attrhdr.bitstream = ntohl(bitstream[1]);
            j=0;
            for(i=0; i<attrhdr.attrfield.size;i+=4){
                context[j].bitstream = ntohl(bitstream[j+2]);
                strcat(username, context[j].payload);
                j++;
            }
            memset(&attrhdr,0,sizeof attrhdr);
            attrhdr.bitstream = ntohl(bitstream[6]);
            memset(&context,0,sizeof context);
            j=0;
            for(i=0; i<attrhdr.attrfield.size;i+=4){
                context[j].bitstream = ntohl(bitstream[j+7]);
                strcat(payloadRecvd, context[j].payload);
                j++;
            }
            if(username!= " \0")
            printf("user %s sent message :%s\n",username,payloadRecvd);
			fflush(stdout);
            pthread_mutex_unlock(&m1);
            memset(&bitstream,0,sizeof bitstream);
        }
    }
    return -1;
}

int getlength(int a, int b){
    int c = 10*a + b;
    return c;
}

int main(int argc, char *argv[])
{
    int numbytes;
    char buf[MAXDATASIZE];
    struct addrinfo hints, *servinfo, *p;
    int sv,rv,cn;
    char s[INET6_ADDRSTRLEN];

    int joined,i,j,remains;

    long int attr_length;
    char* input;
    union msghdr_t msghdr;
    union attrhdr_t attrhdr;
    union attrpayload_t attrpayload[4];
    uint32_t bitstream[7];

    pthread_t threads[2];

    if (argc != 4) {
        fprintf(stderr,"usage: client hostname\n");
        exit(1);
    }

    //initialize the addrinfo structure
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = 0;
    hints.ai_flags = AI_PASSIVE;

    //get the server address information
    if ((sv = getaddrinfo(argv[2], argv[3], &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(sv));
        return 1;
    }

    // loop through all the results and connect to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            perror("client: socket");
            continue;
        }

        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("client: connect");
            continue;
        }

        break;
    }

    if (p == NULL) {
        fprintf(stderr, "client: failed to connect\n");
        return 2;
    }

    inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),s, sizeof s);
    printf("client: connecting to %s\n", s);

    while(joined == 0){
       //join the server
       printf("My Username is :%s\n",argv[1]);
       //fgets(input,16,argv[1]);
       memset(bitstream,0,16);
       memset(attrpayload,0,16);
       attr_length = strlen(argv[1])+4;

       msghdr.msgfield.vrsn = 3;
       msghdr.msgfield.type = JOIN;
       msghdr.msgfield.length = HEADERLENGTH + attr_length;
       bitstream[0] = htonl(msghdr.bitstream);

       attrhdr.attrfield.attrtype = USERNAME;
       attrhdr.attrfield.size = attr_length;
       bitstream[1] = htonl(attrhdr.bitstream);

       input = argv[1];
       j=0;
       for(i=2;i<6;i++){
            if(j<strlen(argv[1])){
                remains = (strlen(argv[1])-j)>4?4:(strlen(argv[1])-j);
                strncpy(attrpayload[i-2].payload,input,remains);
               // printf("payload %d is %08x: ",i-1,attrpayload[i-2].bitstream);
                input+=4;
                j+=4;
                bitstream[i] = htonl(attrpayload[i-2].bitstream);
            }
       }

       if (send(sockfd,&bitstream,MAXDATASIZE,0)!=-1) joined = 1;
    }

    if(fork()<0){
       fprintf(stderr,"Failure to create child process!\n");
       return -1;
    }
    //send message
    //pthread_create(&threads[0],NULL,send_message(),NULL);
    //pthread_create(&threads[1],NULL,recv_message(),NULL);
    else if(fork()==0){
        if(recv_message()==-1){
        fprintf(stderr,"Failure to recv message!\n");
        exit(-1);

       }
    }
    //receive message
    else{
       if(send_message()==-1){
       fprintf(stderr,"Failure to send message!\n");
       exit(-1);
       }
    }

    freeaddrinfo(servinfo); // all done with this structure

    close(sockfd);

    return 0;

}
