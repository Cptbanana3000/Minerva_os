#ifndef MINERVA_MEMORY_H
#define MINERVA_MEMORY_H

#include <stddef.h>

void heap_init(void);
void* kmalloc(size_t size);
void kfree(void* ptr);

#endif
