#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "helper.h"
#include "ErrorCodes.h"
#include <pthread.h>
#include <errno.h>
#include <signal.h>
#include <netinet/tcp.h>
#include <netdb.h>

#define RESET "\e[0m"
#define RED "\e[0;31m"

void *sendAndRecieveFromSS(char *details, char *buffer) {
    char *ipOfSS = strtok(details, ",");
    char *portOfSS = strtok(NULL, ",");
    // Generated with copilot
    int sock;
    struct sockaddr_in ss_addr;

    // Create socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        // perror("Socket creation failed");
        printErrorDetails(7);
        return NULL;
    }

    // Set up server address
    ss_addr.sin_family = AF_INET;
    ss_addr.sin_port = htons(atoi(portOfSS));
    if (inet_pton(AF_INET, ipOfSS, &ss_addr.sin_addr) <= 0) {
        // perror("Invalid IP address");
        printErrorDetails(8);
        close(sock);
        return NULL;
    }

    // Connect to server
    if (connect(sock, (struct sockaddr *) &ss_addr, sizeof(ss_addr)) < 0) {
        // perror("Connection failed");
        printErrorDetails(9);
        close(sock);
        return NULL;
    }

    // Send buffer
    if (send_good(sock, buffer, strlen(buffer)) < 0) {
        // perror("Send failed");
        printErrorDetails(10);
        close(sock);
        return NULL;
    }

    // Receive data continuously
    char response[BUFFER_SIZE];
    while (1) {
        ssize_t bytes_received = recv_good(sock, response, sizeof(response) - 1);
        if (bytes_received <= 0) {
            break;
        }
        response[bytes_received] = '\0';
        printf("%s", response);
    }

    fflush(stdin);
    close(sock);
    return NULL;
}

volatile int isPaused = 0; // Global flag for play/pause control

// Thread function to handle user input for play/pause
void *handleUserInput(void *arg) {
    while (1) {
        char input = getchar(); // Wait for user input
        if (input == 'p') { // 'p' for play/pause toggle
            isPaused = !isPaused;
            if (isPaused) {
                printf("Playback paused\n");
            } else {
                printf("Playback resumed\n");
            }
        }
    }
    return NULL;
}

void *streamMusicFromSS(char *details, char *buffer) {
    char *ipOfSS = strtok(details, ",");
    char *portOfSS = strtok(NULL, ",");
    int sock;
    struct sockaddr_in ss_addr;

    // Create socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        // perror("Socket creation failed");
        printErrorDetails(7);
        return NULL;
    }

    // Set up server address
    ss_addr.sin_family = AF_INET;
    ss_addr.sin_port = htons(atoi(portOfSS));
    if (inet_pton(AF_INET, ipOfSS, &ss_addr.sin_addr) <= 0) {
        // perror("Invalid IP address");
        printErrorDetails(8);
        close(sock);
        return NULL;
    }

    // Connect to server
    if (connect(sock, (struct sockaddr *) &ss_addr, sizeof(ss_addr)) < 0) {
        // perror("Connection failed");
        printErrorDetails(9);
        close(sock);
        return NULL;
    }

    // Send buffer to server
    if (send(sock, buffer, strlen(buffer), 0) < 0) {
        // perror("Send failed");
        printErrorDetails(10);
        close(sock);
        return NULL;
    }

    // Open FFplay pipe for streaming
    FILE *ffplayPipe = popen("ffplay -autoexit -", "w");
    if (!ffplayPipe) {
        // perror("Failed to open FFplay");
        printErrorDetails(36);
        close(sock);
        return NULL;
    }

    // Start a thread to handle user input for play/pause
    pthread_t inputThread;
    pthread_create(&inputThread, NULL, handleUserInput, NULL);

    // Buffer for receiving data
    char response[BUFFER_SIZE_FOR_MUSIC];
    ssize_t bytes_received;
    int firstTime = 1;

    while (1) {
        // Pause logic: Wait until playback is resumed
        while (isPaused) {
            usleep(100000); // Sleep for 100ms to avoid busy waiting
        }

        bytes_received = recv(sock, response, sizeof(response), 0);
        if (bytes_received <= 0) {
            if (bytes_received == 0) {
                printf("End of stream\n");
            } else {
                if (firstTime) {
                    printf("File not Found\n");
                }
                perror("Receive failed");
            }
            break;
        }

        // Write data to FFplay
        if (fwrite(response, 1, bytes_received, ffplayPipe) != (size_t) bytes_received) {
            // perror("Error writing to FFplay");
            printErrorDetails(36);
            break;
        }

        fflush(ffplayPipe); // Ensure data is flushed to the FFplay process
        firstTime = 0;
    }

    // Clean up
    pclose(ffplayPipe);
    close(sock);
    pthread_cancel(inputThread);     // Cancel the input thread
    pthread_join(inputThread, NULL); // Ensure thread cleanup
    return NULL;
}

int count_words(const char *input) {
    char buffer[100]; // Temporary buffer to avoid modifying the input
    strcpy(buffer, input);

    int count = 0;
    char *token = strtok(buffer, " "); // Tokenize using space as delimiter

    while (token != NULL) {
        count++;
        token = strtok(NULL, " "); // Get the next token
    }

    return count;
}


char ASYNC_PORT_FOR_CLIENT[7];
char IPADDR[INET_ADDRSTRLEN];


void *listen_async(void *arg) {
    int client = *(int *) arg;
    char buffer[BUFFER_SIZE] = {0};
    if (recv_good(client, buffer, BUFFER_SIZE) <= 0) {
        perror("Failed to receive message from client");
        close(client);
        return NULL;
    }
    printf("\033[0;32mAsync %d>>|%s|\033[0m\n", client, buffer);  // Green text



}


void *async_setup(void *arg) {
    int async_socket = *(int *) arg;
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_size = sizeof(client_addr);
        int client_socket = accept(async_socket, (struct sockaddr *) &client_addr, &addr_size);
        if (client_socket < 0) {
            perror("Accept failed");
            continue;
        }
        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, listen_async, &client_socket) != 0)
            perror("Failed to create thread");

    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <naming_server_ip>\n", argv[0]);
        exit(EXIT_FAILURE);
    }


    // ||||||| ASYNC CREATION |||||||

    // Create a new socket for listening to async
    int async_socket;
    if ((async_socket = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    int reuse = 1;
// Enable port reuse on async_socket
    if (setsockopt(async_socket, SOL_SOCKET, SO_REUSEADDR, (char *) &reuse, sizeof(reuse)) < 0) {
        perror("setsockopt SO_REUSEADDR failed");
        close(async_socket);
        return 1;
    }

// Set TCP_NODELAY for async_socket
    if (setsockopt(async_socket, IPPROTO_TCP, TCP_NODELAY, (char *) &reuse, sizeof(reuse)) < 0) {
        perror("setsockopt TCP_NODELAY failed");
        close(async_socket);
        return 1;
    }

// Bind the socket to a port
    struct sockaddr_in async_addr;
    async_addr.sin_family = AF_INET;
    async_addr.sin_addr.s_addr = INADDR_ANY;
    async_addr.sin_port = 0;

    if (bind(async_socket, (struct sockaddr *) &async_addr, sizeof(async_addr)) < 0) {
        perror("Bind failed");
        close(async_socket);
        return 1;
    }

    socklen_t addr_len = sizeof(async_addr);
    //Getting the details back
    if (getsockname(async_socket, (struct sockaddr *) &async_addr, &addr_len) < 0) {
        perror("getsockname failed");
        close(async_socket);
        exit(EXIT_FAILURE);
    }

    inet_ntop(AF_INET, &async_addr.sin_addr, IPADDR, INET_ADDRSTRLEN);
    int port = ntohs(async_addr.sin_port);
    sprintf(ASYNC_PORT_FOR_CLIENT, "%d", port);
    //Getting the IP
    char hostname[NI_MAXHOST];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        struct addrinfo hints, *res, *p;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET; // IPv4 only
        hints.ai_socktype = SOCK_STREAM;

        if (getaddrinfo(hostname, NULL, &hints, &res) == 0) {
            for (p = res; p != NULL; p = p->ai_next) {
                struct sockaddr_in *addr = (struct sockaddr_in *) p->ai_addr;
                inet_ntop(AF_INET, &addr->sin_addr, IPADDR, INET_ADDRSTRLEN);
                break; // Use the first available address
            }
            freeaddrinfo(res);
        } else {
            perror("getaddrinfo failed");
            close(async_socket);
            exit(EXIT_FAILURE);
        }
    } else {
        perror("gethostname failed");
        close(async_socket);
        exit(EXIT_FAILURE);
    }


    printf("Client IP Address: %s\n", IPADDR);
    printf("Async Port: %d\n", port);



// Start listening for incoming connections
    if (listen(async_socket, 5) < 0) {
        perror("Listen failed");
        close(async_socket);
        return 1;
    }

// Create a thread and pass the async_socket to it to the async_setup function
    pthread_t async_thread;
    if (pthread_create(&async_thread, NULL, async_setup, &async_socket) != 0) {
        perror("Failed to create async thread");
        close(async_socket);
        return 1;
    }


    // ||||||| ASYNC Finished |||||||


    int client_socket;
    struct sockaddr_in server_addr;



    // Create socket
    if ((client_socket = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        // perror("Socket creation failed");
        printErrorDetails(7);
        exit(EXIT_FAILURE);
    }

    // Set server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(CLIENT_PORT_NS);
    server_addr.sin_addr.s_addr = inet_addr(argv[1]);

    // Connect to Naming Server
    if (connect(client_socket, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        // perror("Connection failed");
        printErrorDetails(9);
        close(client_socket);
        exit(EXIT_FAILURE);
    }

    printf("Connected to Naming Server @ %s:%d\n", argv[1], CLIENT_PORT_NS);

    char buffer[BUFFER_SIZE];
    char buffer2[BUFFER_SIZE];

    while (1) {
        bzero(buffer, BUFFER_SIZE);
        bzero(buffer2, BUFFER_SIZE);
        printf(">> ");
        if (fgets(buffer, BUFFER_SIZE, stdin) == NULL) {
            printf("\nExiting...\n");
            break; // Exit loop on EOF (Ctrl+D)
        }

        trim(buffer); // Remove trailing newline or extra spaces
        if (strlen(buffer) == 0)
            continue; // Skip empty input

        int c = count_words(buffer);
        if (c <= 1) {
            // printf("Incorrect format\n");
            printErrorDetails(69);
            continue;
        }

        char *path_for_write;
        if (strncmp(buffer, "write", strlen("write")) == 0) {
            char *tmpdup = strdup(buffer);
            path_for_write = strtok(tmpdup, " ");
            path_for_write = strtok(NULL, " ");
            path_for_write = strtok(NULL, " ");
            // free(tmpdup); // Avoid memory leak   RUDRA
        }

        // Send command to Naming Server
        if (send_good(client_socket, buffer, strlen(buffer)) < 0) {
            perror("Naming Server not responding");
            continue;
        }
        // char buffer2[BUFFER_SIZE];
        bzero(buffer2, BUFFER_SIZE);
        if (strcmp(buffer, "ls ~") == 0 || strcmp(buffer, "ls .") == 0) {
            int bytes = 0;
            while ((bytes = recv_good(client_socket, buffer2, BUFFER_SIZE)) > 0) {
                char *end_msg = strstr(buffer2, "@#!@#over@$");
                if (end_msg != NULL) {
                    *end_msg = '\0';       // Terminate string before end message
                    printf("%s", buffer2); // Print remaining data
                    break;                 // Exit loop
                }
                buffer2[bytes] = '\0';
                printf("%s", buffer2);
            }
        } else if (strncmp(buffer, "mkdir", 5) == 0 || strncmp(buffer, "rmdir", 5) == 0 ||
                   strncmp(buffer, "touch", 5) == 0 || strncmp(buffer, "rm", 2) == 0 ||
                   strncmp(buffer, "copy", 4) == 0) {

            int bytes = 0;
            printf("Buffer before %s\n", buffer2);
            if ((bytes = recv_good(client_socket, buffer2, BUFFER_SIZE)) <= 0) {
                // perror("Naming Server not responding");
                printErrorDetails(3);
            }
            buffer2[bytes] = '\0';
            printf("Received from NM Directly |%s|\n", buffer2);
            bzero(buffer2, BUFFER_SIZE);
        } else if (strncmp(buffer, "write", strlen("write")) == 0) {

            char write_buff[BUFFER_SIZE] = {0};

            int bytes_received = recv_good(client_socket, write_buff, BUFFER_SIZE);
            if (bytes_received <= 0) {
                // perror("Naming Server not responding\n");
                printErrorDetails(3);
            }
            // else {
            //     strcat(write_buff, "\0");
            // }

            fflush(stdin);
            fflush(stdout);
            printf("ns response: %s\n", write_buff);
            if (strncmp(write_buff, "PATH_OK", strlen("PATH_OK")) == 0) {
                char *input_str = (char *) malloc(sizeof(char) * 4096); // to change size of max_write
                int i;
                if (strncmp(write_buff, "PATH_OK_SYNC", strlen("PATH_OK_SYNC")) == 0) {
                    strcpy(input_str, "WRITE_SYNC|");
                    strcat(input_str, path_for_write);
                    strcat(input_str, "|");
                    i = 11;
                    i += strlen(path_for_write) + 1;
//                    strcat(input_str, IPADDR);
//                    strcat(input_str, "|");
//                    i += strlen(IPADDR) + 1;

                }
                if (strncmp(write_buff, "PATH_OK_ASYNC", strlen("PATH_OK_ASYNC")) == 0) {
                    strcpy(input_str, "WRITE_ASYNC|");
                    strcat(input_str, path_for_write);
                    strcat(input_str, "|");
                    i = 12;
                    i += strlen(path_for_write) + 1;

                    strcat(input_str, IPADDR);
                    strcat(input_str, "|");
                    strcat(input_str, ASYNC_PORT_FOR_CLIENT);
                    strcat(input_str, "|");
                    i += strlen(IPADDR) + strlen(ASYNC_PORT_FOR_CLIENT) + 2;

                }
//                strcat(input_str, path_for_write);
//                strcat(input_str, "|");
//                i += strlen(path_for_write) + 1;
//
//
//                strcat(input_str, IPADDR);
//                strcat(input_str, "|");
//                strcat(input_str, ASYNC_PORT_FOR_CLIENT);
//                strcat(input_str, "|");
//                i += strlen(IPADDR) + strlen(ASYNC_PORT_FOR_CLIENT) + 2;
                char ch;
                printf("enter data here:");
                for (;;) {
                    ch = fgetc(stdin);
                    if (ch != '\n')
                        input_str[i++] = ch;
                    else
                        break;
                }
                input_str[i] = '\0'; // Null-terminate the input string
                // printf("inputstr: %s\n", input_str);
                char *temp_buffer = strdup(write_buff);
                char *ipOfSS = strtok(write_buff, "|");
                ipOfSS = strtok(NULL, "|");
                char *portOfSS = strtok(NULL, "|");

                // Generated with copilot
                int sock;
                struct sockaddr_in ss_addr;

                // Create socket
                if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
                    // perror("Socket creation failed");
                    printErrorDetails(7);
                    return 2;
                }

                // Set up server address
                ss_addr.sin_family = AF_INET;
                ss_addr.sin_port = htons(atoi(portOfSS));
                if (inet_pton(AF_INET, ipOfSS, &ss_addr.sin_addr) <= 0) {
                    // perror("Invalid IP address");
                    printErrorDetails(8);
                    close(sock);
                    return 2;
                }

                // Connect to server
                if (connect(sock, (struct sockaddr *) &ss_addr, sizeof(ss_addr)) < 0) {
                    // perror("Connection failed");
                    printErrorDetails(9);
                    close(sock);
                    return 2;
                }

                // Send buffer
                printf("Sending to SS |%s|\n", input_str);
                if (send_good(sock, input_str, strlen(input_str)) < 0) {
                    // perror("Send failed");
                    printErrorDetails(10);
                    close(sock);
                    return 2;
                }

                char *rec_msg = (char *) malloc(sizeof(char) * 4096);
                if (recv_good(sock, rec_msg, BUFFER_SIZE) <= 0) {
                    // perror("Storage Server not responding\n");
                    printErrorDetails(11);
                }
                printf("Received from SS |%s|\n", rec_msg);
                free(rec_msg);
            } else {
                // TODO error
            }
        } else {
            if (recv_good(client_socket, buffer2, BUFFER_SIZE) <= 0) {
                // printf("Naming Server not responding\n");
                printErrorDetails(3);
                continue;
            }
            printf("Got this Other Commands from NM|%s|\n", buffer2);


            if (strstr(buffer2, "not found") != NULL) {
                continue;
            }
            char buffer1[BUFFER_SIZE];
            char transformed_buffer2[BUFFER_SIZE];

            if (strncmp(buffer2, "Backup", strlen("Backup")) == 0) {
                // Extract client info and username from buffer2
                char *token = strtok(buffer2 + strlen("Backup "), ","); // Skip "Backup " prefix
                if (token) {
                    // Get IP and port
                    strncpy(transformed_buffer2, token, sizeof(transformed_buffer2) - 1);
                    token = strtok(NULL, ",");
                    if (token) {
                        strncat(transformed_buffer2, ",",
                                sizeof(transformed_buffer2) - strlen(transformed_buffer2) - 1);
                        strncat(transformed_buffer2, token,
                                sizeof(transformed_buffer2) - strlen(transformed_buffer2) - 1);
                    }

                    // Get username
                    token = strtok(NULL, ",");
                    char *username = token;


                    if (strncmp(buffer, "stream", strlen("stream")) == 0) {
                        // Keep stream command as is, just use transformed buffer2
                        streamMusicFromSS(transformed_buffer2, buffer);
                    } else {
                        // For other commands, use transformed buffer2
                        sendAndRecieveFromSS(transformed_buffer2, buffer);
                    }
                }
            }
            else{
                if (strncmp(buffer, "stream", strlen("stream")) == 0) {
                    // Keep stream command as is, just use transformed buffer2
                    streamMusicFromSS(buffer2, buffer);
                } else {
                    printf("dfvsfgsfgsdf\n");
                    // For other commands, use transformed buffer2
                    sendAndRecieveFromSS(buffer2, buffer);
                }
            }
        }

        bzero(buffer, BUFFER_SIZE);
    }






    // Close socket before exiting
    close(client_socket);
    return 0;
}
