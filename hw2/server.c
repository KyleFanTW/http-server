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
#include <sys/wait.h>
#include <errno.h>


#define MAX_CONNECTIONS 140
#define ERR_EXIT(a) { perror(a); exit(1); }
#define BUFFER_SIZE 16384


#define ERROR401 "HTTP/1.1 401 Unauthorized\r\nServer: CN2024Server/1.0\r\nWWW-Authenticate: Basic realm=\"B11902050\"\r\nContent-Type: text/plain\r\nContent-Length: 13\r\n\r\nUnauthorized\n"
#define ERROR404 "HTTP/1.1 404 Not Found\r\nServer: CN2024Server/1.0\r\nContent-Type: text/plain\r\nContent-Length: 10\r\n\r\nNot Found\n"
#define ERROR4051 "HTTP/1.1 405 Method Not Allowed\r\nServer: CN2024Server/1.0\r\nAllow: GET\r\nContent-Length: 0\r\n\r\n"
#define ERROR4050 "HTTP/1.1 405 Method Not Allowed\r\nServer: CN2024Server/1.0\r\nAllow: POST\r\nContent-Length: 0\r\n\r\n"
#define ERROR500 "HTTP/1.1 500 Internal Server Error\r\nServer: CN2024Server/1.0\r\nContent-Length: 0\r\n\r\n"



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
    fprintf(stderr, "[CONVERTER] Converting video: %s\n", video_name);
    struct stat st = {0};
    if (stat("./web/videos/", &st) == -1) {
        mkdir("./web/videos/", 0700);
    }
    char video_path[256];
    snprintf(video_path, sizeof(video_path), "./web/videos/%s", video_name);
    //remove the directory if it exists
    if (stat(video_path, &st) == 0) {
        char rm_command[264];
        snprintf(rm_command, sizeof(rm_command), "rm -rf %s", video_path);
        system(rm_command);
    }
    if (stat(video_path, &st) == -1) {
        mkdir(video_path, 0700);
    }
    snprintf(command, sizeof(command), 
        "ffmpeg -re -i \"./web/tmp/%s.mp4\" -c:a aac -c:v libx264 "
        "-map 0 -b:v:1 6M -s:v:1 1920x1080 -profile:v:1 high "
        "-map 0 -b:v:0 144k -s:v:0 256x144 -profile:v:0 baseline "
        "-bf 1 -keyint_min 120 -g 120 -sc_threshold 0 -b_strategy 0 "
        "-ar:a:1 22050 -use_timeline 1 -use_template 1 "
        "-adaptation_sets \"id=0,streams=v id=1,streams=a\" -f dash "
        "\"./web/videos/%s/dash.mpd\"", 
        video_name, video_name);
    fprintf(stderr, "[CONVERTER] Running command: %s\n", command);
    system(command);
    fprintf(stderr, "[CONVERTER] Video converted: %s\n", video_name);
    free(video_name);
    return NULL;
}

char* find_boundary(const char* haystack, size_t haystack_len, const char* needle, size_t needle_len) {
    fprintf(stderr, "[UTIL] Finding boundary\n");
    if (haystack_len < needle_len) {
        fprintf(stderr, "[UTIL] Error: haystack_len %ld < needle_len %ld\n", haystack_len, needle_len);
        return NULL;
    }
    for (size_t i = 0; i <= haystack_len - needle_len; i++) {
        if (memcmp(haystack + i, needle, needle_len) == 0) {
            return (char*)(haystack + i);
        }
    }
    return NULL;
}

bool parseData(char *buffer, int buffer_length, int connfd, int video) {
    // Find the boundary
    //fprintf(stderr, "[PARSER] Parsing: %s\n", buffer);
    char *boundary_start = strstr(buffer, "\r\n");
    if (!boundary_start) {
        fprintf(stderr, "[PARSER] Failed to find boundary\n");
        return false;
    }
    char boundary[128];
    strncpy(boundary, buffer, boundary_start - buffer);
    boundary[boundary_start - buffer] = '\0';
    fprintf(stderr, "[PARSER] Boundary: %s\n", boundary);
    char boundary_end[144];
    snprintf(boundary_end, sizeof(boundary_end), "\r\n%s", boundary);


    // Find the filename
    char *filename_start = strstr(buffer, "filename=\"");
    if (!filename_start) {
        fprintf(stderr, "[PARSER] Failed to find filename\n");
        return false;
    }
    filename_start += strlen("filename=\"");
    char *filename_end = strchr(filename_start, '"');
    char filename[128];
    strncpy(filename, filename_start, filename_end - filename_start);
    filename[filename_end - filename_start] = '\0';
    fprintf(stderr, "[PARSER] Filename: %s\n", filename);

    char *file_content_start = strstr(filename_end, "\r\n\r\n");
    if (!file_content_start) {
        fprintf(stderr, "[PARSER] Failed to find start of file content\n");
        return false;
    }
    file_content_start += 4; // Move past "\r\n\r\n"

    char *file_content_end = find_boundary(buffer, buffer_length, boundary_end, strlen(boundary_end));
    if (!file_content_end) {
        fprintf(stderr, "[PARSER] Failed to find end of file content\n");
        return false;
    }
    
    fprintf(stderr, "[PARSER] Found end of file content\n");
    fprintf(stderr, "[PARSER] File content size: %ld\n", file_content_end - file_content_start);


    char file_path[256];
    if (video == 1) {
        snprintf(file_path, sizeof(file_path), "./web/tmp/%s", filename);
    } else {
        snprintf(file_path, sizeof(file_path), "./web/files/%s", filename);
    }
    fprintf(stderr, "[PARSER] File path: %s\n", file_path);
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
    fprintf(stderr, "[PARSER] Ensure directory exists\n");
    FILE *file = fopen(file_path, "wb");
    if (!file) {
        send(connfd, ERROR500, strlen(ERROR500), 0);
        fprintf(stderr, "[PARSER] File creation failed\n");
        return false;
    }
    fprintf(stderr, "[PARSER] File created\n");

    fwrite(file_content_start, 1, file_content_end - file_content_start, file);
    fclose(file);
    fprintf(stderr, "[PARSER] File written\n");
    if (video == 1) {
        pthread_t tid;
        if (strstr(filename, ".mp4") != NULL) {
            filename[strlen(filename) - 4] = '\0';
        }
        char *filename_copy = (char *)malloc(strlen(filename) + 1);
        strcpy(filename_copy, filename);
        pthread_create(&tid, NULL, convert_video, (void *)filename_copy);
        pthread_detach(tid);
    }
    fprintf(stderr, "[PARSER] File saved to %s\n", file_path);
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



bool authenticate(char *encoded){
    //check correct base64 encoding
    fprintf(stderr, "[AUTH] Authenticating for %s\n", encoded);
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
    if (!file) {
        // send 500
        fprintf(stderr, "[AUTH] Failed to open secret file\n");
        return false;
    }
    fprintf(stderr, "[AUTH] Opened secret file\n");
    char line[64];
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\n")] = '\0';  // Remove newline character
        fprintf(stderr, "[AUTH] Comparing %s with %s\n", line, decoded);
        if (strcmp(line, decoded) == 0) {
            fprintf(stderr, "[AUTH] Authenticated for user: %s\n", line);
            fclose(file);
            free(decoded);
            return true;
        }
    }
    free(decoded);
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
        fprintf(stderr, "[PAGE] Send 404 due to file not found for %s\n", filename);
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
                memset(response, 0, response_length);
                snprintf(response, response_length, "HTTP/1.1 200 OK\r\nServer: CN2024Server/1.0\r\nContent-Type: text/html\r\nContent-Length: %d\r\n\r\n%s", read_bytes, buffer);
                send(connfd, response, response_length, 0);
                free(response);
            }
            fprintf(stderr, "[PAGE] Send 200 OK for %s\n", filename);
            fprintf(stderr, "[PAGE] Send %s for %s\n", filename, filename);

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
            //CHECK THIS
            if (video == 1) {
                struct stat st = {0};
                if (stat("./web/videos", &st) == -1) {
                    mkdir("./web/videos", 0700);
                }
            }
            else {
                struct stat st = {0};
                if (stat("./web/files", &st) == -1) {
                    mkdir("./web/files", 0700);
                }
            }
            DIR *dir = (video == 0) ? opendir("./web/files") : opendir("./web/videos");
            if (!dir) {
                send(connfd, ERROR404, strlen(ERROR404), 0);
                fprintf(stderr, "[PAGE] Send 404 due to directory not found for %s\n", filename);
            } else {
                struct dirent *entry;
                char files_list[4096] = "";
                while ((entry = readdir(dir)) != NULL) {
                    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
                    char *encoded_name = url_encode(entry->d_name);  // Encode the file name
                    char row[324];
                    if (video == 0) snprintf(row, sizeof(row), "<tr><td><a href=\"/api/file/%s\">%s</a></td></tr>\n", encoded_name, entry->d_name);
                    else snprintf(row, sizeof(row), "<tr><td><a href=\"/video/%s\">%s</a></td></tr>\n", encoded_name, entry->d_name);
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
                    //fprintf(stderr, "[PAGE] Send listf.rhtml: %s\n", final_content);

                    int response_length = strlen("HTTP/1.1 200 OK\r\nServer: CN2024Server/1.0\r\n") + strlen("Content-Type: text/html\r\n\r\n") +
                            strlen("Content-Length: ") + snprintf(NULL, 0, "%ld", strlen(final_content)) + strlen("\r\n\r\n") + strlen(final_content) + 1;
                    char *response = (char *)malloc(response_length);
                    snprintf(response, response_length, "HTTP/1.1 200 OK\r\nServer: CN2024Server/1.0\r\nContent-Type: text/html\r\nContent-Length: %ld\r\n\r\n%s", strlen(final_content), final_content);
                    send(connfd, response, response_length, 0);
                    free(response);
                    fprintf(stderr, "[PAGE] Send 200 OK for following file: %s\n", filename);
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
                fprintf(stderr, "[PLAYER] Placeholder <?VIDEO_NAME?> not found in player.rhtml\n");
                send(connfd, ERROR404, strlen(ERROR404), 0);
                return false;
            }
            // Calculate the lengths
            size_t prefix_len = video_placeholder - file_content;

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
                fprintf(stderr, "[PLAYER] Placeholder <?MPD_PATH?> not found in player.rhtml\n");
                free(mpd_path);
                send(connfd, ERROR404, strlen(ERROR404), 0);
                return false;
            }

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
            fprintf(stderr, "[PLAYER] Send 200 OK for player page: %s\n", filename);
            return true;
        }
    }
    return false;
}

bool downloader(char *url, int connfd, int video) {
    //get the filename
    
    char *tmpfilename = (video == 0) ? strstr(url, "/api/file/") + strlen("/api/file/") : strstr(url, "/api/video/") + strlen("/api/video/");
    // parse the filename from the URL

    char *filename = url_decode(tmpfilename);
    fprintf(stderr, "[DL] Requested file: .%s.\n", filename);
    //open the file
    char file_path[256];
    // sanitize the filename, if it contains ".." or the filename is empty, return 404
    if ((strstr(filename, "..") != NULL) || (strlen(filename) == 0)) {
        send(connfd, ERROR404, strlen(ERROR404), 0);
        fprintf(stderr, "[DL] Send 404 due to file not found for /api/file or video/%s\n", filename);
        return false;
    }
    if (video == 1) {
        snprintf(file_path, sizeof(file_path), "./web/videos/%s", filename);
    } else {
        snprintf(file_path, sizeof(file_path), "./web/files/%s", filename);
    }
    
    int file_fd = open(file_path, O_RDONLY);
    if (file_fd < 0) {
        send(connfd, ERROR404, strlen(ERROR404), 0);
        fprintf(stderr, "[DL] Send 404 due to file not found for /api/file or video/%s\n", filename);
        return false;
    } else {
        // Determine the file size using stat
        fprintf(stderr, "[DL] File found: %s\n", filename);
        struct stat file_stat;
        if (fstat(file_fd, &file_stat) < 0) {
            send(connfd, ERROR500, strlen(ERROR500), 0);
            close(file_fd);
            fprintf(stderr, "[DL] Send 500 Internal Server Error due to stat failure\n");
            return false;
        }
        int file_size = file_stat.st_size;
        fprintf(stderr, "[DL] File size: %d\n", file_size);
        char *extension = strrchr(filename, '.');
        char *content_type;
        if (extension == NULL) {
            content_type = "text/plain";
        } else{
            if (strcmp(extension, ".html") == 0 || strcmp(extension, ".rhtml") == 0) content_type = "text/html";
            else if (strcmp(extension, ".mp4") == 0 || strcmp(extension, ".m4v") == 0) content_type = "video/mp4";
            else if (strcmp(extension, ".m4s") == 0) content_type = "video/iso.segment";
            else if (strcmp(extension, ".m4a") == 0) content_type = "audio/mp4";
            else if (strcmp(extension, ".mpd") == 0) content_type = "application/dash+xml";
            else content_type = "text/plain";
        }
        
        // Prepare and send the HTTP header once
        char header[1024];
        snprintf(header, sizeof(header), "HTTP/1.1 200 OK\r\nServer: CN2024Server/1.0\r\nContent-Type: %s\r\nContent-Length: %d\r\n\r\n", content_type, file_size);
        send(connfd, header, strlen(header), 0);
        fprintf(stderr, "[DL] Send 200 OK for /api/file/%s\n", filename);
        // Now send the file content in chunks
        int read_bytes;
        char buffer[BUFFER_SIZE];
        while ((read_bytes = read(file_fd, buffer, sizeof(buffer))) > 0) {
            int sent_bytes = send(connfd, buffer, read_bytes, MSG_NOSIGNAL); // Use MSG_NOSIGNAL
            if (sent_bytes < 0) {
                fprintf(stderr, "[DL] Send failed for /api/file/%s\n", filename);
                break;
            }
        }
        fprintf(stderr, "[DL] Sent file %s for /api/file/%s\n", filename, filename);
        free(filename);
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

    //Check existence of web directory
    struct stat st = {0};
    if (stat("./web", &st) == -1) {
        mkdir("./web", 0700);
    }
    if (stat("./web/files", &st) == -1) {
        mkdir("./web/files", 0700);
    }
    if (stat("./web/videos", &st) == -1) {
        mkdir("./web/videos", 0700);
    }
    if (stat("./web/tmp", &st) == -1) {
        mkdir("./web/tmp", 0700);
    }


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

            fprintf(stderr, "[MAIN - CON] New connection: fd %d\n", connfd);

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
                    fprintf(stderr, "[MAIN - CON] Client disconnected or error occurred on connfd %d\n", poll_fds[i].fd);
                    close(poll_fds[i].fd);
                    poll_fds[i] = poll_fds[nfds - 1];  // Remove from poll set
                    nfds--;
                    i--;  // Process the new entry at index i
                    continue;
                }

                buffer[bytes_received] = '\0';
                fprintf(stderr, "[MAIN - REQ] Received request: %s\n", buffer);

                // for each line, parse the headers with a loop
                char auth_prefix[] = "Authorization: Basic ";
                char typeprefix[] = "Content-Type: ";
                char lengthprefix[] = "Content-Length: ";
                char boundaryprefix[] = "boundary=";
                char conprefix[] = "Connection: ";


                char encoded[256];
                char type[64] = "";
                int content_length = 0;
                char boundary[256];

                char method[16], url[256];
                char *request;
                sscanf(buffer, "%s %s", method, url);

                fprintf(stderr, "[MAIN - REQ PARSE] URL: .%s.\n", url);
                fprintf(stderr, "[MAIN - REQ PARSE] Method: .%s.\n", method);

                //divide the request by \r\n\r\n
                //copy the first part of the buffer to a new buffer, header
                char *header;
                char *header_end = strstr(buffer, "\r\n\r\n");
                if (header_end) {
                    // Calculate the length of the headers
                    size_t header_length = header_end - buffer + 4; // +4 to include the "\r\n\r\n"
                
                    // Allocate memory for the header buffer
                    header = (char *)malloc(header_length + 1); // +1 for null terminator
                    
                    if (header) {
                        // Copy the headers to the new buffer
                        strncpy(header, buffer, header_length);
                        header[header_length] = '\0'; // Null-terminate the header buffer
                
                        fprintf(stderr, "Headers:\n%s\n", header);
                    } else {
                        fprintf(stderr, "Failed to allocate memory for headers\n");
                    }
                } else {
                    fprintf(stderr, "Failed to find end of headers\n");
                }
                

                char *line = strtok(header, "\r\n");

                while (line != NULL) {
                    // if \r\n\r\n is found, break
                    if (strstr(line, "\r\n\r\n") != NULL) {
                        break;
                    }
                    if (strncasecmp(line, auth_prefix, strlen(auth_prefix)) == 0) {
                        sscanf(line + strlen(auth_prefix), "%255s", encoded);
                        auth = authenticate(encoded);
                        fprintf(stderr, "[MAIN - AUTH] Authentication %s\n", auth ? "succeeded" : "failed");
                    }
                    // Parse Content-Type header
                    else if (strncasecmp(line, typeprefix, strlen(typeprefix)) == 0) {
                        sscanf(line + strlen(typeprefix), "%63s", type);
                        // strip off the trailing ;
                        char *semicolon = strchr(type, ';');
                        if (semicolon) {
                            *semicolon = '\0';
                        }
                        fprintf(stderr, "[MAIN - REQ PARSE] Content-Type: %s\n", type);
                        if (strcmp(type, "multipart/form-data") == 0) {
                            char *boundary_start = strstr(line, boundaryprefix);
                            if (!boundary_start) {
                                fprintf(stderr, "[MAIN - REQ PARSE] Failed to find boundary\n");
                                continue;
                            }
                            strncpy(boundary, boundary_start + strlen(boundaryprefix), sizeof(boundary));
                            boundary[strcspn(boundary, "\r\n")] = '\0';
                            fprintf(stderr, "[MAIN - REQ PARSE] Boundary: %s\n", boundary);
                        }

                    }
                    else if (strncasecmp(line, lengthprefix, strlen(lengthprefix)) == 0) {
                        sscanf(line + strlen(lengthprefix), "%d", &content_length);
                        fprintf(stderr, "[MAIN - REQ PARSE] Content-Length: %d\n", content_length);
                    }
                    else if (strncasecmp(line, conprefix, strlen(conprefix)) == 0) {
                        const char *connection_header = line + strlen(conprefix);
                        if (strncasecmp(connection_header, "keep-alive", 10) == 0) keep_alive = true;
                        if (strncasecmp(connection_header, "close", 5) == 0) keep_alive = false;
                        fprintf(stderr, "[MAIN - CON] Checked and found connfd %d request is keep-alive: %d\n", poll_fds[i].fd, keep_alive);
                    }
                    else {
                        fprintf(stderr, "[MAIN - REQ PARSE] Ignoring header: %s\n", line);
                    }

                    line = strtok(NULL, "\r\n");
                }

                
                    
                    
                    
                if (strcmp(type, "multipart/form-data") != 0) {
                    fprintf(stderr, "[MAIN - Full Req] No file upload\n");
                }
                else {
                    fprintf(stderr, "[MAIN - Full Req] Content-Length: %d\n", content_length);
                    fprintf(stderr, "[MAIN - Full Req] Boundary: %s\n", boundary);
                        //malloc a binary buffer to store the whole request

                    int total_received = 0;
                    request = (char *)malloc(content_length + 10);
                    //copy the rest of the buffer ie after the first \r\n\r\n to the request buffer, if there are any
                    char *start = strstr(buffer, "\r\n\r\n");
                    if (start != NULL) {
                        start += 4;
                        int start_len = buffer + bytes_received - start;
                        memcpy(request, start, start_len);
                        total_received += start_len;
                    }



                    fprintf(stderr, "[MAIN - Full Req] Setting up request buffer\n");
                    

                    while (total_received < content_length) {
                        int bytes_to_read = content_length - total_received;
                        int received = recv(connfd, request + total_received, bytes_to_read, 0);
                        if (received <= 0) {
                            perror("recv failed");
                            free(request);
                            return false;
                        }
                        total_received += received;
                        fprintf(stderr, "[UPLOAD] Received %d bytes (%d/%d).\n", received, total_received, content_length);
                        // print first 1000 bytes of the request
                            
                    }
                    fprintf(stderr, "[MAIN - Full Req] Parsing request buffer\n");
                    fprintf(stderr, "[MAIN - Full Req] ===DATA========\n%s\n============\n", request);
                }

                    
                    
                    
                
                    // Check if the request is for "/"
                if (strcmp(url, "/") == 0) {
                    if (strcmp(method, "GET") != 0) {
                        send(connfd, ERROR4051, strlen(ERROR4051), 0);
                        fprintf(stderr, "[MAIN - /] Send 405 Method Not Allowed for /, it should be GET\n");
                        continue;
                    }
                    sendPage(connfd, "./web/index.html", 0);
                } 
                else if (strstr(url, "/api") != NULL) {
                    if (strstr(url, "/api/file") != NULL) {
                        if (strcmp(url, "/api/file") == 0) {
                            //upload file
                            if (strstr(method, "POST") == NULL || strcmp(type, "multipart/form-data") != 0) {
                                send(connfd, ERROR4050, strlen(ERROR4050), 0);
                                fprintf(stderr, "[MAIN - File Uploads] Send 405 Method Not Allowed for /api/file, it should be POST\n");
                                continue;
                            }
                            if (auth == false) {
                                send(connfd, ERROR401, strlen(ERROR401), 0);
                                fprintf(stderr, "[MAIN - File Uploads] Send 401 Unauthorized for /api/file\n");
                                continue;
                            }
                            
                            if (parseData(request, content_length, connfd, 0)) {
                                fprintf(stderr, "[MAIN - File Uploads] File uploaded successfully\n");
                                //response 200 OK
                                char response[105] = "HTTP/1.1 200 OK\r\nServer: CN2024Server/1.0\r\nContent-Type: text/plain\r\nContent-Length: 14\r\n\r\nFile uploaded\n";
                                send(connfd, response, strlen(response), 0);
                            } else {
                                fprintf(stderr, "[MAIN - File Uploads] 500 File upload failed\n");
                                send(connfd, ERROR500, strlen(ERROR500), 0);
                            }
                            free(request);
                            
                        } 
                        else if (strstr(url, "/api/file/") != NULL) {
                            //provide file
                            if (strstr(method, "GET") == NULL) {
                                send(connfd, ERROR4051, strlen(ERROR4051), 0);
                                fprintf(stderr, "[MAIN - File Serve] Send 405 Method Not Allowed for /api/file/<filename>, it should be GET\n");
                                continue;
                            }
                            downloader(url, connfd, 0);
                        }
                        else {
                            send(connfd, ERROR404, strlen(ERROR404), 0);
                            fprintf(stderr, "[MAIN - API Misc] Send 404 due to file not found for %s\n", url);
                        }
                        
                    } else if (strstr(url, "/api/video") != NULL) {
                        if (strcmp(url, "/api/video") == 0) {
                            fprintf(stderr, "[MAIN - Vid Uploads] Request: %s\n", buffer);
                            //upload file
                            if (strstr(method, "POST") == NULL || strcmp(type, "multipart/form-data") != 0) {
                                send(connfd, ERROR4050, strlen(ERROR4050), 0);
                                fprintf(stderr, "[MAIN - Vid Uploads] Send 405 Method Not Allowed for /api/video, it should be POST\n");
                                continue;
                            }
                            if (auth == false) {
                                send(connfd, ERROR401, strlen(ERROR401), 0);
                                fprintf(stderr, "[MAIN - Vid Uploads] Send 401 Unauthorized for /api/video\n");
                                continue;
                            }
                            
                            if (parseData(request, content_length, connfd, 1)) {
                                fprintf(stderr, "[MAIN - Vid Uploads] Video uploaded successfully\n");
                                //response 200 OK
                                char response[106] = "HTTP/1.1 200 OK\r\nServer: CN2024Server/1.0\r\nContent-Type: text/plain\r\nContent-Length: 14\r\n\r\nVideo uploaded\n";
                                send(connfd, response, strlen(response), 0);
                            } else {
                                fprintf(stderr, "[MAIN - Vid Uploads] 500 File upload failed\n");
                                send(connfd, ERROR500, strlen(ERROR500), 0);
                            }
                            free(request);
                            
                        } 
                        else if (strstr(url, "/api/video/") != NULL) {
                            //provide file
                            if (strstr(method, "GET") == NULL) {
                                send(connfd, ERROR4051, strlen(ERROR4051), 0);
                                fprintf(stderr, "[MAIN - Vid Serve] Send 405 Method Not Allowed for /api/video/<filename>, it should be GET\n");
                                continue;
                            }
                            downloader(url, connfd, 1);
                        }
                        else {
                            send(connfd, ERROR404, strlen(ERROR404), 0);
                            fprintf(stderr, "[MAIN - API Misc] Send 404 due to file not found for %s\n", url);
                        }
                    } else {
                        send(connfd, ERROR404, strlen(ERROR404), 0);
                        fprintf(stderr, "[MAIN - API Misc] Send 404 due to file not found for %s\n", url);
                    }
                } 
                else if (strstr(url, "/upload/") != NULL) {
                    // check if the endpoint is defined, it must strictly be /upload/file or /upload/video
                    if (strcmp(url, "/upload/video") == 0) {
                            if (strcmp(method, "GET") != 0) {
                            send(connfd, ERROR4051, strlen(ERROR4051), 0);
                            fprintf(stderr, "[MAIN - Uploader] Send 405 Method Not Allowed for /upload/video, it should be GET\n");
                            continue;
                        }
                        if (auth == false) {
                            send(connfd, ERROR401, strlen(ERROR401), 0);
                            fprintf(stderr, "[MAIN - Uploader] Send 401 Unauthorized for /upload/video\n");
                            continue;
                        }
                        sendPage(connfd, "./web/uploadv.html", 0);
                    }
                    else if (strcmp(url, "/upload/file") == 0) {
                        if (strcmp(method, "GET") != 0) {
                            send(connfd, ERROR4051, strlen(ERROR4051), 0);
                            fprintf(stderr, "[MAIN - Uploader] Send 405 Method Not Allowed for /upload/file, it should be GET\n");
                            continue;
                        }
                        if (auth == false) {
                            send(connfd, ERROR401, strlen(ERROR401), 0);
                            fprintf(stderr, "[MAIN - Uploader] Send 401 Unauthorized for /upload/file or video\n");
                            continue;
                        }
                        sendPage(connfd, "./web/uploadf.html", 0);
                    }
                    else {
                        send(connfd, ERROR404, strlen(ERROR404), 0);
                        fprintf(stderr, "[MAIN - Uploader] Send 404 due to file not found for /upload/\n");
                    }
                }
                else if (strcmp(url, "/file/") == 0){
                    if (strcmp(method, "GET") != 0){
                        send(connfd, ERROR4051, strlen(ERROR4051), 0);
                        fprintf(stderr, "[MAIN - File List] Send 405 Method Not Allowed for /file/, it should be GET\n");
                        continue;
                    }
                    sendPage(connfd, "./web/listf.rhtml", 1);
                }
                else if (strstr(url, "/video/") != NULL) {
                    if (strcmp(url, "/video/") == 0){
                        if (strcmp(method, "GET") != 0){
                            send(connfd, ERROR4051, strlen(ERROR4051), 0);
                            fprintf(stderr, "[MAIN - Vid List] Send 405 Method Not Allowed for /video/, it should be GET\n");
                            continue;
                        }
                        sendPage(connfd, "./web/listv.rhtml", 1);
                    }
                    else {
                        if (strcmp(method, "GET") != 0) {
                            send(connfd, ERROR4051, strlen(ERROR4051), 0);
                            fprintf(stderr, "[MAIN - Vid Player] Send 405 Method Not Allowed for /video/<filename>, it should be GET\n");
                            continue;
                        }
                        fprintf(stderr, "[MAIN - Vid Player] Requested video: %s\n", url);
                        //parse the url
                        char *filename_start = strstr(url, "/video/") + strlen("/video/");
                        fprintf(stderr, "[MAIN - Vid Player] Filename: %s\n", filename_start);
                        
                        char tmpfilename[128];
                        strncpy(tmpfilename, filename_start, strlen(filename_start));
                        tmpfilename[strlen(filename_start)] = '\0';
                        char *filename = url_decode(tmpfilename);
                        fprintf(stderr, "[MAIN - Vid Player] Requested video: %s\n", filename);
                        sendPage(connfd, filename, 2);
                        free(filename);
                    }
                }
                else {
                    //pass if favicon.ico for now :/
                    
                    send(connfd, ERROR404, strlen(ERROR404), 0);
                    fprintf(stderr, "[MAIN - Misc] Send 404 Not Found for unknown request\n");
                    //fprintf(stderr, "[MAIN - Misc] Unknown request: %s\n", url);
                }

                if (!keep_alive) {
                    fprintf(stderr, "[Main - CON] Closing connection: fd %d due to Connection: close\n", poll_fds[i].fd);
                    close(poll_fds[i].fd);
                    poll_fds[i] = poll_fds[nfds - 1];  // Remove from poll set
                    nfds--;
                    i--;  // Process the new entry at index i
                }
                
            }
        }
    }

    close(listenfd);
    return 0;
}