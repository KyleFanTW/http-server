#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<sys/socket.h>
#include<arpa/inet.h>
#include<sys/ioctl.h>
#include<net/if.h>
#include<unistd.h> 

#define PORT 9999
#define ERR_EXIT(a){ perror(a); exit(1); }

int main(int argc, char *argv[]){
    int listenfd, connfd;
    struct sockaddr_in server_addr, client_addr;
    int client_addr_len = sizeof(client_addr);
    char *message = "Hello World!";

    // Get socket file descriptor
    if((listenfd = socket(AF_INET , SOCK_STREAM , 0)) < 0){
        ERR_EXIT("socket()");
    }

    // Set server address information
    bzero(&server_addr, sizeof(server_addr)); // erase the data
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(PORT);

    // Bind the server file descriptor to the server address
    if(bind(listenfd, (struct sockaddr *)&server_addr , sizeof(server_addr)) < 0){
        ERR_EXIT("bind()");
    }

    // Listen on the server file descriptor
    if(listen(listenfd , 3) < 0){
        ERR_EXIT("listen()");
    }

    // Accept the client and get client file descriptor
    if((connfd = accept(listenfd, (struct sockaddr *)&client_addr, (socklen_t*)&client_addr_len)) < 0){
        ERR_EXIT("accept()");
    }

    if(send(connfd, message, strlen(message), 0) < 0){
        ERR_EXIT("send()");
    }

    close(connfd);
    close(listenfd);
}
