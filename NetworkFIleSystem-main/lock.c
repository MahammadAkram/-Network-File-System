#include "lock.h"

#define CYN "\e[0;36m"
#define RESET "\e[0m"

void init_lock(Lock_Struct *file, const char *path_to_file) {
    pthread_mutex_init(&file->mutex, NULL);
    pthread_cond_init(&file->cond, NULL);
    file->readers = 0;
    file->is_writing = 0;
    file->is_deleting = 0;
    file->path_to_file = (char *)path_to_file;
}

// Destroy the file structure
void destroy_file(Lock_Struct *file) {
    pthread_mutex_destroy(&file->mutex);
    pthread_cond_destroy(&file->cond);
}

// Acquire read lock
void acquire_read_lock(Lock_Struct *file) {
    pthread_mutex_lock(&file->mutex);
    while (file->is_writing || file->is_deleting) {   //file->is_writing || file->is_deleting

        printf(CYN "\nWaiting For File Access...\n" RESET);
        pthread_cond_wait(&file->cond, &file->mutex);
        printf(CYN "\nGot File Access...\n" RESET);

    }
    file->readers++;
    pthread_mutex_unlock(&file->mutex);
}

// Release read lock
void release_read_lock(Lock_Struct *file) {
    pthread_mutex_lock(&file->mutex);
    file->readers--;
    if (file->readers == 0) {

        //To check if it works
        sleep(5);

        pthread_cond_signal(&file->cond);
    }
    pthread_mutex_unlock(&file->mutex);
}

// Acquire write lock
void acquire_write_lock(Lock_Struct *file) {
    pthread_mutex_lock(&file->mutex);
    while (file->is_writing || file->is_deleting || file->readers > 0) {

                printf(CYN "\nWaiting For File Access...\n" RESET);

        pthread_cond_wait(&file->cond, &file->mutex);

                printf(CYN "\nGot File Access...\n" RESET);


    }
    file->is_writing = 1;
    pthread_mutex_unlock(&file->mutex);
}

// Release write lock
void release_write_lock(Lock_Struct *file) {
    pthread_mutex_lock(&file->mutex);
    file->is_writing = 0;
    pthread_cond_signal(&file->cond);
    pthread_mutex_unlock(&file->mutex);
}

// Acquire delete lock
void acquire_delete_lock(Lock_Struct *file) {
    pthread_mutex_lock(&file->mutex);
    while (file->is_writing || file->is_deleting || file->readers > 0) {

                printf(CYN "\nWaiting For File Access...\n" RESET);

        pthread_cond_wait(&file->cond, &file->mutex);

                printf(CYN "\nGot File Access...\n" RESET);

    }
    file->is_deleting = 1;
    pthread_mutex_unlock(&file->mutex);
}

// Release delete lock
void release_delete_lock(Lock_Struct *file) {
    pthread_mutex_lock(&file->mutex);
    file->is_deleting = 0;
    pthread_cond_broadcast(&file->cond);
    pthread_mutex_unlock(&file->mutex);
}

// Reader thread function
// void *reader_thread(void *arg) {
//     Lock_Struct *file = (Lock_Struct *)arg;
//     printf("Reader trying to read: %s\n", file->path_to_file);
//     acquire_read_lock(file);
//     printf("Reader reading: %s\n", file->path_to_file);
//     sleep(1); // Simulate read operation
//     printf("Reader finished reading: %s\n", file->path_to_file);
//     release_read_lock(file);
//     return NULL;
// }

// // Writer thread function
// void *writer_thread(void *arg) {
//     Lock_Struct *file = (Lock_Struct *)arg;
//     printf("Writer trying to write: %s\n", file->path_to_file);
//     acquire_write_lock(file);
//     printf("Writer writing: %s\n", file->path_to_file);
//     sleep(2); // Simulate write operation
//     printf("Writer finished writing: %s\n", file->path_to_file);
//     release_write_lock(file);
//     return NULL;
// }

// Deleter thread function
// void *deleter_thread(void *arg) {
//     Lock_Struct *file = (Lock_Struct *)arg;
//     printf("Deleter trying to delete: %s\n", file->path_to_file);
//     acquire_delete_lock(file);
//     printf("Deleter deleting: %s\n", file->path_to_file);
//     sleep(3); // Simulate delete operation
//     printf("Deleter finished deleting: %s\n", file->path_to_file);
//     release_delete_lock(file);
//     return NULL;
// }

// Main function
// int main() {
//     Lock_Struct file;
//     init_file(&file, "test_file");

//     pthread_t threads[6];

//     // Simulate multiple readers, writers, and deleters
//     pthread_create(&threads[0], NULL, reader_thread, &file);
//     pthread_create(&threads[1], NULL, writer_thread, &file);
//     pthread_create(&threads[2], NULL, deleter_thread, &file);
//     pthread_create(&threads[3], NULL, reader_thread, &file);
//     pthread_create(&threads[4], NULL, writer_thread, &file);
//     pthread_create(&threads[5], NULL, deleter_thread, &file);

//     // Wait for all threads to finish
//     for (int i = 0; i < 6; i++) {
//         pthread_join(threads[i], NULL);
//     }

//     destroy_file(&file);
//     return 0;
// }