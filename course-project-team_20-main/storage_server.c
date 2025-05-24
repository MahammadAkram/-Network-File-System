#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>

#define NAMING_SERVER_IP "127.0.0.1" // IP address of Naming Server
#define NAMING_SERVER_PORT 8080      // Port of Naming Server
#define SS_NAMING_PORT 9002          // Port for Storage Server's communication with Naming Server
#define SS_CLIENT_PORT 9003          // Port for client communication
#define BUFFER_SIZE 256
#define MAX_PATH 256
#define MAX_FILES 100
#define CHUNK_SIZE 32

#define ACK_DONE 1
#define ACK_FAIL 0
#define DIR_FAIL_ACK 2
#define AWRITE_ACK 3

#pragma pack(push, 1)
typedef struct
{
    char filepath[BUFFER_SIZE];
    char *complete_data; // Pointer to full data
    size_t data_length;
    int client_fd;
} write_task;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct SS_response
{
    int ssport;
    int clientport;
    char ip[20];
    int path_count;
} SS_response;
SS_response SS1;

typedef struct SS_details
{
    int ssport;
    int clientport;
    char ip[20];
} SS_details;
SS_details SS1d;

typedef struct
{
    char filepath[MAX_PATH];
    bool is_writing;
    pthread_cond_t cond;
    pthread_mutex_t mutex;
} FileEntry;

FileEntry files[MAX_FILES];
int file_count = 0;

// int NAMING_SERVER_PORT;
// char* NAMING_SERVER_IP;

char paths[MAX_FILES][MAX_PATH];
#pragma pack(pop)
int naming_server_socket, client_socket;

typedef struct
{
    char *file_path;
    char *data;
    size_t data_size;
    int client_fd;
} async_write_args;

void handle_signal(int signum)
{
    close(naming_server_socket);
    close(client_socket);
    exit(signum);
}

int find(const char *path)
{
    for (int i = 0; i < file_count; i++)
    {
        if (strcmp(files[i].filepath, path) == 0)
        {
            return i;
        }
    }
    return -1; // File not found
}

int add(const char *path)
{
    if (file_count >= MAX_FILES)
    {
        printf("Error: File array is full.\n");
        return -1;
    }

    if (find(path) != -1)
    {
        printf("Error: File already exists.\n");
        return -1;
    }

    strncpy(files[file_count].filepath, path, MAX_PATH);
    files[file_count].is_writing = false;
    pthread_cond_init(&files[file_count].cond, NULL);
    pthread_mutex_init(&files[file_count].mutex, NULL);
    file_count++;

    return 0; // Successfully added
}

int delete_path(const char *path)
{
    int index = find(path);
    if (index == -1)
    {
        printf("Error: File not found.\n");
        return -1;
    }

    pthread_mutex_destroy(&files[index].mutex);
    pthread_cond_destroy(&files[index].cond);

    for (int i = index; i < file_count - 1; i++)
    {
        files[i] = files[i + 1];
    }

    file_count--;
    return 0; // Successfully deleted
}

void *async_write(void *args)
{
    async_write_args *write_args = (async_write_args *)args;
    int index = find(write_args->file_path);
    pthread_mutex_lock(&(files[index].mutex));
    while(files[index].is_writing == 1)
    {
        pthread_cond_wait(&(files[index].cond),&(files[index].mutex));
    }
    files[index].is_writing = 1;
    pthread_mutex_unlock(&(files[index].mutex));

    FILE *file = fopen(write_args->file_path, "a");
    if (!file)
    {
        perror("Error opening file for asynchronous write");
        const char *error_message = "ERROR: Unable to open file.";
        send(write_args->client_fd, error_message, strlen(error_message), 0);
        free(write_args->file_path);
        free(write_args->data);
        free(write_args);
        pthread_exit(NULL);
    }

    size_t bytes_written = 0;
    while (bytes_written < write_args->data_size)
    {
        printf("bytes written: %ld\n", bytes_written);
        size_t chunk_size = (write_args->data_size - bytes_written > CHUNK_SIZE)
                                ? CHUNK_SIZE
                                : write_args->data_size - bytes_written;

        if (fwrite(write_args->data + bytes_written, 1, chunk_size, file) != chunk_size)
        {
            perror("Error writing to file");
            fclose(file);
            const char *error_message = "ERROR: Failed during write.";
            send(write_args->client_fd, error_message, strlen(error_message), 0);
            free(write_args->file_path);
            free(write_args->data);
            free(write_args);
            pthread_exit(NULL);
        }

        bytes_written += chunk_size;
        sleep(4);
    }

    fclose(file);

    pthread_mutex_lock(&files[index].mutex);
    files[index].is_writing = 0;
    pthread_cond_signal(&(files[index].cond));
    pthread_mutex_unlock(&files[index].mutex);

    // Send acknowledgment to client

    free(write_args->file_path);
    free(write_args->data);
    free(write_args);
    pthread_exit(NULL);
}

void delete(char path[MAX_PATH])
{
    int temp = 0;
    for (int i = 0; i < SS1.path_count; i++)
    {
        if (strcmp(paths[i], path) == 0)
        {
            temp = i;
            break;
        }
    }
    for (int i = temp; i < SS1.path_count; i++)
    {
        strcpy(paths[i], paths[i + 1]);
    }
    SS1.path_count--;
}
void create(char path[MAX_PATH])
{
    strcpy(paths[SS1.path_count++], path);
}

int delete_directory(char *path)
{
    DIR *dir = opendir(path);
    if (!dir)
    {
        perror("Failed to open directory");
        return -1;
    }

    struct dirent *entry;
    char filepath[BUFFER_SIZE];
    struct stat path_stat;
    delete (path);

    while ((entry = readdir(dir)) != NULL)
    {
        // Skip "." and ".." entries
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        // Check if the constructed path fits into the buffer
        if (snprintf(filepath, sizeof(filepath), "%s/%s", path, entry->d_name) >= sizeof(filepath))
        {
            fprintf(stderr, "Path too long: %s/%s\n", path, entry->d_name);
            closedir(dir);
            return -1;
        }

        // Get the status of the file
        if (stat(filepath, &path_stat) == -1)
        {
            perror("Failed to get file status");
            closedir(dir);
            return -1;
        }

        if (S_ISDIR(path_stat.st_mode))
        {
            // Recursively delete subdirectory
            if (delete_directory(filepath) != 0)
            {
                closedir(dir);
                return -1;
            }
        }
        else
        {
            // Delete file
            if (unlink(filepath) != 0)
            {
                perror("Failed to delete file");
                closedir(dir);
                return -1;
            }
            delete (filepath);
            delete_path(filepath);
        }
    }

    closedir(dir);

    // Delete the now-empty directory
    if (rmdir(path) != 0)
    {
        perror("Failed to delete directory");
        return -1;
    }
    delete (path);
    delete_path(path);

    return 0;
}

void *receive_commands(void *args)
{
    char buffer[BUFFER_SIZE];

    while (1)
    {
        // Receive message from Naming Server
        int bytes_received = recv(naming_server_socket, buffer, sizeof(buffer) + 1, 0);
        if (bytes_received > 0)
        {
            buffer[bytes_received] = '\0';
            printf("Command received from Naming Server: %s\n", buffer);

            // Parse and execute command
            char *command = strtok(buffer, " ");
            char *path = strtok(NULL, " ");

            if (command && strcmp(command, "CREATE") == 0 && path)
            {
                // Perform file creation
                char *filename = strtok(NULL, " ");

                if (filename)
                {
                    char filepath[BUFFER_SIZE];
                    snprintf(filepath, sizeof(filepath), "%s/%s", path, filename);
                    if (filename[strlen(filename) - 1] == '/')
                    {
                        if (mkdir(filepath, 0755) == -1) // 0755 is a common mode for directories
                        {
                            perror("mkdir failed");
                            continue;
                        }
                        int ack = ACK_DONE;
                        printf("Directory created succesfully\n");
                        send(naming_server_socket, &ack, sizeof(ack), 0);
                    }
                    else
                    {
                        FILE *file = fopen(filepath, "w");
                        if (file)
                        {
                            int ack = ACK_DONE;
                            send(naming_server_socket, &ack, sizeof(ack), 0);
                            fclose(file);
                            add(filepath);
                            printf("File created: %s\n", filepath);
                            
                        }
                        else
                        {
                            int ack = ACK_FAIL;
                            send(naming_server_socket, &ack, sizeof(ack), 0);
                            perror("File creation failed");

                        }
                    }
                }
            }
            else if (strcmp(command, "COPY") == 0)
            {
                char *dest_path = strtok(NULL, " ");
                char *dest_ip = strtok(NULL, " ");
                char *dest_port = strtok(NULL, " ");
                printf("Source file: %s, Destination file: %s\n", path, dest_path);

                // Open source file to read its contents
                FILE *source_file = fopen(path, "rb");
                if (!source_file)
                {
                    printf("!SOURCE_PATH\n");
                    perror("Failed to open source file for reading");
                    int ack = ACK_FAIL;
                    send(naming_server_socket, &ack, sizeof(ack), 0);
                    continue;
                }
                printf("%s %s\n", dest_ip, SS1.ip);

                // If source and destination are on the same storage server

                if (strcmp(dest_ip, SS1.ip) == 0)
                {
                    // Open file with O_TRUNC to overwrite
                    int dest_fd = open(dest_path, O_WRONLY | O_TRUNC, 0644);
                    if (dest_fd == -1)
                    {
                        perror("Failed to open destination file");
                        int ack = ACK_FAIL;
                        send(naming_server_socket, &ack, sizeof(ack), 0);
                        fclose(source_file);
                        continue;
                    }

                    // Copy the file contents using low-level file operations
                    char file_buffer[BUFFER_SIZE];
                    ssize_t bytes_read;
                    while ((bytes_read = fread(file_buffer, 1, sizeof(file_buffer), source_file)) > 0)
                    {
                        if (write(dest_fd, file_buffer, bytes_read) != bytes_read)
                        {
                            perror("Failed to write to destination file");
                            close(dest_fd);
                            fclose(source_file);
                            int ack = ACK_FAIL;
                            send(naming_server_socket, &ack, sizeof(ack), 0);
                            continue;
                        }
                    }

                    close(dest_fd);
                    printf("File copied successfully on the same storage server.\n");
                    int ack = ACK_DONE; // Acknowledge successful copy
                    send(naming_server_socket, &ack, sizeof(ack), 0);
                }
                else
                {
                    // If the source and destination are on different storage servers
                    // Send the file content to the destination storage server

                    char temp_buffer[BUFFER_SIZE];
                    size_t bytes_read;
                    while ((bytes_read = fread(temp_buffer, 1, sizeof(temp_buffer), source_file)) > 0)
                    {
                        send(naming_server_socket, temp_buffer, bytes_read, 0);
                    }

                    // Indicate the end of file transfer
                    send(naming_server_socket, "[EOF]", 5, 0);
                    printf("File contents sent to destination server: %s\n", dest_ip);

                    int ack;
                    // Wait for acknowledgment from the destination server
                    recv(naming_server_socket, &ack, sizeof(ack), 0);
                    if (ack == ACK_DONE)
                    {
                        printf("COPY operation successful between different servers.\n");
                    }
                    else
                    {
                        printf("COPY operation failed between different servers.\n");
                    }
                }

                fclose(source_file); // Close the source file after the operation
            }
            else if (command && strcmp(command, "DELETE") == 0 && path)
            {
                struct stat path_stat;

                // Check if path exists and determine if it is a file or directory
                if (stat(path, &path_stat) == -1)
                {
                    perror("Path not found");
                    continue;
                }

                if (S_ISDIR(path_stat.st_mode))
                {
                    // Delete directory recursively
                    if (delete_directory(path) == 0)
                    {
                        printf("Directory deleted: %s\n", path);
                        int ack = DIR_FAIL_ACK;
                        send(naming_server_socket, &ack, sizeof(ack), 0);
                    }
                }
                else
                {
                    // Delete single file
                    if (remove(path) == 0)
                    {
                        delete (path);
                        delete_path(path);
                        printf("File deleted: %s\n", path);
                        int ack = ACK_DONE;
                        send(naming_server_socket, &ack, sizeof(ack), 0);
                    }
                    else
                    {
                        perror("File deletion failed");
                    }
                }
            }
            else
            {
                printf("Invalid command received from Naming Server\n");
            }
        }
        else if (bytes_received == 0)
        {
            printf("Connection closed by Naming Server\n");
            break;
        }
        else
        {
            perror("Failed to receive message from Naming Server");
            break;
        }
    }

    return NULL;
}

void *handleclient(void *args)
{
    int client_fd = *(int *)args;
    char request[BUFFER_SIZE];
    int bytes_received = recv(client_fd, request, BUFFER_SIZE, 0);
    printf("%ld\n", strlen(request));
    request[strlen(request) - 1] = '\0';

    if (bytes_received > 0)
    {
        printf("Message received from client: %s\n", request);

        char *token = strtok(request, " ");
        if (strcmp(token, "READ") == 0)
        {
            token = strtok(NULL, " "); // This token is the file path
            printf("%s\n", token);
            if (token)
            {
                FILE *file = fopen(token, "r");
                if (file)
                {
                    char file_buffer[BUFFER_SIZE];
                    while (fgets(file_buffer, sizeof(file_buffer), file) != NULL)
                    {
                        // Send file content to the client
                        send(client_fd, file_buffer, strlen(file_buffer), 0);
                    }
                    fclose(file);

                    // Send end-of-file marker to the client
                    const char *eof_marker = "[EOF]";
                    send(client_fd, eof_marker, strlen(eof_marker), 0);
                }
                else
                {
                    perror("Error opening file");
                    const char *error_message = "ERROR: Unable to open file.";
                    send(client_fd, error_message, strlen(error_message), 0);
                }
            }
            else
            {
                const char *error_message = "ERROR: File path not specified.";
                send(client_fd, error_message, strlen(error_message), 0);
            }
        }
        else if (strcmp(token, "WRITE") == 0)
        {
            char *filepath = strtok(NULL, " ");
            char *data = strtok(NULL, "\n");

            if (filepath && data)
            {
                int ack = 10;
                if (strlen(data) > 128)
                {
                    ack = 11;
                    send(client_fd, &ack, sizeof(ack), 0);
                    size_t data_size = strlen(data);

                    // Prepare arguments for asynchronous writing
                    async_write_args *write_args = malloc(sizeof(async_write_args));
                    write_args->file_path = strdup(filepath);
                    write_args->data = malloc(data_size + 1);
                    strcpy(write_args->data, data);
                    write_args->data_size = data_size;
                    write_args->client_fd = client_fd;

                    // Create a thread for asynchronous writing
                    pthread_t write_thread;
                    if (pthread_create(&write_thread, NULL, async_write, write_args) != 0)
                    {
                        perror("Error creating thread for AWRITE");
                        free(write_args->file_path);
                        free(write_args->data);
                        free(write_args);
                        const char *error_message = "ERROR: Failed to start async write.";
                        send(client_fd, error_message, strlen(error_message), 0);
                    }
                    else
                    {
                        pthread_detach(write_thread); // Allow thread to execute independently
                        printf("AWRITE operation started for file: %s\n", filepath);
                    }
                }
                else
                {
                    send(client_fd, &ack, sizeof(ack), 0);
                    int index = find(filepath);
                    pthread_mutex_lock(&files[index].mutex);
                    while(files[index].is_writing == 1)
                    {
                        pthread_cond_wait(&(files[index].cond), &(files[index].mutex));
                    }
                    files[index].is_writing = 1;
                    pthread_mutex_unlock(&files[index].mutex);
                    FILE *file = fopen(filepath, "a");
                    if (file)
                    {
                        fprintf(file, "%s\n", data);
                        fclose(file);
                        printf("Data written to file '%s': %s\n", filepath, data);
                        pthread_mutex_lock(&files[index].mutex);
                        files[index].is_writing = 0;
                        pthread_cond_signal(&(files[index].cond));
                        pthread_mutex_unlock(&files[index].mutex);

                        const char *ack_message = "Sync write operation completed successfully";
                        send(client_fd, ack_message, strlen(ack_message), 0);
                    }
                    else
                    {
                        perror("Failed to open file for writing");
                        const char *error_message = "ERROR: Failed to open file for writing";
                    }
                }
            }
            else
            {
                printf("Invalid WRITE command: Missing filepath or data\n");
                const char *error_message = "ERROR: Invalid WRITE command";
            }
        }
        else if (strcmp(token, "INFO") == 0)
        {
            token = strtok(NULL, " "); // This token is the file path
            printf("INFO command received for file: %s\n", token);
            if (token)
            {
                struct stat file_stat;
                if (stat(token, &file_stat) == -1)
                {
                    perror("Error retrieving file metadata");
                    const char *error_message = "ERROR: Unable to retrieve file metadata.";
                    send(client_fd, error_message, strlen(error_message), 0);
                }
                else
                {
                    // Format metadata into a string
                    char metadata_buffer[BUFFER_SIZE];
                    snprintf(metadata_buffer, sizeof(metadata_buffer),
                             "Metadata for: %s\n"
                             "File Size: %ld bytes\n"
                             "Number of Links: %ld\n"
                             "File Inode: %ld\n"
                             "File Permissions: %c%c%c%c%c%c%c%c%c\n"
                             "Last Access Time: %s"
                             "Last Modification Time: %s"
                             "Last Status Change Time: %s",
                             token,
                             file_stat.st_size,
                             file_stat.st_nlink,
                             file_stat.st_ino,
                             (file_stat.st_mode & S_IRUSR) ? 'r' : '-',
                             (file_stat.st_mode & S_IWUSR) ? 'w' : '-',
                             (file_stat.st_mode & S_IXUSR) ? 'x' : '-',
                             (file_stat.st_mode & S_IRGRP) ? 'r' : '-',
                             (file_stat.st_mode & S_IWGRP) ? 'w' : '-',
                             (file_stat.st_mode & S_IXGRP) ? 'x' : '-',
                             (file_stat.st_mode & S_IROTH) ? 'r' : '-',
                             (file_stat.st_mode & S_IWOTH) ? 'w' : '-',
                             (file_stat.st_mode & S_IXOTH) ? 'x' : '-',
                             ctime(&file_stat.st_atime),
                             ctime(&file_stat.st_mtime),
                             ctime(&file_stat.st_ctime));

                    // Send metadata to the client
                    send(client_fd, metadata_buffer, strlen(metadata_buffer), 0);

                    // Send end-of-metadata marker to the client
                    const char *eof_marker = "[EOF]";
                    send(client_fd, eof_marker, strlen(eof_marker), 0);
                }
            }
            else
            {
                const char *error_message = "ERROR: File path not specified.";
                send(client_fd, error_message, strlen(error_message), 0);
            }
        }
        else if (strcmp(token, "AWRITE") == 0)
        {
            int ack = AWRITE_ACK;
            send(naming_server_socket, &ack, sizeof(ack), 0);
        }
        else if (strcmp(token, "STREAM") == 0)
        {
            token = strtok(NULL, " "); // This token is the file path
            if (token)
            {
                FILE *file = fopen(token, "rb");
                if (file)
                {
                    char file_buffer[BUFFER_SIZE];
                    size_t bytes_read;
                    while ((bytes_read = fread(file_buffer, 1, sizeof(file_buffer), file)) > 0)
                    {
                        send(client_fd, file_buffer, bytes_read, 0);
                    }
                    fclose(file);
                    printf("File streamed to client: %s\n", token);
                }
                else
                {
                    perror("Error opening file for streaming");
                    const char *error_message = "ERROR: Unable to open file for streaming.";
                    send(client_fd, error_message, strlen(error_message), 0);
                }
            }
            else
            {
                const char *error_message = "ERROR: File path not specified.";
                send(client_fd, error_message, strlen(error_message), 0);
            }
        }
        else if (strcmp(token, "STREAM") == 0)
        {
            token = strtok(NULL, " "); // This token is the file path
            if (token)
            {
                FILE *file = fopen(token, "rb");
                if (file)
                {
                    char file_buffer[BUFFER_SIZE];
                    size_t bytes_read;
                    while ((bytes_read = fread(file_buffer, 1, sizeof(file_buffer), file)) > 0)
                    {
                        send(client_fd, file_buffer, bytes_read, 0);
                    }
                    fclose(file);
                    printf("File streamed to client: %s\n", token);
                }
                else
                {
                    perror("Error opening file for streaming");
                    const char *error_message = "ERROR: Unable to open file for streaming.";
                    send(client_fd, error_message, strlen(error_message), 0);
                }
            }
            else
            {
                const char *error_message = "ERROR: File path not specified.";
                send(client_fd, error_message, strlen(error_message), 0);
            }
        }
        else
        {
            const char *error_message = "ERROR: Unsupported command.";
            send(client_fd, error_message, strlen(error_message), 0);
        }
    }
    else if (bytes_received == 0)
    {
        printf("Client disconnected.\n");
    }

    else
    {
        perror("Failed to receive message from client");
    }

    close(client_fd);
    pthread_exit(NULL); // End the thread
}

void *acceptclient(void *args)
{

    while (1)
    {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);

        int *client_fd = malloc(sizeof(int));
        if (!client_fd)
        {
            perror("Memory allocation failed");
            continue;
        }

        *client_fd = accept(client_socket, (struct sockaddr *)&client_addr, &client_addr_len);
        if (*client_fd < 0)
        {
            perror("Client connection acceptance failed");
            free(client_fd);
            continue;
        }

        printf("Client connected.\n");

        pthread_t client_thread;
        pthread_create(&client_thread, NULL, handleclient, client_fd);
        pthread_detach(client_thread); // Detach to prevent memory leaks
    }

    return NULL;
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

    // Create socket to connect to the Naming Server
    naming_server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (naming_server_socket < 0)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(naming_server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("setsockopt failed");
        close(naming_server_socket);
        exit(EXIT_FAILURE);
    }

    // Set up Naming Server address
    nm_addr.sin_family = AF_INET;
    nm_addr.sin_port = htons(NAMING_SERVER_PORT);
    if (inet_pton(AF_INET, NAMING_SERVER_IP, &nm_addr.sin_addr) <= 0)
    {
        perror("Invalid address or Address not supported");
        close(naming_server_socket);
        exit(EXIT_FAILURE);
    }

    // Connect to the Naming Server
    if (connect(naming_server_socket, (struct sockaddr *)&nm_addr, sizeof(nm_addr)) < 0)
    {
        perror("Connection to Naming Server failed");
        close(naming_server_socket);
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in ss_client_addr;
    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket < 0)
    {
        perror("Client socket creation failed");
        exit(EXIT_FAILURE);
    }
    if (setsockopt(client_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("setsockopt failed");
        close(client_socket);
        exit(EXIT_FAILURE);
    }

    ss_client_addr.sin_family = AF_INET;
    ss_client_addr.sin_addr.s_addr = INADDR_ANY;
    ss_client_addr.sin_port = htons(SS_CLIENT_PORT);

    if (bind(client_socket, (struct sockaddr *)&ss_client_addr, sizeof(ss_client_addr)) < 0)
    {
        perror("Client socket binding failed");
        close(client_socket);
        exit(EXIT_FAILURE);
    }

    if (listen(client_socket, 5) < 0)
    {
        perror("Client socket listen failed");
        close(client_socket);
        exit(EXIT_FAILURE);
    }
    SS1.clientport = 9003;
    SS1.ssport = 9002;
    strcpy(SS1.ip, "127.0.0.1");
    SS1.path_count = 0;
    printf("%d\n", SS1.path_count);
    SS1d.clientport = SS1.clientport;
    SS1d.ssport = SS1.ssport;
    strcpy(SS1d.ip, SS1.ip);
    printf("Sending Normal Registration message to Naming Server...\n");
    if (send(naming_server_socket, &SS1d, sizeof(SS1d), 0) == -1)
    {
        perror("Send failed");
    }
    else
    {
        printf("Normal Registration message sent to Naming Server\n");
    }
    int id;
    size_t bytes_received = recv(naming_server_socket, &id, sizeof(int), 0);
    if (bytes_received > 0)
    {
        if (id == 0)
        {
            printf("Enter Paths AND Enter END to stop input paths:\n");
            while (1)
            {
                char temp[MAX_PATH];
                scanf("%s", temp);
                if (strcmp(temp, "END") == 0)
                    break;
                getchar();
                temp[strlen(temp)] = '\0';
                if (SS1.path_count < MAX_FILES)
                {
                    strcpy(paths[SS1.path_count++], temp);
                }
                else
                {
                    printf("Max paths reached\n");
                    break;
                }
            }
            for (int i = 0; i < SS1.path_count; i++)
            {
                printf("Path %d: %s\n", i + 1, paths[i]);
            }
            if (send(naming_server_socket, &SS1, sizeof(SS1), 0) == -1)
            {
                perror("Send failed");
            }
            else
            {
                printf("INITIAL Registration message sent to Naming Server\n");
            }
            printf("Sending PATHS....\n");
            for (int i = 0; i < SS1.path_count; i++)
            {
                ssize_t bytes_sent = send(naming_server_socket, paths[i], MAX_PATH, 0);

                if (bytes_sent <= 0)
                {
                    perror("Failed to send path");
                    printf("Error sending path %d\n", i);
                    continue;
                }
            }
        }
        else
        {
            printf("Reconnecting with Naming server\n");
            int totpaths;
            recv(naming_server_socket, &totpaths, sizeof(totpaths), 0);
            SS1.path_count = totpaths;
            char temp[MAX_PATH];
            for (int i = 0; i < totpaths; i++)
            {
                recv(naming_server_socket, &temp, sizeof(temp), 0);
                strcpy(paths[i], temp);
            }
        }
    }

    // Create a thread to handle incoming commands from the Naming Server
    pthread_t command_thread;
    pthread_create(&command_thread, NULL, receive_commands, NULL);

    pthread_t clientthread;
    pthread_create(&clientthread, NULL, acceptclient, NULL);

    // Wait for the command handling thread to finish
    pthread_join(command_thread, NULL);

    // Close the socket when done
    close(naming_server_socket);

    return 0;
}