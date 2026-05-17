#ifndef MINERVA_FS_H
#define MINERVA_FS_H

#include <stdint.h>

typedef void (*fs_list_cb_t)(const char *name, uint32_t size, void *ctx);

typedef struct {
    uint32_t first_cluster;
    uint32_t size;
    uint32_t position;
    int open;
} fs_file_t;

#define FS_WRITE_CREATE   0x01u
#define FS_WRITE_TRUNCATE 0x02u
#define FS_WRITE_APPEND   0x04u
#define FS_WRITE_EXCL     0x08u

int fs_init(void);
int fs_is_ready(void);
int fs_list_root(fs_list_cb_t cb, void *ctx);
int fs_create(const char *name);
int fs_open(const char *name, fs_file_t *file);
uint32_t fs_read(fs_file_t *file, uint8_t *buffer, uint32_t count);
uint32_t fs_file_size(const fs_file_t *file);
uint32_t fs_tell(const fs_file_t *file);
int fs_close(fs_file_t *file);
int fs_read_file(const char *name, uint8_t *buffer, uint32_t buffer_size, uint32_t *out_size);
int fs_write_file(const char *name, const uint8_t *buffer, uint32_t size);
int fs_append_file(const char *name, const uint8_t *buffer, uint32_t size);
int fs_truncate_file(const char *name);
int fs_write(const char *name, const uint8_t *buffer, uint32_t size, uint32_t flags);
int fs_delete_file(const char *name);
int fs_rename_file(const char *old_name, const char *new_name);

#endif
