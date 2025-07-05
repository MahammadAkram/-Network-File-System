//
// Created by George Rahul on 16/11/24.
//

#ifndef NFS_OSN_ERRORCODES_H
#define NFS_OSN_ERRORCODES_H

#define RESET "\e[0m"
#define RED "\e[0;31m"

#include <stddef.h>
#include <stdio.h>
// Define a struct for error details
typedef struct {
    int code;           // Error number
    const char *name;   // Error code name
    const char *message; // Error description
} Error;

// Define the error list
const Error errors[] = {
        {0, "ERR_NO_FILE_FOUND", "No file found"},
        {1, "ERR_NO_FOLDER_FOUND", "No folder found"},
        {2, "ERR_STORAGE_SERVER_DOWN", "Storage server is down"},
        {3, "ERR_NAMING_SERVER_UNREACHABLE", "Network is unreachable"},
        {4, "ERR_PERMISSION_DENIED", "Permission denied"},
        {5, "ERR_DISK_FULL", "Disk is full"},
        {6, "ERR_UNKNOWN_ERROR", "Unknown error"},
        {7, "SOCK_CREAT_FAIL", "Socket creation failed"},
        {8, "INV_IP_ADDR", "invalid IP"},
        {9, "CONN_FAIL", "Connection failed"},
        {10, "SEND_FAIL", "Send failed"},
        {11, "RECV_FAIL", "Receive failed"},
        {12, "ERR_INVALID_PATH", "Invalid path"},
        {13, "ERR_PATH_NOT_FOUND", "Path not found"},
        {14, "ERR_INVALID_FORMAT", "Invalid format"},
        {15, "ERR_INVALID_INPUT", "Invalid input"},
        {16, "ERR_INVALID_COMMAND", "Invalid command"},
        {17, "ERR_INVALID_IP", "Invalid IP address"},
        {18, "ERR_INVALID_PORT", "Invalid port number"},
        {19, "ERR_INVALID_FILE", "Invalid file"},
        {20, "ERR_INVALID_DIRECTORY", "Invalid directory"},
        {21, "ERR_INVALID_FILE_PATH", "Invalid file path"},
        {22, "ERR_INVALID_DIRECTORY_PATH", "Invalid directory path"},
        {23, "ERR_INVALID_FILE_NAME", "Invalid file name"},
        {24, "ERR_INVALID_DIRECTORY_NAME", "Invalid directory name"},
        {25, "ERR_INVALID_FILE_EXTENSION", "Invalid file extension"},
        {26, "ERR_INVALID_DIRECTORY_EXTENSION", "Invalid directory extension"},
        {27, "ERR_INVALID_FILE_SIZE", "Invalid file size"},
        {28, "ERR_INVALID_DIRECTORY_SIZE", "Invalid directory size"},
        {29, "ERR_INVALID_FILE_CONTENT", "Invalid file content"},
        {30, "ERR_INVALID_DIRECTORY_CONTENT", "Invalid directory content"},
        {31, "ERR_INVALID_FILE_PERMISSION", "Invalid file permission"},
        {32, "ERR_INVALID_DIRECTORY_PERMISSION", "Invalid directory permission"},
        {33, "ERR_INVALID_FILE_OWNER", "Invalid file owner"},
        {34, "ERR_INVALID_DIRECTORY_OWNER", "Invalid directory owner"},
        {35, "ERR_INVALID_FILE_GROUP", "Invalid file group"},
        {36, "FFPLAY_FAIL", "ffplay failed."},
        {37, "SS_NO_RESPONSE", "Storage Server not responding"},
        {38, "BACKUP1_FAIL", "Failed to send command to backup server 1"},
        {39, "SSINFO_FAIL/CLIENT_DISCONN", "Failed to receive storage server info or client disconnected"},
        {40, "LISTEN_FAIL", "listen failed."},
        {41, "BIND_FAIL", "Bind failed."},
        {42, "ACCEPT_FAIL", "Accept failed."},
        {43, "THREAD_FAIL", "Failed to create thread"},
        {44, "SETSOCKOPT_FAIL", "setsockopt failed."},
        {45, "SS_CONN_FAIL", "Storage server connection failed"},
        {46, "PING_SEND_FAIL", "Failed to send ping command"},
        {47, "PING_REC_FAIL", "Failed to recive ping command"},
        {48, "BUFFER_DUP_FAIL", "Failed to duplicate the buffer"},
        {49, "SEND_TO_SS_FAIL", "Failed to send to source storage seerver"},
        {50, "RECV_FROM_SS_FAIL", "Failed to receive from source storage server"},
        {51, "CLIENT_NO_RESP", "Client is not responding"},
        {52, "FAIL_SEND_CLIENT", "failed to send response to client"},
        {53, "RECV_FROM_CLIENT_FAIL", "Failed to receive client"},
        {54, "SS_NO_RESP", "storage server not responding"},
        {55, "NS_CONN_FAIL", "Connection to Naming Server Failed"},
        {56, "BACKUP_CREAT_FAIL", "Failed to create backup folder"},
        {57, "GETSOCKNAME_FAIL", "getsockname failed."},
        {69, "FORMAT_ERR", "Incorrect Input Format!"}





};

// Number of errors in the array
#define ERROR_COUNT (sizeof(errors) / sizeof(errors[0]))

// Function to print error details by code
void printErrorDetails(int errorCode) {
    for (int i = 0; i < ERROR_COUNT; i++) {
        if (errors[i].code == errorCode) {
            printf(RED"Error Number: %d\n", errors[i].code);
            printf("Error Code: %s\n", errors[i].name);
            printf("Error Message: %s\n" RESET, errors[i].message);
            return;
        }
    }
    printf(RED "Error: Invalid error code (%d)\n" RESET, errorCode);
}

#endif //NFS_OSN_ERRORCODES_H
