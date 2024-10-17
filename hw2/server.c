#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdbool.h>
#include <pthread.h>
#include "utils/base64.h"
#include <dirent.h>
#include <sys/types.h>
#include <ctype.h>
#include <poll.h>
#include <sys/stat.h>
#include <signal.h>


#define MAX_CONNECTIONS 100
#define PORT 8081
#define ERR_EXIT(a) { perror(a); exit(1); }


#define ERROR401 "HTTP/1.1 401 Unauthorized\r\nServer: CN2023Server/1.0\r\nWWW-Authenticate: Basic realm=\"B11902050\"\r\nContent-Type: text/plain\r\nContent-Length: 13\r\n\r\nUnauthorized\n"
#define ERROR404 "HTTP/1.1 404 Not Found\r\nServer: CN2023Server/1.0\r\nContent-Type: text/plain\r\nContent-Length: 10\r\n\r\nNot Found\n"
#define ERROR4051 "HTTP/1.1 405 Method Not Allowed\r\nServer: CN2023Server/1.0\r\nAllow: GET\r\nContent-Length: 0\r\n\r\n"
#define ERROR4050 "HTTP/1.1 405 Method Not Allowed\r\nServer: CN2023Server/1.0\r\nAllow: POST\r\nContent-Length: 0\r\n\r\n"
#define ERROR500 " HTTP/1.1 500 Internal Server Error\r\nServer: CN2023Server/1.0\r\nContent-Length: 0\r\n\r\n"






bool should_keep_alive(const char *request) {
    const char *connection_header = strstr(request, "Connection: ");
    if (connection_header) {
        connection_header += strlen("Connection: ");
        if (strncasecmp(connection_header, "keep-alive", 10) == 0) {
            return true;
        }

        if (strncasecmp(connection_header, "close", 5) == 0) {
            return false;
        }
    }
    return true;
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


bool authenticate(const char *auth_header) {
    //fprintf(stderr, "Auth header: %s\n", auth_header);
    if (!auth_header) return false;
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


    size_t decoded_len;
    unsigned char *decoded_bytes = base64_decode(encoded, strlen(encoded), &decoded_len);
    char *decoded = (char *)malloc(decoded_len + 1);  // +1 for null terminator
    if (decoded_bytes) {
        memcpy(decoded, decoded_bytes, decoded_len);
        decoded[decoded_len] = '\0';  // Null-terminate the decoded string
    }
    free(decoded_bytes);


    // Check credentials against a stored username:password
    //fprintf(stderr, "Decoded: %s\n", decoded);

    FILE *file = fopen("./secret", "r");
    char line[64];
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\n")] = '\0';  // Remove newline character
        //fprintf(stderr, "Checking user: %s\n", line);
        //fprintf(stderr, "Checking password: %s\n", decoded);
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
    struct pollfd poll_fds[MAX_CONNECTIONS];
    int nfds = 1;  // Start with 1, for the listening socket
    signal(SIGPIPE, SIG_IGN); //CHECK THIS
    if (argc != 2) {
        fprintf(stderr, "Usage: ./server [port]\n");
        return -1;
    }

    if (atoi(argv[1]) < 1024 || atoi(argv[1]) > 65535) {
        fprintf(stderr, "Port number out of range\n");
        return -1;
    }

    int port = atoi(argv[1]);
    // Create listening socket
    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        ERR_EXIT("socket()");
    }

    bzero(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port);

    int opt = 1;
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        ERR_EXIT("setsockopt()");
    }

    if (bind(listenfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ERR_EXIT("bind()");
    }

    if (listen(listenfd, 10) < 0) {
        ERR_EXIT("listen()");
    }

    printf("Server listening on port %d...\n", PORT);

    
    poll_fds[0].fd = listenfd;
    poll_fds[0].events = POLLIN;  

    while (1) {
        int poll_count = poll(poll_fds, nfds, -1);  // Wait for events

        if (poll_count < 0) {
            perror("poll failed");
            continue;
        }

        // Check for events on the listening socket (new connections)
        if (poll_fds[0].revents & POLLIN) {
            connfd = accept(listenfd, (struct sockaddr *)&client_addr, (socklen_t *)&client_addr_len);
            if (connfd < 0) {
                perror("accept failed");
                continue;
            }

            printf("New connection accepted: fd %d\n", connfd);

            // Add new connection to the poll_fds array
            if (nfds < MAX_CONNECTIONS) {
                poll_fds[nfds].fd = connfd;
                poll_fds[nfds].events = POLLIN;  // Monitor for incoming data
                nfds++;
            } else {
                printf("Max connections reached, closing new connection\n");
                close(connfd);
            }
        }

        // Check for events on client sockets
        for (int i = 1; i < nfds; i++) {
            if (poll_fds[i].revents & POLLIN) {
                char buffer[1024];
                int bytes_received = recv(poll_fds[i].fd, buffer, sizeof(buffer) - 1, 0);
                bool keep_alive = true;
                bool auth = false;

                if (bytes_received <= 0) {
                    // Client disconnected or error occurred
                    printf("Closing connection: fd %d\n", poll_fds[i].fd);
                    close(poll_fds[i].fd);
                    poll_fds[i] = poll_fds[nfds - 1];  // Remove from poll set
                    nfds--;
                    i--;  // Process the new entry at index i
                } else {
                    buffer[bytes_received] = '\0';
                    keep_alive = should_keep_alive(buffer);
                    fprintf(stderr, "Keep alive: %d\n", keep_alive);
                    
                    if (bytes_received < 0) {
                        ERR_EXIT("recv()");
                    }
                    printf("Received request at connfd %d\n", connfd);

                    buffer[bytes_received] = '\0';
                    char *auth_header = strstr(buffer, "Authorization: ");
                    //fprintf(stderr, "Received request: %s\n", buffer);
                    //fprintf(stderr, "Auth header IS : %s\n", auth_header);
                
                    if (!auth_header || !authenticate(auth_header)) auth = false;
                    else auth = true;
                
                
                
                // Check if the request is for "/"
                if (strstr(buffer, " / ") != NULL) {
                    //response 405 if not GET
                    if (strstr(buffer, "GET / ") == NULL) {
                        send(connfd, ERROR4051, strlen(ERROR4051), 0);
                        fprintf(stderr, "[SYS] Send 405 Method Not Allowed for /, it should be GET\n");
                        continue;
                    }
                    // Read index.html file
                    int file_fd = open("./web/index.html", O_RDONLY);
                    if (file_fd < 0) {
                        send(connfd, ERROR404, strlen(ERROR404), 0);
                        fprintf(stderr, "[SYS] Send 404 due to file not found for /\n");
                    } else {
                        int read_bytes;
                        while ((read_bytes = read(file_fd, buffer, sizeof(buffer))) > 0) {
                            int response_length = strlen("HTTP/1.1 200 OK\r\nServer: CN2023Server/1.0\r\n") + strlen("Content-Type: text/html\r\n\r\n") +
                                strlen("Content-Length: ") + snprintf(NULL, 0, "%d", read_bytes) +
                                strlen("\r\n\r\n") + strlen(buffer) + 1;
                            char *response = (char *)malloc(response_length);
                            snprintf(response, response_length, "HTTP/1.1 200 OK\r\nServer: CN2023Server/1.0\r\nContent-Type: text/html\r\nContent-Length: %d\r\n\r\n%s", read_bytes, buffer);
                            send(connfd, response, response_length, 0);
                            free(response);
                        }
                        fprintf(stderr, "[SYS] Send 200 OK for /\n");
                        fprintf(stderr, "[SYS] Send index.html for /\n");

                        close(file_fd);
                    }
                } else if (strstr(buffer, " /api/file") != NULL) {
                    if (strstr(buffer, "/api/file/") == NULL) {
                        //this is for /api/file, the upload part
                        if (strstr(buffer, "POST /api/file") == NULL) {
                            send(connfd, ERROR4050, strlen(ERROR4050), 0);
                            fprintf(stderr, "[SYS] Send 405 Method Not Allowed for /api/file, it should be POST\n");
                            continue;
                        }
                        if (auth == false){
                            send(connfd, ERROR401, strlen(ERROR401), 0);
                            fprintf(stderr, "[SYS] Send 401 Unauthorized for /api/file\n");
                            continue;
                        }
                        // start uoploading file
                        fprintf(stderr, "[SYS] Start uploading file - TODO\n");
                    }
                    else {
                        //this is for /api/file/<filename>, the display part
                        if (strstr(buffer, "GET /api/file/") == NULL) {
                            send(connfd, ERROR4051, strlen(ERROR4051), 0);
                            fprintf(stderr, "[SYS] Send 405 Method Not Allowed for /api/file/<filename>, it should be GET\n");
                            continue;
                        }
                        //get the filename by decoding the url
                        //strategy: find the first / after /api/file/, then cut the string after that
                        char *filename = strstr(buffer, "/api/file/") + strlen("/api/file/");
                        char *end = strchr(filename, ' ');
                        if (end) {
                            *end = '\0';
                        }
                        filename = url_decode(filename);
                        fprintf(stderr, "[SYS] Requested file: %s\n", filename);
                        //open the file
                        char file_path[256];
                        snprintf(file_path, sizeof(file_path), "./web/files/%s", filename);
                        int file_fd = open(file_path, O_RDONLY);
                        if (file_fd < 0) {
                            send(connfd, ERROR404, strlen(ERROR404), 0);
                            fprintf(stderr, "[SYS] Send 404 due to file not found for /api/file/%s\n", filename);
                        } else {
                            // Determine the file size using stat
                            struct stat file_stat;
                            if (fstat(file_fd, &file_stat) < 0) {
                                send(connfd, ERROR500, strlen(ERROR500), 0);
                                close(file_fd);
                                fprintf(stderr, "[SYS] Send 500 Internal Server Error due to stat failure\n");
                                return;
                            }
                            int file_size = file_stat.st_size;
                            char *extension = strrchr(filename, '.');
                            char *content_type;
                            
                            if (strcmp(extension, ".html") == 0 || strcmp(extension, ".rhtml") == 0) {
                                    content_type = "text/html";
                            } else if (strcmp(extension, ".mp4") == 0 || strcmp(extension, ".m4v") == 0) {
                                content_type = "video/mp4";
                            } else if (strcmp(extension, ".m4s") == 0) {
                                content_type = "video/iso.segment";
                            } else if (strcmp(extension, ".m4a") == 0) {
                                content_type = "audio/mp4";
                            } else if (strcmp(extension, ".mpd") == 0) {
                                content_type = "application/dash+xml";
                            } else {
                                content_type = "text/plain";
                            }

                            // Prepare and send the HTTP header once
                            char header[1024];
                            snprintf(header, sizeof(header), "HTTP/1.1 200 OK\r\n"
                                                            "Server: CN2023Server/1.0\r\n"
                                                            "Content-Type: %s\r\n"
                                                            "Content-Length: %d\r\n\r\n", 
                                                            content_type, file_size);
                            send(connfd, header, strlen(header), 0);
                            fprintf(stderr, "[SYS] Send 200 OK for /api/file/%s\n", filename);

                            // Now send the file content in chunks
                            int read_bytes;
                            while ((read_bytes = read(file_fd, buffer, sizeof(buffer))) > 0) {
                                int sent_bytes = send(connfd, buffer, read_bytes, MSG_NOSIGNAL); // Use MSG_NOSIGNAL
                                if (sent_bytes < 0) {
                                    fprintf(stderr, "[SYS] Send failed for /api/file/%s\n", filename);
                                    break;
                                }
                            }
                            fprintf(stderr, "[SYS] Sent file %s for /api/file/%s\n", filename, filename);

                            // Close the file descriptor after reading is complete
                            close(file_fd);
                        }
                    }
                    
                } else if (strstr(buffer, " /upload/file") != NULL) {
                    if (strstr(buffer, "GET /upload/file") == NULL) {
                        send(connfd, ERROR4051, strlen(ERROR4051), 0);
                        fprintf(stderr, "[SYS] Send 405 Method Not Allowed for /upload/file, it should be GET\n");
                        continue;
                    }
                    if (auth == false){
                        send(connfd, ERROR401, strlen(ERROR401), 0);
                        fprintf(stderr, "[SYS] Send 401 Unauthorized for /upload/file\n");
                        continue;
                    }
                    int file_fd = open("./web/uploadf.html", O_RDONLY);
                    if (file_fd < 0) {
                        send(connfd, ERROR404, strlen(ERROR404), 0);
                        fprintf(stderr, "[SYS] Send 404 due to file not found for /upload/file\n");
                    } else {
                        int read_bytes;
                        while ((read_bytes = read(file_fd, buffer, sizeof(buffer))) > 0) {
                            int response_length = strlen("HTTP/1.1 200 OK\r\nServer: CN2023Server/1.0\r\n") + strlen("Content-Type: text/html\r\n\r\n") +
                                strlen("Content-Length: ") + snprintf(NULL, 0, "%d", read_bytes) +
                                strlen("\r\n\r\n") + strlen(buffer) + 1;
                            char *response = (char *)malloc(response_length);
                            snprintf(response, response_length, "HTTP/1.1 200 OK\r\nServer: CN2023Server/1.0\r\nContent-Type: text/html\r\nContent-Length: %d\r\n\r\n%s", read_bytes, buffer);
                            send(connfd, response, response_length, 0);
                            free(response);
                        }
                        fprintf(stderr, "[SYS] Send 200 OK for /upload/file\n");
                        fprintf(stderr, "[SYS] Send uploadf.html for /upload/file\n");

                        close(file_fd);
                    }
                } else if (strstr(buffer, " /upload/video") != NULL) {
                    if (strstr(buffer, "GET /upload/video") == NULL) {
                        send(connfd, ERROR4051, strlen(ERROR4051), 0);
                        fprintf(stderr, "[SYS] Send 405 Method Not Allowed for /upload/video, it should be GET\n");
                        continue;
                    }
                    if (auth == false){
                        send(connfd, ERROR401, strlen(ERROR401), 0);
                        fprintf(stderr, "[SYS] Send 401 Unauthorized for /upload/video\n");
                        continue;
                    }
                    
                    int file_fd = open("./web/uploadv.html", O_RDONLY);
                    if (file_fd < 0) {
                        send(connfd, ERROR404, strlen(ERROR404), 0);
                        fprintf(stderr, "[SYS] Send 404 due to file not found for /upload/video\n");
                    } else {
                        int read_bytes;
                        while ((read_bytes = read(file_fd, buffer, sizeof(buffer))) > 0) {
                            int response_length = strlen("HTTP/1.1 200 OK\r\nServer: CN2023Server/1.0\r\n") + strlen("Content-Type: text/html\r\n\r\n") +
                                strlen("Content-Length: ") + snprintf(NULL, 0, "%d", read_bytes) +
                                strlen("\r\n\r\n") + strlen(buffer) + 1;
                            char *response = (char *)malloc(response_length);
                            snprintf(response, response_length, "HTTP/1.1 200 OK\r\nServer: CN2023Server/1.0\r\nContent-Type: text/html\r\nContent-Length: %d\r\n\r\n%s", read_bytes, buffer);
                            send(connfd, response, response_length, 0);
                            free(response);
                        }
                        fprintf(stderr, "[SYS] Send 200 OK for /upload/video\n");
                        fprintf(stderr, "[SYS] Send uploadv.html for /upload/video\n");

                        close(file_fd);
                    }
                } else if (strstr(buffer, " /file/ ") != NULL) {
                    if (strstr(buffer, "GET /file/") == NULL) {
                        send(connfd, ERROR4051, strlen(ERROR4051), 0);
                        fprintf(stderr, "[SYS] Send 405 Method Not Allowed for /file/, it should be GET\n");
                        continue;
                    }
                    int file_fd = open("./web/listf.rhtml", O_RDONLY);
                    if (file_fd < 0) {
                        send(connfd, ERROR404, strlen(ERROR404), 0);
                        fprintf(stderr, "[SYS] Send 404 due to file not found for /file/\n");
                    } else {
                        char file_content[4096];
                        int read_bytes = read(file_fd, file_content, sizeof(file_content) - 1);
                        if (read_bytes < 0) {
                            close(file_fd);
                            ERR_EXIT("read()");
                        }
                        file_content[read_bytes] = '\0';
                        close(file_fd);

                        DIR *dir = opendir("./web/files");
                        if (!dir) {
                            send(connfd, ERROR404, strlen(ERROR404), 0);
                            fprintf(stderr, "[SYS] Send 404 due to file not found for /file/\n");
                        } else {
                            struct dirent *entry;
                            char files_list[4096] = "";
                            while ((entry = readdir(dir)) != NULL) {
                                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                                    continue;

                                char *encoded_name = url_encode(entry->d_name);  // Encode the file name

                                char row[256];
                                snprintf(row, sizeof(row), "<tr><td><a href=\"/api/file/%s\">%s</a></td></tr>\n", encoded_name, entry->d_name);
                                strcat(files_list, row);  // Append the row to the file_list

                                free(encoded_name);  // Free the encoded name
                            }
                            closedir(dir);

                            // Replace the pseudo-HTML tag "<?FILE_LIST?>" with the actual list of files
                            char *placeholder = strstr(file_content, "<?FILE_LIST?>");
                            if (placeholder) {
                                char final_content[8192];
                                strncpy(final_content, file_content, placeholder - file_content);
                                final_content[placeholder - file_content] = '\0';

                                strcat(final_content, files_list);
                                strcat(final_content, placeholder + strlen("<?FILE_LIST?>"));

                                int response_length = strlen("HTTP/1.1 200 OK\r\nServer: CN2023Server/1.0\r\n") + strlen("Content-Type: text/html\r\n\r\n") +
                                        strlen("Content-Length: ") + snprintf(NULL, 0, "%ld", strlen(final_content)) + strlen("\r\n\r\n") + strlen(final_content) + 1;
                                char *response = (char *)malloc(response_length);
                                snprintf(response, response_length, "HTTP/1.1 200 OK\r\nServer: CN2023Server/1.0\r\nContent-Type: text/html\r\nContent-Length: %ld\r\n\r\n%s", strlen(final_content), final_content);
                                send(connfd, response, response_length, 0);
                                free(response);
                                fprintf(stderr, "[SYS] Send 200 OK for /file/\n");
                                fprintf(stderr, "[SYS] Send listf.rhtml for /file/\n");
                            }
                        }
                    }
                } else if (strstr(buffer, " /video/ ") != NULL) {
                    // Open the template file
                    if (strstr(buffer, "GET /video/") == NULL) {
                        send(connfd, ERROR4051, strlen(ERROR4051), 0);
                        fprintf(stderr, "[SYS] Send 405 Method Not Allowed for /video/, it should be GET\n");
                        continue;
                    }
                    int file_fd = open("./web/listv.rhtml", O_RDONLY);
                    if (file_fd < 0) {
                        send(connfd, ERROR404, strlen(ERROR404), 0);
                        fprintf(stderr, "[SYS] Send 404 due to file not found for /video/\n");
                    } else {
                        // Load the content of the template into a buffer
                        char file_content[4096];  // Assuming the file size is less than 4096 bytes
                        int read_bytes = read(file_fd, file_content, sizeof(file_content) - 1);
                        if (read_bytes < 0) {
                            close(file_fd);
                            ERR_EXIT("read()");
                        }
                        file_content[read_bytes] = '\0';  // Null-terminate the content

                        // Close the template file after reading
                        close(file_fd);

                        // Scan the "hw2/web/videos" directory to get the list of video folders/files
                        DIR *dir = opendir("./web/videos");
                        if (!dir) {
                            send(connfd, ERROR404, strlen(ERROR404), 0);
                            fprintf(stderr, "[SYS] Send 404 due to file not found for /video/\n");
                        } else {
                            struct dirent *entry;
                            char video_list[4096] = "";  // Buffer to store the generated video list
                            while ((entry = readdir(dir)) != NULL) {
                                // Skip the "." and ".." directories
                                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                                    continue;

                                char *encoded_name = url_encode(entry->d_name);
                                // Create the HTML row for each video folder/file
                                char row[256];
                                snprintf(row, sizeof(row), "<tr><td><a href=\"/video/%s\">%s</a></td></tr>\n", encoded_name, entry->d_name);
                                strcat(video_list, row);  // Append the row to the video_list
                            }
                            closedir(dir);

                            // Replace the pseudo-HTML tag "<?VIDEO_LIST?>" with the actual list of videos
                            char *placeholder = strstr(file_content, "<?VIDEO_LIST?>");
                            if (placeholder) {
                                // Create a buffer for the final HTML content
                                char final_content[8192];
                                strncpy(final_content, file_content, placeholder - file_content);  // Copy the content before the placeholder
                                final_content[placeholder - file_content] = '\0';  // Null-terminate the string

                                strcat(final_content, video_list);  // Insert the video list
                                strcat(final_content, placeholder + strlen("<?VIDEO_LIST?>"));  // Append the content after the placeholder

                                // Send the final content to the client
                                int response_length = strlen("HTTP/1.1 200 OK\r\nServer: CN2023Server/1.0\r\n") + strlen("Content-Type: text/html\r\n\r\n") +
                                        strlen("Content-Length: ") + snprintf(NULL, 0, "%ld", strlen(final_content)) + strlen("\r\n\r\n") + strlen(final_content) + 1;
                                char *response = (char *)malloc(response_length);
                                snprintf(response, response_length, "HTTP/1.1 200 OK\r\nServer: CN2023Server/1.0\r\nContent-Type: text/html\r\nContent-Length: %ld\r\n\r\n%s", strlen(final_content), final_content);
                                send(connfd, response, response_length, 0);
                                free(response);
                                
                                fprintf(stderr, "[SYS] Send 200 OK for /video/\n");
                                fprintf(stderr, "[SYS] Send listv.rhtml for /video/\n");
                            }
                        }
                    }
                } else {
                    // pass if its for favicon.ico for now
                    if (strstr(buffer, "favicon.ico") != NULL) {
                        continue;
                    }
                    send(connfd, ERROR500, strlen(ERROR500), 0);
                    fprintf(stderr, "[SYS] Send 500 Internal Server Error\n");
                    fprintf(stderr, "[SYS] Error: Invalid request\n");
                    fprintf(stderr, "[SYS] Request: %s\n", buffer);
                
                }

                if (!keep_alive) {
                        printf("Closing connection due to Connection: close, fd: %d\n", poll_fds[i].fd);
                        close(poll_fds[i].fd);
                        poll_fds[i] = poll_fds[nfds - 1];  // Remove from poll set
                        nfds--;
                        i--;  // Process the new entry at index i
                    }
                }
            }
        }
    }

    close(listenfd);
    return 0;
}