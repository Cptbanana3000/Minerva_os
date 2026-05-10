#include "libc.h"

size_t strlen(const char* string) {
    size_t length = 0;

    while (string[length] != '\0') {
        length++;
    }

    return length;
}

void* memcpy(void* destination, const void* source, size_t count) {
    unsigned char* dst = (unsigned char*)destination;
    const unsigned char* src = (const unsigned char*)source;

    for (size_t index = 0; index < count; index++) {
        dst[index] = src[index];
    }

    return destination;
}

void* memset(void* destination, int value, size_t count) {
    unsigned char* dst = (unsigned char*)destination;

    for (size_t index = 0; index < count; index++) {
        dst[index] = (unsigned char)value;
    }

    return destination;
}

char* strcpy(char* destination, const char* source) {
    char* result = destination;

    while ((*destination++ = *source++) != '\0') {
    }

    return result;
}

int strcmp(const char* left, const char* right) {
    while (*left && *left == *right) {
        left++;
        right++;
    }

    return (unsigned char)*left - (unsigned char)*right;
}