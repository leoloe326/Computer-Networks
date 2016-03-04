/**********************************************************
*   Date: 10/14/2014
*   Project: Simple HTTP Proxy
*
*   File Description: This is the client end of HTTP Proxy
*   Functions:  Using TCP to send GET request and receive ASCII
*   files from the proxy server
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
#include <sys/socket.h>

#define MAXDATASIZE 1000
#define MAXPACKETSIZE 10000

int main(int argc, char* argv[]){
    FILE *fp;
    int sd,sockaddrlen,nbytes,hostlen,contentlen,contentfound=0,contpack=0;
    struct sockaddr_in proxy;
    char hostname[16],port[6],url[50],urlname[50],packetbuf[MAXPACKETSIZE],recvbuf[MAXPACKETSIZE],fullpath[100],remotehost[50],filename[50];
    char *ptr=NULL,*temp=NULL,*lengthptr=NULL,*contentptr=NULL,*urlptr;

    if (argc!=4){
        printf("Please input the command line like:./client proxy_address proxy_port url\n");
        return 0;
    }

    //New socket
    if((sd = socket(AF_INET,SOCK_STREAM,0))<0){
        perror("Client: Socket");
        return 0;
    }

    strncpy(hostname,argv[1],16);
    //printf("%s\n",hostname);
    strncpy(port,argv[2],6);
    strncpy(url,argv[3],50);


    memset(&proxy,0,sizeof proxy);
    proxy.sin_family = AF_INET;
    proxy.sin_port = htons(atoi(port));
    proxy.sin_addr.s_addr = inet_addr(hostname);

    sockaddrlen = sizeof proxy;

    if(connect(sd,(struct sockaddr *)&proxy,(socklen_t)sockaddrlen)<0){
        close(sd);
        perror("Client: Connect");
        return 0;
    }

    printf("Connected\n");
    if(strncasecmp(url,"http://",7)==0){            //Start with http://
        temp = url + 7;
        //printf("in http://\n");

        if((urlptr=strchr(temp,'/'))==NULL){
            temp[strlen(temp)]='/';
            sprintf(filename,"index.html");
        }
        else{
            if(strchr(urlptr,'.')==NULL){
                sprintf(filename,"index.html");
            }
            else
                sprintf(filename,"%s",strrchr(temp,'/')+1);
        }


        hostlen = strchr(temp,'/')-temp;
        printf("host length: %d\n",hostlen);
        strncpy(remotehost,temp,hostlen);
        remotehost[hostlen]='\0';
        printf("remote host: %s\n",remotehost);
        strcpy(urlname,strchr(temp,'/'));
        printf("urlname: %s\n",urlname);



        sprintf(packetbuf,"GET %s HTTP/1.0\r\nHost:%s\r\n\r\n",urlname,remotehost); //The GET Message
    }
    else                                        //without the prefix of http://
    {
        temp = url;

        if((urlptr=strchr(temp,'/'))==NULL){
            temp[strlen(temp)]='/';
            sprintf(filename,"index.html");
        }
        else{
            if(strchr(urlptr,'.')==NULL){
                sprintf(filename,"index.html");
            }
            else
                sprintf(filename,"%s",strrchr(temp,'/')+1);
        }

        if(strchr(temp,'/')==NULL) temp[strlen(temp)]='/';
        hostlen = strchr(temp,'/')-temp;
        strncpy(remotehost,temp,hostlen);
        remotehost[hostlen]='\0';
        printf("remote host: %s\n",remotehost);
        strcpy(urlname,strchr(temp,'/'));
        printf("urlname: %s\n",urlname);


        sprintf(packetbuf,"GET %s HTTP/1.0\r\nHost:%s\r\n\r\n",urlname,remotehost); //The GET Message
    }

    /*
    *****************************************
    *   Receive packet from proxy
    ******************************************
    */

    if(send(sd,packetbuf,sizeof packetbuf,0)==-1 && errno != EAGAIN){
        close(sd);
        perror("Client: Send");
        return 0;
    };
    printf("GET request sent\n");
    while(1){
        //ptr = recvbuf;
        memset(recvbuf,0,MAXPACKETSIZE);
        if((nbytes = recv(sd,recvbuf,sizeof recvbuf,0))==-1 && errno != EAGAIN){
            close(sd);
            perror("Client: Receive");
            return 0;
        }
        else if (nbytes == 0 || errno == EAGAIN) continue;
        else{
//            printf("Received HTTP response packet\n");
            //printf("%s\n",recvbuf);
//            if(!contpack){
//                if(strncasecmp(recvbuf,"HTTP/1.0",8)!=0 && strncasecmp(recvbuf,"HTTP/1.1",8)!=0){
//                    printf("Version Error\n");
//                    return 0;
//                }
//            }
            //printf("Received:\n%s\n",recvbuf);
            if(strncasecmp(recvbuf,"HTTP/1.0 200",12)==0 || strncasecmp(recvbuf,"HTTP/1.1 200",12)==0){       //A new response packet
                printf("Receive packet from proxy server\n");
                if((lengthptr = strstr(recvbuf,"Content-Length"))==NULL){
                }

                if ((ptr = strchr(lengthptr,' '))!=NULL){
                }
                contentlen = atoi(ptr+1);     //Get the length of HTML
                //printf("content length is %d\n",contentlen);
                if ((contentptr = strstr(recvbuf,"\r\n\r\n"))==NULL){        //No content received, loop back to get new packet
                    contpack = 1;
                    continue;
                }
                contentptr+=4;

                contpack = 1;
                contentfound = 1;

                //Find the start of content
                getcwd(fullpath,50);
                //sprintf(filename,"/%s-%s.html",hostname,url);
                strcat(fullpath,"/");
                strcat(fullpath,filename);

                fp = fopen(fullpath,"w");      //New file
                fwrite(contentptr,1,strlen(contentptr),fp);
//                printf("Packet content length %d\n",strlen(contentptr));
                fclose(fp);
                printf("Successfully got the HTML file\n");
                if ((contentlen-=nbytes-(contentptr-recvbuf))>0){
                    //printf("content length left %d\n",contentlen);
                    continue;
                }
                printf("Transmission completed\n");
                close(sd);                      //Reach the end of the content
                return 0;
            }
            else if (strncasecmp(recvbuf,"HTTP/1.0 404",12)==0 || strncasecmp(recvbuf,"HTTP/1.1 404",12)==0){
                printf("404 error\n");
                return 0;
            }
            else if (contpack){
                if(contentfound == 1){
                    fp=fopen(fullpath,"a");
                    fwrite(recvbuf,1,nbytes,fp);
                    fclose(fp);

//                    printf("Packet content length %d\n",strlen(recvbuf));
                    if ((contentlen-=nbytes)>0){
                        //printf("content length left %d\n",contentlen);
                        continue;
                    }
                    printf("Transmission completed\n");
                    close(sd);                  //Reach the end of the content
                    return 0;
                }
                else{
                    if ((contentptr = strstr(recvbuf,"\r\n\r\n"))==NULL){        //No content received, loop back to get new packet
                        //printf("No content found\n");
                        continue;
                    }

                    contentptr+=4;

                    contentfound = 1;
                    getcwd(fullpath,50);
                    //sprintf(filename,"/\"%s.html\"",url);
                    strcat(fullpath,"/");
                    strcat(fullpath,filename);
                    printf("Fullpath is %s\n",fullpath);

                    fp = fopen(fullpath,"w");      //New file
                    fwrite(contentptr,1,strlen(contentptr),fp);
                    printf("Successfully got the HTML file\n");
                    fclose(fp);
                    if ((contentlen-=nbytes-(contentptr-recvbuf))>0) {
                        //printf("content length left %d\n",contentlen);
                        continue;
                    }
                    printf("Transmission completed\n");
                    close(sd);                      //Reach the end of the content
                    return 0;
                }
            }
        }
    }
}
