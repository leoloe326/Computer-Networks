// Implement TFTP server
/**********************************************************
Date: 		Oct 5th, 2014
Project :	ECEN 602 Project TFTP Server(Only support RRQ~)

Programers:
Lihao Zou
Shalini

File:		TFTP Server (main)
Purpose:	A TFTP server that will accept a connections from
		a client and transefet files.
Notes:		Here we are using the sendto and recvfrom
		functions so the server and client can exchange data.
***********************************************************************/

/* Include our header which contains libaries and defines */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>          //For error information
#include <sys/wait.h>       //For multithreading
#include <dirent.h>
#include <sys/time.h> //For keeping the timer table
#include <time.h>

#define ERROR -1

#define RRQ 0x01
#define WRQ 0x02
#define DATA 0x03
#define ACK 0x04
#define ERR 0x05

#define ACKTIMEOUT 100 /*amount of time to wait for an ACK/Data Packet in 1000microseconds 1000 = 1 second*/
//#define RETRIES 3 /* Number of times to resend a data OR ack packet beforing giving up */
#define CONNECTTIMEOUT 5000 // duration of time to wait for a transmission to complete
//#define MAXACKFREQ 16 // Maximum number of packets before ack */
#define MAXDATASIZE 512 /* Maximum data size allowed */
#define BUFSIZE 516
#define MAXCLIENTNUM 50

const char workdir[8]="./data/";
char hostname[16];

struct packet_t{
    unsigned char opcode[2];
    unsigned char blk[2];
    char data[MAXDATASIZE];
};

char err_msg [7][40] = {"Not defined, see error message if any",
                        "File not fount",
                        "Access Violation",
                        "Disk full, or allocation exceeded",
                        "Illegal TFTP operation",
                        "Unknown transfer ID",
                        "File already exists"};
/* Function Declaration */
int tsend (char *, struct sockaddr_in, char *, int);
int isnotvaliddir (char *);
void getblknum(unsigned char [], const int);
int divide(FILE *,int , int, char[]);
/*Implementation*/

void getblknum(unsigned char blk[],const int blknum){
    int temp = blknum;
    int lo = blknum%(256);
    int hi = blknum/(256);
//    printf("lo is %x, hi is %x\n",lo,hi);
    blk[0]=(unsigned char) hi;
    blk[1]=(unsigned char) lo;
//    printf("blk1 is %x, blk2 is %x\n",blk[0],blk[1]);
    return;
}

int divide(FILE *fd,int size,int id,char data[]){                   //Divide the file in case it exceeds the max data size
    int nbytes;
    FILE *tmp=fd;
    long offset;
    int counter=0,flag;
    offset = size * id;
    fseek(tmp,offset,0);
    if ((nbytes = fread(data,1,MAXDATASIZE,tmp))>=0){
        return nbytes;                                               //If success and not reach EOF, return 0
    }
    else if (ferror(tmp)){                                           //If error, return -1
        perror("Reading file");
        return -1;
    }
}

int tsend (char *pFilename, struct sockaddr_in client, char *pMode, int tid){
    FILE *fd;                   //file descriptor
    int i,rv,len,sd,nbytes,acked,flag=0,chunknum=0,bufptr;
    unsigned short blk_num=1;           //DATA Packet starts from the block number 1
    int sdbytes;
    char filename[16], mode[10],fullpath[100],blkstr[2],blkget[3];
    char payload[MAXDATASIZE];	// The data payload
    unsigned char blk[2];
    unsigned char packetbuf[MAXDATASIZE+4],rcvbuf[MAXDATASIZE+4];
    struct timeval crttime,starttime,begintime;
    struct addrinfo hints,*serverInfo,*temp;
    struct packet_t packet;
    int client_len;
    double waittime,runtime;

    strcpy(mode,pMode);
    getcwd(fullpath,100);
    sprintf(filename,"/%s",pFilename);
    strcat(fullpath,filename);

    if ((sd = socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0){	//startup a new socket
        printf ("Server reconnect for sending did not work correctly\n");
        return -1;
    }


    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = 0;

    if((rv = getaddrinfo(hostname, NULL, &hints, &serverInfo)) == -1){          //Bind the socket to a random port
		fprintf(stderr, "getaddrinfo failed: %s\n", gai_strerror(rv));
		return 1;
	}

    for(temp = serverInfo; temp != NULL; temp = temp->ai_next){
        if( bind(sd, temp->ai_addr, temp->ai_addrlen) < 0 ){
			perror("bind");
			continue;
		}
        break;
    }
    printf("Full pathname is :%s\n",fullpath);

    client_len = sizeof client;
    if (!(fd=fopen(fullpath,"rb"))){
        perror("File not found");
        len = sprintf ((char *) packetbuf, "%c%c%c%cFile not found in %s%c",
                       0x00,0x05, 0x00, 0x01, fullpath, 0x00);
        if (sendto (sd, packetbuf, len, 0, (struct sockaddr *) &client, (socklen_t) client_len) != len)	// send the error packet indicating non-existence of file
            printf("Mismatch in number of sent bytes while trying to send error packet\n");             //using the client sockaddr passed from main
//        else
//            printf("From Sender: Error Packet Sent\n");
    }

    memset(&packet,0,sizeof packet);
    //printf("From Sender: Start Transfer Data\n");

    while((len=divide(fd,MAXDATASIZE,chunknum++,payload))==MAXDATASIZE){
        client_len = sizeof client;
        if(blk_num<256*256)
            getblknum(blk,blk_num++);
        else
        {
            blk_num = 1;
            getblknum(blk,blk_num++);
        }
        packet.opcode[0]=0x00;
        packet.opcode[1]=0x03;
        //memcpy(packet.opcode[0],0x00,1);
        //memcpy(packet.opcode[1],0x03,1);
        packet.blk[0]=blk[0];
        packet.blk[1]=blk[1];
        memcpy(packet.data,payload,MAXDATASIZE);

        if(sendto(sd,&packet,516,0,(struct sockaddr *)&client,(socklen_t) client_len)!=516){                        //send data packet
            printf("Mismatch in number of sent bytes while trying to send data packet\n");
        }
//        else
            //printf("From Sender: Data Packet Sent\n");

        gettimeofday(&begintime,0);
        gettimeofday(&crttime,0);
        runtime = 1000000*(crttime.tv_sec-begintime.tv_sec)+(crttime.tv_usec-begintime.tv_usec);
        acked = 0;
        while(!acked &&
            runtime/1000< CONNECTTIMEOUT){
            //printf("Duration of connect is %f\n",runtime/1000);
            gettimeofday(&starttime,0);
            gettimeofday(&crttime,0);
            waittime = 1000000*(crttime.tv_sec-starttime.tv_sec)+(crttime.tv_usec-starttime.tv_usec);
            while(waittime/1000<=ACKTIMEOUT){
                gettimeofday(&crttime,0);
                runtime = 1000000*(crttime.tv_sec-begintime.tv_sec)+(crttime.tv_usec-begintime.tv_usec);
                //printf("Timeout is %f\n",runtime/1000);
                if (runtime/1000> CONNECTTIMEOUT){
                    printf("Connection timeout, transmission dumped2\n");
                    return 2;
                }
                if((nbytes=recvfrom(sd,rcvbuf,BUFSIZE,0,(struct sockaddr* )&client,(socklen_t *)&client_len))<=0){
                    if (nbytes==0){
                        printf("ACK Listener: No data received\n");
                        gettimeofday(&crttime,0);
                        usleep(1000);
                        continue;
                    }
                    else{
                        perror("Receive ACK");
                        return -1;
                    }
                }
                bufptr = 0;
                if(rcvbuf[bufptr++]!=0x00){
                    printf("Wrong package formation\n");
                    memset(packetbuf,0,BUFSIZE);
                    len=sprintf((char *)packetbuf,"%c%c%c%c%s%c",
                                0x00,0x05,0x00,0x00,"Wrong package formation",0x00);
                    if(sendto(sd,packetbuf,len,0,(struct sockaddr *)&client,(socklen_t) client_len)!=len){
                        printf("Mismatch in number of sent bytes while trying to send error packet\n");
                    }
                    return -1;
                }

                if(rcvbuf[bufptr++]!=0x04){
                    printf("Waiting for ACK, type mismatch\n");
                    gettimeofday(&crttime,0);
                    usleep(1000);
                    continue;
                }

                if (rcvbuf[bufptr++]!= blk[0] || rcvbuf[bufptr++]!=blk[1]){
                    //printf("blk number is %x  %x\n",rcvbuf[bufptr-2],rcvbuf[bufptr-1]);
                    printf("Waiting for ACK, block num mismatch\n");
                    gettimeofday(&crttime,0);
                    usleep(1000);
                    continue;
                }
                //printf("blk number is %x  %x\n",rcvbuf[bufptr-2],rcvbuf[bufptr-1]);
                acked = 1;
                break;
            }
            if(sendto(sd,packetbuf,len,0,(struct sockaddr *)&client,(socklen_t) client_len)!=len){      //Try to resend the data packet
                printf("Mismatch in number of sent bytes while trying to send data packet\n");
            }
        }
        if (!acked){
            printf("Duration of connect is %f\n",runtime/1000);
            printf("Connection timeout, transmission dumped1\n");
            return 2;
        }
        else{
            //printf("ACK received, transmit next block\n");
            memset(payload,0,MAXDATASIZE);
            memset(&packet,0,MAXDATASIZE+4);
        }

    }
    if (len < MAXDATASIZE && len != -1){                                                                     //The final data packet
        printf("The final data packet\n");
        client_len = sizeof client;
        getblknum(blk,blk_num);
        printf("block number %d is %x %x\n",blk_num,blk[0],blk[1]);

        packet.opcode[0]=0x00;
        packet.opcode[1]=0x03;
        packet.blk[0]=blk[0];
        packet.blk[1]=blk[1];
        memcpy(packet.data,payload,len);

        if((sdbytes=sendto(sd,&packet,len+4,0,(struct sockaddr *) &client,(socklen_t) client_len))!=(len+4)){                        //send data packet
            printf("Mismatch in number of sent bytes while trying to send data packet\n");
            printf("Length of packet is %d\n",len);
            printf("Sent %d bytes\n",sdbytes);
        }

        gettimeofday(&begintime,0);
        gettimeofday(&crttime,0);
        runtime = 1000000*(crttime.tv_sec-begintime.tv_sec)+(crttime.tv_usec-begintime.tv_usec);

        acked = 0;

        while(!acked &&
              runtime/1000< CONNECTTIMEOUT){
            gettimeofday(&starttime,0);
            gettimeofday(&crttime,0);
            waittime = 1000000*(crttime.tv_sec-starttime.tv_sec)+(crttime.tv_usec-starttime.tv_usec);

            while(waittime/1000<=ACKTIMEOUT){
                gettimeofday(&crttime,0);
                runtime = 1000000*(crttime.tv_sec-begintime.tv_sec)+(crttime.tv_usec-begintime.tv_usec);
                //printf("Timeout is %f\n",runtime/1000);
                if (runtime/1000> CONNECTTIMEOUT){
                    printf("Connection timeout, transmission dumped\n");
                    return 2;
                }
                if((nbytes=recvfrom(sd,rcvbuf,BUFSIZE,0,(struct sockaddr* )&client,(socklen_t *)&client_len))<=0){
                    if (nbytes==0){
                        printf("ACK Listener: No data received\n");
                        gettimeofday(&crttime,0);
                        usleep(1000);
                        continue;
                    }
                    else{
                        perror("Receive ACK");
                        return -1;
                    }
                }
                bufptr = 0;
                if(rcvbuf[bufptr++]!=0x00){
                    printf("Wrong package formation\n");
                    memset(packetbuf,0,BUFSIZE);
                    len=sprintf((char *)packetbuf,"%c%c%c%c%s%c",
                                0x00,0x05,0x00,0x00,"Wrong package formation",0x00);
                    if(sendto(sd,packetbuf,len,0,(struct sockaddr *)&client,(socklen_t) client_len)!=len){
                        printf("Mismatch in number of sent bytes while trying to send error packet\n");
                    }
                    return -1;
                }

                if(rcvbuf[bufptr++]!=0x04){
                    printf("Waiting for ACK, type mismatch\n");
                    gettimeofday(&crttime,0);
                    usleep(1000);
                    continue;
                }

                if (rcvbuf[bufptr++]!= blk[0] || rcvbuf[bufptr++]!=blk[1]){
                    printf("Waiting for ACK, block num mismatch\n");
                    gettimeofday(&crttime,0);
                    usleep(1000);
                    continue;
                }
                acked = 1;
                break;
            }
            if(sendto(sd,packetbuf,len,0,(struct sockaddr *)&client,(socklen_t) client_len)!=len){              //Retry Sending Packet
                printf("Mismatch in number of sent bytes while trying to send data packet\n");
            }
        }
        if (!acked){
            printf("Connection timeout, transmission dumped\n");
            return 2;
        }
        else{
            printf("ACK received, transmission completed\n");
            memset(payload,0x00,MAXDATASIZE);
        }
    }
    if (flag == -1){
        printf("Error in divide\n");
    }

    return 0;
}

int main(int argc, char* argv[]){
    struct sockaddr_in server,client;
    int sd,len,nbytes,
        client_len,filename_len,mode_len,
        i,status;
    short tid;
    char temp[16],
        port[5],sdbuf[516],
        rcvbuf[50],filename[16],
        mode[10],blk_number[3],
        packetbuf[MAXDATASIZE];
    char *delimiter,*bufindex;
    if (argc!=3)
        printf("Please put command line like this: ./server SERVER_IP_ADDRESS SERVER_PORT\n");
    else{
        strcpy(hostname,argv[1]);
        strcpy(port,argv[2]);
    }
    memset(&server,0,sizeof server);
    server.sin_family = AF_INET;
    server.sin_port = htons((unsigned short)atoi(port));    //Generally is 69
    //inet_aton(serveraddr,&server.sin_addr.s_addr);
    server.sin_addr.s_addr=inet_addr(hostname);

    if((sd = socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP))<0){
        perror("Socket Failure");
        return 0;
    }

    if(bind(sd,(struct sockaddr*)& server,(socklen_t) sizeof server)<0){
        perror("Bind Failure");
        return 0;
    }
    //init_time();

    while (1){
        client_len = sizeof (client);	//get the length of the client
        memset (sdbuf, 0, MAXDATASIZE+4);	//clear the buffer
        memset (rcvbuf,0,MAXDATASIZE+4);
        nbytes = 0;


        while (errno == EAGAIN || nbytes == 0){	// If no data is available, keep receiving.
            waitpid (-1, &status, WNOHANG);
            nbytes = recvfrom (sd, rcvbuf, BUFSIZE, MSG_DONTWAIT, //To receive message from the client
                            (struct sockaddr *) &client,
                            (socklen_t *) & client_len
                         );
            if (nbytes < 0 && errno != EAGAIN){
                  perror ("The server could not receive from the client");
                  return 0;
            }
            usleep (1000);
        }
        printf("Received Request\n");
        tid = ntohs(client.sin_port);

        if(rcvbuf[0]!=0x00){                // The first byte of any packet should be 0x00
            printf("Wrong package formation\n");
            memset(packetbuf,0,BUFSIZE);
            len=sprintf((char *)packetbuf,"%c%c%c%c%s%c",
                        0x00,0x05,0x00,0x00,"Wrong package formation",0x00);
            if(sendto(sd,packetbuf,len,0,(struct sockaddr *)&client,sizeof client)!=len){
                printf("Mismatch in number of sent bytes while trying to send error packet\n");
            }
            continue;
        }

        switch(rcvbuf[1]){
            case RRQ:{              //Only RRQ is acceptable on port 69
                memset(filename,0,16);
                bufindex = rcvbuf+2;
                delimiter = strchr(bufindex,0x00);
                filename_len = delimiter - bufindex;
                strncpy(filename,bufindex,filename_len);
//                printf("Filename is:%s\n",filename);
                bufindex = delimiter+1;
                delimiter = strchr(bufindex,0x00);
                mode_len = delimiter - bufindex;
                strncpy(mode,bufindex,mode_len);
                if (!fork()){       //Create a child process for sending data and receiving ack
                    tsend(filename,client,mode,tid);
                    exit(0);
                }
            }
            break;
            default:{
            }
            break;
        }
    }
    return 0;
}

