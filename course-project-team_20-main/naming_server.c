#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define PORT 8080
#define CLIENT_PORT 9000
#define MAX_SS 3
#define MAX_CLIENT 5
#define BUFFER_SIZE 256
#define MAX_PATH 256
#define MAX_FILES 100
#define LRU_SIZE 5

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

#define ALL_MAX_FILES 300
#define MIN(a, b) ((a) < (b) ? (a) : (b))

char all_paths[300][256];
int totalpaths = 0;

typedef struct LRUNODE
{
    int id;
    char path[MAX_PATH];
    struct LRUNODE *next;
} LRUNODE;

typedef struct trienode
{
    char charac;
    struct trienode *childchar[2000];
    int ispath;
} trienode;
#pragma pack(push, 1)
typedef struct SS_details
{
    int id;
    int ssport;
    int clientport;
    char ip[20];
    int ss_socket;
    trienode *root;
    int pathcount;
} SS_details;
#pragma pack(pop)
#pragma pack(push, 1)
typedef struct SS_response
{
    int ssport;
    int clientport;
    char ip[20];
    int path_count;
} SS_response;

typedef struct SS_responsed
{
    int ssport;
    int clientport;
    char ip[20];
} SS_responsed;

#pragma pack(pop)

typedef struct client_details
{
    int id;
    int clientsocket;
} client_details;

#pragma pack(push, 1)
typedef struct senddetails
{
    int clientport;
    char ip[20];
} senddetails;
#pragma pack(pop)
// Global variables with initialization
SS_details ssd[MAX_SS] = {0};        // Initialize all members to 0
client_details cd[MAX_CLIENT] = {0}; // Initialize all members to 0

#define LOG_FILE "naming_server.log"
#define INET_ADDRSTR_LEN 16
FILE *log_file = NULL;

LRUNODE *head = NULL;

int handlerrorcommands(char buffer[BUFFER_SIZE])
{
    if (strcmp(buffer, "\n") == 0)
    {
        return INVALID_COMMAND;
    }
    char *token = strtok(buffer, " ");
    if (strcmp("LIST", token) == 0)
    {
        return VALID;
    }
    if (strcmp("READ", token) == 0 || strcmp("DELETE", token) == 0 || strcmp("STREAM", token) == 0 || strcmp("WRITE", token) == 0 || strcmp("COPY", token) == 0)
    {
        char temp[BUFFER_SIZE];
        strcpy(temp, token);
        token = strtok(NULL, " ");
        if (token == NULL)
        {
            return NO_PATH;
        }
        token = strtok(NULL, " ");
        if (token == NULL && strcmp("WRITE", temp) == 0)
        {
            return NO_DATA;
        }
        if (token == NULL && strcmp("COPY", temp) == 0)
        {
            return NO_DEST;
        }
        if (token == NULL && strcmp("CREATE", temp) == 0)
        {
            return NO_FILENAME;
        }
        return VALID;
    }
}

// void PRINT_LRU()
// {
//     LRUNODE *temp = head;

//     if (temp == NULL)
//     {
//         printf("LRU Cache is empty.\n");
//         return;
//     }

//     printf("LRU Cache Contents:\n");
//     printf("--------------------\n");
//     while (temp != NULL)
//     {
//         printf("ID: %d, Path: %s\n", temp->id, temp->path);
//         temp = temp->next;
//     }
//     printf("--------------------\n");
// }

// Initialize a new trie node

trienode *createNode()
{
    trienode *newNode = (trienode *)malloc(sizeof(trienode));
    if (newNode == NULL)
    {
        perror("Failed to allocate memory for trie node");
        return NULL;
    }

    newNode->charac = '\0';
    newNode->ispath = 0;

    for (int i = 0; i < 2000; i++)
    {
        newNode->childchar[i] = NULL;
    }

    return newNode;
}

void trieinsert(char path[MAX_PATH], trienode *root)
{
    if (!root || !path)
    {
        printf("Invalid root or path in trieinsert\n");
        return;
    }

    trienode *curr = root;
    int len = strlen(path);
    int i = 0;
    for (; i < len && i < MAX_PATH; i++)
    {
        int index = (unsigned char)path[i] % 2000;

        if (curr->childchar[index] == NULL)
        {
            curr->childchar[index] = createNode();
            if (curr->childchar[index] == NULL)
            {
                printf("Failed to create trie node for character '%c'\n", path[i]);
                return;
            }
            curr->childchar[index]->charac = path[i];
            printf("Inserted character: %c\n", curr->childchar[index]->charac);
        }
        else if (!curr->childchar[index])
        {
            printf("Unexpected NULL child after creation for character %c\n", path[i]);
            return;
        }

        curr = curr->childchar[index];
    }

    if (len == i)
    {
        curr->ispath = 1;
        printf("Path inserted successfully: %s\n", path);
    }
    else
    {
        printf("Path not fully inserted: %s\n", path);
    }
}

int search_file(char path[MAX_PATH])
{
    if (!path)
    {
        return 0;
    }

    int len = strlen(path);
    if (len >= MAX_PATH)
    {
        return 0;
    }

    for (int i = 0; i < MAX_SS; i++)
    {
        if (ssd[i].root == NULL)
        {
            continue;
        }
        int k = 0;
        trienode *curr = ssd[i].root;
        int j;

        for (j = 0; j < len; j++)
        {
            int index = (unsigned char)path[j] % 2000;

            if (curr->childchar[index] && curr->childchar[index]->charac == path[j])
            {
                curr = curr->childchar[index];
            }
            else
            {
                k = 1;
                break;
            }
        }
        if (k == 1)
        {
            continue;
        }

        if (j == len && curr && curr->ispath)
        {
            return i + 1;
        }
    }
    return 0;
}

void freeTrie(trienode *root)
{
    if (root == NULL)
        return;

    for (int i = 0; i < 2000; i++)
    {
        if (root->childchar[i] != NULL)
        {
            freeTrie(root->childchar[i]);
        }
    }

    free(root);
}

void deleteSubtreeFromSlash(trienode *node)
{
    if (!node)
        return;

    // First recursively delete all children
    for (int i = 0; i < 2000; i++)
    {
        if (node->childchar[i])
        {
            deleteSubtreeFromSlash(node->childchar[i]);
            node->childchar[i] = NULL;
        }
    }

    // Free the node itself
    free(node);
}

int hasChildren(trienode *node)
{
    if (!node)
        return 0;
    for (int i = 0; i < 2000; i++)
    {
        if (node->childchar[i])
        {
            return 1;
        }
    }
    return 0;
}

void triedelete(char path[MAX_PATH], int num, int id)
{
    if (!path || id < 1 || id > MAX_SS || !ssd[id - 1].root)
        return;

    trienode *curr = ssd[id - 1].root;
    trienode *path_nodes[MAX_PATH];
    int path_indices[MAX_PATH];
    int depth = 0;
    int path_len = strlen(path);

    // Navigate to target node while storing path
    for (int i = 0; i < path_len; i++)
    {
        int index = (unsigned char)path[i] % 2000;
        if (!curr->childchar[index] || curr->childchar[index]->charac != path[i])
        {
            return; // Path not found
        }
        path_nodes[depth] = curr;
        path_indices[depth] = index;
        depth++;
        curr = curr->childchar[index];
    }

    if (!curr)
        return;

    if (num == 1)
    { // Folder deletion
        int slash_index = '/' % 2000;
        int has_other_children = 0;

        // Check for non-slash children
        for (int i = 0; i < 2000; i++)
        {
            if (i != slash_index && curr->childchar[i])
            {
                has_other_children = 1;
                break;
            }
        }

        // Handle slash subtree if it exists
        if (curr->childchar[slash_index])
        {
            deleteSubtreeFromSlash(curr->childchar[slash_index]);
            curr->childchar[slash_index] = NULL;
        }

        // If no other children exist, we can delete upward
        if (!has_other_children)
        {
            curr->ispath = 0;
            for (int i = depth - 1; i >= 0; i--)
            {
                trienode *parent = path_nodes[i];
                int child_index = path_indices[i];

                free(parent->childchar[child_index]);
                parent->childchar[child_index] = NULL;

                if (parent->ispath || hasChildren(parent))
                {
                    break;
                }
            }
        }
        else
        {
            curr->ispath = 0;
        }
    }
    else if (num == 2)
    { // Regular deletion
        // First check if node has children
        if (!hasChildren(curr))
        {
            curr->ispath = 0;
            // Delete upward until we find a node that's a path or has other children
            for (int i = depth - 1; i >= 0; i--)
            {
                trienode *parent = path_nodes[i];
                int child_index = path_indices[i];

                free(parent->childchar[child_index]);
                parent->childchar[child_index] = NULL;

                if (parent->ispath || hasChildren(parent))
                {
                    break;
                }
            }
        }
        else
        {
            // Just mark as not a path if it has children
            curr->ispath = 0;
        }
    }
}

void collect_paths(trienode *root, char *current_path, int depth, char *result, size_t max_result)
{
    if (!root || depth >= MAX_PATH - 1)
        return;

    if (root->ispath)
    {
        current_path[depth] = '\0';
        size_t current_len = strlen(result);
        size_t path_len = strlen(current_path);

        // Add path with proper formatting
        if (current_len + path_len + 2 < max_result)
        { // +2 for newline and null terminator
            if (current_len > 0)
            {
                strcat(result, "\n");
            }
            // Ensure path starts with /
            if (current_path[0] != '/')
            {
                strcat(result, "/");
            }
            strcat(result, current_path);
        }
    }

    for (int i = 0; i < 2000; i++)
    {
        if (root->childchar[i])
        {
            current_path[depth] = root->childchar[i]->charac;
            collect_paths(root->childchar[i], current_path, depth + 1, result, max_result);
        }
    }
}

void delete(char path[MAX_PATH])
{
    int temp = 0;
    for (int i = 0; i < totalpaths; i++)
    {
        if (strcmp(all_paths[i], path) == 0)
        {
            temp = i;
            break;
        }
    }
    for (int i = temp; i < totalpaths - 1; i++)
    {
        strcpy(all_paths[i], all_paths[i + 1]);
    }
    totalpaths--;
}
void create(char path[MAX_PATH])
{
    strcpy(all_paths[totalpaths++], path);
}

void REPLACE_LRU(char path[MAX_PATH], int id)
{
    LRUNODE *temp = (LRUNODE *)malloc(sizeof(LRUNODE));
    temp->id = id;
    temp->next = NULL;
    strcpy(temp->path, path);
    LRUNODE *temp1 = head;
    head = temp;
    head->next = temp1;
    LRUNODE *temp2 = head;
    for (int i = 0; i < 8; i++)
    {
        temp2 = temp2->next;
    }
    LRUNODE *temp3 = temp2->next;
    free(temp3);
    temp2->next = NULL;
    return;
}

void ADD_LRU(char path[MAX_PATH], int num, int id)
{
    if (num == 1)
    {
        head = (LRUNODE *)malloc(sizeof(LRUNODE));
        head->id = id;
        head->next = NULL;
        strcpy(head->path, path);
        return;
    }
    if (num == 0)
    {
        LRUNODE *temp = head;
        int nodes = 0;
        while (temp->next != NULL)
        {
            temp = temp->next;
            nodes++;
        }

        if (nodes < LRU_SIZE)
        {
            temp->next = (LRUNODE *)malloc(sizeof(LRUNODE));
            temp->next->id = id;
            temp->next->next = NULL;
            strcpy(temp->next->path, path);
        }
        else
        {
            REPLACE_LRU(path, id);
        }
        return;
    }
}

int SEARCH_LRU(char path[MAX_PATH])
{
    // PRINT_LRU();
    LRUNODE *temp = head;
    if (temp == NULL)
    {
        int id = search_file(path);
        ADD_LRU(path, 1, id);
        // PRINT_LRU();
        return id;
    }
    else
    {
        LRUNODE *temp = head;
        while (temp != NULL)
        {
            if (strcmp(path, temp->path) == 0)
            {
                return temp->id;
            }
            temp = temp->next;
        }
        int id = search_file(path);

        ADD_LRU(path, 0, id);

        // PRINT_LRU();
        return id;
    }
}
void traverse_and_collect(trienode *node, char *current_path, int depth, int storage_sock)
{
    if (node == NULL)
    {
        return;
    }

    if (node->ispath)
    {
        current_path[depth] = '\0';
        send(storage_sock, &current_path, MAX_PATH, 0);
    }

    for (int i = 0; i < 2000; i++)
    {
        if (node->childchar[i] != NULL)
        {
            current_path[depth] = node->childchar[i]->charac;
            traverse_and_collect(node->childchar[i], current_path, depth + 1, storage_sock);
        }
    }
}

void send_paths(int id, int storage_sock)
{
    if (id < 0 || id >= MAX_SS)
    {
        printf("Invalid SSD id\n");
        return;
    }

    trienode *root = ssd[id].root;
    char current_path[MAX_PATH];
    traverse_and_collect(root, current_path, 0, storage_sock);
}

void init_logger()
{
    log_file = fopen(LOG_FILE, "a");
    if (!log_file)
    {
        perror("Failed to open log file");
        exit(EXIT_FAILURE);
    }
}

void log_message(const char *level, const char *message)
{
    if (!log_file)
        return;

    time_t now;
    time(&now);
    char *time_str = ctime(&now);
    time_str[strlen(time_str) - 1] = '\0'; // Remove newline

    fprintf(log_file, "[%s] %s: %s\n", time_str, level, message);
    fflush(log_file);
}

void log_command(const char *command)
{
    if (!log_file)
        return;

    time_t now;
    time(&now);
    char *time_str = ctime(&now);
    time_str[strlen(time_str) - 1] = '\0';

    fprintf(log_file, "[%s] COMMAND  - %s\n",
            time_str, command);
    fflush(log_file);
}

void close_logger()
{
    if (log_file)
    {
        fclose(log_file);
        log_file = NULL;
    }
}

void *acceptss(void *args)
{
    int ss_socket = *((int *)args);
    struct sockaddr_in ss_addr;
    socklen_t addr_size = sizeof(ss_addr);
    int ss_count = 0;

    while (ss_count < MAX_SS)
    {
        int new_ss_socket = accept(ss_socket, (struct sockaddr *)&ss_addr, &addr_size);
        if (new_ss_socket < 0)
        {
            perror("Failed to accept Storage Server connection");
            log_message("ERROR", "Failed to accept Storage Server connection");
            continue;
        }
        SS_responsed ssrd = {0};
        ssize_t bytes_received = recv(new_ss_socket, &ssrd, sizeof(ssrd), 0);
        if (bytes_received <= 0)
        {
            perror("Failed to receive Storage Server information");
            freeTrie(ssd[ss_count].root);
            ssd[ss_count].root = NULL;
            close(new_ss_socket);
            continue;
        }
        int check = 0;
        for (int i = 0; i < MAX_SS; i++)
        {
            if (strcmp(ssd[i].ip, ssrd.ip) == 0)
            {
                check = i + 1;
                break;
            }
        }
        printf("%d\n", check);
        send(new_ss_socket, &check, sizeof(check), 0);
        if (check == 0)
        {
            trienode *new_root = createNode();
            if (!new_root)
            {
                perror("Failed to create trie root");
                close(new_ss_socket);
                continue;
            }

            // Initialize storage server details
            ssd[ss_count].root = new_root;
            ssd[ss_count].id = ss_count + 1;
            ssd[ss_count].ss_socket = new_ss_socket;

            // Receive storage server information
            SS_response ssr = {0};

            printf("Accepted Storage Server %d connection\n", ssd[ss_count].id);
            ssize_t bytes_received = recv(new_ss_socket, &ssr, sizeof(ssr), 0);
            ssd[ss_count].pathcount = ssr.path_count;
            char log_buf[256];
            snprintf(log_buf, sizeof(log_buf), "Accepted Storage Server connection %d", ss_count + 1);
            log_message("INFO", log_buf);
            if (bytes_received <= 0)
            {
                perror("Failed to receive Storage Server information");
                freeTrie(ssd[ss_count].root);
                ssd[ss_count].root = NULL;
                close(new_ss_socket);
                continue;
            }

            if (ssr.path_count < 0 || ssr.path_count > MAX_FILES)
            {
                printf("Invalid path count received: %d\n", ssr.path_count);
                freeTrie(ssd[ss_count].root);
                ssd[ss_count].root = NULL;
                close(new_ss_socket);
                continue;
            }

            printf("ip: %s\n", ssr.ip);
            ssd[ss_count].ssport = ssr.ssport;
            ssd[ss_count].clientport = ssr.clientport;
            strcpy(ssd[ss_count].ip, ssr.ip);
            printf("ip:%d %s\n", ss_count, ssd[ss_count].ip);

            printf("Storage Server %d configured with port %d\n", ssd[ss_count].id, ssd[ss_count].ssport);

            // Insert paths into the trie
            printf("Receiving and Inserting %d paths for Storage Server %d\n", ssr.path_count, ssd[ss_count].id);

            for (int i = 0; i < ssr.path_count; i++)
            {
                char path_buffer[MAX_PATH];
                ssize_t path_bytes = recv(new_ss_socket, path_buffer, MAX_PATH, MSG_WAITALL);

                if (path_bytes <= 0)
                {
                    printf("Failed to receive path %d\n", i);
                    continue;
                }

                path_buffer[path_bytes - 1] = '\0';
                trieinsert(path_buffer, ssd[ss_count].root);
                create(path_buffer);
            }
            ss_count++;
            printf("Inserted Paths\n");
            log_message("INFO", "INSERTED PATHS OF STORAGE SERVER");
        }
        else
        {
            printf("Establishing Reconnection with Storage Server with id:%d\n", check);
            send(new_ss_socket, &ssd[check].pathcount, sizeof(int), 0);
            send_paths(check, new_ss_socket);
        }
    }

    return NULL;
}
void *handleclient(void *args)
{
    int client_socket = *((int *)args);
    char buffer[BUFFER_SIZE];
    char log_buf[512];
    while (1)
    {
        int bytes_received = recv(client_socket, buffer, BUFFER_SIZE, 0);
        if (bytes_received > 0)
        {

            snprintf(log_buf, sizeof(log_buf), "Received client request: %s", buffer);
            log_command(buffer);
            log_message("INFO", log_buf);
            printf("Receiving Client Request\n");
            char tempbuffer[BUFFER_SIZE];
            strcpy(tempbuffer, buffer);

            int response = handlerrorcommands(tempbuffer);

            if (response != VALID)
            {
                send(client_socket, &response, sizeof(response), 0);
            }

            buffer[bytes_received] = '\0';
            printf("Received from client: %s\n", buffer);

            char dupbuffer[BUFFER_SIZE];
            strcpy(dupbuffer, buffer);
            char *command = strtok(dupbuffer, " \n"); // Split on both space and newline
            if (command && strncmp(command, "LIST", 4) == 0)
            {
                send(client_socket, &response, sizeof(response), 0);
                char *result = calloc(BUFFER_SIZE * MAX_FILES, 1);
                char *current_path = calloc(MAX_PATH, 1);

                if (!result || !current_path)
                {
                    const char *error = "Memory allocation failed";
                    send(client_socket, error, strlen(error), 0);
                    send(client_socket, "[EOF]", 5, 0);
                    free(result);
                    free(current_path);
                    continue;
                }

                // Collect paths from all storage servers
                for (int i = 0; i < MAX_SS; i++)
                {
                    if (ssd[i].root != NULL)
                    {
                        collect_paths(ssd[i].root, current_path, 0, result, BUFFER_SIZE * MAX_FILES);
                    }
                }

                // Send the response in chunks to avoid fragmentation
                size_t total_len = strlen(result);
                size_t sent = 0;
                printf("Sending all accessible paths to client\n");
                while (sent < total_len)
                {
                    size_t chunk_size = MIN(BUFFER_SIZE - 1, total_len - sent);
                    send(client_socket, result + sent, chunk_size, MSG_MORE);
                    sent += chunk_size;
                }

                // Send EOF marker
                send(client_socket, "[EOF]", 5, 0);
                log_message("INFO", "SENT ALL ACCESIBLE PATHS TO CLIENT");
                free(result);
                free(current_path);
                continue;
            }

            char *path = strtok(NULL, " ");
            printf("%s\n", path);
            if (strcmp(command, "CREATE") == 0 || strcmp(command, "WRITE") == 0 || strcmp(command, "COPY") == 0)
            {
            }
            else
            {
                path[strlen(path) - 1] = '\0';
            }
            printf("%s\n", path);
            char *filename;
            int currid = SEARCH_LRU(path);
            int destid = 1;
            printf("%d\n", currid);
            if (currid == 0)
            {
                response = INVALID_PATH;
                send(client_socket, &response, sizeof(response), 0);
                continue;
            }
            else
            {
                if (strcmp(command, "COPY") == 0)
                {
                    char *dest = strtok(NULL, " ");
                    dest[strlen(dest) - 1] = '\0';
                    destid = SEARCH_LRU(dest);
                }
                if (destid == 0)
                {
                    response = INVALID_PATH;
                    send(client_socket, &response, sizeof(response), 0);
                    continue;
                }
            }

            if (response == VALID)
            {
                send(client_socket, &response, sizeof(response), 0);
            }
            if (command && strcmp(command, "COPY") == 0 && path)
            {

                if (!path)
                {
                    const char *error = "Destination path not specified";
                    send(client_socket, error, strlen(error), 0);
                    continue;
                }
                // Check if both paths are on the same storage server
                if (currid == destid)
                {
                    // Same server: Forward COPY command to the storage server
                    char temp1[1024];
                    strcpy(temp1, buffer);
                    strcat(temp1, " ");
                    strcat(temp1, ssd[currid - 1].ip);
                    strcat(temp1, " ");
                    char clientport_str[10];                                   // Ensure the buffer is large enough to hold the integer as a string
                    sprintf(clientport_str, "%d", ssd[destid - 1].clientport); // Convert the integer to a string
                    strcat(temp1, clientport_str);                             // Concatenate the integer as a string

                    printf("%s\n", temp1);
                    send(ssd[currid - 1].ss_socket, temp1, strlen(temp1), 0);
                }
                else
                {
                    // Different servers: Inform source server to send to destination server
                    char temp1[1024];
                    strcpy(temp1, buffer);
                    strcat(temp1, " ");
                    strcat(temp1, ssd[destid - 1].ip);
                    strcat(temp1, " ");
                    char clientport_str[10];                                   // Ensure the buffer is large enough to hold the integer as a string
                    sprintf(clientport_str, "%d", ssd[destid - 1].clientport); // Convert the integer to a string
                    strcat(temp1, clientport_str);                             // Concatenate the integer as a string

                    printf("%s\n", temp1);
                    send(ssd[currid - 1].ss_socket, temp1, strlen(temp1), 0);
                }

                // Wait for acknowledgment from the source storage server
                int ack;
                recv(ssd[currid - 1].ss_socket, &ack, sizeof(ack), 0);

                if (ack == ACK_DONE)
                {
                    send(client_socket, "ok", 10, 0);
                    printf("COPY operation successful\n");
                    log_message("INFO", "COPY SUCCESFULLY DONE");
                }
                else
                {
                    send(client_socket, "notok", 10, 0);
                    printf("COPY operation failed\n");
                    log_message("ERROR", "COPY IS UNSUCCESSFUL");
                }
            }
            else if (command && strcmp(command, "CREATE") == 0 && path)
            {
                filename = strtok(NULL, " ");
                filename[strlen(filename) - 1] = '\0';
                printf("Forwarding CREATE request to Storage Server\n");
                snprintf(buffer, BUFFER_SIZE, "CREATE %s %s", path, filename);
                send(ssd[currid - 1].ss_socket, buffer, strlen(buffer), 0); // Send to first Storage Server
                int ack;
                recv(ssd[currid - 1].ss_socket, &ack, sizeof(ack), 0);
                if (ack == ACK_FAIL)
                {
                    printf("Create unsuccessful\n");
                    log_message("ERROR", "CREATE IS UNSUCCESSFUL");
                }
                else if (ack == ACK_DONE)
                {
                    printf("create successful\n");
                    char filepath[BUFFER_SIZE];
                    snprintf(filepath, sizeof(filepath), "%s/%s", path, filename);
                    trieinsert(filepath, ssd[currid - 1].root);
                    create(filepath);
                    log_message("INFO", "CREATE SUCCESFULLY DONE");
                }
                send(client_socket, &ack, sizeof(ack), 0);
            }
            else if (command && strcmp(command, "DELETE") == 0 && path)
            {
                filename = strtok(NULL, " ");
                printf("Forwarding DELETE request to Storage Server\n");
                snprintf(buffer, BUFFER_SIZE, "DELETE %s %s", path, filename);
                send(ssd[currid - 1].ss_socket, buffer, strlen(buffer), 0);
                int ack;
                recv(ssd[currid - 1].ss_socket, &ack, sizeof(ack), 0);
                if (ack == ACK_FAIL)
                {
                    printf("delete unsuccessful\n");
                    log_message("ERROR", "DELETE IS UNSUCCESSFUL");
                }
                else if (ack == ACK_DONE)
                {
                    printf("file is deleted\n");
                    triedelete(path, 1, currid);
                    log_message("INFO", "FILE DELETED SUCCCESFULLY");
                }
                else if (ack == DIR_FAIL_ACK)
                {
                    printf("Directory is deleted");
                    triedelete(path, 2, currid);
                    log_message("INFO", "DIRECTORY DELETED SUCCCESFULLY");
                }
                send(client_socket, &ack, sizeof(ack), 0);
            }
            else if (command && strcmp(command, "READ") == 0 && path)
            {
                senddetails s = {0}; // Initialize the entire structure to zero
                s.clientport = ssd[currid - 1].clientport;
                printf("%d\n", s.clientport);
                strncpy(s.ip, ssd[currid - 1].ip, sizeof(s.ip) - 1); // Use strncpy with proper size limit
                s.ip[sizeof(s.ip) - 1] = '\0';                       // Ensure null termination
                printf("Debug - IP being sent: %s\n", s.ip);
                send(client_socket, &s, sizeof(s), 0);
                log_message("INFO", "READ COMMAND HANDLED CORRECTLY");
            }
            else if (command && strcmp(command, "WRITE") == 0 && path)
            {
                senddetails s = {0}; // Initialize the entire structure to zero
                s.clientport = ssd[currid - 1].clientport;
                strncpy(s.ip, ssd[currid - 1].ip, sizeof(s.ip) - 1); // Use strncpy with proper size limit
                s.ip[sizeof(s.ip) - 1] = '\0';                       // Ensure null termination
                printf("Debug - IP being sent: %s\n", s.ip);
                send(client_socket, &s, sizeof(s), 0);
                log_message("INFO", "WRITE COMMAND HANDLED CORRECTLY");
            }
            else if (command && strcmp(command, "INFO") == 0 && path)
            {
                senddetails s = {0}; // Initialize the entire structure to zero
                s.clientport = ssd[currid - 1].clientport;
                printf("%d\n", s.clientport);
                strncpy(s.ip, ssd[currid - 1].ip, sizeof(s.ip) - 1); // Use strncpy with proper size limit
                s.ip[sizeof(s.ip) - 1] = '\0';                       // Ensure null termination
                printf("Debug - IP being sent: %s\n", s.ip);
                send(client_socket, &s, sizeof(s), 0);
                log_message("INFO", "INFO COMMAND HANDLED CORRECTLY");
            }
            else if (command && strcmp(command, "STREAM") == 0 && path)
            {
                senddetails s = {0}; // Initialize the entire structure to zero
                s.clientport = ssd[currid - 1].clientport;
                strncpy(s.ip, ssd[currid - 1].ip, sizeof(s.ip) - 1); // Use strncpy with proper size limit
                s.ip[sizeof(s.ip) - 1] = '\0';                       // Ensure null termination
                printf("Debug - IP being sent: %s\n", s.ip);
                send(client_socket, &s, sizeof(s), 0);
                log_message("INFO", "STREAM COMMAND HANDLED CORRECTLY");
            }
            else
            {
                printf("Invalid command received from client.\n");
                log_command("INVALID COMMAND");
            }
        }
    }
    return NULL;
}
void *acceptclient(void *args)
{
    int client_socket = *((int *)args);
    struct sockaddr_in client_addr;
    socklen_t addr_size = sizeof(client_addr);
    int client_count = 0;

    while (client_count < MAX_CLIENT)
    {
        int new_client_socket = accept(client_socket, (struct sockaddr *)&client_addr, &addr_size);
        if (new_client_socket < 0)
        {
            perror("Failed to accept Client connection");
            log_message("ERROR", "Failed to accept Client connection");
            continue;
        }

        cd[client_count].id = client_count + 1;
        cd[client_count].clientsocket = new_client_socket;

        printf("Accepted Client %d connection\n", cd[client_count].id);

        char log_buf[256];
        snprintf(log_buf, sizeof(log_buf), "Accepted Client %d connection", client_count + 1);
        log_message("INFO", log_buf);

        pthread_t client_thread;
        pthread_create(&client_thread, NULL, handleclient, &cd[client_count].clientsocket);
        pthread_detach(client_thread);

        client_count++;
    }

    return NULL;
}

int main()
{

    init_logger();
    log_message("INFO", "Naming server started");
    for (int i = 0; i < MAX_SS; i++)
    {
        ssd[i].id = -1;
    }
    for (int i = 0; i < MAX_SS; i++)
    {
        strcpy(ssd[i].ip, "-1");
    }

    for (int i = 0; i < MAX_CLIENT; i++)
    {
        cd[i].id = -1;
    }

    int ss_socket, client_socket;
    struct sockaddr_in ss_addr, client_addr;

    // Initialize SS socket
    ss_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (ss_socket < 0)
    {
        perror("Failed to create SS socket");
        exit(EXIT_FAILURE);
    }
    int opt = 1;
    if (setsockopt(ss_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("setsockopt failed");
        close(ss_socket);
        exit(EXIT_FAILURE);
    }
    ss_addr.sin_family = AF_INET;
    ss_addr.sin_addr.s_addr = INADDR_ANY;
    ss_addr.sin_port = htons(PORT);

    if (bind(ss_socket, (struct sockaddr *)&ss_addr, sizeof(ss_addr)) < 0)
    {
        perror("SS bind failed");
        close(ss_socket);
        exit(EXIT_FAILURE);
    }
    if (listen(ss_socket, MAX_SS) < 0)
    {
        perror("SS listen failed");
        close(ss_socket);
        exit(EXIT_FAILURE);
    }
    printf("Listening for Storage Server connections on port %d...\n", PORT);

    // Initialize Client socket
    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket < 0)
    {
        perror("Failed to create Client socket");
        close(ss_socket);
        exit(EXIT_FAILURE);
    }
    client_addr.sin_family = AF_INET;
    client_addr.sin_addr.s_addr = INADDR_ANY;
    client_addr.sin_port = htons(CLIENT_PORT);

    if (bind(client_socket, (struct sockaddr *)&client_addr, sizeof(client_addr)) < 0)
    {
        perror("Client bind failed");
        close(client_socket);
        close(ss_socket);
        exit(EXIT_FAILURE);
    }
    if (listen(client_socket, MAX_CLIENT) < 0)
    {
        perror("Client listen failed");
        close(client_socket);
        close(ss_socket);
        exit(EXIT_FAILURE);
    }
    printf("Listening for Client connections on port %d...\n", CLIENT_PORT);

    pthread_t threads[2];
    pthread_create(&threads[0], NULL, acceptss, &ss_socket);
    pthread_create(&threads[1], NULL, acceptclient, &client_socket);

    pthread_join(threads[0], NULL);
    pthread_join(threads[1], NULL);

    for (int i = 0; i < MAX_SS; i++)
    {
        if (ssd[i].root != NULL)
        {
            freeTrie(ssd[i].root);
        }
    }

    log_message("INFO", "Naming server shutting down");
    close_logger();
    close(ss_socket);
    close(client_socket);

    return 0;
}