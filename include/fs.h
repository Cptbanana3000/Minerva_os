#ifndef MINERVA_FS_H
#define MINERVA_FS_H

#include <stdint.h>

typedef void (*fs_list_cb_t)(const char *name, uint32_t size, void *ctx);

int fs_init(void);
int fs_is_ready(void);
int fs_list_root(fs_list_cb_t cb, void *ctx);
int fs_read_file(const char *name, uint8_t *buffer, uint32_t buffer_size, uint32_t *out_size);

#endif
