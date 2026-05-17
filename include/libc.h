#ifndef MINERVA_LIBC_H
#define MINERVA_LIBC_H

#include <stddef.h>

size_t strlen(const char* string);
void* memcpy(void* destination, const void* source, size_t count);
void* memset(void* destination, int value, size_t count);
char* strcpy(char* destination, const char* source);
int strcmp(const char* left, const char* right);

#endif