#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<sys/socket.h> 
#include<arpa/inet.h>
#include<netdb.h>
#include<unistd.h>
#include<fcntl.h>
#include<stdbool.h>
#include"utils/base64.h"

#define BUFF_SIZE 2048
#define ERR_EXIT(a) { perror(a); exit(1); }

void send_http_request(int sockfd, const char *request) {
    if (send(sockfd, request, strlen(request), 0) < 0) {
        ERR_EXIT("send()");
    }
}

void receive_http_response(int sockfd) {
    char buffer[BUFF_SIZE];
    ssize_t n;
    if ((n = recv(sockfd, buffer, sizeof(buffer) - 1, 0)) < 0) {
        ERR_EXIT("recv()");
    }
    buffer[n] = '\0';
    printf("%s\n", buffer);
    if ((n = recv(sockfd, buffer, sizeof(buffer) - 1, 0)) < 0) {
        ERR_EXIT("recv()");
    }
    buffer[n] = '\0';
    printf("%s\n", buffer);
    if (strstr(buffer, "200 OK") == NULL) {
        fprintf(stderr, "Command failed.\n");
    }
    else {
        fprintf(stderr, "Command successful.\n");
    }
}

void receive_http_get_response(int sockfd, char *filename) {
    char buffer[BUFF_SIZE];
    ssize_t n;
    if ((n = recv(sockfd, buffer, sizeof(buffer) - 1, 0)) < 0) {
        ERR_EXIT("recv()");
    }
    buffer[n] = '\0';
    printf("%s\n", buffer);
    
    printf("%s\n", buffer);
    if (strstr(buffer, "200 OK") == NULL) {
        fprintf(stderr, "Command failed.\n");
    }
    else {
        fprintf(stderr, "Command successful.\n");
        //parse content length
        char *content_length_start = strstr(buffer, "Content-Length: ") + strlen("Content-Length: ");
        char *content_length_end = strstr(content_length_start, "\r\n");
        char content_length_str[64];
        strncpy(content_length_str, content_length_start, content_length_end - content_length_start);
        content_length_str[content_length_end - content_length_start] = '\0';
        int content_length = atoi(content_length_str);
        fprintf(stderr, "Content-Length: %d\n", content_length);
        //parse content
        char *filebuffer = (char *)malloc(content_length+10);
        int total_received = 0;
        while (total_received < content_length) {
            int bytes_to_read = content_length - total_received;
            int received = recv(sockfd, filebuffer + total_received, bytes_to_read, 0);
            if (received <= 0) {
                perror("recv failed");
                free(filebuffer);
                return;
            }
            total_received += received;
        }
        fprintf(stderr, "Received %d bytes\n", total_received);
        FILE *file = fopen(filename, "wb");
        fwrite(filebuffer, 1, content_length, file);
        fclose(file);
        free(filebuffer);



        
    }
}

int main(int argc, char *argv[]) {
    int sockfd;
    struct sockaddr_in addr;
    char buffer[BUFF_SIZE] = {};
    bool auth = false;
    char auth_header[512] = {};

    // Parse the arguments
    if (argc != 3 && argc != 4) {
        fprintf(stderr, "Usage: ./client [host] [port] [username:password]\n");
        return -1;
    }

    if (argc == 4) {
        // Check if the username and password are valid using secret.txt
        FILE *file = fopen("./secret", "r");
        char line[64];
        char *username_password = argv[3];
        while (fgets(line, sizeof(line), file)) {
            line[strcspn(line, "\n")] = '\0';  // Remove newline character
            if (strcmp(line, username_password) == 0) {
                auth = true;
                //fprintf(stderr, "[AUTH] Authenticated for user: %s\n", line);
                // Encode the username and password
                size_t encoded_length;
                char *encoded = base64_encode((const unsigned char *)username_password, strlen(username_password), &encoded_length);
                if (encoded == NULL) {
                    //fprintf(stderr, "Failed to encode credentials.\n");
                    return -1;
                }
                snprintf(auth_header, sizeof(auth_header), "Authorization: Basic %s", encoded);
                //fprintf(stderr, "[AUTH] Auth header: %s\n", auth_header);
                free(encoded);
                break;
            }
        }
        fclose(file);
        if (!auth) {
            fprintf(stderr, "Invalid user or wrong password.\n");
            return -1;
        }
    }

    // Get socket file descriptor
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        ERR_EXIT("socket()");
    }

    // Resolve domain name to IP address
    struct hostent *host;
    if ((host = gethostbyname(argv[1])) == NULL) {
        ERR_EXIT("gethostbyname()");
    }
    char *ip = inet_ntoa(*((struct in_addr*)host->h_addr_list[0]));
    //fprintf(stderr, "[CON] Connecting to %s:%s\n", ip, argv[2]);

    // Set server address
    bzero(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(ip);
    addr.sin_port = htons(atoi(argv[2]));

    // Connect to the server
    if (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        ERR_EXIT("connect()");
    }

    while (1) {
        printf("> ");
        fflush(stdout);
        if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
            break;
        }
        buffer[strcspn(buffer, "\n")] = '\0';  // Remove newline character

        if (strncmp(buffer, "put ", 4) == 0) {
            // Upload file to /api/file
            char filename[128];
            sscanf(buffer, "put %s", filename);
            fprintf(stderr, "[PUT] Uploading file: %s\n", filename);
            FILE *file = fopen(filename, "rb");
            if (!file) {
                fprintf(stderr, "Command failed.\n");
                continue;
            }

            fseek(file, 0, SEEK_END);
            long filesize = ftell(file);
            fseek(file, 0, SEEK_SET);

            char *file_buffer = malloc(filesize);
            fread(file_buffer, 1, filesize, file);
            fclose(file);

            snprintf(buffer, sizeof(buffer), "POST /api/file HTTP/1.1\r\n"
                                         "Host: %s:%s\r\n"
                                         "User-Agent: CN2024Client/1.0\r\n"
                                         "Content-Type: multipart/form-data\r\n"
                                         "Content-Length: %ld\r\n"
                                         "Connection: keep-alive\r\n"
                                         "%s\r\n"
                                         "\r\n",
                     argv[1], argv[2], filesize, auth ? auth_header : "");

            send_http_request(sockfd, buffer);
            send(sockfd, file_buffer, filesize, 0);
            free(file_buffer);

            receive_http_response(sockfd);
        } else if (strncmp(buffer, "putv ", 5) == 0) {
            // Upload video to /api/video
            char *filename = buffer + 5;
            FILE *file = fopen(filename, "rb");
            if (!file) {
                fprintf(stderr, "Command failed.\n");
                continue;
            }

            fseek(file, 0, SEEK_END);
            long filesize = ftell(file);
            fseek(file, 0, SEEK_SET);

            char *file_buffer = malloc(filesize);
            fread(file_buffer, 1, filesize, file);
            fclose(file);

            snprintf(buffer, sizeof(buffer), "POST /api/video HTTP/1.1\r\n"
                                         "Host: %s:%s\r\n"
                                         "User-Agent: CN2024Client/1.0\r\n"
                                         "Content-Type: multipart/form-data\r\n"
                                         "Content-Length: %ld\r\n"
                                         "Connection: keep-alive\r\n"
                                         "%s\r\n"
                                         "\r\n",
                     argv[1], argv[2], filesize, auth ? auth_header : "");

            send_http_request(sockfd, buffer);
            send(sockfd, file_buffer, filesize, 0);
            free(file_buffer);

            receive_http_response(sockfd);
        } else if (strncmp(buffer, "get ", 4) == 0) {
            // Download file from /api/file/{filepath}
            char filename[128];
            sscanf(buffer, "get %s", filename);
            fprintf(stderr, "[GET] Downloading file: %s\n", filename);
            
            snprintf(buffer, sizeof(buffer), "GET /api/file/%s HTTP/1.1\r\n"
                                           "Host: %s:%s\r\n"
                                           "User-Agent: CN2024Client/1.0\r\n"
                                           "Connection: keep-alive\r\n"
                                           "%s\r\n"
                                           "\r\n",
                     filename, argv[1], argv[2], auth ? auth_header : "");

            send_http_request(sockfd, buffer);
            receive_http_get_response(sockfd, filename);
        } else if (strcmp(buffer, "quit") == 0) {
            printf("Bye.\n");
            break;
        } else {
            fprintf(stderr, "Command Not Found.\n");
        }
    }

    close(sockfd);
    return 0;
}
