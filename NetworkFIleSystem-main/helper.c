


#include <netinet/in.h>
#include "helper.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>  // For fcntl
#include <errno.h>  // For errno
#include <ctype.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <pwd.h>
#include <unistd.h>
#include <limits.h>
#include "helper.h"

#define MAX_WORDS 4096


Command commands[] = {

        {"mkdir", (CommandFunction) create_directory},
        {"rmdir", (CommandFunction) delete_directory},
        {"touch", (CommandFunction) create_file},
        {"rm",    (CommandFunction) remove_file}
};
size_t num_commands = sizeof(commands) / sizeof(commands[0]);

// Modified recv function to handle errors and disconnects
ssize_t recv_good(int sockfd, void *buf, size_t len) {
    ssize_t bytes_received = recv(sockfd, buf, len, 0);
    if (bytes_received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0; // No data available, just return
        } else {
            perror("recv failed");
            return -1; // An error occurred
        }
    }
    return bytes_received;
}

// Modified send function to handle errors
ssize_t send_good(int sockfd, const void *buf, size_t len) {
    char str[len + 1];
    memcpy(str, buf, len + 1);
    str[len] = '\0';
    ssize_t bytes_sent = send(sockfd, str, len, 0);
    if (bytes_sent < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0; // Socket is not ready to send data, just return
        } else {
            perror("send failed");
            return -1; // An error occurred
        }
    }
    return bytes_sent;
}


// https://chatgpt.com/share/6734fa7e-8ce8-8011-9a03-3cae826783eb for the file/folder handling
// Helper function to parse command into arguments
void parse_command(char *command, char **args) {
    int i = 0;
    char *token = strtok(command, " ");
    while (token != NULL) {
        args[i++] = strdup(token);
        token = strtok(NULL, " ");
    }
}

// sending meta data

void send_file_metadata(const char *filepath, int sockfd) {
    struct stat fileStat;
    char buffer[1024];    // Buffer to hold the metadata
    char permissions[11]; // For file permissions
    char fullpath[256]; // Full path to handle relative paths

    // Use stat to get the metadata of the file

    if (stat(filepath, &fileStat) == -1) {
        perror("Error retrieving file metadata");
        snprintf(buffer, sizeof(buffer), "Error retrieving file metadata for %s\n", filepath);
        send(sockfd, buffer, strlen(buffer), 0);
        return;
    }

    // Generate file permissions string
    snprintf(permissions, sizeof(permissions), "%c%c%c%c%c%c%c%c%c%c",
             (S_ISDIR(fileStat.st_mode)) ? 'd' : '-',
             (fileStat.st_mode & S_IRUSR) ? 'r' : '-',
             (fileStat.st_mode & S_IWUSR) ? 'w' : '-',
             (fileStat.st_mode & S_IXUSR) ? 'x' : '-',
             (fileStat.st_mode & S_IRGRP) ? 'r' : '-',
             (fileStat.st_mode & S_IWGRP) ? 'w' : '-',
             (fileStat.st_mode & S_IXGRP) ? 'x' : '-',
             (fileStat.st_mode & S_IROTH) ? 'r' : '-',
             (fileStat.st_mode & S_IWOTH) ? 'w' : '-',
             (fileStat.st_mode & S_IXOTH) ? 'x' : '-');

    // Format metadata into the buffer
    snprintf(buffer, sizeof(buffer),
             "Metadata for file or path: %s\n"
             "-----------------------------------\n"
             "File Size: %lld bytes\n"
             "Number of Blocks: %lld\n"
             "Block Size: %lld bytes\n"
             "File Permissions: %s\n"
             "Number of Links: %ld\n"
             "File Owner (UID): %d\n"
             "File Group (GID): %d\n"
             "Last Access Time: %s"
             "Last Modification Time: %s"
             "Last Status Change Time: %s"
             "-----------------------------------\n",
             filepath,
             (long long) fileStat.st_size,
             (long long) fileStat.st_blocks,
             (long long) fileStat.st_blksize,
             permissions,
             (long) fileStat.st_nlink,
             fileStat.st_uid,
             fileStat.st_gid,
             ctime(&fileStat.st_atime),
             ctime(&fileStat.st_mtime),
             ctime(&fileStat.st_ctime));

    // Send metadata over the socket
    if (send(sockfd, buffer, strlen(buffer), 0) == -1) {
        perror("Error sending file metadata over socket");
    }
}

// Create a directory
int create_directory(char **args, char *buffer2) {

    if (args[1] == NULL) {
        snprintf(buffer2, BUFFER_SIZE, "Invalid command format\n");
        fprintf(stderr, "%s", buffer2);
        return -1;
    }


    if (mkdir(args[1], 0777) == -1) {
        snprintf(buffer2, BUFFER_SIZE, "Could not create folder @ %s\n", args[1]);
        printf("%s", buffer2);
        perror("mkdir");
        return -1;
    }
    snprintf(buffer2, BUFFER_SIZE, "Created folder @ %s\n", args[1]);
    printf("%s", buffer2);
    return 0;
}

// Delete a directory
int delete_directory(char **args, char *buffer2) {

    printf("delete dir");
    if (args[1] == NULL) {
        snprintf(buffer2, BUFFER_SIZE, "Invalid command format\n");
        fprintf(stderr, "%s", buffer2);
        return -1;
    }


    if (rmdir(args[1]) == -1) {
        snprintf(buffer2, BUFFER_SIZE, "Could not delete folder @ %s\n", args[1]);
        printf("%s", buffer2);
        perror("rmdir");
        return -1;
    }
    snprintf(buffer2, BUFFER_SIZE, "Deleted folder @ %s\n", args[1]);
    printf("%s", buffer2);
    return 0;
}

// Create a file
int create_file(char **args, char *buffer2) {


    if (args[1] == NULL) {
        snprintf(buffer2, BUFFER_SIZE, "Invalid command format\n");
        fprintf(stderr, "%s", buffer2);
        return -1;
    }

    FILE *file = fopen(args[1], "w");
    if (file == NULL) {
        snprintf(buffer2, BUFFER_SIZE, "Could not create file @ %s\n", args[1]);
        printf("%s", buffer2);
        perror("fopen");
        return -1;
    }
    snprintf(buffer2, BUFFER_SIZE, "Created file @ %s\n", args[1]);
    printf("%s", buffer2);
    fclose(file);
    return 0;
}


// List files in a directory
int list_file(char **args, int sock) {

    if (args[1] == NULL) {
        const char *end_msg = "Invalid Command Format";
        if (send_good(sock, end_msg, strlen(end_msg)) < 0) {
            perror("Failed to send end message");
            return -1;
        }
        fprintf(stderr, "Invalid command format\n");
        return -1;
    }

    DIR *dir = opendir(args[1]);
    if (dir == NULL) {
        printf("Could not list folder @ %s\n", args[1]);
        char buffer2[BUFFER_SIZE];
        snprintf(buffer2, BUFFER_SIZE, "Could not list folder @ %s\n", args[1]);
        if (send_good(sock, buffer2, strlen(buffer2)) < 0) {
            perror("Failed to send end message");
            closedir(dir);
            return -1;
        }
        perror("opendir");
        return -1;
    }

    struct dirent *entry;
    char buffer[BUFFER_SIZE];
    size_t offset = 0;

    while ((entry = readdir(dir)) != NULL) {
        char color_code[10] = "\033[0m"; // Default color
        if (entry->d_type == DT_DIR) {
            snprintf(color_code, sizeof(color_code), "\033[34m"); // Blue for directories
        } else if (entry->d_type == DT_REG) {
            snprintf(color_code, sizeof(color_code), "\033[0m"); // Default for regular files
        }

        int len = snprintf(buffer + offset, BUFFER_SIZE - offset, "%s%s\033[0m\n", color_code, entry->d_name);
        if (len < 0 || len >= BUFFER_SIZE - offset) {
            if (send_good(sock, buffer, offset) < 0) {
                perror("Failed to send directory listing");
                closedir(dir);
                return -1;
            }
            offset = 0;
            len = snprintf(buffer + offset, BUFFER_SIZE - offset, "%s%s\033[0m\n", color_code, entry->d_name);
        }
        offset += len;
    }

    if (offset > 0) {
        if (send_good(sock, buffer, offset) < 0) {
            perror("Failed to send directory listing");
            closedir(dir);
            return -1;
        }
    }

    closedir(dir);
    return 0;
}


int remove_file(char **args, char *buffer2) {
    printf("remove file");
    if (args[1] == NULL) {
        snprintf(buffer2, BUFFER_SIZE, "Invalid command format\n");
        fprintf(stderr, "%s", buffer2);
        return -1;
    }

    if (remove(args[1]) == -1) {
        snprintf(buffer2, BUFFER_SIZE, "Could not remove file @ %s\n", args[1]);
        printf("%s", buffer2);
        perror("remove");
        return -1;
    }
    snprintf(buffer2, BUFFER_SIZE, "Removed file @ %s\n", args[1]);
    printf("%s", buffer2);
    return 0;
}


int read_file(char **args, int sock) {

    if (args[1] == NULL) {
        const char *end_msg = "Invalid Command Format";
        if (send_good(sock, end_msg, strlen(end_msg)) < 0) {
            perror("Failed to send end message");
            return -1;
        }
        fprintf(stderr, "Invalid command format\n");
        close(sock);
        return -1;
    }

    int fd = open(args[1], O_RDONLY);
    if (fd < 0) {
        printf("Could not open file: |%s|\n", args[1]);
        char buffer[BUFFER_SIZE];
        snprintf(buffer, BUFFER_SIZE, "Could not open file: %s\n", args[1]);
        if (send_good(sock, buffer, strlen(buffer)) < 0) {
            perror("Failed to send error message");
            close(sock);
            return -1;
        }
        perror("open");
        close(sock);
        return -1;
    }

    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;

    while ((bytes_read = read(fd, buffer, BUFFER_SIZE)) > 0) {
        if (send_good(sock, buffer, bytes_read) < 0) {
            perror("Failed to send file content");
            close(fd);
            close(sock);
            return -1;
        }
    }

    if (bytes_read < 0) {
        perror("read");
        close(fd);
        close(sock);
        return -1;
    }

    close(fd);
    close(sock);
    printf("Disconnected Client\n");
    return 0;
}


int read_mp3_file(char **args, int sock) {


    // Validate command format
    if (args[1] == NULL) {
        const char *end_msg = "Invalid Command Format";
        if (send_good(sock, end_msg, strlen(end_msg)) < 0) {
            perror("Failed to send end message");
            return -1;
        }
        fprintf(stderr, "Invalid command format\n");
        close(sock);
        return -1;
    }

    // Open the MP3 file
    int fd = open(args[1], O_RDONLY);
    if (fd < 0) {
        printf("Could not open file: |%s|\n", args[1]);
        char buffer[BUFFER_SIZE_FOR_MUSIC];
        snprintf(buffer, BUFFER_SIZE_FOR_MUSIC, "Could not open file: %s\n", args[1]);
        if (send_good(sock, buffer, strlen(buffer)) < 0) {
            perror("Failed to send error message");
            close(sock);
            return -1;
        }
        perror("open");
        close(sock);
        return -1;
    }

    // Send MP3 file data in chunks
    char buffer[BUFFER_SIZE_FOR_MUSIC];
    ssize_t bytes_read;

    while ((bytes_read = read(fd, buffer, BUFFER_SIZE_FOR_MUSIC)) > 0) {
        if (send_good(sock, buffer, bytes_read) < 0) {
            perror("Failed to send MP3 data");
            close(fd);
            close(sock);
            return -1;
        }
    }

    if (bytes_read < 0) {
        perror("read");
        close(fd);
        close(sock);
        return -1;
    }

    close(fd);
    close(sock);
    printf("MP3 file sent successfully.\n");
    return 0;
}


void handle_ctrl_z() {
    // Send the clear command to the terminal to clear the screen
    printf("\033[H\033[J"); // ANSI escape codes for clearing the screen
    fflush(stdout); // Make sure the command is executed immediately
}

void trim(char *buffer) {
    char *end;

    // Trim leading space and newlines
    while (isspace((unsigned char) *buffer) || *buffer == '\n') buffer++;

    if (*buffer == 0)  // All spaces or newlines?
        return;

    // Trim trailing space and newlines
    end = buffer + strlen(buffer) - 1;
    while (end > buffer && (isspace((unsigned char) *end) || *end == '\n')) end--;

    // Write new null terminator
    *(end + 1) = 0;
}


void _indexSubFolders(char *basePath, int sock) {
    struct dirent *dp;
    DIR *dir = opendir(basePath);

    // Unable to open directory stream
    if (!dir) {
        perror("opendir");
        return;
    }
    char temppath[MAX_PATH_SIZE];
    snprintf(temppath, sizeof(temppath), "%s\n", basePath); // adding newline to send it
    if (send_good(sock, temppath, strlen(temppath)) <
        0) { // To handle the edge case of the base folder not having any file in it
        perror("Failed to send path to client");
        closedir(dir); // Close the directory before returning
        return;
    }
    while ((dp = readdir(dir)) != NULL) {

        // Skip the names "." and ".." as we don't want to recurse on them. Also, skip hidden folders
        if (strncmp(dp->d_name, ".", 1) != 0 && strncmp(dp->d_name, "..", 2) != 0 && dp->d_name[0] != '.') {
            fflush(stdout);

            // Construct new path from our base path
            char path[MAX_PATH_SIZE];
            snprintf(path, sizeof(path), "%s/%s\n", basePath, dp->d_name);
            printf("%s\n", path);

            if (send_good(sock, path, strlen(path)) < 0) {
                perror("Failed to send path to client");
                closedir(dir); // Close the directory before returning
                return;
            }

            // Recursively call this function if the entry is a directory
            if (dp->d_type == DT_DIR) {
                path[strlen(path) - 1] = '\0'; // to remove the \n at the end
                _indexSubFolders(path, sock);
            }
        }
    }

// Close the directory after the loop
    closedir(dir);

}

void indexSubFolder(char *basePath, int sock) {
    _indexSubFolders(basePath, sock);
    printf("Finished Indexing\n");
    if (send_good(sock, "@!-Finished Indexing-!@\n", strlen("@!-Finished Indexing-!@\n")) < 0) {
        perror("Failed to send path to client");
        return;
    }
    usleep(20);

}

void copy_file(const char *src, const char *dest) {
    int src_fd = open(src, O_RDONLY);
    if (src_fd < 0) {
        perror("Failed to open source file");
        return;
    }

    int dest_fd = open(dest, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dest_fd < 0) {
        perror("Failed to open destination file");
        close(src_fd);
        return;
    }

    char buffer[4096];
    ssize_t bytes_read, bytes_written;
    while ((bytes_read = read(src_fd, buffer, sizeof(buffer))) > 0) {
        bytes_written = write(dest_fd, buffer, bytes_read);
        if (bytes_written < 0) {
            perror("Failed to write to destination file");
            break;
        }
    }

    if (bytes_read < 0) {
        perror("Failed to read from source file");
    }

    close(src_fd);
    close(dest_fd);
}

void copy_directory(const char *src, const char *dest) {
    struct stat st;
    if (stat(dest, &st) != 0) {
        if (mkdir(dest, 0755) != 0) {
            perror("Failed to create directory");
            return;
        }
    }

    DIR *dir = opendir(src);
    if (!dir) {
        perror("Failed to open source directory");
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char src_path[MAX_PATH_SIZE], dest_path[MAX_PATH_SIZE];
        snprintf(src_path, sizeof(src_path), "%s/%s", src, entry->d_name);
        snprintf(dest_path, sizeof(dest_path), "%s/%s", dest, entry->d_name);

        if (entry->d_type == DT_DIR) {
            copy_directory(src_path, dest_path);
        } else if (entry->d_type == DT_REG) {
            copy_file(src_path, dest_path);
        }
    }

    closedir(dir);
}

void copy_same(char *buffer, char *buffer2) {
    char src[MAX_PATH_SIZE], dest[MAX_PATH_SIZE];
    if (sscanf(buffer, "copysame %s %s", src, dest) != 2) {
        fprintf(stderr, "Invalid input format. Expected: copysame <srcpath> <destpath>\n");
        strcpy(buffer2, "Invalid input format. Expected: copysame <srcpath> <destpath>\n");
        return;
    }

    struct stat st;
    if (stat(src, &st) != 0) {
        perror("Source path does not exist");
        strcpy(buffer2, "Source path does not exist\n");
        return;
    }

    // Add the source directory to the destination path if the source is a directory
    char final_dest[MAX_PATH_SIZE];
    if (S_ISDIR(st.st_mode)) {
        // Create the destination directory structure
        snprintf(final_dest, sizeof(final_dest), "%s/%s", dest, strrchr(src, '/') + 1);
        copy_directory(src, final_dest);
    } else if (S_ISREG(st.st_mode)) {
        // For a regular file, just copy to the specified destination
        snprintf(final_dest, sizeof(final_dest), "%s/%s", dest, strrchr(src, '/') + 1);
        copy_file(src, final_dest);
    } else {
        fprintf(stderr, "Unsupported file type\n");
        strcpy(buffer2, "Unsupported file type\n");
        return;
    }

    strcpy(buffer2, "Finished Copying\n");
}


// int write_file(char *command, char *buffer2) {

//     char *path = strtok(command, "|");
//     path = strtok(NULL, "|");
//     char *data = strtok(NULL, "|");


//     FILE *file = fopen(path, "a");
//     if (file == NULL) {
//         snprintf(buffer2, BUFFER_SIZE, "Could not open file @ %s \n", path);
//         printf("%s", buffer2);
//         perror("fopen");
//         return -1;
//     }

//     fprintf(file, "%s\n", data);
//     snprintf(buffer2, BUFFER_SIZE, "Data written successfully\n");
//     fflush(file);
//     fclose(file);
//     return 0;
// }

// void *write_file_async(void *arg) {
//     void **arguments = (void **) arg;
//     char *command = (char *) arguments[0];
//     int sock = *(int *) arguments[1];


//     printf("the command inside WFA: %s\n", command);
//     printf("Sock after sending: %d\n", sock);
//     printf("Server IP: %s\n", (char *) arguments[2]);
//     char ack_msg[] = "Write Request Recieved";
//     printf("ack message: %s:%d\n", ack_msg, sock);
//     if (send_good(sock, ack_msg, strlen(ack_msg)) < 0) {
//         perror("Failed to send acknowledgment");
//     }

//     // printf("args are: command: %s  sock: %d\n", command, sock);
//     char *command_copy = strdup(command);  // Create a copy to tokenize
//     char *path = strtok(command_copy, "|");
//     path = strtok(NULL, "|");
//     char *ip = strtok(NULL, "|");
//     char *port_of_cl = strtok(NULL, "|");
//     char *data = strtok(NULL, "|");

//     printf("I HAVE ip: %s, port: %s\n", ip, port_of_cl);
//     fflush(stdin);
//     FILE *file = fopen(path, "a");
//     if (file == NULL) {
//         perror("Failed to open file");
//         const char *error_msg = "Failed to open file\n";
//         send_good(sock, error_msg, strlen(error_msg));
//         close(sock);
//         return NULL;
//     }

//     char buffer[ASYNC_BUFF_LEN + 1];
//     size_t buffer_offset = 0;
//     int data_offset = 0;

//     for (int i = 0; i < strlen(data); i++) {
//         buffer[buffer_offset++] = data[data_offset++];
//         if (buffer_offset == ASYNC_BUFF_LEN) {
//             fprintf(file, "%s", buffer);
//             bzero(buffer, ASYNC_BUFF_LEN + 1);
//             buffer_offset = 0;
//         }
//     }

//     if (buffer_offset >= 0) {
//         fprintf(file, "%s\n", buffer);
//     }

//     sleep(1);
//     // Send acknowledgment to the client
//     // ack_msg = "Data written successfully";
//     // if (send_good(sock, ack_msg, strlen(ack_msg)) < 0) {
//     //     perror("Failed to send acknowledgment");
//     // }
//     // printf("qazxswedcvfrtgbnjhyuhnmkiuioklop\n");


//     // sending ACK

//     int sock1;
//     struct sockaddr_in nm_addr;

// // Create the sock1et
//     if ((sock1 = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
//         perror("sock1et creation failed");
//         exit(EXIT_FAILURE);
//     }

// // Disable Nagle's algorithm to avoid buffering delays
//     int flag = 1;
//     if (setsockopt(sock1, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof(int)) < 0) {
//         printf("setsock1opt TCP_NODELAY failed");
//         close(sock1);
//         exit(EXIT_FAILURE);
//     }

// // Set up Naming Server address
//     nm_addr.sin_family = AF_INET;

// // Use ASYNC_PORT instead of SERVER_PORT
//     nm_addr.sin_port = htons(ASYNC_PORT_FOR_NM);

//     if (inet_pton(AF_INET, (char *) arguments[2], &nm_addr.sin_addr) <= 0) {
//         perror("Invalid IP address");
//         close(sock1);
//         exit(EXIT_FAILURE);
//     }

// // Connect to Naming Server
//     if (connect(sock1, (struct sock1addr *) &nm_addr, sizeof(nm_addr)) < 0) {
//         perror("Connection to Naming Server failed");
//         close(sock1);
//         exit(EXIT_FAILURE);
//     }
//     printf("Connected to Naming Server at %s:%d\n", (char *) arguments[2], ASYNC_PORT_FOR_NM);

// // Send data to Naming Server
//     char buffer2[BUFFER_SIZE] = {0};
//     snprintf(buffer2, BUFFER_SIZE, "writeasync %s %s %s\n", path, ip, port_of_cl);
//     if (send_good(sock1, buffer2, strlen(buffer2)) < 0) {
//         perror("Send failed");
//         close(sock1);
//         exit(EXIT_FAILURE);
//     }
//     printf("\033[32mACK for write async on path %s sent to Naming Server\033[0m\n", path);
//     close(sock1);

//     fclose(file);
//     close(sock);
//     return NULL;
// }

int ss_info_to_socket(char *ipOfSS, int portOfSS) {
    // Generated with copilot
    int sock;
    struct sockaddr_in ss_addr;


    // Create socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation failed");
        return -1;
    }

    // Set up server address
    ss_addr.sin_family = AF_INET;
    ss_addr.sin_port = htons(portOfSS);
    if (inet_pton(AF_INET, ipOfSS, &ss_addr.sin_addr) <= 0) {
        perror("Invalid IP address");
        close(sock);
        return -1;
    }

    // Connect to server
    if (connect(sock, (struct sockaddr *) &ss_addr, sizeof(ss_addr)) < 0) {
        perror("Connection failed");
        close(sock);
        return -1;
    }
    return sock;
}


// Helper function to safely send acknowledgments
void send_ack(int socket) {
    send(socket, "Message received", 16, 0);
}

// Helper function to receive acknowledgments
int receive_ack(int socket) {
    char ack[BUFFER_SIZE];
    ssize_t bytes_received = recv(socket, ack, sizeof(ack) - 1, 0);
    //printf("ACK: |%s|\n", ack);
    if (bytes_received <= 0) return -1;
    ack[bytes_received] = '\0';
    return strcmp(ack, "Message received") == 0 ? 0 : -1;
}

void copy_different_src(char *buffer, char *buffer2, int ss_socket, int lcopy) {
    char src[MAX_PATH_SIZE], dest[MAX_PATH_SIZE];
    char ip[16];  // Added for storing IP
    int port;     // Added for storing port
    char _dummy[BUFFER_SIZE];

    // Parse the command from the client - now includes IP and port
    if (sscanf(buffer, "%s %s %s %s %d", _dummy, src, dest, ip, &port) != 5) {
        snprintf(buffer2, BUFFER_SIZE,
                 "Invalid input format. Expected: copydifferent/lcopy <srcpath> <destpath> <ip> <port>\n");
        return;
    }

    // Check if the source exists
    struct stat st;
    if (stat(src, &st) != 0) {
        snprintf(buffer2, BUFFER_SIZE, "Source path %s does not exist\n", src);
        return;
    }

    if (receive_ack(ss_socket) < 0) {
        snprintf(buffer2, BUFFER_SIZE, "Failed to receive acknowledgment for destination info\n");
        return;
    }
    // Send destination path to receiving server
    char dest_info[MAX_PATH_SIZE + 10];
    snprintf(dest_info, sizeof(dest_info), "DEST %s\n", dest);
    if (send(ss_socket, dest_info, strlen(dest_info), 0) < 0) {
        snprintf(buffer2, BUFFER_SIZE, "Failed to send destination info\n");
        return;
    }

    if (receive_ack(ss_socket) < 0) {
        snprintf(buffer2, BUFFER_SIZE, "Failed to receive acknowledgment for destination info\n");
        return;
    }

    int length_to_skip = length_to_skip = strlen(src) -
                                          strlen(strrchr(src, '/') + 1); // To skip the source path when sending data
    if (length_to_skip == 2 && strncmp(dest, "./backupfolderforss", strlen("./backupfolderforss")) == 0)
        length_to_skip = 0; //since this means it is a root directory so, received will remove the ./ stuffs and a lot please forgive me god :C

    if (S_ISDIR(st.st_mode)) {
        send_directory_over_network(ss_socket, src, length_to_skip);
    } else if (S_ISREG(st.st_mode)) {
        if (lcopy == 1) {
            length_to_skip = 0;
        }
        send_file_over_network(ss_socket, src, length_to_skip);
    } else {
        snprintf(buffer2, BUFFER_SIZE, "Unsupported file type\n");
        return;
    }

    // Send END marker
    send(ss_socket, "END\n", 4, 0);

    snprintf(buffer2, BUFFER_SIZE, "Finished Copying\n");
}


void send_file_over_network(int ss_socket, const char *src, int length_to_skip) {
    printf("Sending File Name : %s\n", src);
    int src_fd = open(src, O_RDONLY);
    if (src_fd < 0) {
        perror("Failed to open source file");
        return;
    }

    struct stat st;
    if (fstat(src_fd, &st) < 0) {
        perror("Failed to get file size");
        close(src_fd);
        return;
    }

    // Send metadata: "FILE <filename>\n"
    char metadata[MAX_PATH_SIZE + 50];

    snprintf(metadata, sizeof(metadata), "FILE %s\n", src + length_to_skip);

    printf("METADATA: %s\n", metadata);

    if (send(ss_socket, metadata, strlen(metadata), 0) < 0 ||
        receive_ack(ss_socket) < 0) {
        close(src_fd);
        return;
    }

    // Send file size
    snprintf(metadata, sizeof(metadata), "%ld\n", st.st_size);
    if (send(ss_socket, metadata, strlen(metadata), 0) < 0 ||
        receive_ack(ss_socket) < 0) {
        close(src_fd);
        return;
    }

    // Send file contents in chunks with acknowledgment
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    while ((bytes_read = read(src_fd, buffer, sizeof(buffer))) > 0) {
        if (send(ss_socket, buffer, bytes_read, 0) < 0 ||
            receive_ack(ss_socket) < 0) {
            break;
        }
    }

    close(src_fd);
}


void send_directory_over_network(int ss_socket, const char *src, int length_to_skip) {

    printf("Send Folder Name : %s\n", src);
    DIR *dir = opendir(src);
    if (!dir) {
        perror("Failed to open source directory");
        return;
    }

    // Send directory metadata
    char metadata[MAX_PATH_SIZE];

    snprintf(metadata, sizeof(metadata), "DIR %s\n",
             src + length_to_skip); //need this so that it is relative in the receiving side too

    if (send(ss_socket, metadata, strlen(metadata), 0) < 0 ||
        receive_ack(ss_socket) < 0) {
        closedir(dir);
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip . and .. entries
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char full_path[MAX_PATH_SIZE];
        snprintf(full_path, sizeof(full_path), "%s/%s", src, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0) {
            fprintf(stderr, "Failed to stat %s\n", full_path);
            continue;
        }

        // Recursively handle subdirectories and files
        if (S_ISDIR(st.st_mode)) {
            send_directory_over_network(ss_socket, full_path, length_to_skip);
        } else if (S_ISREG(st.st_mode)) {
            send_file_over_network(ss_socket, full_path, length_to_skip);
        }
        // Other file types are ignored
    }

    closedir(dir);
}

void receive_and_save_directory(int ss_socket, const char *dest) {
    printf("Recieved Directory Name : %s\n", dest);
    // Create the destination directory
    if (mkdir(dest, 0755) != 0 && errno != EEXIST) {
        perror("Failed to create destination directory");
        return;
    }
}

void receive_and_save_file(int ss_socket, const char *dest) {
    printf("Received File Name: %s\n", dest);

    // Receive file size
    char buffer[BUFFER_SIZE] = {0};
    ssize_t bytes_received = recv(ss_socket, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received <= 0) {
        fprintf(stderr, "Failed to receive file size: %s\n", strerror(errno));
        return;
    }
    buffer[bytes_received] = '\0';

    long file_size = atol(buffer);
    printf("Expected file size: %ld bytes\n", file_size);
    send_ack(ss_socket);

    // Create destination file in binary mode
    FILE *dest_file = fopen(dest, "wb");  // Changed to binary mode
    if (dest_file == NULL) {
        fprintf(stderr, "Failed to create destination file %s: %s\n", dest, strerror(errno));
        return;
    }

    // Receive and write file contents
    long total_received = 0;
    while (total_received < file_size) {
        memset(buffer, 0, BUFFER_SIZE);
        size_t remaining = file_size - total_received;
        size_t to_receive = (remaining < sizeof(buffer)) ? remaining : sizeof(buffer);

        // Keep receiving until we get all expected bytes for this chunk
        size_t chunk_received = 0;
        while (chunk_received < to_receive) {
            bytes_received = recv(ss_socket,
                                  buffer + chunk_received,
                                  to_receive - chunk_received,
                                  0);
            if (bytes_received <= 0) {
                fprintf(stderr, "Failed to receive file contents: %s\n", strerror(errno));
                goto cleanup;  // Use goto for proper cleanup in error case
            }
            chunk_received += bytes_received;
        }



        // Write full chunk
        size_t written = fwrite(buffer, 1, chunk_received, dest_file);
        if (written != chunk_received) {
            fprintf(stderr, "Failed to write to file: %s\n", strerror(errno));
            goto cleanup;
        }

        // Flush after each write
        fflush(dest_file);


        total_received += chunk_received;
        send_ack(ss_socket);
    }


    cleanup:
    // Final flush and close
    fflush(dest_file);
    fclose(dest_file);

}

void copy_different_dest(int ss_socket) {
    send_ack(ss_socket);
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, sizeof(BUFFER_SIZE));
    char base_dest_path[MAX_PATH_SIZE];
    memset(buffer, 0, sizeof(MAX_PATH_SIZE));

    // First receive the destination path
    ssize_t bytes_received = recv(ss_socket, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received <= 0) {
        perror("Failed to receive destination path");
        return;
    }
    buffer[bytes_received] = '\0';

    if (strncmp(buffer, "DEST ", 5) == 0) {
        sscanf(buffer + 5, "%s", base_dest_path);
        send_ack(ss_socket);
    } else {
        perror("Invalid protocol: expected destination path");
        return;
    }

    // Create base destination directory if it doesn't exist
    mkdir(base_dest_path, 0755);  // Ignore errors if directory exists

    while (1) {
        bytes_received = recv(ss_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0) {
            perror("Error receiving data");
            break;
        }
        buffer[bytes_received] = '\0';

        if (strncmp(buffer, "END\n", 4) == 0) {
            printf("Transfer complete.\n");
            break;
        }

        char full_path[MAX_PATH_SIZE];
        if (strncmp(buffer, "FILE ", 5) == 0) {
            printf("\033[32mReceived path is : %s\033[0m\n", buffer);
            char filename[MAX_PATH_SIZE];
            sscanf(buffer + 5, "%s", filename);
            if (strncmp(filename, "./", 2) == 0) {
                snprintf(full_path, sizeof(full_path), "%s/%s", base_dest_path, filename + 2);
            } else {
                snprintf(full_path, sizeof(full_path), "%s/%s", base_dest_path, filename);
            }

            send_ack(ss_socket);
            receive_and_save_file(ss_socket, full_path);
        } else if (strncmp(buffer, "DIR ", 4) == 0) {
            char dirname[MAX_PATH_SIZE];
            sscanf(buffer + 4, "%s", dirname);
            if (strncmp(dirname, "./", 2) == 0) {
                snprintf(full_path, sizeof(full_path), "%s/%s", base_dest_path, dirname + 2);
            } else {
                snprintf(full_path, sizeof(full_path), "%s/%s", base_dest_path, dirname);
            }
            send_ack(ss_socket);
            receive_and_save_directory(ss_socket, full_path);
        }
    }
}


void copy_different_dest_b(int ss_socket) {
    send_ack(ss_socket);
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, sizeof(BUFFER_SIZE));
    char base_dest_path[MAX_PATH_SIZE];
    memset(buffer, 0, sizeof(MAX_PATH_SIZE));

    // First receive the destination path
    ssize_t bytes_received = recv(ss_socket, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received <= 0) {
        perror("Failed to receive destination path");
        return;
    }
    buffer[bytes_received] = '\0';


    printf("Whole command I am getting: %s\n", buffer);
    if (strncmp(buffer, "DEST ", 5) == 0) {
        sscanf(buffer + 5, "%s", base_dest_path);
        send_ack(ss_socket);
    } else {
        perror("Invalid protocol: expected destination path");
        return;
    }

    printf("THE PATH I AM GETTING: %s", base_dest_path);

    // Create base destination directory if it doesn't exist
    mkdir(base_dest_path, 0755);  // Ignore errors if directory exists

    while (1) {
        bytes_received = recv(ss_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0) {
            perror("Error receiving data");
            break;
        }
        buffer[bytes_received] = '\0';

        if (strncmp(buffer, "END\n", 4) == 0) {
            printf("Transfer complete.\n");
            break;
        }

        char full_path[MAX_PATH_SIZE];
        if (strncmp(buffer, "FILE ", 5) == 0) {
            char filename[MAX_PATH_SIZE];
            sscanf(buffer + 5, "%s", filename);
            snprintf(full_path, sizeof(full_path), "%s/%s", base_dest_path, filename);


            send_ack(ss_socket);
            receive_and_save_file(ss_socket, full_path);
        } else if (strncmp(buffer, "DIR ", 4) == 0) {
            char dirname[MAX_PATH_SIZE];
            sscanf(buffer + 4, "%s", dirname);

            snprintf(full_path, sizeof(full_path), "%s/%s", base_dest_path, dirname);
            send_ack(ss_socket);
            receive_and_save_directory(ss_socket, full_path);
        }
    }
}


void copy_different_src_b(char *buffer, char *buffer2, int ss_socket) {
    char src[MAX_PATH_SIZE], dest[MAX_PATH_SIZE];
    char ip[16];  // Added for storing IP
    int port;     // Added for storing port

    // Parse the command from the client - now includes IP and port
    if (sscanf(buffer, "copydifferentb %s %s %s %d", src, dest, ip, &port) != 4) {
        snprintf(buffer2, BUFFER_SIZE,
                 "Invalid input format. Expected: copydifferent <srcpath> <destpath> <ip> <port>\n");
        return;
    }

    // Check if the source exists
    struct stat st;
    if (stat(src, &st) != 0) {
        snprintf(buffer2, BUFFER_SIZE, "Source path %s does not exist\n", src);
        return;
    }

    if (receive_ack(ss_socket) < 0) {
        snprintf(buffer2, BUFFER_SIZE, "Failed to receive acknowledgment for destination info\n");
        return;
    }
    // Send destination path to receiving server
    char dest_info[MAX_PATH_SIZE + 10];
    snprintf(dest_info, sizeof(dest_info), "DEST %s\n", dest);
    if (send(ss_socket, dest_info, strlen(dest_info), 0) < 0) {
        snprintf(buffer2, BUFFER_SIZE, "Failed to send destination info\n");
        return;
    }

    if (receive_ack(ss_socket) < 0) {
        snprintf(buffer2, BUFFER_SIZE, "Failed to receive acknowledgment for destination info\n");
        return;
    }

    int length_to_skip = length_to_skip = strlen(src) -
                                          strlen(strrchr(src, '/') + 1); // To skip the source path when sending data
    if (length_to_skip == 2)
        length_to_skip = 0; //since this means it is a root directory so, received will remove the ./ stuffs and a lot please forgive me god :C

    if (S_ISDIR(st.st_mode)) {
        send_directory_over_network(ss_socket, src, length_to_skip);
    } else if (S_ISREG(st.st_mode)) {

        send_file_over_network(ss_socket, src, length_to_skip);
    } else {
        snprintf(buffer2, BUFFER_SIZE, "Unsupported file type\n");
        return;
    }

    // Send END marker
    send(ss_socket, "END\n", 4, 0);

    snprintf(buffer2, BUFFER_SIZE, "Finished Copying\n");
}

void log_it(char* ip, int sock, char* message) {
    FILE *file = fopen("log.txt", "a");
    if (file == NULL) {
        perror("Failed to open log file");
        return;
    }

    char buffer[BUFFER_SIZE];
    if(sock != -1) sprintf(buffer, "IP[%s] : Port[%d] : %s\n", ip, sock, message);
    else sprintf(buffer, "%s", message);
    // buffer[strcspn(buffer, '\n')] = '\0';
    fprintf(file, "%s\n", buffer);
    fclose(file);
}