#include <stdint.h>
#include <stddef.h>

#include "memory.h"

#define HEAP_SIZE (256 * 1024)

typedef struct block_header {
    size_t size;
    uint8_t in_use;
    struct block_header* next;
} block_header_t;

static uint8_t heap_memory[HEAP_SIZE];
static block_header_t* free_list = NULL;

void heap_init(void) {
    free_list = (block_header_t*)heap_memory;
    free_list->size = HEAP_SIZE - sizeof(block_header_t);
    free_list->in_use = 0;
    free_list->next = NULL;
}

void* kmalloc(size_t size) {
    if (size == 0 || free_list == NULL) {
        return NULL;
    }

    block_header_t* current = free_list;
    block_header_t* best_fit = NULL;
    size_t best_fit_size = (size_t)-1;

    while (current != NULL) {
        if (!current->in_use && current->size >= size) {
            if (current->size < best_fit_size) {
                best_fit = current;
                best_fit_size = current->size;
            }
        }
        current = current->next;
    }

    if (best_fit == NULL) {
        return NULL;
    }

    size_t remaining = best_fit->size - size;
    if (remaining > sizeof(block_header_t)) {
        block_header_t* new_block = (block_header_t*)((uint8_t*)best_fit + sizeof(block_header_t) + size);
        new_block->size = remaining - sizeof(block_header_t);
        new_block->in_use = 0;
        new_block->next = best_fit->next;

        best_fit->size = size;
        best_fit->next = new_block;
    }

    best_fit->in_use = 1;
    return (void*)((uint8_t*)best_fit + sizeof(block_header_t));
}

void kfree(void* ptr) {
    if (ptr == NULL || free_list == NULL) {
        return;
    }

    block_header_t* block = (block_header_t*)((uint8_t*)ptr - sizeof(block_header_t));
    block->in_use = 0;

    block_header_t* current = free_list;
    while (current != NULL) {
        if (!current->in_use && current->next != NULL && !current->next->in_use) {
            current->size += sizeof(block_header_t) + current->next->size;
            current->next = current->next->next;
        } else {
            current = current->next;
        }
    }
}
