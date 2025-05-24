#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include <signal.h>

#define NAMING_SERVER_IP "127.0.0.1" // IP address of the Naming Server
#define NAMING_SERVER_PORT 9000    // Port of the Naming Server
#define BUFFER_SIZE 256            // Buffer size for data transfer
#define MAX_PATH 256
#define MAX_FILES 100

#define INVALID_COMMAND -20
#define NO_PATH -21
#define NO_DATA -22
#define NO_DEST -23
#define NO_FILENAME -24
#define INVALID_PATH -25
#define VALID 17

#define ACK_DONE 1
#define ACK_FAIL 0
#define DIR_FAIL_ACK 2
#define AWRITE_ACK 3


#pragma pack(push,1)
typedef struct senddetails
{
    int clientport;
    char ip[20];
} senddetails;
#pragma pack(pop)
typedef struct trienode
{
    char charac;
    struct trienode *childchar[2000];
    int ispath;
} trienode;
#pragma pack(push,1)

typedef struct SS_details
{
    int id;
    int ssport;
    int clientport;
    char ip[20];
    int ss_socket;
    trienode *root;
} SS_details;
#pragma pack(pop)
#pragma pack(push,1)
typedef struct SS_response
{
    int ssport;
    int clientport;
    char ip[20];
    int path_count;
    char paths[MAX_FILES][MAX_PATH];
} SS_response;
#pragma pack(pop)

// int NAMING_SERVER_PORT;
// char* NAMING_SERVER_IP;
int sock;
void handle_signal(int signum) {
    close(sock);
    exit(0);
}

int handlerrorcommands(char buffer[BUFFER_SIZE])
{
    if(buffer == NULL)
    {
        printf("No command is entered\n");
        return 0;
    }
    char* token = strtok(buffer," ");
    if(strcmp("LIST",token) == 0)
    return 1;
    token = strtok(NULL," ");
    if(token == NULL)
    {
        printf("Path is not specified\n");
        return 0;
    }
    if(strcmp("READ", token) == 0 || strcmp("DELETE",token) == 0 || strcmp("STREAM",token) == 0)
    {
       return 1;
    }
    if(strcmp("WRITE", token) == 0)
    {
        token = strtok(NULL," ");
        if(token == NULL)
        {
            printf("Data is given\n");
            return 0;
        }
        return 1;
    }
    if(strcmp("CREATE",token) == 0) 
    {
        token = strtok(NULL," ");
        if(token == NULL)
        {
            printf("Name is not specified\n");
            return 0;
        }
        return 1;
    }
    if(strcmp("COPY",token) == 0) 
    {
        token = strtok(NULL," ");
        if(token == NULL)
        {
            printf("Dest is not specified\n");
            return 0;
        }
        return 1;
    }
    printf("No such command\n");
    return 0;
}

int main()
{
// int main(int argc,char* argv[])
// {
//     if(argc != 3)
//     {
//         return -1;
//     }
//     NAMING_SERVER_IP = argv[1];
//     NAMING_SERVER_PORT = atoi(argv[2]);
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    struct sockaddr_in nm_addr;
    char buffer[BUFFER_SIZE];

    // Create socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("setsockopt failed");
        close(sock);
        exit(EXIT_FAILURE);
    } 

    // Configure the Naming Server address
    nm_addr.sin_family = AF_INET;
    nm_addr.sin_port = htons(NAMING_SERVER_PORT);

    // Convert IP address from text to binary form
    if (inet_pton(AF_INET, NAMING_SERVER_IP, &nm_addr.sin_addr) <= 0)
    {
        perror("Invalid address or Address not supported");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // Connect to the Naming Server
    if (connect(sock, (struct sockaddr *)&nm_addr, sizeof(nm_addr)) < 0)
    {
        perror("Connection to Naming Server failed");
        close(sock);
        exit(EXIT_FAILURE);
    }

    printf("Connected to Naming Server at %s:%d\n", NAMING_SERVER_IP, NAMING_SERVER_PORT);

    // Send a request message to the Naming Server
    while (1)
    {
        char input[BUFFER_SIZE];
        printf("Enter your Request: ");
        fgets(input, BUFFER_SIZE, stdin);

        char tempinput1[BUFFER_SIZE];
        strcpy(tempinput1, input);

        if (strcmp(input, "Stop") == 0)
            break;

        send(sock, input, strlen(input), 0);

        int response;
        recv(sock,&response,sizeof(response),0);
        switch (response) {

        case INVALID_COMMAND:
            printf("Error: Invalid command provided.\n");
            break;
        case NO_PATH:
            printf("Error: No path specified.\n");
            break;
        case NO_DATA:
            printf("Error: No data available.\n");
            break;
        case NO_DEST:
            printf("Error: No destination specified.\n");
            break;
        case NO_FILENAME:
            printf("Error: No filename provided.\n");
            break;
        case INVALID_PATH:
            printf("Error: The provided path is invalid.\n");
            break;
    }


        char tempinput[BUFFER_SIZE];
        strcpy(tempinput, input);
        

        char *token = strtok(tempinput, " ");
        printf("%s\n", token);
        int async = 0;
        if (strncmp(token, "LIST",4) == 0) {
            
            printf("\nAvailable paths:\n");
            printf("----------------------------------------\n");
            
            char response[BUFFER_SIZE * MAX_FILES] = {0};
            size_t total = 0;
            bool done = false;
            
            while (!done && total < sizeof(response) - 1) {
                char chunk[BUFFER_SIZE] = {0};
                ssize_t received = recv(sock, chunk, BUFFER_SIZE - 1, 0);
                if (received <= 0) break;
                
                if (strstr(chunk, "[EOF]")) {
                    done = true;
                    *strstr(chunk, "[EOF]") = '\0';
                }
                
                size_t chunk_len = strlen(chunk);
                if (total + chunk_len < sizeof(response)) {
                    memcpy(response + total, chunk, chunk_len);
                    total += chunk_len;
                }
            }
            
            if (total > 0) {
                response[total] = '\0';
                char *path = strtok(response, "\n");
                while (path) {
                    if (*path == '/') {  // Only print paths that start with /
                        printf("  %s\n", path);
                    }
                    path = strtok(NULL, "\n");
                }
            }
            
            printf("----------------------------------------\n\n");
            continue;
        }

       
        if (strcmp("DELETE", token) == 0)
        {
            int ack;
            recv(sock,&ack,sizeof(ack),0);
            if(ack == ACK_FAIL)
            {
                printf("delete unsuccessful\n");
            }
            else if(ack == ACK_DONE)
            {
                printf("file is deleted\n");
            }
            else if(ack == ACK_FAIL)
            {
                printf("Directory is deleted");
            }
        }
        if(strcmp("COPY",token)==0)
        {
            // it receives the acknowledgement from naming server 
             int ack;
            recv(sock,&ack,sizeof(ack),0);
            if(ack == ACK_FAIL)
            {
                printf("copy unsuccessful\n");
            }
            else if(ack == ACK_DONE)
            {
                printf("copy successful\n");
            }
        }
        if(strcmp("CREATE",token) == 0)
        {
            int ack;
            recv(sock,&ack,sizeof(ack),0);
            if(ack == ACK_FAIL)
            {
                printf("create unsuccessful\n");
            }
            else if(ack == ACK_DONE)
            {
                printf("create successful\n");
            }
        }
        if (strcmp("READ", token) == 0 || strcmp("WRITE", token) == 0 || strcmp("STREAM", token) == 0 ||  strcmp("AWRITE", token) == 0 || strcmp("INFO", token) == 0)
        {
            senddetails s;
            ssize_t bytes_received = recv(sock, &s, sizeof(s), 0);
            if (bytes_received <= 0)
            {
                perror("recv failed");
                // Handle the error, e.g., by exiting or retrying
                return 0;
            }
            printf("%s\n", s.ip);
            //  use ssd.clientport ssd.ip to connect to the storage server
            struct sockaddr_in ss_addr;
            int ss_sock = socket(AF_INET, SOCK_STREAM, 0);
            if (ss_sock < 0)
            {
                perror("Storage Server socket creation failed");
                exit(EXIT_FAILURE);
            }

            ss_addr.sin_family = AF_INET;
            ss_addr.sin_port = htons(s.clientport);
            printf("%s\n", s.ip);
            if (inet_pton(AF_INET, s.ip, &ss_addr.sin_addr) <= 0)
            {
                perror("Invalid Storage Server IP 111");
                close(ss_sock);
                exit(EXIT_FAILURE);
            }

            if (connect(ss_sock, (struct sockaddr *)&ss_addr, sizeof(ss_addr)) < 0)
            {
                perror("Connection to Storage Server failed");
                close(ss_sock);
                exit(EXIT_FAILURE);
            }
            send(ss_sock, input, sizeof(input), 0);
            printf("Message sneding to ss: %s\n", input);

            if (strcmp(token, "READ") == 0)
            {
                char file_buffer[BUFFER_SIZE];
                printf("File contents from Storage Server:\n");
                while (1)
                {
                    int bytes_received = recv(ss_sock, file_buffer, sizeof(file_buffer) - 1, 0);
                    if (bytes_received <= 0)
                        break;

                    file_buffer[bytes_received] = '\0';

                    if (strcmp(file_buffer, "[EOF]") == 0)
                        break;

                    printf("%s", file_buffer);
                }
            }
            if (strcmp(token, "AWRITE") == 0)
            {
                int ack = ACK_FAIL;
                recv(sock,&ack,sizeof(ack),0);
                if(ack == AWRITE_ACK)
                {
                    printf("Asynchronous write is started\n");
                }
            }
            if (strcmp(token, "INFO") == 0)
            {
                char metadata_buffer[BUFFER_SIZE];
                printf("File metadata from Storage Server:\n");

                while (1)
                {
                    int bytes_received = recv(ss_sock, metadata_buffer, sizeof(metadata_buffer) - 1, 0);
                    if (bytes_received <= 0)
                    {
                        perror("Error receiving metadata");
                        break;
                    }

                    metadata_buffer[bytes_received] = '\0'; // Null-terminate the received data

                    if (strcmp(metadata_buffer, "[EOF]") == 0)
                        break; // End of metadata

                    printf("%s", metadata_buffer); // Print metadata
                }
            }
            if(strcmp(token,"WRITE") == 0)
            {
                int ack;
                recv(ss_sock,&ack,sizeof(ack),0);
                if(ack == 11)
                {
                    printf("async write is started\n");
                }
                else if(ack == 10)
                {
                    printf("write successful\n");
                }
            }
            if (strcmp("STREAM", token) == 0) {
                printf("Entered stream\n");

                FILE *mpv = popen("mpv --no-terminal -", "w");
                if (!mpv) {
                    perror("Failed to open mpv");
                    close(ss_sock);
                    exit(EXIT_FAILURE);
                }

                char file_buffer[BUFFER_SIZE];
                while ((bytes_received = recv(ss_sock, file_buffer, sizeof(file_buffer), 0)) > 0) {
                    fwrite(file_buffer, 1, bytes_received, mpv);
                }

                pclose(mpv);
                close(ss_sock);
            }
            printf("\n");
            close(ss_sock);
        }
    }

    return 0;
}