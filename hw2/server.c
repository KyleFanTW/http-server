#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdbool.h>
#include "utils/base64.h"

//#define PORT 9999
#define ERR_EXIT(a) { perror(a); exit(1); }


bool authenticate(const char *auth_header) {
    if (!auth_header) return false;

    // Decode base64 and check credentials
    // Assuming auth_header is in the form "Basic base64credentials=="
    char encoded[512];
    sscanf(auth_header, "Authorization: Basic %s", encoded);

    //check correct base64 encoding
    for (int i = 0; i < strlen(encoded); i++) {
        if (!((encoded[i] >= 'A' && encoded[i] <= 'Z') || (encoded[i] >= 'a' && encoded[i] <= 'z') || (encoded[i] >= '0' && encoded[i] <= '9') || encoded[i] == '+' || encoded[i] == '/' || encoded[i] == '=')) {
            return false;
        }
    }
    fprintf(stderr, "Encoded: %s\n", encoded);
    //strip off the trailing \n
    if (encoded[strlen(encoded) - 1] == '\n') {
        encoded[strlen(encoded) - 1] = '\0';
    }
    //add padding if needed
    if (strlen(encoded) % 4 != 0) {
        int padding = 4 - (strlen(encoded) % 4);
        for (int i = 0; i < padding; i++) {
            strcat(encoded, "=");
        }
    }

    // Decoding base64 (simplified, use a proper base64 decoding library in real applications)
    size_t decoded_len;
    unsigned char *decoded_bytes = base64_decode(encoded, strlen(encoded), &decoded_len);
    char *decoded = (char *)malloc(decoded_len + 1);  // +1 for null terminator
    if (decoded_bytes) {
        memcpy(decoded, decoded_bytes, decoded_len);
        decoded[decoded_len] = '\0';  // Null-terminate the decoded string
    }
    free(decoded_bytes);


    // Check credentials against a stored username:password
    fprintf(stderr, "Decoded: %s\n", decoded);

    FILE *file = fopen("./secret", "r");
    char line[64];
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\n")] = '\0';  // Remove newline character
        fprintf(stderr, "Checking user: %s\n", line);
        fprintf(stderr, "Checking password: %s\n", decoded);
        if (strcmp(line, decoded) == 0) {
            fprintf(stderr, "Authenticated user: %s\n", decoded);
            fclose(file);
            return true;
        }
    }
    fclose(file);
    return false;
}

int main(int argc, char *argv[]) {
    int listenfd, connfd;
    struct sockaddr_in server_addr, client_addr;
    int client_addr_len = sizeof(client_addr);
    char buffer[1024];
    
    // Parse the arguments
    if (argc != 2) {
        fprintf(stderr, "Usage: ./server [port]\n");
        return -1;
    }

    for (int i = 0; i < strlen(argv[1]); i++) {
        if (argv[1][i] < '0' || argv[1][i] > '9') {
            fprintf(stderr, "Port must be a number\n");
            return -1;
        }
    }

    if (atoi(argv[1]) < 1024 || atoi(argv[1]) > 65535) {
        fprintf(stderr, "Port number out of range\n");
        return -1;
    }

    int PORT = atoi(argv[1]);

    // Get socket file descriptor
    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        ERR_EXIT("socket()");
    }
    
    // Set server address information
    bzero(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(PORT);

    // Bind the server file descriptor to the server address
    int opt = 1;
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        ERR_EXIT("setsockopt()");
    }

    if (bind(listenfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ERR_EXIT("bind()");
    }

    // Listen on the server file descriptor
    if (listen(listenfd, 3) < 0) {
        ERR_EXIT("listen()");
    }

    printf("Server listening on port %d...\n", PORT);

    while (1) {
        // Accept the client and get client file descriptor
        if ((connfd = accept(listenfd, (struct sockaddr *)&client_addr, (socklen_t *)&client_addr_len)) < 0) {
            ERR_EXIT("accept()");
        }

        // Receive HTTP request from the client
        int bytes_received = recv(connfd, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received < 0) {
            ERR_EXIT("recv()");
        }

        buffer[bytes_received] = '\0';
        char *auth_header = strstr(buffer, "Authorization: ");
        fprintf(stderr, "Received request: %s\n", buffer);
        fprintf(stderr, "Auth header IS : %s\n", auth_header);
        if (!auth_header || !authenticate(auth_header)) {
            char *response = "HTTP/1.1 401 Unauthorized\r\n"
                            "WWW-Authenticate: Basic realm=\"Access to the staging site\", charset=\"UTF-8\"\r\n"
                            "Content-Length: 0\r\n\r\n";
            send(connfd, response, strlen(response), 0);
            close(connfd);
            continue;  // Go back to the start of the loop and wait for a new connection
        }

        // Check if the request is for "/"
        if (strstr(buffer, "GET / ") != NULL) {
            // Read index.html file
            int file_fd = open("./web/index.html", O_RDONLY);
            if (file_fd < 0) {
                // Send 404 Not Found response if the file can't be opened
                char *not_found_response = "HTTP/1.1 404 Not Found\r\nContent-Length: 13\r\n\r\n404 Not Found";
                send(connfd, not_found_response, strlen(not_found_response), 0);
            } else {
                // Send 200 OK response with the content of index.html
                char *header = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n";
                send(connfd, header, strlen(header), 0);
                
                int read_bytes;
                while ((read_bytes = read(file_fd, buffer, sizeof(buffer))) > 0) {
                    send(connfd, buffer, read_bytes, 0);
                }

                close(file_fd);
            }
        } else if (strstr(buffer, "GET /upload/file") != NULL) {
            // Read secret.html file
            int file_fd = open("./web/uploadf.html", O_RDONLY);
            if (file_fd < 0) {
                // Send 404 Not Found response if the file can't be opened
                char *not_found_response = "HTTP/1.1 404 Not Found\r\nContent-Length: 13\r\n\r\n404 Not Found";
                send(connfd, not_found_response, strlen(not_found_response), 0);
            } else {
                // Send 200 OK response with the content of secret.html
                char *header = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n";
                send(connfd, header, strlen(header), 0);
                
                int read_bytes;
                while ((read_bytes = read(file_fd, buffer, sizeof(buffer))) > 0) {
                    send(connfd, buffer, read_bytes, 0);
                }

                close(file_fd);
            }
        } else if (strstr(buffer, "GET /upload/video") != NULL) {
            // Read secret.html file
            int file_fd = open("./web/uploadv.html", O_RDONLY);
            if (file_fd < 0) {
                // Send 404 Not Found response if the file can't be opened
                char *not_found_response = "HTTP/1.1 404 Not Found\r\nContent-Length: 13\r\n\r\n404 Not Found";
                send(connfd, not_found_response, strlen(not_found_response), 0);
            } else {
                // Send 200 OK response with the content of secret.html
                char *header = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n";
                send(connfd, header, strlen(header), 0);
                
                int read_bytes;
                while ((read_bytes = read(file_fd, buffer, sizeof(buffer))) > 0) {
                    send(connfd, buffer, read_bytes, 0);
                }

                close(file_fd);
            }
        }  else if (strstr(buffer, "GET /file/") != NULL) {
            // Read secret.html file
            int file_fd = open("./web/listf.rhtml", O_RDONLY);
            if (file_fd < 0) {
                // Send 404 Not Found response if the file can't be opened
                char *not_found_response = "HTTP/1.1 404 Not Found\r\nContent-Length: 13\r\n\r\n404 Not Found";
                send(connfd, not_found_response, strlen(not_found_response), 0);
            } else {
                // Send 200 OK response with the content of secret.html
                char *header = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n";
                send(connfd, header, strlen(header), 0);
                
                int read_bytes;
                while ((read_bytes = read(file_fd, buffer, sizeof(buffer))) > 0) {
                    send(connfd, buffer, read_bytes, 0);
                }

                close(file_fd);
            }
        }else {
            // Send 405 Method Not Allowed response for unsupported routes
            char *method_not_allowed_response = "HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\n\r\n";
            send(connfd, method_not_allowed_response, strlen(method_not_allowed_response), 0);
        }

        close(connfd);
    }

    close(listenfd);
    return 0;
}
