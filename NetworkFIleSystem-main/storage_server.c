// StorageServer.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <sys/stat.h>
#include "helper.h"
#include "ErrorCodes.h"
#include "lock.h"

#ifndef __APPLE__
#include <signal.h>
#endif
char *ROOT_PATH;
char *nm_ip;

Lock_Struct conc_array[MAX_REQ];
int num_of_requests = 0;
 
int doesLockRequestedFileAlreadExist(char *pathToSearch)
{
    for (int i = 0; i < num_of_requests; i++)
    {
        if (strcmp(conc_array[i].path_to_file, pathToSearch) == 0)
        {
            return i;
        }
    }
 
    return -1;
}


int write_file(char *command, char *buffer2) {

    char *path = strtok(command, "|");
    path = strtok(NULL, "|");
    char *data = strtok(NULL, "|");



    int lock_array_index = doesLockRequestedFileAlreadExist(path);
        if (lock_array_index == -1)
        {
            init_lock(&conc_array[num_of_requests], path);
            lock_array_index = num_of_requests;
            num_of_requests++;
        }
        acquire_write_lock(&conc_array[lock_array_index]);


    FILE *file = fopen(path, "a");
    if (file == NULL) {
        snprintf(buffer2, BUFFER_SIZE, "Could not open file @ %s \n", path);
        printf("%s", buffer2);
        perror("fopen");
        return -1;
    }

    fprintf(file, "%s\n", data);

    sleep(10);

    release_write_lock(&conc_array[lock_array_index]);

    snprintf(buffer2, BUFFER_SIZE, "Data written successfully\n");
    fflush(file);
    fclose(file);
    return 0;
}

void *write_file_async(void *arg) {
    void **arguments = (void **) arg;
    char *command = (char *) arguments[0];
    int sock = *(int *) arguments[1];


    printf("the command inside WFA: %s\n", command);
    printf("Sock after sending: %d\n", sock);
    printf("Server IP: %s\n", (char *) arguments[2]);
    char ack_msg[] = "Write Request Recieved";
    printf("ack message: %s:%d\n", ack_msg, sock);
    if (send_good(sock, ack_msg, strlen(ack_msg)) < 0) {
        perror("Failed to send acknowledgment");
    }

    // printf("args are: command: %s  sock: %d\n", command, sock);
    char *command_copy = strdup(command);  // Create a copy to tokenize
    char *path = strtok(command_copy, "|");
    path = strtok(NULL, "|");
    char *ip = strtok(NULL, "|");
    char *port_of_cl = strtok(NULL, "|");
    char *data = strtok(NULL, "|");



    int lock_array_index = doesLockRequestedFileAlreadExist(path);
        if (lock_array_index == -1)
        {
            init_lock(&conc_array[num_of_requests], path);
            lock_array_index = num_of_requests;
            num_of_requests++;
        }
        acquire_write_lock(&conc_array[lock_array_index]);

    printf("I HAVE ip: %s, port: %s\n", ip, port_of_cl);
    fflush(stdin);
    FILE *file = fopen(path, "a");
    if (file == NULL) {
        perror("Failed to open file");
        const char *error_msg = "Failed to open file\n";
        send_good(sock, error_msg, strlen(error_msg));
        close(sock);
        return NULL;
    }

    char buffer[ASYNC_BUFF_LEN + 1];
    size_t buffer_offset = 0;
    int data_offset = 0;

    for (int i = 0; i < strlen(data); i++) {
        buffer[buffer_offset++] = data[data_offset++];
        if (buffer_offset == ASYNC_BUFF_LEN) {
            fprintf(file, "%s", buffer);
            bzero(buffer, ASYNC_BUFF_LEN + 1);
            buffer_offset = 0;
        }
    }

    if (buffer_offset >= 0) {
        fprintf(file, "%s\n", buffer);
    }

    sleep(10);
    // Send acknowledgment to the client
    // ack_msg = "Data written successfully";
    // if (send_good(sock, ack_msg, strlen(ack_msg)) < 0) {
    //     perror("Failed to send acknowledgment");
    // }
    // printf("qazxswedcvfrtgbnjhyuhnmkiuioklop\n");


    release_write_lock(&conc_array[lock_array_index]);

    // sending ACK

    int sock1;
    struct sockaddr_in nm_addr;

// Create the sock1et
    if ((sock1 = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("sock1et creation failed");
        exit(EXIT_FAILURE);
    }

// Disable Nagle's algorithm to avoid buffering delays
    int flag = 1;
    if (setsockopt(sock1, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof(int)) < 0) {
        printf("setsock1opt TCP_NODELAY failed");
        close(sock1);
        exit(EXIT_FAILURE);
    }

// Set up Naming Server address
    nm_addr.sin_family = AF_INET;

// Use ASYNC_PORT instead of SERVER_PORT
    nm_addr.sin_port = htons(ASYNC_PORT_FOR_NM);

    if (inet_pton(AF_INET, (char *) arguments[2], &nm_addr.sin_addr) <= 0) {
        perror("Invalid IP address");
        close(sock1);
        exit(EXIT_FAILURE);
    }

// Connect to Naming Server
    if (connect(sock1, (struct sock1addr *) &nm_addr, sizeof(nm_addr)) < 0) {
        perror("Connection to Naming Server failed");
        close(sock1);
        exit(EXIT_FAILURE);
    }
    printf("Connected to Naming Server at %s:%d\n", (char *) arguments[2], ASYNC_PORT_FOR_NM);

// Send data to Naming Server
    char buffer2[BUFFER_SIZE] = {0};
    snprintf(buffer2, BUFFER_SIZE, "writeasync %s %s %s\n", path, ip, port_of_cl);
    if (send_good(sock1, buffer2, strlen(buffer2)) < 0) {
        perror("Send failed");
        close(sock1);
        exit(EXIT_FAILURE);
    }
    printf("\033[32mACK for write async on path %s sent to Naming Server\033[0m\n", path);
    close(sock1);

    fclose(file);
    close(sock);
    return NULL;
}

void SendACKToNM(char *command) {
    int sock1;
    struct sockaddr_in nm_addr;

// Create the sock1et
    if ((sock1 = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("sock1et creation failed");
        exit(EXIT_FAILURE);
    }

// Disable Nagle's algorithm to avoid buffering delays
    int flag = 1;
    if (setsockopt(sock1, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof(int)) < 0) {
        printf("setsock1opt TCP_NODELAY failed");
        close(sock1);
        exit(EXIT_FAILURE);
    }

// Set up Naming Server address
    nm_addr.sin_family = AF_INET;

// Use ASYNC_PORT instead of SERVER_PORT
    nm_addr.sin_port = htons(ASYNC_PORT_FOR_NM);

    if (inet_pton(AF_INET, nm_ip, &nm_addr.sin_addr) <= 0) {
        perror("Invalid IP address");
        close(sock1);
        exit(EXIT_FAILURE);
    }

// Connect to Naming Server
    if (connect(sock1, (struct sock1addr *) &nm_addr, sizeof(nm_addr)) < 0) {
        perror("Connection to Naming Server failed");
        close(sock1);
        exit(EXIT_FAILURE);
    }
    printf("Connected to Naming Server at %s:%d\n", (char *) nm_ip, ASYNC_PORT_FOR_NM);

// Send data to Naming Server
    if (send_good(sock1, command, strlen(command)) < 0) {
        perror("Send failed");
        close(sock1);
        exit(EXIT_FAILURE);
    }
    printf("\033[32mACK for write SYNC on path %s sent to Naming Server\033[0m\n", command);
    close(sock1);
}

#define RED "\e[0;31m"
#define RESET "\e[0m"

void process_client(int sock) {
    int notasynccommand = 1;

    char buffer[BUFFER_SIZE];
    int bytes_received = recv_good(sock, buffer, BUFFER_SIZE);
    if (bytes_received <= 0) {
        printf("Client Disconnected\n");
    }
    buffer[bytes_received] = '\0';

    if (strncmp(buffer, "WRITE_SYNC|", strlen("WRITE_SYNC|")) == 0) {

        //The incoming data is of the form WRITE_SYNC|path|data

        printf("WRITE SYNC DATA: %s", buffer);

        char *buffer2 = (char *) calloc(sizeof(char), BUFFER_SIZE);
        char* _buffer = strdup(buffer);
        write_file(buffer, buffer2);
        if (send_good(sock, buffer2, strlen(buffer2)) < 0) {
            perror("Failed to send message to client");
        }
        SendACKToNM(_buffer);
        free(_buffer);
        free(buffer2);
        close(sock);
        return;

    } else if (strncmp(buffer, "ping", strlen("ping")) == 0) {
        send_good(sock, "-ping-", strlen("-ping-"));
    } else if (strncmp(buffer, "WRITE_ASYNC|", strlen("WRITE_ASYNC|")) == 0) {
        //The incoming data is of the form WRITE_ASYNC|path|IP|PORT|data

//        printf("WRITE ASYNC DATAL: %s", buffer);
        notasynccommand = 0;
        pthread_t async_write_thread;
        void **thread_args = (void **) malloc(3 * sizeof(void *));
        thread_args[0] = (void *) strdup(buffer);
        thread_args[1] = (void *) malloc(sizeof(int));  // Allocate new memory
        *((int *) thread_args[1]) = sock;  // Copy socket value, not its address
        thread_args[2] = nm_ip;
        pthread_create(&async_write_thread, NULL, write_file_async, (void *) thread_args);

        printf("writing asynchronously %s\n", buffer);
    }

    printf("Client >> %s\n", buffer);
    char *args[2];
    char *buffer2 = (char *) calloc(BUFFER_SIZE, 1);
    char *tempBuffer = strdup(buffer);
    parse_command(tempBuffer, args);
    free(tempBuffer);
    printf("---|%s|----\n", args[1]);

    parse_command(tempBuffer, args);

    printf("Converted to  |%s|\n", args[1]);
    if (strncmp(buffer, "sscopyb", strlen("sscopyb")) == 0) {
        //This is because storage server will connect as a client
        printf("Starting to sscopy on command |%s|\n", buffer);
        copy_different_dest_b(sock);
    } else if (strncmp(buffer, "sscopy", strlen("sscopy")) == 0) {
        //This is because storage server will connect as a client
        printf("Starting to sscopy on command |%s|\n", buffer);
        copy_different_dest(sock);
    } else if (strncmp(buffer, "ls", strlen("ls")) == 0) {
        list_file(args, sock);
    } else if (strncmp(buffer, "cat", strlen("cat")) == 0) {



    int lock_array_index = doesLockRequestedFileAlreadExist(args[1]);
        if (lock_array_index == -1)
        {
            init_lock(&conc_array[num_of_requests], args[1]);
            lock_array_index = num_of_requests;
            num_of_requests++;
        }
        acquire_read_lock(&conc_array[lock_array_index]);
        read_file(args, sock);
        release_read_lock(&conc_array[lock_array_index]);

    } else if (strncmp(buffer, "stream", strlen("stream")) == 0) {
        read_mp3_file(args, sock);
    } else if (strncmp(buffer, "get_info", strlen("get_info")) == 0) {
        send_file_metadata(args[1], sock);
    }
    // if (send_good(sock, buffer2, strlen(buffer2)) < 0)
    // {
    //     perror("Failed to send message to client");
    // }

    if (notasynccommand) {
        free(buffer2);
        close(sock); // Closing so that the client knows it is done
    }

}


void listen_to_nm(int sock) {
    while (1) {
        char buffer[BUFFER_SIZE];
        bzero(buffer, BUFFER_SIZE);

        int bytes_received = recv_good(sock, buffer, BUFFER_SIZE);
        if (bytes_received <= 0) {
            printf("NS Disconnected\n");
            exit(0);
        }
        buffer[bytes_received] = '\0';
        printf("NM >> %s\n", buffer);
        char *args[5]; // Max 5 arg command can come
        char *buffer2 = (char *) malloc(BUFFER_SIZE);
        char *tempBuffer = strdup(buffer);
        parse_command(tempBuffer, args);

        if (strncmp(buffer, "ping", strlen("ping")) == 0) {
            send_good(sock, "-ping-", strlen("-ping-"));
        }

        if (strncmp(buffer, "touchb", strlen("touchb")) == 0) {
            // For Backup
            // assume args[1] is ./foldername/filename now, change it to ./backupfolderforss/foldername/filename
            char backup_path[MAX_PATH_SIZE];
            snprintf(backup_path, sizeof(backup_path), "./backupfolderforss/%s/%s", ROOT_PATH + 2,
                     args[1] + 2); // Skip the "./" part
            args[1] = backup_path;
            create_file(args, buffer2);
            free(buffer2);
            continue; // so that it does not go to the normal touch and also it does not try to send the buffer2
        }
        else if (strncmp(buffer, "rmdirb", strlen("rmdirb")) == 0) {
            // For Backup
            char backup_path[MAX_PATH_SIZE];
            snprintf(backup_path, sizeof(backup_path), "./backupfolderforss/%s/%s", ROOT_PATH + 2,
                     args[1] + 2); // Skip the "./" part
            args[1] = backup_path;
            delete_directory(args, buffer2);
            free(buffer2);
            continue;
        } else if (strncmp(buffer, "rmb", strlen("rmb")) == 0) {
            // For Backup
            char backup_path[MAX_PATH_SIZE];
            snprintf(backup_path, sizeof(backup_path), "./backupfolderforss/%s/%s", ROOT_PATH + 2,
                     args[1] + 2); // Skip the "./" part
            args[1] = backup_path;
            remove_file(args, buffer2);
            free(buffer2);
            continue;

        } else if (strncmp(buffer, "mkdirb", strlen("mkdirb")) == 0) {
            // For Backup
            char backup_path[MAX_PATH_SIZE];
            snprintf(backup_path, sizeof(backup_path), "./backupfolderforss/%s/%s", ROOT_PATH + 2,
                     args[1] + 2); // Skip the "./" part
            args[1] = backup_path;

            create_directory(args, buffer2);
            free(buffer2);
            continue;
        }
        else if (strncmp(buffer, "copydifferentb", strlen("copydifferentb")) == 0) {
            // Copy between SS
            printf("Starting to copydifferent on command %s\n", buffer);
            // "copydifferent ./foldertest ./testfolder 127.0.0.1 58303" this is how the input comes
            //send "sscopy" to the destination server
            char *command = strdup(buffer);
            char *token;
            char *ip = NULL;
            int port;
            // Tokenize the command string
            token = strtok(command, " ");
            int count = 0;

            while (token != NULL) {
                count++;

                // Extract the 4th token as IP address
                if (count == 4) {
                    ip = token;
                }
                    // Extract the 5th token as port number
                else if (count == 5) {
                    port = atoi(token);
                }

                // Move to the next token
                token = strtok(NULL, " ");
            }
            printf("Resolved the SS to %s:%d\n", ip, port);
            int ss_socket = ss_info_to_socket(ip, port);
            if (ss_socket == -1) {
                printf("Could not connect to the destination server\n");
                strcpy(buffer2, "Could not connect to the destination server\n");
            } else {
                if (send_good(ss_socket, "sscopyb", strlen("sscopyb")) < 0) {
                    // perror("Failed to send message to client");
                    printErrorDetails(52);
                    strcpy(buffer2, "Failed to send message to client\n");
                } else {
                    copy_different_src_b(buffer, buffer2, ss_socket);
                }
            }

        }
        else if (strncmp(buffer, "touch", strlen("touch")) == 0) {
            create_file(args, buffer2);
        } else if (strncmp(buffer, "rmdir", strlen("rmdir")) == 0) {
            delete_directory(args, buffer2);
        } else if (strncmp(buffer, "rm", strlen("rm")) == 0) {



        int lock_array_index = doesLockRequestedFileAlreadExist(args[1]);
        if (lock_array_index == -1)
        {
            init_lock(&conc_array[num_of_requests], args[1]);
            lock_array_index = num_of_requests;
            num_of_requests++;
        }
        acquire_delete_lock(&conc_array[lock_array_index]);
            sleep(5);
            remove_file(args, buffer2);
            
        release_delete_lock(&conc_array[lock_array_index]);

        } else if (strncmp(buffer, "mkdir", strlen("mkdir")) == 0) {
            create_directory(args, buffer2);
        } else if (strncmp(buffer, "write", strlen("write")) == 0) {
            printf("I SHOULD NOT COME HERE!!\n");
            write_file(buffer, buffer2);


        } else if (strncmp(buffer, "reindex", strlen("reindex")) == 0) {
            // Execute `tree` command and print the tree of root path
            indexSubFolder(ROOT_PATH, sock);
            free(buffer2);
            free(tempBuffer);
            continue;
        } else if (strncmp(buffer, "copysame", strlen("copysame")) == 0) {
            // Copy within the SS itself
            printf("Starting to copysame on command %s\n", buffer);
            copy_same(buffer, buffer2);
        }
        else if (strncmp(buffer, "lcopy", strlen("lcopy")) == 0) {
            // Copy between SS
            printf("Starting to copydifferent on command %s\n", buffer);
            // "copydifferent ./foldertest ./testfolder 127.0.0.1 58303" this is how the input comes
            //send "sscopy" to the destination server
            char *command = strdup(buffer);
            char *token;
            char *ip = NULL;
            int port;
            // Tokenize the command string
            token = strtok(command, " ");
            int count = 0;

            while (token != NULL) {
                count++;

                // Extract the 4th token as IP address
                if (count == 4) {
                    ip = token;
                }
                    // Extract the 5th token as port number
                else if (count == 5) {
                    port = atoi(token);
                }

                // Move to the next token
                token = strtok(NULL, " ");
            }
            printf("Resolved the SS to %s:%d\n", ip, port);
            int ss_socket = ss_info_to_socket(ip, port);
            if (ss_socket == -1) {
                printf("Could not connect to the destination server\n");
                strcpy(buffer2, "Could not connect to the destination server\n");
            } else {
                if (send_good(ss_socket, "sscopy", strlen("sscopy")) < 0) {
                    // perror("Failed to send message to client");
                    printErrorDetails(52);
                    strcpy(buffer2, "Failed to send message to client\n");
                } else {
                    copy_different_src(buffer, buffer2, ss_socket,1);
                }
            }

        }
        else if (strncmp(buffer, "copydifferent", strlen("copydifferent")) == 0) {
            // Copy between SS
            printf("Starting to copydifferent on command %s\n", buffer);
            // "copydifferent ./foldertest ./testfolder 127.0.0.1 58303" this is how the input comes
            //send "sscopy" to the destination server
            char *command = strdup(buffer);
            char *token;
            char *ip = NULL;
            int port;
            // Tokenize the command string
            token = strtok(command, " ");
            int count = 0;

            while (token != NULL) {
                count++;

                // Extract the 4th token as IP address
                if (count == 4) {
                    ip = token;
                }
                    // Extract the 5th token as port number
                else if (count == 5) {
                    port = atoi(token);
                }

                // Move to the next token
                token = strtok(NULL, " ");
            }
            printf("Resolved the SS to %s:%d\n", ip, port);
            int ss_socket = ss_info_to_socket(ip, port);
            if (ss_socket == -1) {
                printf("Could not connect to the destination server\n");
                strcpy(buffer2, "Could not connect to the destination server\n");
            } else {
                if (send_good(ss_socket, "sscopy", strlen("sscopy")) < 0) {
                    perror("Failed to send message to client");
                    strcpy(buffer2, "Failed to send message to client\n");
                } else {
                    copy_different_src(buffer, buffer2, ss_socket,0);
                }
            }

        }


        if (send_good(sock, buffer2, strlen(buffer2)) < 0) {
            // perror("Failed to send message to client");
            printErrorDetails(52);
        }
        free(buffer2);
        free(tempBuffer);
    }
}

void listen_to_client(int server_sock) {
    while (1) {
        if (listen(server_sock, 5) < 0) {
            // perror("Listen failed");
            printErrorDetails(40);
            close(server_sock);
            exit(EXIT_FAILURE);
        }

        int client_sock = accept(server_sock, NULL, NULL);
        if (client_sock < 0) {
            // perror("Failed to accept client connection");
            printErrorDetails(42);
            continue;
        }
        printf("Got a client\n");
        pthread_t client_thread;
        if (pthread_create(&client_thread, NULL, (void *(*)(void *)) process_client, (void *) (intptr_t) client_sock) !=
            0) {
            // perror("Failed to create thread for client");
            printErrorDetails(43);
            close(client_sock);
        }
    }
}

// Function to connect to the Naming Server and listen for messages
void *connect_to_nm(void *args) {
    ConnectToNamingServerArgs *threadArgs = (ConnectToNamingServerArgs *) args;
    const char *nm_ip = threadArgs->nm_ip;
    StorageServerInfo *ssi = threadArgs->ssi;

    int sock;
    struct sockaddr_in nm_addr;

    // Create the socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        // perror("Socket creation failed");
        printErrorDetails(7);
        exit(EXIT_FAILURE);
    }
    int flag = 1;
    if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof(int)) < 0) {
        // So that the socket does not get buffered causing a lot of problems
        // printf("setsockopt TCP_NODELAY failed");
        printErrorDetails(44);
        close(sock);
        return NULL;
    }
    // Set up Naming Server address
    nm_addr.sin_family = AF_INET;
    nm_addr.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, nm_ip, &nm_addr.sin_addr) <= 0) {
        // perror("Invalid IP address");
        printErrorDetails(17);
        close(sock);
        exit(EXIT_FAILURE);
    }

    // Connect to Naming Server
    if (connect(sock, (struct sockaddr *) &nm_addr, sizeof(nm_addr)) < 0) {
        // perror("Connection to Naming Server failed");
        printErrorDetails(55);
        close(sock);
        exit(EXIT_FAILURE);
    }
    printf("Connected to Naming Server at %s:%d\n", nm_ip, SERVER_PORT);

    if (send_good(sock, (void *) ssi, sizeof(StorageServerInfo)) == -1) {
        // perror("Send failed");
        printErrorDetails(10);
        printErrorDetails(52);
        exit(EXIT_FAILURE);
    }
    printf("Starting to index files\n");
    indexSubFolder(ROOT_PATH, sock);
    listen_to_nm(sock);

    close(sock);
    return NULL;
}

int validate_path(char* path) {
    if (path[0] != '.' || path[1] != '/') {
        return -1;
    }
    struct stat buffer;
    if (stat(path, &buffer) == 0) {
        return 1; // Path exists
    } else {
        return -2; // Path does not exist
    }
}


int main(int argc, char *argv[]) {
    signal(SIGTSTP, handle_ctrl_z);

    if (argc < 3) {
        fprintf(stderr, "Usage: %s <NM_IP> <ROOT_PATH>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int path_var = validate_path(argv[2]);
    if(path_var == -1) {
        printf(RED"Invalid format! please re-check the path and start ss again!\n"RESET);
        return 1;
    } else if(path_var == -2) {
        printf(RED"Path doesn't exist!! please re-check the path and start ss again!\n"RESET);
        return 1;
    }

    ROOT_PATH = malloc(strlen(argv[2]) + 1);
    ROOT_PATH = argv[2];
    // Create code to first rm -rf "backupfolderforss" and then create a new folder "backupfolderforss"

    if (system("mkdir -p backupfolderforss") == -1) {
        // perror("Failed to create backupfolderforss");
        printErrorDetails(56);
        exit(EXIT_FAILURE);
    }


    // Parse command-line arguments
    nm_ip = argv[1];

    StorageServerInfo ssi;

    ConnectToNamingServerArgs args;

    args.nm_ip = nm_ip;
    args.ssi = &ssi;
    pthread_t nm_thread;

    if (pthread_create(&nm_thread, NULL, connect_to_nm, &args)) {
        // perror("Failed to create thread for Naming Server connection");
        printErrorDetails(43);
        exit(EXIT_FAILURE);
    }

    // Create the server socket
    int server_sock;
    struct sockaddr_in server_addr;

    // Create the socket
    if ((server_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        // perror("Socket creation failed");
        printErrorDetails(7);
        exit(EXIT_FAILURE);
    }

    // Set up the server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = 0; //To auto assign the ports

    // Bind the socket to the address
    if (bind(server_sock, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        // perror("Bind failed");
        printErrorDetails(41);
        close(server_sock);
        exit(EXIT_FAILURE);
    }
    socklen_t addr_len = sizeof(server_addr);
    //Getting the details back
    if (getsockname(server_sock, (struct sockaddr *) &server_addr, &addr_len) < 0) {
        // perror("getsockname failed");
        printErrorDetails(57);
        close(server_sock);
        exit(EXIT_FAILURE);
    }
    ssi.portClient = ntohs(server_addr.sin_port);

    // Start listening to clients
    printf("Storage server listening for clients on port %d\n", ssi.portClient);
    listen_to_client(server_sock);
    free(ROOT_PATH);
    return 0;
}
