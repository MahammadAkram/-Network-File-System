//
// Created by George Rahul on 09/11/24.
//

#ifndef NFS_HELPER_H
#define NFS_HELPER_H

#include "tries.h"

#define SERVER_PORT 9001
#define BUFFER_SIZE 1024
#define CLIENT_PORT_NS 9024
#define MAX_STORAGE_SERVERS 10
#define ASYNC_BUFF_LEN 32
#define ASYNC_PORT_FOR_NM 9037


#define BUFFER_SIZE_FOR_MUSIC 8192
#define MAX_PATH_SIZE 2048
#define CACHE_CAPACITY 3

typedef struct {
    int socket;
    struct sockaddr_in addr;
} StorageServerConnectionInfo;

typedef struct {
    StorageServerConnectionInfo clientInfo;
    int portClient;
    int socket;
    trienode* root;
    int backupServer1;
    int backupServer2;
    int isUp;

} StorageServerInfo;

typedef void (*CommandFunction)(const char *input, char *output);

typedef struct {
    const char *command;
    CommandFunction function;
} Command;

typedef struct {
    const char *nm_ip;
    StorageServerInfo *ssi;
} ConnectToNamingServerArgs;


ssize_t recv_good(int sockfd, void *buf, size_t len);
ssize_t send_good(int sockfd, const void *buf, size_t len);
int list_file(char ** args,int sock);
int create_file(char ** args,char * buffer2);
int delete_directory(char ** args,char * buffer2);
int create_directory(char ** args,char * buffer2);
int remove_file(char ** args,char * buffer2);
int send_and_receive(int sockfd, const char *buffer);
int read_file(char ** args,int sock);
int read_mp3_file(char ** args, int sock);
int write_file(char* command, char* buffer2);
void* write_file_async(void* arg);

void handle_ctrl_z();
void trim(char* buffer);
void indexSubFolder(char *basePath, int sock);
void parse_command(char *buffer, char *args[]);
extern Command commands[];
void copy_same(char* buffer,char* buffer2);
extern size_t num_commands;
void copy_different_dest(int ss_socket);
void copy_different_src(char *buffer, char *buffer2, int ss_socket,int lcopy);
int ss_info_to_socket(char* ipOfSS, int portOfSS);
void send_file_over_network(int ss_socket, const char *src,int length_to_skip);
void send_directory_over_network(int ss_socket, const char *src,int length_to_skip);
void receive_and_save_file(int ss_socket, const char *dest);
void receive_and_save_directory(int ss_socket, const char *dest);
void send_file_metadata(const char *filepath, int sockfd);
void copy_different_src_b(char *buffer, char *buffer2, int ss_socket);
void copy_different_dest_b(int ss_socket);
void log_it(char* ip, int sock, char* message);
#endif //NFS_HELPER_H
