
#include "tries.h"


trienode *CreateNode(char *data) {
    trienode *new_node = (trienode *) malloc(sizeof(trienode));
    for (int i = 0; i < MAX_FOLDERS; i++) new_node->childeren[i] = NULL;
    new_node->isLeaf = true;
    new_node->data = (char *) malloc(strlen(data) + 1);
    strcpy(new_node->data, data);
    return new_node;
}

// Helper function to determine if a node is last among its siblings
bool isLastChild(trienode *node, trienode *parent) {
    if (parent == NULL) return true;

    int nodeIndex = -1;
    // Find the current node's index
    for (int i = 0; i < MAX_FOLDERS; i++) {
        if (parent->childeren[i] == node) {
            nodeIndex = i;
            break;
        }
    }

    // Check if there are any non-null childeren after this index
    for (int i = nodeIndex + 1; i < MAX_FOLDERS; i++) {
        if (parent->childeren[i] != NULL) {
            return false;
        }
    }
    return true;
}

void PrintTree(trienode *root, int depth, char *prefix, trienode *parent) {
    if (root == NULL) {
        return;
    }

    // Print indentation and branch connectors
    for (int i = 0; i < depth; i++) {
        if (prefix[i] == '1') {
            printf("\033[33m│   \033[0m"); // Yellow color for lines
        } else {
            printf("    ");
        }
    }

    // Print current node
    if (depth > 0) {
        bool isLast = isLastChild(root, parent);
        printf("\033[33m%s── \033[0m", isLast ? "└" : "├"); // Yellow color for branch connectors
        printf("%s%s\033[0m\n",
               root->isLeaf ? "\033[37m" : "\033[34m", // White for files, Blue for folders
               root->data);  // Indicate if it's a file
    } else {
        printf("\033[34m%s\033[0m\n", root->data);  // Blue color for root node
    }

    // Prepare prefix for childeren
    char new_prefix[MAX_PATH];
    strcpy(new_prefix, prefix);
    new_prefix[depth] = isLastChild(root, parent) ? '0' : '1';
    new_prefix[depth + 1] = '\0';

    // Count non-null childeren
    for (int i = 0; i < MAX_FOLDERS; i++) {
        if (root->childeren[i] != NULL) {
        }
    }

    // Recursively print childeren
    for (int i = 0; i < MAX_FOLDERS; i++) {
        if (root->childeren[i] != NULL) {
            PrintTree(root->childeren[i], depth + 1, new_prefix, root);
        }
    }
}

void CountItems(trienode *root, int *dirs, int *files) {
    if (root == NULL) return;

    if (root->isLeaf) {
        (*files)++;
    } else {
        (*dirs)++;
    }

    for (int i = 0; i < MAX_FOLDERS; i++) {
        if (root->childeren[i] != NULL) {
            CountItems(root->childeren[i], dirs, files);
        }
    }
}

void DisplayTrie(trienode *root) {
    char prefix[MAX_PATH] = {0};
    int totalDirs = 0, totalFiles = 0;

    // First display the tree
    PrintTree(root, 0, prefix, NULL);

    // Count directories and files
    CountItems(root, &totalDirs, &totalFiles);

    // Print summary
    printf("\n%d directories", totalDirs);
    printf(", %d files\n", totalFiles);
}

// Helper function to count directories and files


void ParsePath(const char *path, char **path_arr) {
    char *path_copy = strdup(path);
    char *token = strtok(path_copy, "/");
    int curr = 0;
    while (token != NULL) {
        char *path_element = (char *) malloc(strlen(token) + 1);
        strcpy(path_element, token);
        if (strcmp(path_element, "~") == 0) {
            token = strtok(NULL, "/");
            continue;
        }
        path_arr[curr++] = path_element;
        token = strtok(NULL, "/");
    }
    path_arr[curr] = NULL;
    free(path_copy);
}


// function to add a path to the trie
//! All paths must begin with a "./"
void AddPathToTrie(char *path, trienode *root) {
    char *path_arr[MAX_PATH];
    ParsePath(path, path_arr);
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
            trienode *new_node = CreateNode(path_arr[i]);
            for (int j = 0; j < MAX_FOLDERS; j++) {
                if (curr->childeren[j] == NULL) {
                    curr->childeren[j] = new_node;
                    curr->isLeaf = false;
                    break;
                }
            }
            curr = new_node;
        }
    }
}





void PrintTreeNetwork(int sock, trienode *root, int depth, char *prefix, trienode *parent) {
    if (root == NULL) {
        return;
    }

    char buffer[1024];
    int len = 0;

    // Print indentation and branch connectors
    for (int i = 0; i < depth; i++) {
        if (prefix[i] == '1') {
            len += snprintf(buffer + len, sizeof(buffer) - len, "\033[33m│   \033[0m"); // Yellow color for lines
        } else {
            len += snprintf(buffer + len, sizeof(buffer) - len, "    ");
        }
    }

    // Print current node
    if (depth > 0) {
        bool isLast = isLastChild(root, parent);
        len += snprintf(buffer + len, sizeof(buffer) - len, "\033[33m%s── \033[0m", isLast ? "└" : "├"); // Yellow color for branch connectors
        len += snprintf(buffer + len, sizeof(buffer) - len, "%s%s\033[0m\n",
                        root->isLeaf ? "\033[37m" : "\033[34m", // White for files, Blue for folders
                        root->data);  // Indicate if it's a file
    } else {
        len += snprintf(buffer + len, sizeof(buffer) - len, "\033[34m%s\033[0m\n", root->data);  // Blue color for root node
    }

    // Send the buffer to the client
    if (send(sock, buffer, len,0) <0) {
        perror("send");
        return; // Client is disconnected or an error occurred
    }

    // Prepare prefix for childeren
    char new_prefix[MAX_PATH];
    strcpy(new_prefix, prefix);
    new_prefix[depth] = isLastChild(root, parent) ? '0' : '1';
    new_prefix[depth + 1] = '\0';

    // Recursively print childeren
    for (int i = 0; i < MAX_FOLDERS; i++) {
        if (root->childeren[i] != NULL) {
            PrintTreeNetwork(sock, root->childeren[i], depth + 1, new_prefix, root);
        }
    }
}

void DisplayTrieNetwork(int sock,trienode *root) {
    char prefix[MAX_PATH] = {0};
    // First display the tree
    PrintTreeNetwork(sock,root, 0, prefix, NULL);

}

// returns 1 if path exists and 0 if it doesn't
int SearchPathHelperTries(trienode* root, char** path_arr) {
    trienode* curr = root;
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
// returns in which ss the path is found.
// returns -1 if not found in any ss.
int SearchPath(trienode* root, char* path) {
    char* path_arr[MAX_PATH];
    ParsePath(path, path_arr);
    if(SearchPathHelperTries(root, path_arr)) return 1;
    return -1;
}


void DeletePath(char* path, trienode* root) {
    char* path_arr[MAX_PATH];
    ParsePath(path, path_arr);
    if(SearchPath(root, path) == -1) { // path doesn't exist
        printf("\033[1;31mError: Path doesn't exist.\033[0m\n");
        return;
    }
    trienode* curr = root;
    trienode* prev = NULL;
    int prev_child_index = -1;
    for(int i = 0; i < MAX_PATH; i++) {
        if(path_arr[i] == NULL) break;
        for(int j = 0; j < MAX_PATH; j++) {
            if(curr->childeren[j] != NULL && strcmp(curr->childeren[j]->data, path_arr[i]) == 0) {
                prev = curr;
                prev_child_index = j;
                curr = curr->childeren[j];
                break;
            }
        }
    }
    free(prev->childeren[prev_child_index]);
    prev->childeren[prev_child_index] = NULL;
}

//int main() {
//    root = CreateNode("~");
//    char path1[MAX_PATH] = "~/folder1/folder2/file1";
//    char path2[MAX_PATH] = "~/folder1/folder3/file2";
//    AddPathToTrie(path1, root); // add path1 to trie
//    AddPathToTrie(path2, root); // add path2 to trie
//    PrintTrieBFS(root); // print the trie layer wise
//    return 0;
//}

