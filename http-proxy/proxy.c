/**********************************************************
*   Date: 10/14/2014
*   Project: Simple HTTP Proxy
*
*   File Description: This is the proxy end of HTTP Proxy
*   Functions:  Using TCP to receive and forward GET request
*   from client and ASCII files from the web server
*
*   Programmers: Leo Zou
*                Shalini
************************************************************
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>          //For error information
#include <sys/wait.h>       //For multithreading
#include <dirent.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>

#define MAXDATASIZE 1000
#define MAXPACKETSIZE 20000
#define BACKLOG 10

#define FORWARD_TO_SERVER 0
#define FORWARD_TO_CLIENT 1
#define BACK_TO_CLIENT 2
#define NO_SEND 3
#define INVALID 4

fd_set fds,temp_fds;
int fd, max_fd, res;
int filenum=0;
char hostname[16],port[6];
char remotename[50],urlname[50];
int nbytes=0;

struct websocket_t{
    int clientsd;               //indicate the client socket number to send file
    int serversd;               //indicate the server socket number to retrieve file
    char filename[32];          //indicate the filename used to store the packet
    int contentlen;             //indicate the content length
    int contentfound;           //indicate find the start of content, useful to calculate the end of packet
    int complete;               //indicate the transmission complete from server to client
    int done;                   //indicate a complete connection between server and client sd has been established
}websocket={0,0,"0",0,0,0,1};

struct websocket_t websocketarray[10];

struct cache_entry_t{
    char remotename[50];
    char urlname[50];
    char filename[32];
    struct in_addr remoteaddr;
    int status;
    time_t timestamp;

    char expire[50];
    int timeout;
}cachetable[10];

struct fpool_t{
    char remotename[50];
    char urlname[50];
    char filename[32];
    char expire[50];
    int status;             //Waiting for verification or not
}fpool_entry={"","","","",0};

char* weekday[7]={"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
char* month[12]={"Jan","Feb","Mar","Apr","May","Jun","July","Aug","Sep","Oct","Nov","Dec"};

struct fpool_t fpool[50];

int makepacket(int, struct sockaddr_in,char[],char[],char[]);

int main(int argc, char *argv[]){
    int i,j,sd,sv,new_sd,temp_sd,sin_size,sockaddrlen,sendbytes,flag;
    struct sockaddr_in proxy,peer;
    char packetbuf[MAXPACKETSIZE],recvbuf[MAXPACKETSIZE],peerhost[16];
    struct addrinfo hints, *servinfo, *ptr;         //For DNS lookup
    struct timeval timeout={3,0};

    if (argc!=3){
        printf("Please input the command line like:./proxy ip_address port_number\n");
        return 0;
    }

    strncpy(hostname,argv[1],16);
    strncpy(port,argv[2],6);

    memset(&proxy,0,sizeof proxy);
    proxy.sin_family = AF_INET;
    proxy.sin_port = htons(atoi(port));
    proxy.sin_addr.s_addr = inet_addr(hostname);

    FD_ZERO(&fds);       //Initialize the read file descriptor set

    sockaddrlen = sizeof proxy;

    if((sd = socket(AF_INET,SOCK_STREAM,0))<0){
        perror("Proxy: Socket");
        return 0;
    }
    int yes=1;
    setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
    if((bind(sd,(struct sockaddr *)&proxy,(socklen_t)sockaddrlen))==-1){
        perror("Proxy: Bind");
        return 0;
    }

    if(listen(sd,BACKLOG)==-1){
        close(sd);
        perror("Proxy: Listen");
        return 0;
    };

    FD_SET(sd,&fds);
    max_fd = sd;

    printf("Proxy: Waiting for connections...\n");
    while(1){   //keep receiving request from peer
        temp_fds = fds;


        if(select(max_fd+1,&temp_fds,NULL,NULL,&timeout) == -1){
			perror("Proxy: Select");
			return 0;
		}

        for (fd = 0; fd <= max_fd; fd++){
            if(FD_ISSET(fd,&temp_fds)){
                //printf("Phase 1\n");
                if (fd == sd){               //Connection made from the port listening to, means a new connection
                    sin_size = sizeof peer;
                    new_sd = accept(sd, (struct sockaddr *)&peer, (socklen_t *)&sin_size);
                    if (new_sd == -1) {
                        perror("Proxy: Accept");
                        continue;
                    }
                    printf("Accept\n");
                    if(new_sd > max_fd)
                        max_fd = new_sd;
                    FD_SET(new_sd, &fds);
                    printf("To client, new socket number added: %d\n",new_sd);
                    inet_ntop(AF_INET, &(peer.sin_addr),peerhost,sizeof peerhost);
                    printf("Got connection from peer: %s\n", peerhost);
                }
                else{                           //GET request or response
                    memset(recvbuf,0,MAXPACKETSIZE);
                    memset(packetbuf,0,MAXPACKETSIZE);
                    if ((nbytes = recv(fd,recvbuf,sizeof recvbuf,0))< 0 && errno != EAGAIN){
                        perror("Proxy: Receive");
                        //cleanup(fd);            //To spare this file descriptor for other connections
                        FD_CLR(fd,&fds);
                        close(fd);              //Close the socket
                    }
                    else if(nbytes == 0 || errno == EAGAIN){}
                    else{
                        if((flag=makepacket(fd, peer,recvbuf,packetbuf,remotename))==FORWARD_TO_SERVER){       //GET request
                            memset(&hints, 0, sizeof hints);
                            hints.ai_family = AF_INET;
                            hints.ai_socktype = SOCK_STREAM;
                            hints.ai_protocol = 0;

                            if((sv=getaddrinfo(remotename,"80",&hints,&servinfo)) == -1){
                                fprintf(stderr, "getaddrinfo failed: %s\n", gai_strerror(sv));
                                return 1;
                            }

                            for(ptr = servinfo; ptr != NULL; ptr = ptr->ai_next){
                                if((temp_sd = socket(ptr->ai_family, ptr->ai_socktype,0)) < 0 ){
                                    perror("Proxy: socket");
                                    continue;
                                }
                                if( connect(temp_sd, ptr->ai_addr, ptr->ai_addrlen) < 0 ){
                                    perror("Proxy: connect");
                                    continue;
                                }
                                break;
                            }
                            for(i=0;i<10;i++){
                                if(cachetable[i].status==1){
                                    cachetable[i].remoteaddr = ((struct sockaddr_in *)(ptr->ai_addr))->sin_addr;
                                    printf("Remote address assigned in cachetable %d\n",i);
                                    //For reverse interpretation
                                    break;
                                }
                            }

                            if (temp_sd > max_fd)
                                max_fd = temp_sd;

                            FD_SET(temp_sd,&fds);

                            printf("To server, New socket added: %d\n",temp_sd);
                            if((sendbytes=send(temp_sd,packetbuf,sizeof packetbuf,0))==-1){
                                perror("Proxy: Send");
                                close(temp_sd);
                                return -1;
                            }
                            printf("Successfully send to web server\n");
                            break;  //To renew the max number of file descriptor
                        }
                        if (flag == FORWARD_TO_CLIENT){                     //send message to client from server
                            for(j=0;j<10;j++){
                                if(fd == websocketarray[j].serversd){
                                    if((sendbytes=send(websocketarray[j].clientsd,packetbuf,strlen(packetbuf),0))==-1){
                                        perror("Proxy: Forward to client");
                                        return -1;
                                    }
                                    if(websocketarray[j].complete == 1){
                                        websocketarray[j].clientsd = 0;
                                        printf("Complete transmission, close server fd\n");
                                        close(fd);
                                        FD_CLR(fd,&fds);
                                    }
                                    break;
                                }
                            }
                            if (j==10){
                                printf("Not find the corresponding entry for client\n");
                                continue;
                            }

                            //printf("Successfully forward packet to client\n");
                            continue;
                        }
                        if (flag == BACK_TO_CLIENT){                       //send message back on the client socket
                            //printf("Successfully send back packet to client\n");
                            continue;
                        }
                        if (flag == NO_SEND){
                            //printf("No useful packet to send\n");
                            continue;
                        }
                        if (flag == INVALID){
                            printf("Invalid type refused\n");
                            continue;
                        }
                    }
                }
            }
        }
        //Reach the end of fd_set, looping around
        //close(new_sd);
    }
    return 0;           //Can't reach here
}

int makepacket(int srcsocknum, struct sockaddr_in peer, char recvbuf[],char packetbuf[],char remotename[]){
    FILE *fp;
    int i,j,k,sd,index,sendbytes,len;
    int webindex=10,contpack=0;
    double livetime,maxlivetime;
    char *temp=recvbuf,*payloadptr=NULL,*lengthptr,*ptr1=NULL;
    char filename[32],second[10],minute[10],hour[10],date[10];
    time_t now;
    struct tm *gmt;

    for (i=0;i<10;i++){                          //Judge if received is a continue packet of HTTP
        if (srcsocknum == websocketarray[i].serversd){
            contpack = 1;
            webindex = i;
            break;
        }
    }

    if (strncasecmp(temp,"GET",3)==0){           //A GET message from client
        memset(urlname,0,50);
        memset(remotename,0,50);
        temp+=4;
        len=strchr(temp,' ')-temp;
        strncpy(urlname,temp,len);
        urlname[len]='\0';
        printf("urlname:%s\n",urlname);
        temp=strchr(temp,':');
        len=strchr(temp,'\r')-temp-1;
        strncpy(remotename,temp+1,len);
        remotename[len]='\0';
        printf("remotename:%s\n",remotename);

        if((sd = socket(AF_INET,SOCK_STREAM,0))<0){
            perror("Proxy: Socket(In Handle)");
            return -1;
        }

        printf("Get Connected with remote server\n");


        for (i=0;i<10;i++){
            if (websocketarray[i].clientsd==0){          //Find a free space
                //printf("Find a free space %d in the web socket table\n",i);
                websocketarray[i].clientsd = srcsocknum; //Record the client side socket number
                websocketarray[i].done = 0;              //Flag the status, to be completed by the server side
                websocketarray[i].complete = 0;
                break;
            }
        }
        webindex = i;
        for(i=0;i<10;++i){
            if(!strcmp(cachetable[i].urlname,urlname)&&!strcmp(cachetable[i].remotename,remotename)){
                cachetable[i].status = 1;       //Means can use this cache if not expire
            }
        }
//
//            strcpy(cachetable[index].remotename,remotename);
//            strcpy(cachetable[index].urlname,urlname);

        for(i=0;i<50;i++){
            if(!strcmp(fpool[i].remotename,remotename) && !strcmp(fpool[i].urlname,urlname)){
                sprintf(packetbuf,"GET %s HTTP/1.1\r\nHost: %s\r\nIf-Modified-Since: %s\r\n\r\n",urlname,remotename,fpool[i].expire);
                printf("Send conditional GET to server\n");
                printf("Packet sent:\n%s",packetbuf);
                fpool[i].status = 1;
                return FORWARD_TO_SERVER;
                }
        }
        // Never GET before

        sprintf(packetbuf,"GET %s HTTP/1.0\r\nHost:%s\r\n\r\n",urlname,remotename);
        printf("Send GET request to server\n");
        return FORWARD_TO_SERVER;
    }
    else if ((strncasecmp(temp,"HTTP/1.0 200",12)==0) || (strncasecmp(temp,"HTTP/1.1 200",12)==0)){     // A new response from web server
        for (i=0;i<10;i++){
            if (websocketarray[i].done==0){          //Find the uncompleted entry
                websocketarray[i].serversd = srcsocknum;
                websocketarray[i].done = 1;
                break;
            }
        }
        webindex = i;

        printf("Retrieve new file from web server\n");

        strcpy(packetbuf,recvbuf);                //Forward the packet


        livetime = 0;
        maxlivetime = 0;
        now = time(NULL);

        for (i=0;i<10;++i){
            if (cachetable[i].status == 1){
                cachetable[i].status = 0;

                for (j=0;j<50;j++)
                    fpool[j].status = 0;

                printf("Replaced cache %d\n",i);
                for(j=0;j<50;j++){
                    if(!strcmp(fpool[j].remotename,cachetable[i].remotename)
                       &&!strcmp(fpool[j].urlname,cachetable[i].urlname)){
                        strcpy(filename,fpool[j].filename);
                    }
                }
                printf("Filename cached: %s\n",filename);
                strcpy(cachetable[i].filename,filename);    //Store filename in the cache table
                strcpy(cachetable[i].remotename,remotename);
                strcpy(cachetable[i].urlname,urlname);
                cachetable[i].timestamp = time(NULL);       //Renew the time flag
                gmt = gmtime(&now);

                if (gmt->tm_sec<10) sprintf(second,"0%d",gmt->tm_sec);
                else sprintf(second,"%d",gmt->tm_sec);

                if (gmt->tm_min<10) sprintf(minute,"0%d",gmt->tm_min);
                else sprintf(minute,"%d",gmt->tm_min);

                if (gmt->tm_hour<10) sprintf(hour,"0%d",gmt->tm_hour);
                else sprintf(hour,"%d",gmt->tm_hour);

                if (gmt->tm_mday<10) sprintf(date,"0%d",gmt->tm_mday);
                else sprintf(date,"%d",gmt->tm_mday);

                sprintf(cachetable[i].expire,"%s, %s %s %d %s:%s:%s GMT",
                        weekday[gmt->tm_wday],
                        date,
                        month[gmt->tm_mon],
                        gmt->tm_year+1900,
                        hour,
                        minute,
                        second);    //Get the expire time
                //cachetable[i].timeout = 0;                   //Restore the status
                break;
            }
        }

        if (i==10){
            for (i=0;i<10;++i){                     //Find the Least Recently Used entry
                livetime=difftime(now,cachetable[i].timestamp);
                if (livetime>maxlivetime){
                    maxlivetime = livetime;
                    index = i;
                }
            }
            cachetable[index].timeout = 1;
            printf("Cache %d renewed\n",index);

            for (i=0;i<10;++i){
                if (cachetable[i].timeout == 1){                 //To replace the entry of cache table
                    printf("Replaced cache %d\n",i);
                    sprintf(filename,"%d",filenum++);            //New file
                    printf("Filename cached: %s\n",filename);
                    strcpy(cachetable[i].filename,filename);    //Store filename in the cache table
                    strcpy(cachetable[i].remotename,remotename);
                    strcpy(cachetable[i].urlname,urlname);
                    cachetable[i].timestamp = time(NULL);       //Renew the time flag
                    gmt = gmtime(&now);
                    if (gmt->tm_sec<10) sprintf(second,"0%d",gmt->tm_sec);
                    else sprintf(second,"%d",gmt->tm_sec);

                    if (gmt->tm_min<10) sprintf(minute,"0%d",gmt->tm_min);
                    else sprintf(minute,"%d",gmt->tm_min);

                    if (gmt->tm_hour<10) sprintf(hour,"0%d",gmt->tm_hour);
                    else sprintf(hour,"%d",gmt->tm_hour);

                    if (gmt->tm_mday<10) sprintf(date,"0%d",gmt->tm_mday);
                    else sprintf(date,"%d",gmt->tm_mday);

                    sprintf(cachetable[i].expire,"%s, %s %s %d %s:%s:%s GMT",
                            weekday[gmt->tm_wday],
                            date,
                            month[gmt->tm_mon],
                            gmt->tm_year+1900,
                            hour,
                            minute,
                            second);    //Get the expire time
                    cachetable[i].timeout = 0;                   //Restore the status
                    break;
                }
            }

        }

        printf("Write file %s\n",filename);

        fp = fopen(filename,"w");                           //Allow the file to be read and append the packet after the existed file
        if (!fp) printf("Proxy: File open failure\n");
        fwrite(recvbuf,1,strlen(recvbuf),fp);               //Cache the file
        fclose(fp);

        for (j=0;j<50;j++){
            if(!strcmp(fpool[j].remotename,remotename)&&!strcmp(fpool[j].urlname,urlname)){
                strcpy(fpool[j].filename,cachetable[i].filename);
                strcpy(fpool[j].expire,cachetable[i].expire);
                printf("%d cache backup\n",i);
                break;
            }
        }

        if (j==50){
            for (j=0;j<50;j++){
                if(strcmp(fpool[j].remotename,"")==0){
                    strcpy(fpool[j].remotename,cachetable[i].remotename);    //backup the LRU file into file pool
                    strcpy(fpool[j].filename,cachetable[i].filename);
                    strcpy(fpool[j].urlname,cachetable[i].urlname);
                    strcpy(fpool[j].expire,cachetable[i].expire);
                    printf("%d cache backup\n",i);
                    break;
                }
            }
        }

        strcpy(websocketarray[webindex].filename,filename);
        if((lengthptr = strstr(recvbuf,"Content-Length"))==NULL){
            printf("Can't find content-length value\n");
            printf("%s\n",recvbuf);
        };          //Here, packet length must larger enough to include the string "Content-length"
        //printf("After content length\n");
        if ((ptr1 = strchr(lengthptr,' '))!=NULL){}
        //printf("After content length space\n");
            //printf("find Content-length\n");
        websocketarray[webindex].contentlen = atoi(ptr1+1);     //Get the length of HTML
        //printf("Content length is:%d\n",websocketarray[webindex].contentlen);

        if ((payloadptr = strstr(temp,"\r\n\r\n"))==NULL){    //Find the start of content
            //printf("No content found\n");
            return FORWARD_TO_CLIENT;
        }

        payloadptr+=4;

        websocketarray[webindex].contentfound = 1;
        websocketarray[webindex].contentlen-=nbytes-(payloadptr-recvbuf);
        //printf("Content left:%d\n",websocketarray[webindex].contentlen);
        if (websocketarray[webindex].contentlen<=0){
            websocketarray[webindex].complete = 1;
            //websocketarray[webindex].clientsd = 0;
            printf("Transmission completed\n");
        }

        return FORWARD_TO_CLIENT;
    }
    else if ((strncasecmp(temp,"HTTP/1.0 304",12)==0) || (strncasecmp(temp,"HTTP/1.1 304",12)==0)){
        printf("File not expired, transmit the local file\n");

        for (i=0;i<10;i++){
            if (websocketarray[i].done==0){          //Find the uncompleted entry
                websocketarray[i].serversd = srcsocknum;
                websocketarray[i].done = 1;
                break;
            }
        }
        //find cached file
        for(j=0;j<10;j++){
            if(cachetable[j].status == 1){
                cachetable[j].status = 0;

                for (k=0;k<50;k++)
                    fpool[k].status = 0;

                if((fp = fopen(cachetable[j].filename,"r"))!=NULL){
                    printf("Find the cached file\nFilename is %s\nURL is %s\n",
                           cachetable[j].filename,cachetable[j].urlname);
                    while(fread(packetbuf,1,MAXPACKETSIZE,fp)){                // Keep transferring back cached files(a loop of every 1000 bytes)
                        //printf("In fread\n");
                        if((sendbytes=send(websocketarray[i].clientsd,packetbuf,strlen(packetbuf),0))==-1){
                            perror("Proxy: Send back to client");
                            return -1;
                        }
                        memset(packetbuf,0,MAXPACKETSIZE);
                        usleep(1000);
                    }
                    if(!ferror(fp)){
                        if((sendbytes=send(websocketarray[i].clientsd,packetbuf,strlen(packetbuf),0))==-1){
                            perror("Proxy: Send back to client");
                            return -1;
                        }
                        //printf("%d bytes are sent\n",sendbytes);
                    }
                    printf("Transmission complete\n");
                    fclose(fp);
                    close(websocketarray[i].clientsd);
                    FD_CLR(websocketarray[i].clientsd,&fds);
                    websocketarray[i].clientsd = 0;
                    return BACK_TO_CLIENT;
                }
            }
        }
        // Not find the cached file
        for (k=0;k<50;k++){
            if (fpool[k].status == 1){
                fpool[k].status =0;
                printf("Find the file in file pool\n");
                livetime = 0;
                maxlivetime = 0;
                now = time(NULL);
                for (j=0;j<10;++j){                     //Find the Least Recently Used entry
                    livetime=difftime(now,cachetable[j].timestamp);
                    if (livetime>maxlivetime){
                        maxlivetime = livetime;
                        index = j;
                    }
                }
                cachetable[index].timeout = 1;

                for (j=0;j<10;++j){
                    if (cachetable[j].timeout == 1){                         //To replace the entry of cache table
                        printf("Filename cached %s\n",fpool[k].filename);
                        strcpy(cachetable[j].filename,fpool[k].filename);    //Store filename in the cache table
                        strcpy(cachetable[j].remotename,fpool[k].remotename);
                        strcpy(cachetable[j].urlname,fpool[k].urlname);
                        cachetable[i].timestamp = time(NULL);               //Renew the time flag
                        strcpy(cachetable[j].expire,fpool[k].expire);
                        //strncpy(cachetable[j].expire,exp,count);
                        cachetable[j].timeout = 0;
                        cachetable[j].status = 0;                           //Restore the status
                        break;
                    }
                }

                if((fp = fopen(cachetable[j].filename,"r"))!=NULL){
                    printf("Re-cached file\nFilename is %s\nURL is %s\n",
                           cachetable[j].filename,cachetable[j].urlname);
                    while(fread(packetbuf,1,MAXPACKETSIZE,fp)){                // Keep transferring back cached files(a loop of every 1000 bytes)
                        //printf("In fread\n");
                        if((sendbytes=send(websocketarray[i].clientsd,packetbuf,strlen(packetbuf),0))==-1){
                            perror("Proxy: Send back to client");
                            return -1;
                        }
                        memset(packetbuf,0,MAXPACKETSIZE);
                        usleep(1000);
                    }
                    if(!ferror(fp)){
                        if((sendbytes=send(websocketarray[i].clientsd,packetbuf,strlen(packetbuf),0))==-1){
                            perror("Proxy: Send back to client");
                            return -1;
                        }
                        //printf("%d bytes are sent\n",sendbytes);
                    }
                    printf("Transmission complete\n");
                    fclose(fp);
                    close(websocketarray[i].clientsd);
                    FD_CLR(websocketarray[i].clientsd,&fds);
                    websocketarray[i].clientsd = 0;
                    return BACK_TO_CLIENT;
                }
            }
        }
    }
    else if (contpack == 1){                                         //A continue packet from web server
        strcpy(packetbuf,recvbuf);     //Forward the packet
        if (websocketarray[webindex].contentfound == 0){

            if ((payloadptr = strstr(temp,"\r\n\r\n"))==NULL){    //Find the start of content
                //printf("No content found\n");
                return FORWARD_TO_CLIENT;
            }

            payloadptr+=4;

            //printf("Find content in %d web socket\n",webindex);
            websocketarray[webindex].contentfound = 1;
            websocketarray[webindex].contentlen-=nbytes-(payloadptr-recvbuf);
            //printf("Content1 left:%d\n",websocketarray[webindex].contentlen);
        }
        else{
            //printf("String length of recv packet is %d\n",nbytes);
            websocketarray[webindex].contentlen-=nbytes;
            //printf("Content2 left:%d\n",websocketarray[webindex].contentlen);
        }


        //If find the content, write it into file
        fp=fopen(websocketarray[webindex].filename,"a");
        if (!fp) printf("Proxy: File open failure\n");
        fwrite(recvbuf,1,nbytes,fp);
        fclose(fp);

        if (websocketarray[webindex].contentlen<=0){
            websocketarray[webindex].complete = 1;
            printf("Transmission completed\n");
        }
        //Reach the end of HTML, release the entry


        //Close the connection with the server
        return FORWARD_TO_CLIENT;
    }
    else if ((strncasecmp(temp,"HTTP/1.0 404",12)==0) || (strncasecmp(temp,"HTTP/1.1 404",12)==0)){
        printf("404 error\n");
        for (i=0;i<10;i++){
            if (websocketarray[i].done==0){          //Find the uncompleted entry
                websocketarray[i].serversd = srcsocknum;
                websocketarray[i].done = 1;
                break;
            }
        }
        webindex = i;
        strcpy(packetbuf,recvbuf);
        return FORWARD_TO_CLIENT;
    }
    else{
        //printf("Unspecified message\n");
        printf("%s",temp);
        return INVALID;
    }
    return -1;
}
