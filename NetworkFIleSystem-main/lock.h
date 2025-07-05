#ifndef LOCK_H
#define LOCK_H


#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h> // For sleep

#define MAX_REQ 1024

// Define the Lock_Struct structure
typedef struct {
    pthread_mutex_t mutex;       // Lock for the file
    pthread_cond_t cond;         // Condition variable
    int readers;                 // Number of active readers
    int is_writing;              // Flag: is a write in progress
    int is_deleting;             // Flag: is a delete in progress
    char *path_to_file;                  // Lock_Struct path_to_file 
} Lock_Struct;

// Function prototypes
void init_lock(Lock_Struct *file, const char *path_to_file);
void destroy_file(Lock_Struct *file);
void acquire_read_lock(Lock_Struct *file);
void release_read_lock(Lock_Struct *file);
void acquire_write_lock(Lock_Struct *file);
void release_write_lock(Lock_Struct *file);
void acquire_delete_lock(Lock_Struct *file);
void release_delete_lock(Lock_Struct *file);

#endif
