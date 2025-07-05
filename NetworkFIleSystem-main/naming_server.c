#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <errno.h>
#include "helper.h"
#include "ErrorCodes.h"
#include <signal.h>

#ifdef __APPLE__

#include <sys/semaphore.h>

#else
#include <semaphore.h>
#endif

#include "tries.h"

int server_socket_for_ss, server_socket_for_client;

typedef struct {
    char *key;  // The string key
    int index;  // Associated index for the key
} CacheEntry;

// Define the LRU Cache as an array of CacheEntry
typedef struct {
    CacheEntry queue[CACHE_CAPACITY]; // Array to act as the queue
    int size;                         // Current size of the cache
} LRUCache;

// Global Cache
LRUCache cache;

StorageServerInfo storage_servers[MAX_STORAGE_SERVERS];
int storage_server_index = 0;
sem_t storage_semaphore;


void *handle_copy_request(char *buffer, int client_socket, int _src_sock, int _dest_sock);

void ctrlCHandler(int sig) {

    printf("\nCaught signal %d, cleaning up and exiting...\n", sig);
    close(server_socket_for_client);
    close(server_socket_for_ss);
    printf("Server socket closed.\n");
    exit(0);
}

void indexHelper(int storage_socket, trienode *root) {
    sem_wait(&storage_semaphore);
    char path[MAX_PATH_SIZE] = {0};
    char temp_buffer[MAX_PATH_SIZE] = ""; // Temporary buffer to hold incomplete paths
    int finished_indexing = false;
    while (finished_indexing == false && recv_good(storage_socket, path, MAX_PATH_SIZE) > 0) {


        // Concatenate the new buffer with the leftover from the previous iteration
        strncat(temp_buffer, path, MAX_PATH_SIZE - strlen(temp_buffer) - 1);

        // Process each line in temp_buffer
        char *current = temp_buffer;
        char *newline = strchr(current, '\n');
        while (newline != NULL) {
            *newline = '\0'; // Replace newline with null terminator to isolate the line

            // Check for the EOF marker
            printf("Current: |%s|\n", current);
            if (strstr(current, "@!-Finished Indexing-!@") != NULL) {
                printf("Received EOF message. Stopping...\n");
                finished_indexing = true;
                break;
            }

            // Add the complete path to the trie if valid
            if (strlen(current) > 0) {
                AddPathToTrie(current, root);
            }

            // Move to the next line
            current = newline + 1;
            newline = strchr(current, '\n');
        }

        // Move any remaining partial line to the beginning of temp_buffer
        if (*current != '\0') {
            memmove(temp_buffer, current, strlen(current) + 1); // Include null terminator
        } else {
            bzero(temp_buffer, MAX_PATH_SIZE); // Clear buffer if no partial line
        }

        // Clear the main buffer for the next iteration
        bzero(path, MAX_PATH_SIZE);
    }
    sem_post(&storage_semaphore);

}

void create_backup_helper(int source_server_index, int dest_server_index) {
    char backup_buffer[BUFFER_SIZE];
    snprintf(backup_buffer, BUFFER_SIZE, "%sb ./backfolderforss/%s", "mkdir",
             storage_servers[dest_server_index].root->childeren[0
             ]->childeren[0]->data);
    if (send_good(storage_servers[dest_server_index].clientInfo.socket, backup_buffer,
                  strlen(backup_buffer)) < 0) {
        // perror("Failed to send command to backup server 1");
        printErrorDetails(38);

    }
    char command_buffer[BUFFER_SIZE];
    snprintf(command_buffer, sizeof(command_buffer),
             "copy ./%s ./backupfolderforss/%s",
             storage_servers[source_server_index].root->childeren[0]->childeren[0]->data,
             storage_servers[dest_server_index].root->childeren[0
             ]->childeren[0]->data
    );

    handle_copy_request(command_buffer, -1, storage_servers[source_server_index].socket, dest_server_index);
}

void *register_storage_server(void *arg) {
    StorageServerConnectionInfo *ssc_info = (StorageServerConnectionInfo *) arg;
    int storage_socket = ssc_info->socket;
    StorageServerInfo ss_info;

    // log
    char* log_message = (char*)calloc(BUFFER_SIZE, sizeof(char));
    sprintf(log_message, "Registered a Storage Server with fd [%d]", storage_socket);
    log_it(inet_ntoa(ssc_info->addr.sin_addr), ntohs(ssc_info->addr.sin_port), log_message);
    free(log_message);
    // log done

    // Receive initial registration data
    if (recv_good(storage_socket, &ss_info, sizeof(ss_info)) <= 0) {
        // perror("Failed to receive storage server info or client disconnected");
        printErrorDetails(39);
        close(storage_socket);
        free(ssc_info);
        return NULL;
    }
    ss_info.clientInfo = *ssc_info;
    ss_info.socket = storage_socket;
    trienode *root = CreateNode(".");
    indexHelper(storage_socket, root);
    char *_path = (char *) malloc(MAX_PATH_SIZE);
    strcpy(_path, ".");
    printf("Paths stored in trie\n");
    DisplayTrie(root);
    free(_path);
    ss_info.root = root;
    ss_info.backupServer1 = -1;
    ss_info.backupServer2 = -1;
    ss_info.isUp = 1;

    // redundancy
    for(int i = 0; i < MAX_STORAGE_SERVERS; i++) {
        if(storage_servers[i].root != NULL) {
            if(strcmp(root->childeren[0]->childeren[0]->data, storage_servers[i].root->childeren[0]->childeren[0]->data) == 0) {
                printf("Found redundancy\n");
                storage_servers[i].isUp = 1;
                storage_servers[i].clientInfo = *ssc_info;
                storage_servers[i].socket = storage_socket;
                storage_servers[i].portClient = ss_info.portClient;
                return NULL;
            }
        }
    }

    sem_wait(&storage_semaphore);
    if (storage_server_index >= MAX_STORAGE_SERVERS) {
        printf("Storage Server limit reached\n");
    } else {
        storage_servers[storage_server_index] = ss_info;
        printf("Storage Server registered:\n");
        printf("IP: %s\n", inet_ntoa(ssc_info->addr.sin_addr));
        printf("NM Port: %d, Client Port: %d BackUpPort: -- Socket:%d\n", ntohs(ssc_info->addr.sin_port),
               ss_info.portClient,
                ss_info.socket);

        if (storage_server_index == 2) {
            printf("Doing backup for 0,1 and 2\n");
            storage_servers[0].backupServer1 = 1;
            storage_servers[0].backupServer2 = 2;
            storage_servers[1].backupServer1 = 0;
            storage_servers[1].backupServer2 = 2;
            storage_servers[2].backupServer1 = 0;
            storage_servers[2].backupServer2 = 1;

            //TODO: Create all of them in parallel, also check if the ss are up before doing the copy operations like there is a possibility it might be down

            create_backup_helper(0, 1);
            create_backup_helper(0, 2);
            create_backup_helper(1, 2);
            create_backup_helper(1, 0);
            create_backup_helper(1, 2);
            create_backup_helper(2, 0);
            create_backup_helper(2, 1);


        } else if (storage_server_index > 2) {
            printf("Doing backup for %d\n", storage_server_index);
            storage_servers[storage_server_index].backupServer1 = storage_server_index - 1;
            storage_servers[storage_server_index].backupServer2 = storage_server_index - 2;
            create_backup_helper(storage_server_index, storage_server_index - 1);
            create_backup_helper(storage_server_index, storage_server_index - 2);
            create_backup_helper(storage_server_index - 1, storage_server_index);
            create_backup_helper(storage_server_index - 2, storage_server_index);
        }

    }
    storage_server_index++;
    sem_post(&storage_semaphore);
    return NULL;

}


void *handle_ss(void *arg) {
    // Bind and listen
    struct sockaddr_in server_addr_for_ss = *(struct sockaddr_in *) arg;
    if (bind(server_socket_for_ss, (struct sockaddr *) &server_addr_for_ss, sizeof(server_addr_for_ss)) < 0) {
        // perror("Bind failed SS");
        printErrorDetails(41);
        close(server_socket_for_ss);
        close(server_socket_for_client);
        exit(EXIT_FAILURE);
    }

    if (listen(server_socket_for_ss, 5) < 0) {
        // perror("Listen failed SS");
        printErrorDetails(40);
        close(server_socket_for_ss);
        close(server_socket_for_client);
        exit(EXIT_FAILURE);
    }

    printf("Naming Server for SS is running on port %d...\n", SERVER_PORT);
    while (1) {
        struct sockaddr_in storage_addr;
        socklen_t addr_size = sizeof(storage_addr);
        int storage_socket = accept(server_socket_for_ss, (struct sockaddr *) &storage_addr, &addr_size);
        if (storage_socket < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No connections yet, continue to the next iteration
                continue;
            } else {
                // perror("Accept failed");
                printErrorDetails(42);
                continue;
            }
        }

        // Allocate and initialize client info
        StorageServerConnectionInfo *ssc_info = malloc(sizeof(StorageServerConnectionInfo));
        ssc_info->socket = storage_socket;
        ssc_info->addr = storage_addr;
        printf("SS Server connected - Socket: %d, IP: %s, Port: %d\n",
               ssc_info->socket,
               inet_ntoa(ssc_info->addr.sin_addr),
               ntohs(ssc_info->addr.sin_port));

        // log
        char* log_message = (char*)calloc(BUFFER_SIZE, sizeof(char));
        sprintf(log_message, "Received registration data from Storage Server with fd [%d]", storage_socket);
        log_it(inet_ntoa(ssc_info->addr.sin_addr), ntohs(ssc_info->addr.sin_port), log_message);
        free(log_message);
        // log done

        // Create a thread to handle the Storage Server
        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, register_storage_server, ssc_info) != 0) {
            // perror("Failed to create thread");
            printErrorDetails(43);
            close(storage_socket);
            free(ssc_info);
        }
    }
}

void initialize_cache() {
    cache.size = 0;
    for (int i = 0; i < CACHE_CAPACITY; i++) {
        cache.queue[i].key = NULL;
        cache.queue[i].index = -1;
    }
}

// Find an element in the queue
int search_cache(const char *key) {
    for (int i = 0; i < cache.size; i++) {
        if (strcmp(cache.queue[i].key, key) == 0) {
            return i; // Return index if found
        }
    }
    return -1; // Return -1 if not found
}

// Remove the least recently used (LRU) element from the cache
void evict_lru() {
    if (cache.size > 0) {
        free(cache.queue[0].key); // Free the memory of the evicted key
        for (int i = 1; i < cache.size; i++) {
            cache.queue[i - 1] = cache.queue[i]; // Shift elements to the left
        }
        cache.size--;
    }
}

// Add a new element to the back of the queue
void add_to_cache(const char *key, int index) {
    if (cache.size == CACHE_CAPACITY) {
        evict_lru(); // Evict the least recently used item if the cache is full
    }
    cache.queue[cache.size].key = strdup(key); // Copy the new key
    cache.queue[cache.size].index = index;     // Set the index
    cache.size++;
}

// Move an existing element to the back of the queue
void move_to_back(int index) {
    CacheEntry temp = cache.queue[index];
    for (int i = index; i < cache.size - 1; i++) {
        cache.queue[i] = cache.queue[i + 1]; // Shift elements to the left
    }
    cache.queue[cache.size - 1] = temp; // Move the element to the back
}


int SearchPathHelper(trienode *root, char **path_arr) {
    trienode *curr = root;
    for (int i = 0; i < MAX_PATH; i++) {
        if (path_arr[i] == NULL) {
            break;
        }
        bool found = false;
        for (int j = 0; j < MAX_FOLDERS; j++) {
            if (curr->childeren[j] != NULL && strcmp(curr->childeren[j]->data, path_arr[i]) == 0) {
                found = true;
                curr = curr->childeren[j];
                break;
            }
        }
        if (!found) {
            return 0;
        }
    }
    return 1;
}

int search_in_tries(char *buffer) {
    char *path_arr[MAX_PATH];
    ParsePath(buffer, path_arr);
    for (int i = 0; i < storage_server_index; i++) {
        if (SearchPathHelper(storage_servers[i].root, path_arr)) return i;
    }
    return -1;
}

int isSSUp(int index) {
    // Creating as a client to ping and see if up
    int sock;
    StorageServerInfo ss = storage_servers[index];

    // Create the socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        // perror("Socket creation failed");
        printErrorDetails(7);
        return 0;
    }

    // Set up the server address
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(ss.portClient);
    if (inet_pton(AF_INET, inet_ntoa(ss.clientInfo.addr.sin_addr), &server_addr.sin_addr) <= 0) {
        // perror("Invalid IP address");
        printErrorDetails(8);
        close(sock);
        return 0;
    }

    // Connect to the server
    if (connect(sock, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        storage_servers[index].isUp = 0;
        // perror("Connection to storage server failed");
        printErrorDetails(45);
        close(sock);
        return 0;
    }

    // Check if SS is up
    printf("Pinging index %d on socket %d\n", index, socket);
    if (send_good(sock, "ping", strlen("ping")) < 0) {
        storage_servers[index].isUp = 0;
        // perror("Failed to send ping command");
        printErrorDetails(10);
        close(sock);
        return 0;
    }

    char buffer[BUFFER_SIZE] = {0};
    if (recv_good(sock, buffer, BUFFER_SIZE) < 0) {
        storage_servers[index].isUp = 0;
        // perror("Failed to receive ping command");
        printErrorDetails(11);
        close(sock);
        return 0;
    }

    storage_servers[index].isUp = 1;
    bzero(buffer, BUFFER_SIZE);
    close(sock);
    return 1;
}

void print_cache() {
    return;
    printf("Current Cache State:\n");
    if (cache.size == 0) {
        printf("Cache is empty.\n");
        return;
    }

    for (int i = 0; i < cache.size; i++) {
        printf("Entry %d: Key = %s, Index = %d\n", i, cache.queue[i].key, cache.queue[i].index);
    }
}

int redundantSS(int index) {
    // Check if SS is up
    printf("Checking for SS for index %d\n", index);
    if (isSSUp(index)) {
        printf("returning ss\n");
        return index;
    }

    int indexOfBS1 = storage_servers[index].backupServer1;
    printf("Checking for BS1 on index: %d\n", indexOfBS1);
    if (indexOfBS1 != -1 && isSSUp(indexOfBS1)) {
        return indexOfBS1;
    }

    int indexOfBS2 = storage_servers[index].backupServer2;
    printf("Checking for BS2 on index: %d\n", indexOfBS2);
    if (indexOfBS2 != -1 && isSSUp(indexOfBS2)) {
        return indexOfBS2;
    }
    printf("No BACKUP SS  also found\n");
    return -1;
}

void delete_from_cache(const char *key) {
    //sem_wait(&storage_semaphore); // Ensure thread safety

    int index = search_cache(key); // Find the key in the cache
    if (index == -1) {
        printf("Key '%s' not found in the cache.\n", key);
        sem_post(&storage_semaphore);
        return;
    }

    // Free the memory of the key
    free(cache.queue[index].key);

    // Shift all elements after the removed one to the left
    for (int i = index; i < cache.size - 1; i++) {
        cache.queue[i] = cache.queue[i + 1];
    }

    // Decrease the cache size
    cache.size--;

    print_cache();

    //printf("Key '%s' removed from the cache.\n", key);
    //sem_post(&storage_semaphore); // Release the semaphore
}


int search_storage_server(char *buffer) {

    print_cache();
    sem_wait(&storage_semaphore);
    int index = search_cache(buffer);

    if (index != -1) {
        int k = cache.queue[index].index;
        move_to_back(index);
        print_cache();
        // printf("used cache\n");
        sem_post(&storage_semaphore);
        int indexToSend = cache.queue[index].index;
        isSSUp(indexToSend);
        return k;


    }

    int found = search_in_tries(buffer);

    if (found != -1) {
        add_to_cache(buffer, found);
        print_cache();
        sem_post(&storage_semaphore);
        isSSUp(found);
        return found;
    } else {
        sem_post(&storage_semaphore);
        print_cache();
        return -1;
    }

}

void *handle_copy_request(char *buffer, int client_socket, int _src_sock, int _dest_index) {
    // Duplicate buffer to preserve the original
    printf("Trying to copy\n");
    char *dup_buffer = strdup(buffer);
    if (dup_buffer == NULL) {
        // perror("Failed to duplicate buffer");
        printErrorDetails(48);
        return NULL;
    }

    if (client_socket == -1) {
        printf("Buffer: |%s|\n", buffer);
        // Bypass path checks and use _src_sock and _dest_sock directly
        printf("Bypassing path checks for Backup operation\n");

        // Modify the buffer for "copydifferent"
        if (strncmp(buffer, "lcopy", strlen("lcopy")) != 0) {
            memmove(buffer + 13, buffer + 4, strlen(buffer + 4) + 1); // Shift the rest of the string
            memcpy(buffer, "copydifferent", 13); // Set the operation to "copydifferent"
        }

        char ss_info[BUFFER_SIZE];
        snprintf(ss_info, BUFFER_SIZE, " %s %d", inet_ntoa(storage_servers[_dest_index].clientInfo.addr.sin_addr),
                 storage_servers[_dest_index].portClient);
        strcat(buffer, ss_info);
        printf("Sending different different |%s|\n", buffer);

        // Send the modified buffer to the source socket
        if (send_good(_src_sock, buffer, strlen(buffer)) < 0) {
            // perror("Failed to send command to source storage server");
            printErrorDetails(49);
            free(dup_buffer);
            return NULL;
        }

        // // log
        // char* log_message = (char*)calloc(BUFFER_SIZE, sizeof(char));
        // sprintf(log_message, "Sent copy command to Storage Server with fd [%d]", _src_sock);
        // log_it(inet_ntoa(storage_servers[_dest_index].clientInfo.addr.sin_addr), ntohs(storage_servers[_dest_index].clientInfo.addr.sin_port), log_message);
        // free(log_message);
        // log done

        // Receive the response from the source storage server
        char response[BUFFER_SIZE] = {0};
        if (recv_good(_src_sock, response, BUFFER_SIZE) <= 0) {
            // perror("Failed to receive response from source storage server");
            printErrorDetails(50);
            free(dup_buffer);
            return NULL;
        }

        // // log
        // log_message = (char*)calloc(BUFFER_SIZE, sizeof(char));
        // sprintf(log_message, "Received response from Storage Server with fd [%d]: %s", _src_sock, response);
        // log_it(inet_ntoa(storage_servers[_dest_index].clientInfo.addr.sin_addr), ntohs(storage_servers[_dest_index].clientInfo.addr.sin_port), log_message);
        // free(log_message);
        // log done

        printf("Received response: %s\n", response);


        printf("Finished Backup operation\n");
        free(dup_buffer);
        return NULL;
    }

    // Normal processing if client_socket != -1
    if (strncmp(dup_buffer, "copy", strlen("copy")) == 0) {
        char *command = strtok(dup_buffer, " ");
        char *src = strtok(NULL, " ");
        char *dest = strtok(NULL, " ");



        // Validate command, src, and dest
        if (!command || !src || !dest || strlen(src) == 0 || strlen(dest) == 0) {
            const char *invalid_request_msg = "Invalid request. Source or destination path missing.";
            if (send_good(client_socket, invalid_request_msg, strlen(invalid_request_msg)) < 0) {
                // perror("Client not responding");

            }
            free(dup_buffer);
            return NULL;
        }

        // Prevent invalid paths
        if (strcmp(src, "/") == 0 || strcmp(src, "./") == 0 ||
            strcmp(dest, "/") == 0 || strcmp(dest, "./") == 0) {
            const char *invalid_path_msg = "Invalid source or destination path.";
            if (send_good(client_socket, invalid_path_msg, strlen(invalid_path_msg)) < 0) {
                // perror("Client not responding");
                printErrorDetails(51);
            }
            free(dup_buffer);
            return NULL;
        }

        // Search for source and destination storage servers
        int src_index = search_storage_server(src);
        int dest_index = search_storage_server(dest);

        if (src_index == -1 || dest_index == -1 || !storage_servers[src_index].isUp ||
            !storage_servers[dest_index].isUp) {
            const char *not_found_msg = "Storage server not found";
            if (send_good(client_socket, not_found_msg, strlen(not_found_msg) + 1) < 0) {
                // perror("Client not responding");
                printErrorDetails(51);
            }
            free(dup_buffer);
            return NULL;
        }

        // Get source and destination sockets
        int src_sock = storage_servers[src_index].clientInfo.socket;
        int dest_sock = storage_servers[dest_index].clientInfo.socket;

        if (src_index == dest_index) {
            memmove(buffer + 8, buffer + 4, strlen(buffer + 4) + 1); // Shift the rest of the string
            memcpy(buffer, "copysame", 8);
            printf("Sending same same |%s|\n", buffer);
        } else {
            memmove(buffer + 13, buffer + 4, strlen(buffer + 4) + 1); // Shift the rest of the string
            memcpy(buffer, "copydifferent", 13); // Copy "copydifferent" into the first par
            char ss_info[BUFFER_SIZE];
            snprintf(ss_info, BUFFER_SIZE, " %s %d", inet_ntoa(storage_servers[dest_index].clientInfo.addr.sin_addr),
                     storage_servers[dest_index].portClient);
            strcat(buffer, ss_info);
            printf("Sending different different |%s|\n", buffer);

        }
        if (send_good(src_sock, buffer, strlen(buffer)) < 0) {
            // perror("Failed to send command to SS");
            printErrorDetails(49);
            return NULL;
        }

        // Receive the response from the storage server
        char *response = calloc(BUFFER_SIZE, sizeof(char));
        if (recv_good(src_sock, response, BUFFER_SIZE) <= 0) {
            // perror("Failed to receive response from SS");
            printErrorDetails(50);
            free(response);
            return NULL;
        }
        printf("Sending to client:%s\n", response);
        // Send the response to the client
        if (send_good(client_socket, response, strlen(response)) < 0) {
            // perror("Failed to send response to client");
            printErrorDetails(52);
            free(response);
            return NULL;
        }
        free(response);
        send(dest_sock, "reindex", strlen("reindex"), 0);
        trienode *_root = CreateNode(".");
        indexHelper(dest_sock,
                    _root); // doing this else, multiple clients crashes and stuff while reindexing
        storage_servers[dest_index].root = _root;
        printf("Finished Reindexing\n");
        int BS1 = storage_servers[dest_index].backupServer1;
        int BS2 = storage_servers[dest_index].backupServer2;
        if (BS1 != -1 && isSSUp(BS1)) {
            char *name = strrchr(src, '/') ? strrchr(src, '/') + 1 : src;
            char _buffer[BUFFER_SIZE];
            snprintf(_buffer, BUFFER_SIZE, "copydifferentb %s/%s ./backupfolderforss/%s/%s %s %d",
                     dest, name,
                     storage_servers[BS1].root->childeren[0]->childeren[0]->data, //+2 to remove ./
                     dest + 2,
                     inet_ntoa(storage_servers[BS1].clientInfo.addr.sin_addr),
                     storage_servers[BS1].portClient);
            printf("Sending to BS1 to copy:%s\n", _buffer);
            send_good(dest_sock, _buffer, strlen(_buffer));

        }
        if (BS2 != -1 && isSSUp(BS2)) {
            char *name = strrchr(src, '/') ? strrchr(src, '/') + 1 : src;
            char _buffer[BUFFER_SIZE];
            snprintf(_buffer, BUFFER_SIZE, "copydifferentb %s/%s ./backupfolderforss/%s/%s %s %d",
                     dest, name,
                     storage_servers[BS2].root->childeren[0]->childeren[0]->data, //+2 to remove ./
                     dest + 2,
                     inet_ntoa(storage_servers[BS2].clientInfo.addr.sin_addr),
                     storage_servers[BS2].portClient);
            printf("Sending to BS2 to copy:%s\n", _buffer);
            send_good(dest_sock, _buffer, strlen(_buffer));

        }

    }

    free(dup_buffer);
    return NULL;
}


void *process_client(void *arg) {
    int client_socket = *(int *) arg;
    while (1) {
        printf("Waiting for new command\n");
        char buffer[BUFFER_SIZE];
        bzero(buffer, BUFFER_SIZE);
        if (recv_good(client_socket, buffer, BUFFER_SIZE) <= 0) {
            // perror("Failed to receive message from client");
            printErrorDetails(53 );
            close(client_socket);
            return NULL;
        }

        // log
        char* log_message = (char*)calloc(BUFFER_SIZE, sizeof(char));
        sprintf(log_message, "Recieved: '%s' -from- Client fd[%d]", buffer, client_socket);
        log_it(NULL, -1, log_message);
        free(log_message);
        // log done
        
        printf("Client %d>>|%s|\n", client_socket, buffer);

        // Handle 'ls ~' and 'ls .' commands
        if (strcmp(buffer, "ls ~") == 0 || strcmp(buffer, "ls .") == 0) {
            printf("Printing root folder\n");
            for (int i = 0; i < storage_server_index; i++) {
                printf("Storage Server %d\n", i);
                // Assuming each storage server has a root node and displaying its contents
                trienode *n = storage_servers[i].root->childeren[0]->childeren[0];
                DisplayTrieNetwork(client_socket, n);
            }

            // Notify the client that the folder listing is complete
            if (send_good(client_socket, "@#!@#over@$", strlen("@#!@#over@$")) < 0) {
                // perror("Client not responding\n");
            }
            continue;
        }

        // Handle 'copy' command
        if (strncmp(buffer, "copy", strlen("copy")) == 0) {
            handle_copy_request(buffer, client_socket, -1, -1);
            continue;
        }

        char *_buffer = strdup(buffer);
        char *command = strtok(_buffer, " ");
        char *path = strtok(NULL, " ");

        if (path == NULL || strcmp(path, "/") == 0 || strcmp(path, "./") == 0) {
            const char *not_found_msg = "Storage server not found";
            if (send_good(client_socket, not_found_msg, strlen(not_found_msg)) < 0) {
                // perror("Client not responding\n");
                printErrorDetails(51);
            }
            free(_buffer);
            continue;
        }

        // Handle 'write' command with synchronization options
        if (strncmp(buffer, "write", strlen("write")) == 0) {
            char *tempbuffer = strdup(buffer);
            char *flag = strtok(tempbuffer, " ");
            flag = strtok(NULL, " "); // flag
            char *path = strtok(NULL, " "); // path
            printf("write in ns path: %s\n", path);

            int ssindex = search_storage_server(path);
            if (ssindex != -1 && storage_servers[ssindex].isUp) {
                char send_buffer[BUFFER_SIZE];
                bzero(send_buffer, BUFFER_SIZE);

                char ss_info[BUFFER_SIZE];
                snprintf(ss_info, BUFFER_SIZE, "|%s|%d", inet_ntoa(storage_servers[ssindex].clientInfo.addr.sin_addr),
                         storage_servers[ssindex].portClient);

                int siz = strlen(ss_info);
                if (strcmp(flag, "-s") == 0) {
                    siz += 12;
                    strcpy(send_buffer, "PATH_OK_SYNC");
                } else if (strcmp(flag, "-a") == 0) {
                    siz += 13;
                    strcpy(send_buffer, "PATH_OK_ASYNC");
                }

                siz++;
                strcat(send_buffer, ss_info);
                strcat(send_buffer, "\0");

                printf("send_buffer: %s\n", send_buffer);
                if (send_good(client_socket, send_buffer, siz) < 0) {
                    // perror("Send error");
                    printErrorDetails(10);
                }
            } else {
                const char *not_found_msg = "Storage server not found";
                if (send_good(client_socket, not_found_msg, strlen(not_found_msg)) < 0) {
                    // perror("Client not responding\n");
                    printErrorDetails(51);
                }

                // log
                log_message = (char*)calloc(BUFFER_SIZE, sizeof(char));
                sprintf(log_message, "Sending: '%s' -to- Client fd[%d]", not_found_msg, client_socket);
                log_it(NULL, -1, log_message);
                free(log_message);
                // log done

            }
            free(tempbuffer);
        }

        // Handle 'mkdir' and 'touch' commands
        if (strcmp(command, "mkdir") == 0 || strcmp(command, "touch") == 0) {
            char *temp_path = strdup(path);
            char *last_slash = strrchr(temp_path, '/');
            if (last_slash != NULL) {
                *last_slash = '\0'; // Truncate the string at the last '/'
            }
            printf("Searching for %s\n", temp_path);
            if (strcmp(temp_path, "./") == 0 || strcmp(temp_path, ".") == 0 || strcmp(temp_path, "/") == 0 ||
                strcmp(temp_path, "") == 0 || strcmp(temp_path, " ") == 0) {
                //Since, these are invalid
                if (send_good(client_socket, "Invalid Path", strlen("Invalid Path")) < 0) {
                    // perror("Client not responding\n");
                    printErrorDetails(51);
                    free(temp_path);
                    return NULL;
                }
                printf("Invalid Path\n");
                free(temp_path);
                continue;
            }
            int ssindex = search_storage_server(temp_path);
            free(temp_path);
            if (ssindex != -1) add_to_cache(path, ssindex);
            if (ssindex != -1 && storage_servers[ssindex].isUp) {
                int ss_sock = storage_servers[ssindex].clientInfo.socket;
                printf("Sending to storage server %s\n", buffer);
                if (send_good(ss_sock, buffer, strlen(buffer)) < 0) {
                    // perror("Client not responding\n");
                    printErrorDetails(51);
                } else {
                    char *response = calloc(BUFFER_SIZE, sizeof(char));
                    int bytes = 0;
                    if ((bytes = recv_good(ss_sock, response, BUFFER_SIZE)) <= 0) {
                        // perror("SS not responding\n");
                        printErrorDetails(54);
                        if (send_good(client_socket, "Storage server not responding",
                                      strlen("Storage server not responding")) < 0) {
                            // perror("Client not responding\n");
                            printErrorDetails(51);
                        }
                        free(response);
                    } else {
                        response[bytes] = '\0';
                        if (send_good(client_socket, response, strlen(response)) < 0) {
                            // perror("Client not responding\n");
                            printErrorDetails(51);
                        }
                        printf("resp: %s\n", response);
                        if (strncmp(response, "Could not", strlen("Could not")) != 0) {
                            printf("Adding the new path to tries\n");

                            int bs1 = storage_servers[ssindex].backupServer1;
                            int bs2 = storage_servers[ssindex].backupServer2;
                            if (bs1 != -1 && isSSUp(bs1)) {
                                // Buffer contains <mkdir/touch> <path> convert it into <mkdirb/touchb> <path> and send it to backup server
                                char backup_buffer[BUFFER_SIZE];
                                snprintf(backup_buffer, BUFFER_SIZE, "%sb %s", command, path);
                                if (send_good(storage_servers[bs1].clientInfo.socket, backup_buffer,
                                              strlen(backup_buffer)) < 0) {
                                    // perror("Failed to send command to backup server 1");
                                    printErrorDetails(38);
                                }
                            }
                            if (bs2 != -1 && isSSUp(bs2)) {
                                char backup_buffer[BUFFER_SIZE];
                                snprintf(backup_buffer, BUFFER_SIZE, "%sb %s", command, path);
                                if (send_good(storage_servers[bs2].clientInfo.socket, backup_buffer,
                                              strlen(backup_buffer)) < 0) {
                                    // perror("Failed to send command to backup server 1");
                                    printErrorDetails(38);
                                }
                            }

                            AddPathToTrie(path, storage_servers[ssindex].root);
                        }
                        free(response);
                    }
                }
            } else {
                if (send_good(client_socket, "Storage server not found",
                              strlen("Storage server not found")) < 0) {
                    // perror("Client not responding\n");
                    printErrorDetails(51);
                }
            }
        }

            // Handle 'rmdir' and 'rm' commands
        else if (strcmp(command, "rmdir") == 0 || strcmp(command, "rm") == 0) {
            if (strcmp(path, "./") == 0 || strcmp(path, ".") == 0 || strcmp(path, "/") == 0 || strcmp(path, "") == 0 ||
                strcmp(path, " ") == 0) {
                //Since, these are invalid
                if (send_good(client_socket, "Invalid Path", strlen("Invalid Path")) < 0) {
                    // perror("Client not responding\n");
                    printErrorDetails(51);
                    return NULL;
                }

                // log
                log_message = (char*)calloc(BUFFER_SIZE, sizeof(char));
                sprintf(log_message, "Sending: '%s' -to- Client fd[%d]", "Invalid Path", client_socket);
                log_it(NULL, -1, log_message);
                free(log_message);
                // log done

                printf("Invalid Path\n");
                continue;
            }
            int ssindex = search_storage_server(path);
            if (ssindex != -1) delete_from_cache(path);
            if (ssindex != -1 && storage_servers[ssindex].isUp) {
                int ss_sock = storage_servers[ssindex].clientInfo.socket;
                printf("Sending buffer %s\n", buffer);
                if (send_good(ss_sock, buffer, strlen(buffer)) < 0) {
                    // perror("Client not responding\n");
                    printErrorDetails(51);
                } else {

                    // log 
                    log_message = (char*)calloc(BUFFER_SIZE, sizeof(char));
                    sprintf(log_message, "Sending: '%s' -to- Storage Server fd[%d]", buffer, ss_sock);
                    log_it(NULL, -1, log_message);
                    free(log_message);
                    // log done

                    char *response = (char *) calloc(BUFFER_SIZE, sizeof(char));
                    int bytes = 0;
                    if ((bytes = recv_good(ss_sock, response, BUFFER_SIZE)) <= 0) {
                        perror("SS not responding\n");
                        if (send_good(client_socket, "Storage server not responding",
                                      strlen("Storage server not responding")) < 0) {
                            // perror("Client not responding\n");
                            printErrorDetails(51);
                            free(response);
                        }

                        // log
                        log_message = (char*)calloc(BUFFER_SIZE, sizeof(char));
                        sprintf(log_message, "recieved: '%s' -from- Storage Server fd[%d]", "Storage server not responding", ss_sock);
                        log_it(NULL, -1, log_message);
                        free(log_message);
                        // log done

                    } else {

                        // log 
                        log_message = (char*)calloc(BUFFER_SIZE, sizeof(char));
                        sprintf(log_message, "recieved: '%s' -from- Storage Server fd[%d]", response, ss_sock);
                        log_it(NULL, -1, log_message);
                        free(log_message);
                        // log done

                        response[bytes] = '\0';
                        printf("resp: %s\n", response);
                        if (send_good(client_socket, response, strlen(response)) < 0) {
                            // perror("Client not responding\n");
                            printErrorDetails(51);
                        }

                        // log
                        log_message = (char*)calloc(BUFFER_SIZE, sizeof(char));
                        sprintf(log_message, "Sending: '%s' -to- Client fd[%d]", response, client_socket);
                        log_it(NULL, -1, log_message);
                        free(log_message);
                        // log done

                        if (strncmp(response, "Could not", strlen("Could not")) != 0) {

                            printf("Deleting the path %s from tries\n", path);
                            DisplayTrie(storage_servers[ssindex].root);
                            int bs1 = storage_servers[ssindex].backupServer1;
                            int bs2 = storage_servers[ssindex].backupServer2;
                            if (bs1 != -1 && isSSUp(bs1)) {
                                // Buffer contains <rmdir/rm> <path> convert it into <mkdirb/touchb> <path> and send it to backup server
                                char backup_buffer[BUFFER_SIZE];
                                snprintf(backup_buffer, BUFFER_SIZE, "%sb %s", command, path);
                                if (send_good(storage_servers[bs1].clientInfo.socket, backup_buffer,
                                              strlen(backup_buffer)) < 0) {
                                    // perror("Failed to send command to backup server 1");
                                    printErrorDetails(38);
                                }

                              // log 
                            //     log_message = (char*)calloc(BUFFER_SIZE, sizeof(char));
                            //     sprintf(log_message, "Sending: '%s' -to- Storage Server fd[%d]", backup_buffer, storage_servers[bs1].clientInfo.socket);
                            //     log_it(NULL, -1, log_message);
                            //     free(log_message);
                            //   // log done

                            }
                            if (bs2 != -1 && isSSUp(bs2)) {
                                char backup_buffer[BUFFER_SIZE];
                                snprintf(backup_buffer, BUFFER_SIZE, "%sb %s", command, path);
                                if (send_good(storage_servers[bs2].clientInfo.socket, backup_buffer,
                                              strlen(backup_buffer)) < 0) {
                                    // perror("Failed to send command to backup server 1");
                                    printErrorDetails(38);
                                }

                                // log
                                log_message = (char*)calloc(BUFFER_SIZE, sizeof(char));
                                sprintf(log_message, "Sending: '%s' -to- Storage Server fd[%d]", backup_buffer, storage_servers[bs2].clientInfo.socket);
                                log_it(NULL, -1, log_message);
                                free(log_message);
                                // log done

                            }
                            DeletePath(path, storage_servers[ssindex].root);
                        }
                        free(response);
                    }
                }
            } else {
                if (send_good(client_socket, "Storage server not found",
                              strlen("Storage server not found")) < 0) {
                    // perror("Client not responding\n");
                    printErrorDetails(51);
                }

                // log
                log_message = (char*)calloc(BUFFER_SIZE, sizeof(char));
                sprintf(log_message, "Sending: '%s' -to- Client fd[%d]", "Storage server not found", client_socket);
                log_it(NULL, -1, log_message);
                free(log_message);
                // log done

            }
        }

            // For other commands, return the storage server info or error message
        else {
            int ssindex = search_storage_server(path);
            int portToSend = storage_servers[ssindex].portClient;
            char *ipToSend = inet_ntoa(storage_servers[ssindex].clientInfo.addr.sin_addr);
            int _index = ssindex;
            if (ssindex != -1) {
                if (!storage_servers[ssindex].isUp) {
                    _index = redundantSS(ssindex);
                    portToSend = storage_servers[_index].portClient;
                    ipToSend = inet_ntoa(storage_servers[_index].clientInfo.addr.sin_addr);
                    // Handle the case where _index is not the same as ssindex
                }
                if (_index != ssindex) {
                    char redundancy_msg[BUFFER_SIZE] = {0};
                    snprintf(redundancy_msg, BUFFER_SIZE, "Backup %s,%d,%s", ipToSend, portToSend,
                             storage_servers[_index].root->childeren[0]->childeren[0]->data);
                    printf("Using redundant server for ssindex(%d): %s\n", ssindex, redundancy_msg);

                    if (send_good(client_socket, redundancy_msg, strlen(redundancy_msg)) < 0) {
                        perror("Client not responding\n");
                    }
                }
                else if (_index != -1) {
                    char ss_info[BUFFER_SIZE] = {0};
                    snprintf(ss_info, BUFFER_SIZE, "%s,%d ", ipToSend, portToSend);
                    printf("Found in ssindex(%d): %s\n", ssindex, ss_info);

                    if (send_good(client_socket, ss_info, strlen(ss_info)) < 0) {
                        // perror("Client not responding\n");
                        printErrorDetails(51);
                    }

                    // log
                    log_message = (char*)calloc(BUFFER_SIZE, sizeof(char));
                    sprintf(log_message, "Sending: '%s' -to- Client fd[%d]", ss_info, client_socket);
                    log_it(NULL, -1, log_message);
                    free(log_message);
                    // log done

                } else {
                    const char *not_found_msg = "Storage server not found";
                    if (send_good(client_socket, not_found_msg, strlen(not_found_msg)) < 0) {
                        // perror("Client not responding\n");
                        printErrorDetails(51);
                    }

                    // log
                    log_message = (char*)calloc(BUFFER_SIZE, sizeof(char));
                    sprintf(log_message, "Sending: '%s' -to- Client fd[%d]", not_found_msg, client_socket);
                    log_it(NULL, -1, log_message);
                    free(log_message);
                    // log done

                }

            } else {
                const char *not_found_msg = "Storage server not found";
                if (send_good(client_socket, not_found_msg, strlen(not_found_msg)) < 0) {
                    // perror("Client not responding\n");
                    printErrorDetails(51);
                }

                // log
                log_message = (char*)calloc(BUFFER_SIZE, sizeof(char));
                sprintf(log_message, "Sending: '%s' -to- Client fd[%d]", not_found_msg, client_socket);
                log_it(NULL, -1, log_message);
                free(log_message);
                // log done

            }

        }
        free(_buffer);
    }
    close(client_socket);

    return NULL;
}

int create_client_socket(const char *ip_address, int port) {
    printf("creating the client socket for ip%s and port %d\n", ip_address, port);
    fflush(stdout);
    int sock;
    struct sockaddr_in client_addr;

    // Create a socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        return -1;
    }

    // Configure the client address structure
    memset(&client_addr, 0, sizeof(client_addr));
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(port);

    // Convert IP address to binary form
    if (inet_pton(AF_INET, ip_address, &client_addr.sin_addr) <= 0) {
        perror("Invalid IP address format or address not supported");
        close(sock);
        return -1;
    }

    // Connect to the client
    if (connect(sock, (struct sockaddr *) &client_addr, sizeof(client_addr)) < 0) {
        perror("Connection to client failed");
        close(sock);
        return -1;
    }

    printf("Connected to client at %s:%d\n", ip_address, port);
    return sock;
}


void *listen_async(void *arg) {
    int client = *(int *) arg;
    printf("\033[0;32mGot a client\033[0m\n");  // Green text
    char buffer[BUFFER_SIZE] = {0};
    if (recv_good(client, buffer, BUFFER_SIZE) <= 0) {
        perror("Failed to receive message from client");
        close(client);
        return NULL;
    }


    printf("\033[0;32mAsync %d>>|%s|\033[0m\n", client, buffer);  // Green text
    if (strncmp(buffer, "WRITE_SYNC", strlen("WRITE_SYNC")) == 0) {
        char *token = strtok(buffer, "|");
        char *path = strtok(NULL, "|"); // Skip the "WRITE_SYNC" part
        printf("-- PATH %s --\n", path);
        int ssindex = search_storage_server(path);
        int BS1 = storage_servers[ssindex].backupServer1;
        int BS2 = storage_servers[ssindex].backupServer2;
        printf("SSINDEX %d\n", ssindex);
        printf("BS1 %d\n", BS1);
        printf("BS2 %d\n", BS2);
        char command_buffer[BUFFER_SIZE];
        if (BS1 != -1 && isSSUp(BS1)) {
            snprintf(command_buffer, BUFFER_SIZE,
                     "lcopy %s ./backupfolderforss/%s",
                     path, // Source path
                     storage_servers[BS1].root->childeren[0]->childeren[0]->data
            );
            printf("\033[0;31m --- Sending this to BS1: %s ----\033[0m\n", command_buffer);
            handle_copy_request(command_buffer, -1, storage_servers[ssindex].socket, BS1);
        }
        printf("III AMM HERERERERE\n");
        if (BS2 != -1 && isSSUp(BS2)) {

            snprintf(command_buffer, BUFFER_SIZE,
                     "lcopy %s ./backupfolderforss/%s",
                     path, // Source path
                     storage_servers[BS2].root->childeren[0]->childeren[0]->data
            );
            printf("\033[0;31m --- Sending this to BS2: %s ----\033[0m\n", command_buffer);
            fflush(stdout);
            handle_copy_request(command_buffer, -1, storage_servers[ssindex].socket, BS2);
        }


        return NULL;
    } else if (strncmp(buffer, "writeasync", strlen("writeasync")) == 0) {
        char *token = strtok(buffer, " ");   // Parse the command type
        char *src_path = strtok(NULL, " "); // Source path
        char *client_ip = strtok(NULL, " "); // Client IP
        char *client_port = strtok(NULL, " "); // Client Port

        printf("-- SRC_PATH: %s -- SRC_PATH: %s --\n", src_path, src_path);
        printf("-- CLIENT_IP: %s -- CLIENT_PORT: %s --\n", client_ip, client_port);

        int ssindex = search_storage_server(src_path);
        int BS1 = storage_servers[ssindex].backupServer1;
        int BS2 = storage_servers[ssindex].backupServer2;

        printf("SSINDEX %d\n", ssindex);
        printf("BS1 %d\n", BS1);
        printf("BS2 %d\n", BS2);

        char command_buffer[BUFFER_SIZE];
        if (BS1 != -1 && isSSUp(BS1)) {
            snprintf(command_buffer, BUFFER_SIZE,
                     "lcopy %s ./backupfolderforss/%s",
                     src_path, // Source path
                     storage_servers[BS1].root->childeren[0]->childeren[0]->data
            );
            printf("\033[0;31m --- Sending this to BS1: %s ----\033[0m\n", command_buffer);
            handle_copy_request(command_buffer, -1, storage_servers[ssindex].socket, BS1);
        }

        if (BS2 != -1 && isSSUp(BS2)) {
            snprintf(command_buffer, BUFFER_SIZE,
                     "lcopy %s ./backupfolderforss/%s",
                     src_path, // Source path
                     storage_servers[BS2].root->childeren[0]->childeren[0]->data
            );
            printf("\033[0;31m --- Sending this to BS2: %s ----\033[0m\n", command_buffer);
            fflush(stdout);
            handle_copy_request(command_buffer, -1, storage_servers[ssindex].socket, BS2);
        }
        printf("Starting to send the ACK to client\n");
        // Send acknowledgment message to the client
        char ack_buffer[BUFFER_SIZE];
        snprintf(ack_buffer, BUFFER_SIZE, "ACK: Backup completed for %s", src_path);
        printf("trying to send ack\n");
        printf("Port is %s\n", client_port);
        int client_sock = create_client_socket(client_ip, atoi(client_port)); // Utility function to create socket
        if (client_sock != -1) {
            send(client_sock, ack_buffer, strlen(ack_buffer), 0);
            printf("\033[0;32m --- Sent ACK to Client: %s ----\033[0m\n", ack_buffer);
            close(client_sock);
        } else {
            printf("\033[0;31m --- Failed to send ACK to Client ----\033[0m\n");
        }

        return NULL;
    } else {
        printf("Invalid command\n");
    }


}

void *handling_client(void *arg) {
    struct sockaddr_in server_addr_for_client = *(struct sockaddr_in *) arg;
    if (bind(server_socket_for_client, (struct sockaddr *) &server_addr_for_client, sizeof(server_addr_for_client)) <
        0) {
        // perror("Bind failed Client");
        printErrorDetails(41);
        close(server_socket_for_ss);
        close(server_socket_for_client);
        exit(EXIT_FAILURE);
    }
    printf("Naming Server for client is running on port %d...\n", CLIENT_PORT_NS);
    if (listen(server_socket_for_client, 5) < 0) {
        // perror("Listen failed Client");
        printErrorDetails(40);
        close(server_socket_for_ss);
        close(server_socket_for_client);
        exit(EXIT_FAILURE);
    }
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_size = sizeof(client_addr);
        int client_socket = accept(server_socket_for_client, (struct sockaddr *) &client_addr, &addr_size);
        if (client_socket < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No connections yet, continue to the next iteration
                continue;
            } else {
                // perror("Accept failed");
                printErrorDetails(42);
                continue;
            }
        }
        printf("Got a client\n");
        // Create a thread to handle the Storage Server

        // log
        char* log_message = (char*)calloc(BUFFER_SIZE, sizeof(char));
        sprintf(log_message, "Accepted connection from Client fd[%d]", client_socket);
        log_it(inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), log_message);
        free(log_message);
        // log done

        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, process_client, &client_socket) != 0) {
            // perror("Failed to create thread");
            printErrorDetails(43);
            close(client_socket);
        }
    }
    return NULL;
}


int main() {

    // clear log file upon initailization if ns.
    FILE* file = fopen("log.txt", "w");
    fclose(file);

    initialize_cache();

    signal(SIGINT, ctrlCHandler);  // Register the signal handler
    signal(SIGTSTP, handle_ctrl_z);
    signal(SIGPIPE, SIG_IGN);
    sem_init(&storage_semaphore, 0, 1); // Initialize semaphore with value 1


    struct sockaddr_in server_addr_for_ss, server_addr_for_client;

    // Create the server socket for ss and client
    if ((server_socket_for_ss = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        // perror("Socket creation failed");
        printErrorDetails(7);
        exit(EXIT_FAILURE);
    }
    if ((server_socket_for_client = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        // perror("Socket creation failed");
        printErrorDetails(7);
        exit(EXIT_FAILURE);
    }


    // Making the sockets non-blocking
    int reuse = 1;

// Enable port reuse on server_socket_for_ss
    if (setsockopt(server_socket_for_ss, SOL_SOCKET, SO_REUSEADDR, (char *) &reuse, sizeof(reuse)) < 0) {
        // perror("setsockopt SO_REUSEADDR failed");
        printErrorDetails(44);
        close(server_socket_for_ss);
        close(server_socket_for_client);
        return 1;
    }

// Enable port reuse on server_socket_for_client
    if (setsockopt(server_socket_for_client, SOL_SOCKET, SO_REUSEADDR, (char *) &reuse, sizeof(reuse)) < 0) {
        // perror("setsockopt SO_REUSEADDR failed");
        printErrorDetails(44);
        close(server_socket_for_ss);
        close(server_socket_for_client);
        return 1;
    }

// Also set TCP_NODELAY for both sockets
    int flag = 1;
    if (setsockopt(server_socket_for_ss, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof(flag)) < 0) {
        // perror("setsockopt TCP_NODELAY failed");
        printErrorDetails(44);
        close(server_socket_for_ss);
        close(server_socket_for_client);
        return 1;
    }

    if (setsockopt(server_socket_for_client, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof(flag)) < 0) {
        // perror("setsockopt TCP_NODELAY failed");
        printErrorDetails(44);
        close(server_socket_for_ss);
        close(server_socket_for_client);
        return 1;
    }



    // Configure server address
    server_addr_for_ss.sin_family = AF_INET;
    server_addr_for_ss.sin_addr.s_addr = INADDR_ANY;
    server_addr_for_ss.sin_port = htons(SERVER_PORT);

    // For client
    server_addr_for_client.sin_family = AF_INET;
    server_addr_for_client.sin_addr.s_addr = INADDR_ANY;
    server_addr_for_client.sin_port = htons(CLIENT_PORT_NS);



    // Accept connections
    pthread_t thread_ss, thread_client;
    if (pthread_create(&thread_ss, NULL, handle_ss, &server_addr_for_ss) != 0) {
        // perror("Failed to create thread");
        printErrorDetails(43);
        close(server_socket_for_ss);
        close(server_socket_for_client);
    }
    if (pthread_create(&thread_client, NULL, handling_client, &server_addr_for_client) != 0) {
        // perror("Failed to create thread");
        printErrorDetails(43);
        close(server_socket_for_ss);
        close(server_socket_for_client);
    }



    // Create a new socket for listening to async
    int async_socket;
    if ((async_socket = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

// Enable port reuse on async_socket
    if (setsockopt(async_socket, SOL_SOCKET, SO_REUSEADDR, (char *) &reuse, sizeof(reuse)) < 0) {
        perror("setsockopt SO_REUSEADDR failed");
        close(async_socket);
        return 1;
    }

// Set TCP_NODELAY for async_socket
    if (setsockopt(async_socket, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof(flag)) < 0) {
        perror("setsockopt TCP_NODELAY failed");
        close(async_socket);
        return 1;
    }

// Bind the socket to a port
    struct sockaddr_in async_addr;
    async_addr.sin_family = AF_INET;
    async_addr.sin_addr.s_addr = INADDR_ANY;
    async_addr.sin_port = htons(ASYNC_PORT_FOR_NM);

    if (bind(async_socket, (struct sockaddr *) &async_addr, sizeof(async_addr)) < 0) {
        perror("Bind failed");
        close(async_socket);
        return 1;
    }

// Start listening for incoming connections
    if (listen(async_socket, 5) < 0) {
        perror("Listen failed");
        close(async_socket);
        return 1;
    }

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


    pthread_join(thread_ss, NULL);
    pthread_join(thread_client, NULL);
    close(server_socket_for_ss);
    close(server_socket_for_client);
    return 0;
}
