#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<sys/socket.h> 
#include<arpa/inet.h>
#include<netdb.h>
#include<unistd.h>
#include<fcntl.h>
#include<stdbool.h>
#include<signal.h>
#include<sys/types.h>
#include<sys/stat.h>

#include"utils/base64.h"

#define BUFF_SIZE 2048
#define ERR_EXIT(a) { perror(a); exit(1); }

int increment;

void send_http_request(int sockfd, const char *request) {
    if (send(sockfd, request, strlen(request), 0) < 0) {
        ERR_EXIT("send()");
    }
}

int receive_http_response(int sockfd) {
    char buffer[BUFF_SIZE];
    ssize_t n;
    if ((n = recv(sockfd, buffer, sizeof(buffer) - 1, 0)) < 0) {
        ERR_EXIT("recv()");
    }
    buffer[n] = '\0';
    //printf("%s\n", buffer);
    
    //if 401 unauthorized return 401
    if (strstr(buffer, "HTTP/1.1 200") != NULL) {
        // fprintf(stderr, "[RCV] Command failed.\n");
        return 1;
    }
    else {
        // fprintf(stderr, "[RCV] Command successful.\n");
        return 0;
    }
}

void receive_http_get_response(int sockfd, char *filename) {
    char buffer[BUFF_SIZE];
    ssize_t n;
    if ((n = recv(sockfd, buffer, sizeof(buffer) - 1, 0)) < 0) {
        ERR_EXIT("recv()");
    }
    buffer[n] = '\0';
    //fprintf(stderr, "Response: %s\n", buffer);
    
    if (strstr(buffer, "HTTP/1.1 200") == NULL) {
        fprintf(stderr, "Command failed.\n");
    }
    else {
        
        //parse content length
        char *content_length_start = strstr(buffer, "Content-Length: ") + strlen("Content-Length: ");
        char *content_length_end = strstr(content_length_start, "\r\n");
        char content_length_str[64];
        strncpy(content_length_str, content_length_start, content_length_end - content_length_start);
        content_length_str[content_length_end - content_length_start] = '\0';
        int content_length = atoi(content_length_str);
        // fprintf(stderr, "Content-Length: %d\n", content_length);
        //parse content
        char *filebuffer = (char *)malloc(content_length+10);
        int total_received = 0;
        char *file_content_start = strstr(buffer, "\r\n\r\n") + 4;
        int file_content_length = n - (file_content_start - buffer);
        memcpy(filebuffer, file_content_start, file_content_length);
        total_received += file_content_length;
        
        while (total_received < content_length) {
            int bytes_to_read = content_length - total_received;
            int received = recv(sockfd, filebuffer + total_received, bytes_to_read, 0);
            if (received < 0) {
                perror("recv failed");
                free(filebuffer);
                return;
            }
            else if (received == 0) {
                break;  // Connection closed
            }
            total_received += received;
        }
        //fprintf(stderr, "Received %d bytes\n", total_received);
        char path[256];
        snprintf(path, sizeof(path), "./files/%s", filename);
        //fprintf(stderr, "Saving file to %s\n", path);
        FILE *file = fopen(path, "wb");
        fwrite(filebuffer, 1, content_length, file);
        fclose(file);
        free(filebuffer);
        fprintf(stderr, "Command succeeded.\n");
        
    }
}


int establish_connection(const char *host_ip, int port) {
    int sockfd;
    struct sockaddr_in addr;

    // Create socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        ERR_EXIT("socket()");
    }

    // Set server address
    bzero(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(host_ip);
    addr.sin_port = htons(port);

    // Connect to the server
    if (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sockfd);
        ERR_EXIT("connect()");
    }

    return sockfd;
}

char *url_encode(const char *str) {
    char *encoded = malloc(strlen(str) * 3 + 1);  
    char *pencoded = encoded;  

    while (*str) {
        if (isalnum(*str) || *str == '-' || *str == '_' || *str == '.' || *str == '~') {
            *pencoded++ = *str;  
        } else {
            pencoded += sprintf(pencoded, "%%%02X", (unsigned char)*str);
        }
        str++;
    }

    *pencoded = '\0';  
    return encoded;
}


char *url_decode(const char *str) {
    char *decoded = malloc(strlen(str) + 1); 
    char *pdecoded = decoded;  
    while (*str) {
        if (*str == '%') {
            int value;
            sscanf(str + 1, "%2x", &value); 
            *pdecoded++ = (char)value;
            str += 3; 
        } else {
            *pdecoded++ = *str++;
        }
    }
    *pdecoded = '\0'; 
    return decoded;
}


int main(int argc, char *argv[]) {
    int sockfd;
    struct sockaddr_in addr;
    char buffer[BUFF_SIZE] = {};
    bool auth = false;
    char auth_header[512] = {};
    signal(SIGPIPE, SIG_IGN);
    increment = 0;

    // Parse the arguments
    if (argc != 3 && argc != 4) {
        fprintf(stderr, "Usage: ./client [host] [port] [username:password]\n");
        return -1;
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

    if (argc == 4) {
        // Check if the username and password are valid using secret.txt
        size_t encoded_length;
        char username_password[256];
        strncpy(username_password, argv[3], sizeof(username_password) - 1);
        username_password[sizeof(username_password) - 1] = '\0';
        // Check if ':' is present and only once
        

        //fprintf(stderr, "[MAIN - AUTH] Username:Password: %s\n", username_password);
        char *encoded = base64_encode((const unsigned char *)username_password, strlen(username_password), &encoded_length);
        if (encoded == NULL) {
            //fprintf(stderr, "Failed to encode credentials.\n");
            return -1;
        }
        snprintf(auth_header, sizeof(auth_header), "Authorization: Basic %s", encoded);
        //fprintf(stderr, "[MAIN - AUTH] Auth header: %s\n", auth_header);
        free(encoded);
        //Try to authenticate with the server
        snprintf(buffer, sizeof(buffer), "GET /upload/file HTTP/1.1\r\n"
                                         "Host: %s:%s\r\n"
                                         "User-Agent: CN2024Client/1.0\r\n"
                                         "Connection: keep-alive\r\n"
                                         "Content-Length: 0\r\n"
                                         "%s\r\n"
                                         "\r\n",
                     argv[1], argv[2], auth_header);

        send_http_request(sockfd, buffer);
        int response = receive_http_response(sockfd);
        if (response == 0) {
            fprintf(stderr, "Invalid user or wrong password.\n");
            return -1; //CHECK THIS
        }
        else {
            // fprintf(stderr, "[MAIN - AUTH] Authenticated.\n");
            auth = true;
        }
    }
    // check directory
    struct stat st = {0};
    if (stat("./files", &st) == -1) {
        mkdir("./files", 0700);
    }

    while (1) {
        printf("> ");
        fflush(stdout);
        if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
            break;
        }
        buffer[strcspn(buffer, "\n")] = '\0';  // Remove newline character

        close(sockfd);
        sockfd = establish_connection(ip, atoi(argv[2]));

        char command[16] = {};
        char filename[256] = {};
        sscanf(buffer, "%s %[^\n]", command, filename);
        


        if (strcmp(command, "put") == 0) {
            
            if (strlen(filename) == 0) {
                fprintf(stderr, "Usage: put [file]\n");
            } else {
                fprintf(stderr, "[PUT] Uploading file: .%s.\n", filename);
                // fprintf(stderr, "[PUT] Uploading file: %s\n", filename);
                FILE *file = fopen(filename, "rb");
                if (!file) {
                    fprintf(stderr, "Command failed.\n");
                    continue;
                }
                // fprintf(stderr, "[PUT] File opened: %s\n", filename);

                fseek(file, 0, SEEK_END);
                long filesize = ftell(file);
                fseek(file, 0, SEEK_SET);

                char *file_buffer = malloc(filesize);
                fread(file_buffer, 1, filesize, file);
                fclose(file);

                // fprintf(stderr, "[PUT] File size: %ld\n", filesize);

                // generate a random boundary
                char boundary[128];
                //let the boundary be the filename encoded in base64
                //size_t encoded_length;
                //char *encoded_name = base64_encode((const unsigned char *)filename, strlen(filename), &encoded_length);
                snprintf(boundary, sizeof(boundary), "----%s%d", "abc", increment);
                increment++;
                boundary[(strlen(boundary) - 1)] = '\0';
                //free(encoded_name);
                char boundaryheader[256];
                snprintf(boundaryheader, sizeof(boundaryheader), "Content-Type: multipart/form-data; boundary=%s", boundary);

                int fullContentLength = strlen(boundary) + 4 + strlen("Content-Disposition: form-data; name=\"upfile\"; filename=\"") + strlen(filename) + strlen("\"\r\n\r\n") + filesize + 2 + strlen(boundary) + 6;



                snprintf(buffer, sizeof(buffer), "POST /api/file HTTP/1.1\r\n"
                                            "Host: %s:%s\r\n"
                                            "User-Agent: CN2024Client/1.0\r\n"
                                            "Connection: keep-alive\r\n"
                                            "%s\r\n"
                                            "Content-Length: %d\r\n"
                                            "%s\r\n"
                                            "\r\n"
                                            "--%s\r\n"
                                            "Content-Disposition: form-data; name=\"upfile\"; filename=\"%s\"\r\n"
                                            "\r\n",
                        argv[1], argv[2], boundaryheader, fullContentLength, auth ? auth_header : "", boundary, filename);
                // fprintf(stderr, "Request: %s\n", buffer);
                send_http_request(sockfd, buffer);
                send(sockfd, file_buffer, filesize, 0);
                // fprintf(stderr, "File: %s\n", file_buffer);
                memset(buffer, 0, sizeof(buffer));
                snprintf(buffer, sizeof(buffer), "\r\n--%s--\r\n", boundary);
                send(sockfd, buffer, strlen(buffer), 0);
                // fprintf(stderr, "Request: %s\n", buffer);
                free(file_buffer);

                (receive_http_response(sockfd))? fprintf(stderr, "Command succeeded.\n") : fprintf(stderr, "Command failed.\n");

            }

        } else if (strcmp(buffer, "putv") == 0) {
            if(strlen(filename) == 0) {
                // No argument found after "put"
                fprintf(stderr, "Usage: putv [file]\n");
            } else {
                // fprintf(stderr, "[PUTV] Uploading file: %s\n", filename);
                FILE *file = fopen(filename, "rb");
                if (!file) {
                    fprintf(stderr, "Command failed.\n");
                    continue;
                }
                // fprintf(stderr, "[PUTV] File opened: %s\n", filename);

                fseek(file, 0, SEEK_END);
                long filesize = ftell(file);
                fseek(file, 0, SEEK_SET);

                char *file_buffer = malloc(filesize);
                fread(file_buffer, 1, filesize, file);
                fclose(file);

                // fprintf(stderr, "[PUTV] File size: %ld\n", filesize);

                // generate a random boundary
                char boundary[128];
                //let the boundary be the filename encoded in base64
                //size_t encoded_length;
                //char *encoded_name = base64_encode((const unsigned char *)filename, strlen(filename), &encoded_length);
                snprintf(boundary, sizeof(boundary), "----%s%d", "abc", increment);
                increment++;
                boundary[(strlen(boundary) - 1)] = '\0';
                //free(encoded_name);
                char boundaryheader[256];
                snprintf(boundaryheader, sizeof(boundaryheader), "Content-Type: multipart/form-data; boundary=%s", boundary);

                int fullContentLength = strlen(boundary) + 4 + strlen("Content-Disposition: form-data; name=\"upfile\"; filename=\"") + strlen(filename) + strlen("\"\r\n\r\n") + filesize + 2 + strlen(boundary) + 6;



                snprintf(buffer, sizeof(buffer), "POST /api/video HTTP/1.1\r\n"
                                            "Host: %s:%s\r\n"
                                            "User-Agent: CN2024Client/1.0\r\n"
                                            "Connection: keep-alive\r\n"
                                            "%s\r\n"
                                            "Content-Length: %d\r\n"
                                            "%s\r\n"
                                            "\r\n"
                                            "--%s\r\n"
                                            "Content-Disposition: form-data; name=\"upfile\"; filename=\"%s\"\r\n"
                                            "\r\n",
                        argv[1], argv[2], boundaryheader, fullContentLength, auth ? auth_header : "", boundary, filename);
                // fprintf(stderr, "Request: %s\n", buffer);
                send_http_request(sockfd, buffer);
                send(sockfd, file_buffer, filesize, 0);
                // fprintf(stderr, "File: %s\n", file_buffer);
                memset(buffer, 0, sizeof(buffer));
                snprintf(buffer, sizeof(buffer), "\r\n--%s\r\n", boundary);
                send(sockfd, buffer, strlen(buffer), 0);
                // fprintf(stderr, "Request: %s\n", buffer);
                free(file_buffer);

                (receive_http_response(sockfd))? fprintf(stderr, "Command succeeded.\n") : fprintf(stderr, "Command failed.\n");
            }
        } else if (strcmp(buffer, "get") == 0) {
            if (strlen(filename) == 0) {
                // No argument found after "put"
                fprintf(stderr, "Usage: get [file]\n");
            } else {
            // fprintf(stderr, "[GET] Downloading file: %s\n", filename);
               char encoded_filename[256];
                snprintf(encoded_filename, sizeof(encoded_filename), "%s", url_encode(filename));

                snprintf(buffer, sizeof(buffer), "GET /api/file/%s HTTP/1.1\r\n"
                                            "Host: %s:%s\r\n"
                                            "User-Agent: CN2024Client/1.0\r\n"
                                            "Connection: keep-alive\r\n"
                                            "%s\r\n"
                                            "\r\n",
                        encoded_filename, argv[1], argv[2], auth ? auth_header : "");
                send_http_request(sockfd, buffer);
                receive_http_get_response(sockfd, filename);
            }
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
