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
#define BUFFER_SIZE 8192


#define ERROR401 "HTTP/1.1 401 Unauthorized\r\nServer: CN2024Server/1.0\r\nWWW-Authenticate: Basic realm=\"B11902050\"\r\nContent-Type: text/plain\r\nContent-Length: 13\r\n\r\nUnauthorized\n"
#define ERROR404 "HTTP/1.1 404 Not Found\r\nServer: CN2024Server/1.0\r\nContent-Type: text/plain\r\nContent-Length: 10\r\n\r\nNot Found\n"
#define ERROR4051 "HTTP/1.1 405 Method Not Allowed\r\nServer: CN2024Server/1.0\r\nAllow: GET\r\nContent-Length: 0\r\n\r\n"
#define ERROR4050 "HTTP/1.1 405 Method Not Allowed\r\nServer: CN2024Server/1.0\r\nAllow: POST\r\nContent-Length: 0\r\n\r\n"
#define ERROR500 " HTTP/1.1 500 Internal Server Error\r\nServer: CN2024Server/1.0\r\nContent-Length: 0\r\n\r\n"


/*
//use shell script to convert video into dash
//ensure that the server is able to serve other requests while the video is being converted
//run ffmpeg -re -i <VIDEO>.mp4 -c:a aac -c:v libx264 \
-map 0 -b:v:1 6M -s:v:1 1920 x1080 -profile:v:1 high \
-map 0 -b:v:0 144k -s:v:0 256 x144 -profile:v:0 baseline \
-bf 1 -keyint_min 120 -g 120 -sc_threshold 0 -b_strategy 0 \
-ar:a:1 22050 -use_timeline 1 -use_template 1 \
-adaptation_sets "id=0, streams=v id=1, streams=a" -f dash \
<PATH>/dash.mpd
*/
void *convert_video(void *arg) {
    char *video_name = (char *)arg;
    char command[512];
    //open the corresponding directory for the video
    struct stat st = {0};
    if (stat("./web/videos/", &st) == -1) {
        mkdir("./web/videos/", 0700);
    }
    char video_path[256];
    snprintf(video_path, sizeof(video_path), "./web/videos/%s", video_name);
    if (stat(video_path, &st) == -1) {
        mkdir(video_path, 0700);
    }
    snprintf(command, sizeof(command), 
        "ffmpeg -re -i ./web/tmp/%s.mp4 -c:a aac -c:v libx264 "
        "-map 0 -b:v:1 6M -s:v:1 1920x1080 -profile:v:1 high "
        "-map 0 -b:v:0 144k -s:v:0 256x144 -profile:v:0 baseline "
        "-bf 1 -keyint_min 120 -g 120 -sc_threshold 0 -b_strategy 0 "
        "-ar:a:1 22050 -use_timeline 1 -use_template 1 "
        "-adaptation_sets \"id=0,streams=v id=1,streams=a\" -f dash "
        "./web/videos/%s/dash.mpd", 
        video_name, video_name);
    system(command);
    return NULL;
}

char* find_boundary(const char* haystack, size_t haystack_len, const char* needle, size_t needle_len) {
    for (size_t i = 0; i <= haystack_len - needle_len; i++) {
        if (memcmp(haystack + i, needle, needle_len) == 0) {
            return (char*)(haystack + i);
        }
    }
    return NULL;
}


bool parse_data(char *buffer, int buffer_length, int connfd, int video) {
    // Find the boundary
    char *boundary_start = strstr(buffer, "\r\n");
    if (!boundary_start) {
        fprintf(stderr, "[SYS] Failed to find boundary\n");
        return false;
    }
    char boundary[256];
    strncpy(boundary, buffer, boundary_start - buffer);
    boundary[boundary_start - buffer] = '\0';
    fprintf(stderr, "[SYS] Boundary: %s\n", boundary);

    // Find the filename
    char *filename_start = strstr(buffer, "filename=\"");
    if (!filename_start) {
        fprintf(stderr, "[SYS] Failed to find filename\n");
        return false;
    }
    filename_start += strlen("filename=\"");
    char *filename_end = strchr(filename_start, '"');
    char filename[128];
    strncpy(filename, filename_start, filename_end - filename_start);
    filename[filename_end - filename_start] = '\0';
    fprintf(stderr, "[SYS] Filename: %s\n", filename);

    char *file_content_start = strstr(filename_end, "\r\n\r\n");
    if (!file_content_start) {
        fprintf(stderr, "[SYS] Failed to find start of file content\n");
        return false;
    }
    file_content_start += 4; // Move past "\r\n\r\n"

    size_t content_size = buffer + buffer_length - file_content_start;
    char *file_content_end = find_boundary(file_content_start, content_size, boundary, strlen(boundary));
    if (!file_content_end) {
        fprintf(stderr, "[SYS] Failed to find end of file content\n");
        return false;
    }
    file_content_end -= 2; // Move back past "\r\n"

    int file_content_length = file_content_end - file_content_start;

    char file_path[256];
    if (video == 1) {
        snprintf(file_path, sizeof(file_path), "./web/tmp/%s", filename);
    } else {
        snprintf(file_path, sizeof(file_path), "./web/files/%s", filename);
    }
    //if is video and the directory does not exist, create it
    if (video == 1) {
        struct stat st = {0};
        if (stat("./web/tmp", &st) == -1) {
            mkdir("./web/tmp", 0700);
        }
    }
    else {
        struct stat st = {0};
        if (stat("./web/files", &st) == -1) {
            mkdir("./web/files", 0700);
        }
    }
    FILE *file = fopen(file_path, "wb");
    if (!file) {
        send(connfd, ERROR500, strlen(ERROR500), 0);
        fprintf(stderr, "[SYS] File creation failed\n");
        return false;
    }

    fwrite(file_content_start, 1, file_content_length, file);
    fclose(file);
    if (video == 1) {
        pthread_t tid;
        //strip off the .mp4
        filename[strlen(filename) - 4] = '\0';
        pthread_create(&tid, NULL, convert_video, (void *)filename);
        pthread_detach(tid);
    }
    fprintf(stderr, "[SYS] File saved to %s\n", file_path);
    return true;
}


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

    FILE *file = fopen("./secret", "r");
    char line[64];
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\n")] = '\0';  // Remove newline character
        if (strcmp(line, decoded) == 0) {
            fprintf(stderr, "[AUTH] Authenticated for user: %s\n", line);
            fclose(file);
            return true;
        }
    }
    fclose(file);
    return false;
}



bool sendPage(int connfd, char *filename, int dynamic) {
    int file_fd;
    if (dynamic==2){
        //serve player
        char *player = "./web/player.rhtml";
        file_fd = open(player, O_RDONLY);
    }
    else file_fd = open(filename, O_RDONLY);
    if (file_fd < 0) {
        send(connfd, ERROR404, strlen(ERROR404), 0);
        fprintf(stderr, "[SYS] Send 404 due to file not found for %s\n", filename);
        return false;
    } else {
        if (dynamic == 0){
            int read_bytes;
            char buffer[BUFFER_SIZE];
            while ((read_bytes = read(file_fd, buffer, sizeof(buffer))) > 0) {
                int response_length = strlen("HTTP/1.1 200 OK\r\nServer: CN2024Server/1.0\r\n") + strlen("Content-Type: text/html\r\n\r\n") +
                    strlen("Content-Length: ") + snprintf(NULL, 0, "%d", read_bytes) +
                    strlen("\r\n\r\n") + strlen(buffer) + 1;
                char *response = (char *)malloc(response_length);
                snprintf(response, response_length, "HTTP/1.1 200 OK\r\nServer: CN2024Server/1.0\r\nContent-Type: text/html\r\nContent-Length: %d\r\n\r\n%s", read_bytes, buffer);
                send(connfd, response, response_length, 0);
                free(response);
            }
            fprintf(stderr, "[SYS] Send 200 OK for %s\n", filename);
            fprintf(stderr, "[SYS] Send %s for %s\n", filename, filename);

            close(file_fd);
            return true;
        }
        else if (dynamic == 1){
            int video = (strstr(filename, "listv.rhtml") != NULL) ? 1 : 0;
            char file_content[4096];
            int read_bytes = read(file_fd, file_content, sizeof(file_content) - 1);
            if (read_bytes < 0) {
                close(file_fd);
                ERR_EXIT("read()");
            }
            file_content[read_bytes] = '\0';
            close(file_fd);
            DIR *dir = (video == 0) ? opendir("./web/files") : opendir("./web/videos");
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
                    if (video == 0) {
                        snprintf(row, sizeof(row), "<tr><td><a href=\"/api/file/%s\">%s</a></td></tr>\n", encoded_name, entry->d_name);
                    } else {
                        snprintf(row, sizeof(row), "<tr><td><a href=\"/video/%s\">%s</a></td></tr>\n", encoded_name, entry->d_name);
                    }
                    
                    strcat(files_list, row);  // Append the row to the file_list

                    free(encoded_name);  // Free the encoded name
                }
                closedir(dir);

                // Replace the pseudo-HTML tag "<?FILE_LIST?>" with the actual list of files
                char *placeholder = (video == 0) ? strstr(file_content, "<?FILE_LIST?>") : strstr(file_content, "<?VIDEO_LIST?>");
                if (placeholder) {
                    char final_content[8192];
                    strncpy(final_content, file_content, placeholder - file_content);
                    final_content[placeholder - file_content] = '\0';

                    strcat(final_content, files_list);
                    int tmp = (video == 0) ? strlen("<?FILE_LIST?>") : strlen("<?VIDEO_LIST?>");
                    strcat(final_content, placeholder + tmp);
                    //fprintf(stderr, "[SYS] Send listf.rhtml: %s\n", final_content);

                    int response_length = strlen("HTTP/1.1 200 OK\r\nServer: CN2024Server/1.0\r\n") + strlen("Content-Type: text/html\r\n\r\n") +
                            strlen("Content-Length: ") + snprintf(NULL, 0, "%ld", strlen(final_content)) + strlen("\r\n\r\n") + strlen(final_content) + 1;
                    char *response = (char *)malloc(response_length);
                    snprintf(response, response_length, "HTTP/1.1 200 OK\r\nServer: CN2024Server/1.0\r\nContent-Type: text/html\r\nContent-Length: %ld\r\n\r\n%s", strlen(final_content), final_content);
                    send(connfd, response, response_length, 0);
                    free(response);
                    fprintf(stderr, "[SYS] Send 200 OK for following file: %s\n", filename);


                }
                return true;
            }
        }
        else if (dynamic == 2) {
            // Step 1: Read the content of player.rhtml
            char file_content[4096];
            ssize_t read_bytes = read(file_fd, file_content, sizeof(file_content) - 1);
            if (read_bytes < 0) {
                close(file_fd);
                ERR_EXIT("read()");
            }
            file_content[read_bytes] = '\0'; // Null-terminate the string
            close(file_fd);

            // Step 2: Replace placeholders
            char final_content[8192];
            memset(final_content, 0, sizeof(final_content));

            // Find and replace <?VIDEO_NAME?>
            char *video_placeholder = strstr(file_content, "<?VIDEO_NAME?>");
            if (!video_placeholder) {
                fprintf(stderr, "[SYS] Placeholder <?VIDEO_NAME?> not found in player.rhtml\n");
                send(connfd, ERROR404, strlen(ERROR404), 0);
                return false;
            }

            // Calculate the lengths
            size_t prefix_len = video_placeholder - file_content;
            size_t video_name_len = strlen(filename);
            size_t mpd_path_len;

            size_t mpd_url_size = strlen("/api/video/") + strlen(filename) + strlen("/dash.mpd") + 3; // 2 quotes + null terminator
            char *mpd_path = (char *)malloc(mpd_url_size);
            if (!mpd_path) {
                perror("malloc");
                return false;
            }
            snprintf(mpd_path, mpd_url_size, "\"/api/video/%s/dash.mpd\"", filename); // Enclose in quotes

            // Find and replace <?MPD_PATH?>
            char *mpd_placeholder = strstr(file_content, "<?MPD_PATH?>");
            if (!mpd_placeholder) {
                fprintf(stderr, "[SYS] Placeholder <?MPD_PATH?> not found in player.rhtml\n");
                free(mpd_path);
                send(connfd, ERROR404, strlen(ERROR404), 0);
                return false;
            }

            // Calculate the lengths
            size_t prefix_len_final = mpd_placeholder - file_content;
            size_t mpd_url_encoded_len = strlen(mpd_path); // Assuming no special encoding needed

            // Start constructing final_content
            // Copy content before <?VIDEO_NAME?>
            strncpy(final_content, file_content, prefix_len);
            final_content[prefix_len] = '\0';

            // Append video name
            strcat(final_content, filename);

            // Append content after <?VIDEO_NAME?> up to <?MPD_PATH?>
            char *after_video = video_placeholder + strlen("<?VIDEO_NAME?>");
            size_t between_len = mpd_placeholder - after_video;
            strncat(final_content, after_video, between_len);
            final_content[strlen(final_content) + between_len] = '\0';

            // Append MPD path with quotes
            strcat(final_content, mpd_path);
            free(mpd_path);

            // Append the rest of the content after <?MPD_PATH?>
            char *after_mpd = mpd_placeholder + strlen("<?MPD_PATH?>");
            strcat(final_content, after_mpd);

            // Step 3: Construct HTTP response headers
            // Determine Content-Length
            size_t final_content_length = strlen(final_content);

            // Calculate the total response length
            size_t response_length = strlen("HTTP/1.1 200 OK\r\nServer: CN2024Server/1.0\r\nContent-Type: text/html\r\nContent-Length: ") +
                                     20 + // Placeholder for Content-Length digits
                                     strlen("\r\n\r\n") +
                                     final_content_length +
                                     1; // Null terminator

            // Allocate memory for the response
            char *response = (char *)malloc(response_length);
            if (!response) {
                perror("malloc");
                send(connfd, ERROR404, strlen(ERROR404), 0);
                return false;
            }

            // Construct the response
            snprintf(response, response_length, 
                     "HTTP/1.1 200 OK\r\n"
                     "Server: CN2024Server/1.0\r\n"
                     "Content-Type: text/html\r\n"
                     "Content-Length: %zu\r\n\r\n%s",
                     final_content_length, final_content);

            // Send the response
            ssize_t sent_bytes = send(connfd, response, strlen(response), 0);
            if (sent_bytes < 0) {
                perror("send");
                free(response);
                return false;
            }

            free(response);
            fprintf(stderr, "[SYS] Send 200 OK for player page: %s\n", filename);
            return true;
        }
    }
    return false;
}

bool downloader(char *buffer, int connfd, int video) {
    //get the filename
    
    char *filename_start = (video == 0) ? strstr(buffer, "/api/file/") + strlen("/api/file/") : strstr(buffer, "/api/video/") + strlen("/api/video/");
    char *filename_end = strchr(filename_start, ' ');
    char tmpfilename[128];
    strncpy(tmpfilename, filename_start, filename_end - filename_start);
    tmpfilename[filename_end - filename_start] = '\0';
    char *filename = url_decode(tmpfilename);
    fprintf(stderr, "[SYS] Requested file: %s\n", filename);
    //open the file
    char file_path[256];
    if (video == 1) {
        snprintf(file_path, sizeof(file_path), "./web/videos/%s", filename);
    } else {
        snprintf(file_path, sizeof(file_path), "./web/files/%s", filename);
    }
    
    int file_fd = open(file_path, O_RDONLY);
    if (file_fd < 0) {
        send(connfd, ERROR404, strlen(ERROR404), 0);
        fprintf(stderr, "[SYS] Send 404 due to file not found for /api/file or video/%s\n", filename);
        return false;
    } else {
        // Determine the file size using stat
        struct stat file_stat;
        if (fstat(file_fd, &file_stat) < 0) {
            send(connfd, ERROR500, strlen(ERROR500), 0);
            close(file_fd);
            fprintf(stderr, "[SYS] Send 500 Internal Server Error due to stat failure\n");
            return false;
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
        snprintf(header, sizeof(header), "HTTP/1.1 200 OK\r\nServer: CN2024Server/1.0\r\nContent-Type: %s\r\nContent-Length: %d\r\n\r\n", content_type, file_size);
        send(connfd, header, strlen(header), 0);
        fprintf(stderr, "[SYS] Send 200 OK for /api/file/%s\n", filename);
        // Now send the file content in chunks
        int read_bytes;
        char buffer[BUFFER_SIZE];
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
        return true;
    }
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

    printf("Server listening on port %d...\n", port);

    
    poll_fds[0].fd = listenfd;
    poll_fds[0].events = POLLIN;  
    int flag = fcntl(listenfd, F_GETFL, 0);
    fcntl(listenfd, F_SETFL, flag | O_NONBLOCK);

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

            fprintf(stderr, "[CON] New connection: fd %d\n", connfd);

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
                char buffer[BUFFER_SIZE];
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
                    fprintf(stderr, "[CON] Checked and found connfd %d request is keep-alive: %d\n", poll_fds[i].fd, keep_alive);
                    
                    if (bytes_received < 0) {
                        ERR_EXIT("recv()");
                    }
                    //printf("Received request at connfd %d\n", connfd);
                    //fprintf(stderr, "Received request: %s\n", buffer);

                    buffer[bytes_received] = '\0';
                    char *auth_header = strstr(buffer, "Authorization: ");
                    //fprintf(stderr, "Received request: %s\n", buffer);
                    //fprintf(stderr, "Auth header IS : %s\n", auth_header);
                
                    if (!auth_header || !authenticate(auth_header)) auth = false;
                    else auth = true;
                
                    char method[16], url[256];
                    sscanf(buffer, "%s %s", method, url);
                    fprintf(stderr, "[SYS] URL: .%s.\n", url);
                    fprintf(stderr, "[SYS] Method: .%s.\n", method);
                
                    // Check if the request is for "/"
                    if (strcmp(url, "/") == 0) {
                        if (strcmp(method, "GET") != 0) {
                            send(connfd, ERROR4051, strlen(ERROR4051), 0);
                            fprintf(stderr, "[SYS] Send 405 Method Not Allowed for /, it should be GET\n");
                            continue;
                        }
                        sendPage(connfd, "./web/index.html", 0);
                    } 
                    else if (strstr(url, "/api") != NULL) {
                        if (strstr(url, "/api/file") != NULL) {
                            if (strstr(url, "/api/file/") == NULL) {
                                //upload file
                                if (strstr(method, "POST") == NULL || strstr(buffer, "Content-Type: multipart/form-data") == NULL) {
                                    send(connfd, ERROR4050, strlen(ERROR4050), 0);
                                    fprintf(stderr, "[SYS] Send 405 Method Not Allowed for /api/file, it should be POST\n");
                                    continue;
                                }
                                if (auth == false) {
                                    send(connfd, ERROR401, strlen(ERROR401), 0);
                                    fprintf(stderr, "[SYS] Send 401 Unauthorized for /api/file\n");
                                    continue;
                                }
                                fprintf(stderr, "[SYS] Uploading\n");
                                int content_length = 0;
                                sscanf(strstr(buffer, "Content-Length: ") + strlen("Content-Length: "), "%d", &content_length);
                                fprintf(stderr, "[SYS] Content-Length: %d\n", content_length);
                                //find the boundary
                                char boundary[256];
                                char *boundary_start = strstr(buffer, "boundary=") + strlen("boundary=");
                                char *boundary_end = strchr(boundary_start, '\r');
                                strncpy(boundary, boundary_start, boundary_end - boundary_start);
                                boundary[boundary_end - boundary_start] = '\0';
                                fprintf(stderr, "[SYS] Boundary: %s\n", boundary);
                                //malloc a binary buffer to store the whole request
                                char *request = (char *)malloc(content_length + 1);
                                
                                fprintf(stderr, "[SYS] Found start of file content\n");
                                
                                
                                int total_received = 0;
                                while (total_received < content_length) {
                                    int bytes_to_read = content_length - total_received;
                                    int received = recv(connfd, request + total_received, bytes_to_read, 0);
                                    if (received <= 0) {
                                        perror("recv failed");
                                        free(request);
                                        return false;
                                    }
                                    total_received += received;
                                }
                            
                                fprintf(stderr, "[SYS] Parsing request buffer\n");
                                fprintf(stderr, "[PACKET] DATA-----\n %s\n-----\n", request);
                                if (parse_data(request, content_length, connfd, 0)) {
                                    fprintf(stderr, "[SYS] File uploaded successfully\n");
                                    //response 200 OK
                                    char response[79] = "HTTP/1.1 200 OK\r\nServer: CN2024Server/1.0\r\nContent-Length: 14\r\n\r\nFile uploaded\n";
                                    send(connfd, response, strlen(response), 0);
                                } else {
                                    fprintf(stderr, "[SYS] 500 File upload failed\n");
                                    send(connfd, ERROR500, strlen(ERROR500), 0);
                                }
                                free(request);
                                
                            } 
                            else {
                                //provide file
                                if (strstr(method, "GET") == NULL) {
                                    send(connfd, ERROR4051, strlen(ERROR4051), 0);
                                    fprintf(stderr, "[SYS] Send 405 Method Not Allowed for /api/file/<filename>, it should be GET\n");
                                    continue;
                                }
                                downloader(buffer, connfd, 0);
                            }
                            
                        } else if (strstr(url, "/api/video") != NULL) {
                            if (strstr(url, "/api/video/") == NULL) {
                                //upload file
                                if (strstr(method, "POST") == NULL || strstr(buffer, "Content-Type: multipart/form-data") == NULL) {
                                    send(connfd, ERROR4050, strlen(ERROR4050), 0);
                                    fprintf(stderr, "[SYS] Send 405 Method Not Allowed for /api/video, it should be POST\n");
                                    continue;
                                }
                                if (auth == false) {
                                    send(connfd, ERROR401, strlen(ERROR401), 0);
                                    fprintf(stderr, "[SYS] Send 401 Unauthorized for /api/video\n");
                                    continue;
                                }
                                int content_length = 0;
                                sscanf(strstr(buffer, "Content-Length: ") + strlen("Content-Length: "), "%d", &content_length);
                                fprintf(stderr, "[SYS] Content-Length: %d\n", content_length);
                                //find the boundary
                                char boundary[256];
                                char *boundary_start = strstr(buffer, "boundary=") + strlen("boundary=");
                                char *boundary_end = strchr(boundary_start, '\r');
                                strncpy(boundary, boundary_start, boundary_end - boundary_start);
                                boundary[boundary_end - boundary_start] = '\0';
                                fprintf(stderr, "[SYS] Boundary: %s\n", boundary);
                                //malloc a binary buffer to store the whole request
                                char *request = (char *)malloc(content_length + 1);
                                
                                int total_received = 0;
                                //find the start of the file content
                                char *file_content_start = strstr(buffer, "\r\n\r\n");
                                fprintf(stderr, "[SYS] File content start is located\n");
                                fprintf(stderr, "[SYS] File content start: %s\n", file_content_start);
                                if (file_content_start) {
                                    file_content_start += 4; // Move past "\r\n\r\n"
                                    int file_content_length = bytes_received - (file_content_start - buffer);
                                    fprintf(stderr, "[SYS] File content length: %d\n", file_content_length);
                                    memcpy(request, file_content_start, file_content_length);
                                    total_received += file_content_length;
                                }
                                
                                
                                while (total_received < content_length) {
                                    int bytes_to_read = content_length - total_received;
                                    int received = recv(connfd, request + total_received, bytes_to_read, 0);
                                    if (received <= 0) {
                                        perror("recv failed");
                                        free(request);
                                        return false;
                                    }
                                    total_received += received;
                                }
                            
                                fprintf(stderr, "[SYS] Parsing request buffer\n");
                                fprintf(stderr, "[PACKET] DATA-----\n %s\n-----\n", request);
                                if (parse_data(request, content_length, connfd, 1)) {
                                    fprintf(stderr, "[SYS] File uploaded successfully\n");
                                    //response 200 OK
                                    char response[79] = "HTTP/1.1 200 OK\r\nServer: CN2024Server/1.0\r\nContent-Length: 14\r\n\r\nFile uploaded\n";
                                    send(connfd, response, strlen(response), 0);
                                } else {
                                    fprintf(stderr, "[SYS] 500 File upload failed\n");
                                    send(connfd, ERROR500, strlen(ERROR500), 0);
                                }
                                free(request);
                                
                            } 
                            else {
                                //provide file
                                if (strstr(method, "GET") == NULL) {
                                    send(connfd, ERROR4051, strlen(ERROR4051), 0);
                                    fprintf(stderr, "[SYS] Send 405 Method Not Allowed for /api/video/<filename>, it should be GET\n");
                                    continue;
                                }
                                downloader(buffer, connfd, 1);
                            }
                        } else {
                            send(connfd, ERROR404, strlen(ERROR404), 0);
                            fprintf(stderr, "[SYS] Send 404 due to file not found for %s\n", url);
                        }
                    } 
                    else if (strstr(url, "/upload/") != NULL) {
                        if (strcmp(method, "GET") != 0) {
                            send(connfd, ERROR4051, strlen(ERROR4051), 0);
                            fprintf(stderr, "[SYS] Send 405 Method Not Allowed for /upload/file, it should be GET\n");
                            continue;
                        }
                        if (auth == false) {
                            send(connfd, ERROR401, strlen(ERROR401), 0);
                            fprintf(stderr, "[SYS] Send 401 Unauthorized for /upload/file\n");
                            continue;
                        }
                        if (strstr(url, "video") != NULL){
                            sendPage(connfd, "./web/uploadv.html", 0);
                        }
                        else if (strstr(url, "file") != NULL){
                            sendPage(connfd, "./web/uploadf.html", 0);
                        }
                        else {
                            send(connfd, ERROR404, strlen(ERROR404), 0);
                            fprintf(stderr, "[SYS] Send 404 due to file not found for /upload/\n");
                        }
                    }
                    else if (strcmp(url, "/file/") == 0){
                        if (strcmp(method, "GET") != 0){
                            send(connfd, ERROR4051, strlen(ERROR4051), 0);
                            fprintf(stderr, "[SYS] Send 405 Method Not Allowed for /file/, it should be GET\n");
                            continue;
                        }
                        sendPage(connfd, "./web/listf.rhtml", 1);
                    }
                    else if (strstr(url, "/video/") != NULL) {
                        if (strcmp(url, "/video/") == 0){
                            if (strcmp(method, "GET") != 0){
                                send(connfd, ERROR4051, strlen(ERROR4051), 0);
                                fprintf(stderr, "[SYS] Send 405 Method Not Allowed for /video/, it should be GET\n");
                                continue;
                            }
                            sendPage(connfd, "./web/listv.rhtml", 1);
                        }
                        else {
                            if (strcmp(method, "GET") != 0) {
                                send(connfd, ERROR4051, strlen(ERROR4051), 0);
                                fprintf(stderr, "[SYS] Send 405 Method Not Allowed for /video/<filename>, it should be GET\n");
                                continue;
                            }
                            fprintf(stderr, "[SYS] Requested video: %s\n", url);
                            //parse the url
                            char *filename_start = strstr(url, "/video/") + strlen("/video/");
                            fprintf(stderr, "[SYS] Filename: %s\n", filename_start);
                            
                            char tmpfilename[128];
                            strncpy(tmpfilename, filename_start, strlen(filename_start));
                            tmpfilename[strlen(filename_start)] = '\0';
                            char *filename = url_decode(tmpfilename);
                            fprintf(stderr, "[SYS] Requested video: %s\n", filename);
                            sendPage(connfd, filename, 2);
                        }
                    }
                    else {
                        //pass if favicon.ico
                        if (strcmp(url, "/favicon.ico") == 0) {
                            continue;
                        }
                        send(connfd, ERROR404, strlen(ERROR404), 0);
                        fprintf(stderr, "[SYS] Send 404 Not Found for unknown request\n");
                        fprintf(stderr, "[SYS] Unknown request: %s\n", url);
                    }

                    if (!keep_alive) {
                        fprintf(stderr, "[CON] Closing connection: fd %d due to Connection: close\n", poll_fds[i].fd);
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