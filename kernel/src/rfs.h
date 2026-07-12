#ifndef RFS_H
#define RFS_H

#include <stdint.h>
#include <stdbool.h>

// Define a unified node structure for an in-memory file
typedef struct RamFile {
    const char *name;         // Name of the file (e.g., "hello.txt")
    const char *content;      // Pointer to the raw text/binary data
    uint32_t size;            // Total byte size of the file contents
    struct RamFile *next;     // Pointer to the next file in the list
} RamFile;

// Simple helper to calculate string lengths manually
static inline uint32_t rfs_strlen(const char *str) {
    uint32_t len = 0;
    while (str[len] != '\0') len++;
    return len;
}




// Global list root pointers
static RamFile *rfs_root = NULL;

void rfs_set_file(RamFile *file_node, const char* content) {
  file_node->content = content;
  file_node->size = rfs_strlen(content);
}

// Appends an explicitly defined file directly into our dynamic file system list
void rfs_create_file(RamFile *new_file_node, const char *name, const char *content) {
    new_file_node->name = name;
    new_file_node->content = content;
    new_file_node->size = rfs_strlen(content);
    new_file_node->next = NULL;

    // If the list is empty, make this file the head node
    if (rfs_root == NULL) {
        rfs_root = new_file_node;
        return;
    }

    // Traverse to the end of the list and link the new file node
    RamFile *current = rfs_root;
    while (current->next != NULL) {
        current = current->next;
    }
    current->next = new_file_node;
}

// Searches the linked list for a filename matching the input string
RamFile* rfs_find_file(const char *name) {
    RamFile *current = rfs_root;
    
    while (current != NULL) {
        // Reuse your existing string_compare logic via manual pointer matching
        const char *s1 = current->name;
        const char *s2 = name;
        while (*s1 && (*s1 == *s2)) { s1++; s2++; }
        
        if (*s1 - *s2 == 0) {
            return current; // Found it! Return the file object pointer
        }
        current = current->next; // Move to next node item
    }
    return NULL; // File not found
}

#endif
