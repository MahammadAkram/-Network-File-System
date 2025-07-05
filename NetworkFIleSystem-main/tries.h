
#ifndef TRIES_H
#define TRIES_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/socket.h>
#define MAX_FOLDERS 256 // maximum number of folders/files in a single folder.
#define MAX_PATH 256


typedef struct trienode{
    struct trienode* childeren[MAX_FOLDERS];
    bool isLeaf;
    char* data;


} trienode;


trienode* CreateNode(char* data);
void ParsePath(const char* path, char** path_arr) ;
void DisplayTrie(trienode* root);
void AddPathToTrie(char* path, trienode* root);
void DisplayTrieNetwork(int sock,trienode *root);
void DeletePath(char* path, trienode* root);

#endif // TRIES_H