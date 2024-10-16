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



// Client handler function
void *handle_client(void *arg) {
    int connfd = *(int *)arg;
    free(arg);  // Free dynamically allocated memory for the connection file descriptor

    char buffer[1024];
    // Receive HTTP request from the client
    int bytes_received = recv(connfd, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received < 0) {
        ERR_EXIT("recv()");
    }

    buffer[bytes_received] = '\0';
    char *auth_header = strstr(buffer, "Authorization: ");
    //fprintf(stderr, "Received request: %s\n", buffer);
    //fprintf(stderr, "Auth header IS : %s\n", auth_header);
    if (!auth_header || !authenticate(auth_header)) {
        char *response = "HTTP/1.1 401 Unauthorized\r\n"
                         "WWW-Authenticate: Basic realm=\"Access to the staging site\", charset=\"UTF-8\"\r\n"
                         "Content-Length: 0\r\n\r\n";
        send(connfd, response, strlen(response), 0);
        close(connfd);
        return NULL;
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
        // Handle file upload page request
        int file_fd = open("./web/uploadf.html", O_RDONLY);
        if (file_fd < 0) {
            char *not_found_response = "HTTP/1.1 404 Not Found\r\nContent-Length: 13\r\n\r\n404 Not Found";
            send(connfd, not_found_response, strlen(not_found_response), 0);
        } else {
            char *header = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n";
            send(connfd, header, strlen(header), 0);

            int read_bytes;
            while ((read_bytes = read(file_fd, buffer, sizeof(buffer))) > 0) {
                send(connfd, buffer, read_bytes, 0);
            }

            close(file_fd);
        }
    } else if (strstr(buffer, "GET /upload/video") != NULL) {
        // Handle file upload page request
        int file_fd = open("./web/uploadv.html", O_RDONLY);
        if (file_fd < 0) {
            char *not_found_response = "HTTP/1.1 404 Not Found\r\nContent-Length: 13\r\n\r\n404 Not Found";
            send(connfd, not_found_response, strlen(not_found_response), 0);
        } else {
            char *header = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n";
            send(connfd, header, strlen(header), 0);

            int read_bytes;
            while ((read_bytes = read(file_fd, buffer, sizeof(buffer))) > 0) {
                send(connfd, buffer, read_bytes, 0);
            }

            close(file_fd);
        }
    } else if (strstr(buffer, "GET /file/") != NULL) {
        // Handle file upload page request
        int file_fd = open("./web/listf.rhtml", O_RDONLY);
        if (file_fd < 0) {
            char *not_found_response = "HTTP/1.1 404 Not Found\r\nContent-Length: 13\r\n\r\n404 Not Found";
            send(connfd, not_found_response, strlen(not_found_response), 0);
        } else {
            char *header = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n";
            send(connfd, header, strlen(header), 0);

            int read_bytes;
            while ((read_bytes = read(file_fd, buffer, sizeof(buffer))) > 0) {
                send(connfd, buffer, read_bytes, 0);
            }

            close(file_fd);
        }
    } else if (strstr(buffer, "GET /video/") != NULL) {
        // Open the template file
        int file_fd = open("./web/listv.rhtml", O_RDONLY);
        if (file_fd < 0) {
            char *not_found_response = "HTTP/1.1 404 Not Found\r\nContent-Length: 13\r\n\r\n404 Not Found";
            send(connfd, not_found_response, strlen(not_found_response), 0);
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
                char *not_found_response = "HTTP/1.1 404 Not Found\r\nContent-Length: 13\r\n\r\n404 Not Found";
                send(connfd, not_found_response, strlen(not_found_response), 0);
            } else {
                struct dirent *entry;
                char video_list[4096] = "";  // Buffer to store the generated video list
                while ((entry = readdir(dir)) != NULL) {
                    // Skip the "." and ".." directories
                    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                        continue;

                    // Create the HTML row for each video folder/file
                    char row[256];
                    snprintf(row, sizeof(row), "<tr><td><a href=\"/video/%s\">%s</a></td></tr>\n", entry->d_name, entry->d_name);
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
                    char *header = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n";
                    send(connfd, header, strlen(header), 0);
                    send(connfd, final_content, strlen(final_content), 0);
                }
            }
        }
    } else {
        // Handle other routes or 405 Method Not Allowed
        char *method_not_allowed_response = "HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\n\r\n";
        send(connfd, method_not_allowed_response, strlen(method_not_allowed_response), 0);
    }

    close(connfd);
    return NULL;
}

int main(int argc, char *argv[]) {
    int listenfd;
    struct sockaddr_in server_addr, client_addr;
    int client_addr_len = sizeof(client_addr);

    // Parse the arguments
    if (argc != 2) {
        fprintf(stderr, "Usage: ./server [port]\n");
        return -1;
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

    int opt = 1;
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        ERR_EXIT("setsockopt()");
    }

    if (bind(listenfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ERR_EXIT("bind()");
    }

    // Listen on the server file descriptor
    if (listen(listenfd, 10) < 0) {
        ERR_EXIT("listen()");
    }

    printf("Server listening on port %d...\n", PORT);

    while (1) {
        int *connfd = malloc(sizeof(int));  // Allocate memory for the connection file descriptor
        if ((*connfd = accept(listenfd, (struct sockaddr *)&client_addr, (socklen_t *)&client_addr_len)) < 0) {
            ERR_EXIT("accept()");
        }

        // Create a new thread for the client connection
        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_client, connfd) != 0) {
            perror("pthread_create()");
            free(connfd);  // Free the memory if thread creation fails
            close(*connfd);
        }

        // Detach the thread to avoid memory leaks
        pthread_detach(thread_id);
    }

    close(listenfd);
    return 0;
}
