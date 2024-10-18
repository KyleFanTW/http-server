#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<sys/socket.h> 
#include<arpa/inet.h>
#include<sys/ioctl.h>
#include<net/if.h>
#include<unistd.h>
#include<netdb.h>
#include<fcntl.h>
#include<stdbool.h>

#define BUFF_SIZE 1024
#define PORT 9999
#define ERR_EXIT(a){ perror(a); exit(1); }

int main(int argc , char *argv[]){
    int sockfd;
    struct sockaddr_in addr;
    char buffer[BUFF_SIZE] = {};
    bool auth = false;

    // Parse the arguments
    if(argc != 3 && argc != 4){
        fprintf(stderr, "Usage: ./client [host] [port] [username:password]\n");
        return -1;
    }

    if(argc == 4){
        // Check if the username and password are valid using secret.txt
        FILE *file = fopen("./secret", "r");
        char line[64];
        char *username_password = argv[3];
        while(fgets(line, sizeof(line), file)){
            line[strcspn(line, "\n")] = '\0';  // Remove newline character
            if(strcmp(line, username_password) == 0){
                auth = true;
                fprintf(stderr, "[AUTH] Authenticated for user: %s\n", line);
                break;
            }
        }
        fclose(file);
        if (!auth){
            fprintf(stderr, "Invalid user or wrong password.\n");
            //CHECK IF RETURN -1
            return -1;
        }

    }

    // Get socket file descriptor
    if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        ERR_EXIT("socket()");
    }
    //If host is a domain name (e.g., csie.ntu.edu.tw), the client needs to convert it into the corresponding IP address in order to create sockets for it.
    
    // resolve domain name to IP address
    struct hostent *host;
    if((host = gethostbyname(argv[1])) == NULL){
        ERR_EXIT("gethostbyname()");
    }
    char *ip = inet_ntoa(*((struct in_addr*)host->h_addr_list[0]));
    fprintf(stderr, "[CON] Connecting to %s:%s\n", ip, argv[2]);

    // Set server address
    bzero(&addr,sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(ip);
    addr.sin_port = htons(atoi(argv[2]));

    // Connect to the server
    if(connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0){
        ERR_EXIT("connect()");
    }
   
    // Receive message from server
    ssize_t n;
    if((n = read(sockfd, buffer, sizeof(buffer))) < 0){
        ERR_EXIT("read()");
    }

    printf("%s\n", buffer);

    close(sockfd);

    return 0;
}
