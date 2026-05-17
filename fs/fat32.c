#include <stdint.h>
#include "ata.h"
#include "fs.h"
#include "libc.h"

#define SECTOR_SIZE 512
#define FAT32_EOC   0x0FFFFFF8u

typedef struct {
    uint8_t  jump[3];
    uint8_t  oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sector_count;
    uint8_t  fat_count;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t  media;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t head_count;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
} __attribute__((packed)) fat32_bpb_t;

typedef struct {
    char     name[11];
    uint8_t  attr;
    uint8_t  nt_reserved;
    uint8_t  creation_time_tenths;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t access_date;
    uint16_t first_cluster_high;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t first_cluster_low;
    uint32_t size;
} __attribute__((packed)) fat32_dirent_t;

static uint8_t sector[SECTOR_SIZE];
static fat32_bpb_t bpb;
static uint32_t fat_start_lba = 0;
static uint32_t data_start_lba = 0;
static int mounted = 0;

static uint16_t rd16(const void *ptr) {
    const uint8_t *p = (const uint8_t*)ptr;
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const void *ptr) {
    const uint8_t *p = (const uint8_t*)ptr;
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint32_t cluster_to_lba(uint32_t cluster) {
    return data_start_lba + (cluster - 2) * bpb.sectors_per_cluster;
}

static uint32_t first_cluster(const fat32_dirent_t *entry) {
    return ((uint32_t)rd16(&entry->first_cluster_high) << 16) |
           (uint32_t)rd16(&entry->first_cluster_low);
}

static uint32_t next_cluster(uint32_t cluster) {
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fat_start_lba + fat_offset / SECTOR_SIZE;
    uint32_t entry_offset = fat_offset % SECTOR_SIZE;

    if (!ata_read_sector(fat_sector, sector)) return FAT32_EOC;
    return rd32(sector + entry_offset) & 0x0FFFFFFFu;
}

static void short_name_to_string(const fat32_dirent_t *entry, char *out) {
    int pos = 0;
    for (int i = 0; i < 8 && entry->name[i] != ' '; i++) {
        out[pos++] = entry->name[i];
    }

    if (entry->name[8] != ' ') {
        out[pos++] = '.';
        for (int i = 8; i < 11 && entry->name[i] != ' '; i++) {
            out[pos++] = entry->name[i];
        }
    }

    out[pos] = 0;
}

static char upper_ascii(char c) {
    if (c >= 'a' && c <= 'z') return (char)(c - 'a' + 'A');
    return c;
}

static void make_short_name(const char *name, char out[11]) {
    for (int i = 0; i < 11; i++) out[i] = ' ';

    int pos = 0;
    while (*name && *name != '.' && pos < 8) {
        out[pos++] = upper_ascii(*name++);
    }

    if (*name == '.') name++;

    pos = 8;
    while (*name && pos < 11) {
        out[pos++] = upper_ascii(*name++);
    }
}

static int valid_file_entry(const fat32_dirent_t *entry) {
    uint8_t first = (uint8_t)entry->name[0];
    if (first == 0x00 || first == 0xE5) return 0;
    if (entry->attr == 0x0F) return 0;
    if (entry->attr & 0x08) return 0;
    return 1;
}

static int find_root_entry(const char *name, fat32_dirent_t *out_entry) {
    char target[11];
    make_short_name(name, target);

    uint32_t cluster = bpb.root_cluster;
    while (cluster < FAT32_EOC) {
        for (uint8_t s = 0; s < bpb.sectors_per_cluster; s++) {
            if (!ata_read_sector(cluster_to_lba(cluster) + s, sector)) return 0;

            fat32_dirent_t *entries = (fat32_dirent_t*)sector;
            for (int i = 0; i < SECTOR_SIZE / (int)sizeof(fat32_dirent_t); i++) {
                if ((uint8_t)entries[i].name[0] == 0x00) return 0;
                if (!valid_file_entry(&entries[i])) continue;
                if (memcmp(entries[i].name, target, 11) == 0) {
                    memcpy(out_entry, &entries[i], sizeof(fat32_dirent_t));
                    return 1;
                }
            }
        }
        cluster = next_cluster(cluster);
    }

    return 0;
}

int fs_init(void) {
    mounted = 0;
    if (!ata_read_sector(0, sector)) return 0;

    memcpy(&bpb, sector, sizeof(bpb));
    if (bpb.bytes_per_sector != SECTOR_SIZE) return 0;
    if (bpb.sectors_per_cluster == 0) return 0;
    if (bpb.fat_count == 0 || bpb.fat_size_32 == 0) return 0;
    if (sector[510] != 0x55 || sector[511] != 0xAA) return 0;

    fat_start_lba = bpb.reserved_sector_count;
    data_start_lba = fat_start_lba + bpb.fat_count * bpb.fat_size_32;
    mounted = 1;
    return 1;
}

int fs_is_ready(void) {
    return mounted;
}

int fs_list_root(fs_list_cb_t cb, void *ctx) {
    if (!mounted || !cb) return 0;

    uint32_t cluster = bpb.root_cluster;
    while (cluster < FAT32_EOC) {
        for (uint8_t s = 0; s < bpb.sectors_per_cluster; s++) {
            if (!ata_read_sector(cluster_to_lba(cluster) + s, sector)) return 0;

            fat32_dirent_t *entries = (fat32_dirent_t*)sector;
            for (int i = 0; i < SECTOR_SIZE / (int)sizeof(fat32_dirent_t); i++) {
                if ((uint8_t)entries[i].name[0] == 0x00) return 1;
                if (!valid_file_entry(&entries[i])) continue;

                char name[13];
                short_name_to_string(&entries[i], name);
                cb(name, rd32(&entries[i].size), ctx);
            }
        }
        cluster = next_cluster(cluster);
    }

    return 1;
}

int fs_read_file(const char *name, uint8_t *buffer, uint32_t buffer_size, uint32_t *out_size) {
    if (out_size) *out_size = 0;
    if (!mounted || !name || !buffer) return 0;

    fat32_dirent_t entry;
    if (!find_root_entry(name, &entry)) return 0;

    uint32_t file_size = rd32(&entry.size);
    uint32_t remaining = file_size;
    uint32_t written = 0;
    uint32_t cluster = first_cluster(&entry);

    while (remaining > 0 && cluster < FAT32_EOC) {
        for (uint8_t s = 0; s < bpb.sectors_per_cluster && remaining > 0; s++) {
            if (!ata_read_sector(cluster_to_lba(cluster) + s, sector)) return 0;

            uint32_t to_copy = remaining > SECTOR_SIZE ? SECTOR_SIZE : remaining;
            if (written + to_copy > buffer_size) {
                to_copy = buffer_size - written;
            }

            if (to_copy > 0) {
                memcpy(buffer + written, sector, to_copy);
                written += to_copy;
            }

            remaining -= remaining > SECTOR_SIZE ? SECTOR_SIZE : remaining;
            if (written == buffer_size && remaining > 0) {
                if (out_size) *out_size = written;
                return 0;
            }
        }
        cluster = next_cluster(cluster);
    }

    if (out_size) *out_size = written;
    return written == file_size;
}
