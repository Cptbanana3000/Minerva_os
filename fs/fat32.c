#include <stdint.h>
#include "ata.h"
#include "fs.h"
#include "libc.h"

#define SECTOR_SIZE 512
#define FAT32_EOC   0x0FFFFFF8u
#define FAT32_EOC_MARK 0x0FFFFFFFu
#define FAT_ATTR_DIRECTORY 0x10
#define FAT_ATTR_ARCHIVE   0x20

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

static void wr16(void *ptr, uint16_t value) {
    uint8_t *p = (uint8_t*)ptr;
    p[0] = (uint8_t)(value & 0xFF);
    p[1] = (uint8_t)((value >> 8) & 0xFF);
}

static void wr32(void *ptr, uint32_t value) {
    uint8_t *p = (uint8_t*)ptr;
    p[0] = (uint8_t)(value & 0xFF);
    p[1] = (uint8_t)((value >> 8) & 0xFF);
    p[2] = (uint8_t)((value >> 16) & 0xFF);
    p[3] = (uint8_t)((value >> 24) & 0xFF);
}

static uint32_t cluster_to_lba(uint32_t cluster) {
    return data_start_lba + (cluster - 2) * bpb.sectors_per_cluster;
}

static uint32_t total_sectors(void) {
    uint32_t total = rd32(&bpb.total_sectors_32);
    if (total == 0) total = (uint32_t)rd16(&bpb.total_sectors_16);
    return total;
}

static uint32_t first_cluster(const fat32_dirent_t *entry) {
    return ((uint32_t)rd16(&entry->first_cluster_high) << 16) |
           (uint32_t)rd16(&entry->first_cluster_low);
}

static uint32_t next_cluster(uint32_t cluster) {
    uint32_t value = FAT32_EOC;

    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fat_start_lba + fat_offset / SECTOR_SIZE;
    uint32_t entry_offset = fat_offset % SECTOR_SIZE;

    if (!ata_read_sector(fat_sector, sector)) return FAT32_EOC;
    value = rd32(sector + entry_offset) & 0x0FFFFFFFu;
    return value;
}

static int read_fat_entry(uint32_t cluster, uint32_t *out_value) {
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fat_start_lba + fat_offset / SECTOR_SIZE;
    uint32_t entry_offset = fat_offset % SECTOR_SIZE;

    if (!out_value || !ata_read_sector(fat_sector, sector)) return 0;
    *out_value = rd32(sector + entry_offset) & 0x0FFFFFFFu;
    return 1;
}

static int write_fat_entry(uint32_t cluster, uint32_t value) {
    uint32_t fat_offset = cluster * 4;
    uint32_t entry_offset = fat_offset % SECTOR_SIZE;

    for (uint8_t fat = 0; fat < bpb.fat_count; fat++) {
        uint32_t fat_sector = fat_start_lba + (uint32_t)fat * bpb.fat_size_32 +
                              fat_offset / SECTOR_SIZE;
        if (!ata_read_sector(fat_sector, sector)) return 0;
        wr32(sector + entry_offset, value);
        if (!ata_write_sector(fat_sector, sector)) return 0;
    }

    return 1;
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

static int valid_short_name_char(char c) {
    c = upper_ascii(c);
    if (c >= 'A' && c <= 'Z') return 1;
    if (c >= '0' && c <= '9') return 1;
    return c == '_' || c == '-' || c == '~';
}

static int make_short_name_checked(const char *name, char out[11]) {
    if (!name || !*name) return 0;
    for (int i = 0; i < 11; i++) out[i] = ' ';

    int base_len = 0;
    while (*name && *name != '.') {
        if (base_len >= 8 || !valid_short_name_char(*name)) return 0;
        out[base_len++] = upper_ascii(*name++);
    }
    if (base_len == 0) return 0;

    if (*name == '.') {
        name++;
        int ext_len = 0;
        while (*name) {
            if (ext_len >= 3 || *name == '.' || !valid_short_name_char(*name)) return 0;
            out[8 + ext_len++] = upper_ascii(*name++);
        }
        if (ext_len == 0) return 0;
    }

    return *name == 0;
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

static int find_root_entry_at(const char *name, fat32_dirent_t *out_entry,
                              uint32_t *out_lba, uint32_t *out_index) {
    char target[11];
    make_short_name(name, target);

    uint32_t cluster = bpb.root_cluster;
    while (cluster < FAT32_EOC) {
        for (uint8_t s = 0; s < bpb.sectors_per_cluster; s++) {
            uint32_t lba = cluster_to_lba(cluster) + s;
            if (!ata_read_sector(lba, sector)) return 0;

            fat32_dirent_t *entries = (fat32_dirent_t*)sector;
            for (int i = 0; i < SECTOR_SIZE / (int)sizeof(fat32_dirent_t); i++) {
                if ((uint8_t)entries[i].name[0] == 0x00) return 0;
                if (!valid_file_entry(&entries[i])) continue;
                if (memcmp(entries[i].name, target, 11) == 0) {
                    if (out_entry) memcpy(out_entry, &entries[i], sizeof(fat32_dirent_t));
                    if (out_lba) *out_lba = lba;
                    if (out_index) *out_index = (uint32_t)i;
                    return 1;
                }
            }
        }
        cluster = next_cluster(cluster);
    }

    return 0;
}

static int find_free_cluster(uint32_t *out_cluster) {
    uint32_t total = total_sectors();
    if (!out_cluster || total <= data_start_lba) return 0;

    uint32_t data_sectors = total - data_start_lba;
    uint32_t max_cluster = data_sectors / bpb.sectors_per_cluster + 2;

    for (uint32_t cluster = 2; cluster < max_cluster; cluster++) {
        uint32_t value = 0;
        if (!read_fat_entry(cluster, &value)) return 0;
        if (value == 0) {
            *out_cluster = cluster;
            return 1;
        }
    }

    return 0;
}

static int free_cluster_chain(uint32_t first_cluster) {
    uint32_t total = total_sectors();
    if (total <= data_start_lba) return 0;

    uint32_t data_sectors = total - data_start_lba;
    uint32_t max_cluster = data_sectors / bpb.sectors_per_cluster + 2;
    uint32_t visited = 0;
    uint32_t cluster = first_cluster;

    while (cluster >= 2 && cluster < FAT32_EOC) {
        if (cluster >= max_cluster || visited++ >= max_cluster) return 0;

        uint32_t next = 0;
        if (!read_fat_entry(cluster, &next)) return 0;
        if (!write_fat_entry(cluster, 0)) return 0;
        cluster = next;
    }

    return 1;
}

static int cluster_is_single(uint32_t cluster) {
    uint32_t next = 0;
    if (cluster < 2 || !read_fat_entry(cluster, &next)) return 0;
    return next >= FAT32_EOC;
}

static int write_buffer_to_cluster(uint32_t cluster, const uint8_t *buffer, uint32_t size) {
    uint32_t written = 0;

    for (uint8_t s = 0; s < bpb.sectors_per_cluster; s++) {
        memset(sector, 0, SECTOR_SIZE);

        uint32_t remaining = size - written;
        uint32_t to_copy = remaining > SECTOR_SIZE ? SECTOR_SIZE : remaining;
        if (to_copy > 0) {
            memcpy(sector, buffer + written, to_copy);
            written += to_copy;
        }

        if (!ata_write_sector(cluster_to_lba(cluster) + s, sector)) return 0;
    }

    return 1;
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

int fs_create(const char *name) {
    if (!mounted || !name) return 0;

    char short_name[11];
    if (!make_short_name_checked(name, short_name)) return 0;

    fat32_dirent_t existing;
    if (find_root_entry(name, &existing)) return 0;

    uint32_t cluster = bpb.root_cluster;
    while (cluster < FAT32_EOC) {
        for (uint8_t s = 0; s < bpb.sectors_per_cluster; s++) {
            uint32_t lba = cluster_to_lba(cluster) + s;
            if (!ata_read_sector(lba, sector)) return 0;

            fat32_dirent_t *entries = (fat32_dirent_t*)sector;
            for (int i = 0; i < SECTOR_SIZE / (int)sizeof(fat32_dirent_t); i++) {
                uint8_t first = (uint8_t)entries[i].name[0];
                if (first != 0x00 && first != 0xE5) continue;

                memset(&entries[i], 0, sizeof(fat32_dirent_t));
                memcpy(entries[i].name, short_name, 11);
                entries[i].attr = FAT_ATTR_ARCHIVE;
                wr16(&entries[i].first_cluster_high, 0);
                wr16(&entries[i].first_cluster_low, 0);
                wr32(&entries[i].size, 0);
                return ata_write_sector(lba, sector);
            }
        }
        cluster = next_cluster(cluster);
    }

    return 0;
}

int fs_open(const char *name, fs_file_t *file) {
    if (!mounted || !name || !file) return 0;

    fat32_dirent_t entry;
    if (!find_root_entry(name, &entry)) return 0;
    if (entry.attr & FAT_ATTR_DIRECTORY) return 0;

    file->first_cluster = first_cluster(&entry);
    file->size = rd32(&entry.size);
    file->position = 0;
    file->open = 1;
    return 1;
}

uint32_t fs_read(fs_file_t *file, uint8_t *buffer, uint32_t count) {
    if (!mounted || !file || !file->open || !buffer || count == 0) return 0;
    if (file->position >= file->size) return 0;

    uint32_t remaining_in_file = file->size - file->position;
    if (count > remaining_in_file) count = remaining_in_file;

    uint32_t bytes_per_cluster = (uint32_t)bpb.sectors_per_cluster * SECTOR_SIZE;
    uint32_t cluster = file->first_cluster;
    uint32_t skip = file->position;
    uint32_t read = 0;

    while (skip >= bytes_per_cluster && cluster < FAT32_EOC) {
        cluster = next_cluster(cluster);
        skip -= bytes_per_cluster;
    }

    while (read < count && cluster < FAT32_EOC) {
        uint8_t sector_index = (uint8_t)(skip / SECTOR_SIZE);
        uint32_t byte_offset = skip % SECTOR_SIZE;

        for (uint8_t s = sector_index; s < bpb.sectors_per_cluster && read < count; s++) {
            if (!ata_read_sector(cluster_to_lba(cluster) + s, sector)) return 0;

            uint32_t available = SECTOR_SIZE - byte_offset;
            uint32_t wanted = count - read;
            uint32_t to_copy = available < wanted ? available : wanted;

            if (to_copy > 0) {
                memcpy(buffer + read, sector + byte_offset, to_copy);
                read += to_copy;
            }

            byte_offset = 0;
        }

        skip = 0;
        cluster = next_cluster(cluster);
    }

    file->position += read;
    return read;
}

uint32_t fs_file_size(const fs_file_t *file) {
    if (!file || !file->open) return 0;
    return file->size;
}

uint32_t fs_tell(const fs_file_t *file) {
    if (!file || !file->open) return 0;
    return file->position;
}

int fs_close(fs_file_t *file) {
    if (!file || !file->open) return 0;
    memset(file, 0, sizeof(fs_file_t));
    return 1;
}

int fs_read_file(const char *name, uint8_t *buffer, uint32_t buffer_size, uint32_t *out_size) {
    if (out_size) *out_size = 0;
    if (!buffer && buffer_size > 0) return 0;

    fs_file_t file;
    if (!fs_open(name, &file)) return 0;

    uint32_t size = fs_file_size(&file);
    uint32_t read = fs_read(&file, buffer, buffer_size);
    fs_close(&file);

    if (out_size) *out_size = read;
    return read == size;
}

int fs_write_file(const char *name, const uint8_t *buffer, uint32_t size) {
    if (!mounted || !name || (!buffer && size > 0)) return 0;

    char short_name[11];
    if (!make_short_name_checked(name, short_name)) return 0;

    uint32_t bytes_per_cluster = (uint32_t)bpb.sectors_per_cluster * SECTOR_SIZE;
    if (size > bytes_per_cluster) return 0;

    fat32_dirent_t entry;
    uint32_t dir_lba = 0;
    uint32_t dir_index = 0;
    if (!find_root_entry_at(name, &entry, &dir_lba, &dir_index)) return 0;
    if (entry.attr & FAT_ATTR_DIRECTORY) return 0;
    if (size == 0) return 1;

    uint32_t target_cluster = first_cluster(&entry);
    if (target_cluster == 0) {
        if (rd32(&entry.size) != 0 || !find_free_cluster(&target_cluster)) return 0;
        if (!write_buffer_to_cluster(target_cluster, buffer, size)) return 0;
        if (!write_fat_entry(target_cluster, FAT32_EOC_MARK)) return 0;
    } else {
        if (!cluster_is_single(target_cluster)) return 0;
        if (!write_buffer_to_cluster(target_cluster, buffer, size)) return 0;
    }

    if (!ata_read_sector(dir_lba, sector)) return 0;
    fat32_dirent_t *entries = (fat32_dirent_t*)sector;
    wr16(&entries[dir_index].first_cluster_high, (uint16_t)(target_cluster >> 16));
    wr16(&entries[dir_index].first_cluster_low, (uint16_t)(target_cluster & 0xFFFF));
    wr32(&entries[dir_index].size, size);
    return ata_write_sector(dir_lba, sector);
}

int fs_append_file(const char *name, const uint8_t *buffer, uint32_t size) {
    if (!mounted || !name || (!buffer && size > 0)) return 0;
    if (size == 0) return 1;

    char short_name[11];
    if (!make_short_name_checked(name, short_name)) return 0;

    fat32_dirent_t entry;
    uint32_t dir_lba = 0;
    uint32_t dir_index = 0;
    if (!find_root_entry_at(name, &entry, &dir_lba, &dir_index)) return 0;
    if (entry.attr & FAT_ATTR_DIRECTORY) return 0;

    uint32_t cluster = first_cluster(&entry);
    uint32_t old_size = rd32(&entry.size);
    uint32_t bytes_per_cluster = (uint32_t)bpb.sectors_per_cluster * SECTOR_SIZE;
    uint32_t new_size = old_size + size;
    if (cluster < 2 || new_size < old_size) return 0;

    uint32_t offset_in_cluster = old_size;
    while (offset_in_cluster > bytes_per_cluster) {
        uint32_t next = next_cluster(cluster);
        if (next >= FAT32_EOC) return 0;
        cluster = next;
        offset_in_cluster -= bytes_per_cluster;
    }

    uint32_t tail_next = 0;
    if (!read_fat_entry(cluster, &tail_next) || tail_next < FAT32_EOC) return 0;

    uint32_t written = 0;
    while (written < size && offset_in_cluster < bytes_per_cluster) {
        uint8_t sector_index = (uint8_t)(offset_in_cluster / SECTOR_SIZE);
        uint32_t byte_offset = offset_in_cluster % SECTOR_SIZE;
        uint32_t lba = cluster_to_lba(cluster) + sector_index;

        if (!ata_read_sector(lba, sector)) return 0;

        uint32_t available = SECTOR_SIZE - byte_offset;
        uint32_t remaining = size - written;
        uint32_t to_copy = available < remaining ? available : remaining;
        memcpy(sector + byte_offset, buffer + written, to_copy);
        if (!ata_write_sector(lba, sector)) return 0;

        written += to_copy;
        offset_in_cluster += to_copy;
    }

    while (written < size) {
        uint32_t new_cluster = 0;
        if (!find_free_cluster(&new_cluster)) return 0;

        uint32_t remaining = size - written;
        uint32_t to_copy = remaining > bytes_per_cluster ? bytes_per_cluster : remaining;
        if (!write_buffer_to_cluster(new_cluster, buffer + written, to_copy)) return 0;
        if (!write_fat_entry(new_cluster, FAT32_EOC_MARK)) return 0;
        if (!write_fat_entry(cluster, new_cluster)) return 0;

        cluster = new_cluster;
        written += to_copy;
    }

    if (!ata_read_sector(dir_lba, sector)) return 0;
    fat32_dirent_t *entries = (fat32_dirent_t*)sector;
    wr32(&entries[dir_index].size, new_size);
    return ata_write_sector(dir_lba, sector);
}

int fs_truncate_file(const char *name) {
    if (!mounted || !name) return 0;

    char short_name[11];
    if (!make_short_name_checked(name, short_name)) return 0;

    fat32_dirent_t entry;
    uint32_t dir_lba = 0;
    uint32_t dir_index = 0;
    if (!find_root_entry_at(name, &entry, &dir_lba, &dir_index)) return 0;
    if (entry.attr & FAT_ATTR_DIRECTORY) return 0;

    uint32_t cluster = first_cluster(&entry);
    if (cluster >= 2 && !free_cluster_chain(cluster)) return 0;

    if (!ata_read_sector(dir_lba, sector)) return 0;
    fat32_dirent_t *entries = (fat32_dirent_t*)sector;
    wr16(&entries[dir_index].first_cluster_high, 0);
    wr16(&entries[dir_index].first_cluster_low, 0);
    wr32(&entries[dir_index].size, 0);
    return ata_write_sector(dir_lba, sector);
}

int fs_write(const char *name, const uint8_t *buffer, uint32_t size, uint32_t flags) {
    if (!mounted || !name || (!buffer && size > 0)) return 0;
    if ((flags & FS_WRITE_APPEND) && (flags & FS_WRITE_TRUNCATE)) return 0;

    char short_name[11];
    if (!make_short_name_checked(name, short_name)) return 0;

    fat32_dirent_t entry;
    int exists = find_root_entry_at(name, &entry, 0, 0);
    if (exists && (entry.attr & FAT_ATTR_DIRECTORY)) return 0;

    if (exists && (flags & FS_WRITE_EXCL)) return 0;
    if (!exists) {
        if (!(flags & FS_WRITE_CREATE)) return 0;
        if (!fs_create(name)) return 0;
    }

    if (flags & FS_WRITE_TRUNCATE) {
        if (!fs_truncate_file(name)) return 0;
        if (size == 0) return 1;
        return fs_write_file(name, buffer, size);
    }

    if (flags & FS_WRITE_APPEND) {
        if (size == 0) return 1;

        fat32_dirent_t current;
        if (!find_root_entry_at(name, &current, 0, 0)) return 0;
        if (first_cluster(&current) == 0) {
            return fs_write_file(name, buffer, size);
        }
        return fs_append_file(name, buffer, size);
    }

    return fs_write_file(name, buffer, size);
}

int fs_delete_file(const char *name) {
    if (!mounted || !name) return 0;

    char short_name[11];
    if (!make_short_name_checked(name, short_name)) return 0;

    fat32_dirent_t entry;
    uint32_t dir_lba = 0;
    uint32_t dir_index = 0;
    if (!find_root_entry_at(name, &entry, &dir_lba, &dir_index)) return 0;
    if (entry.attr & FAT_ATTR_DIRECTORY) return 0;

    uint32_t cluster = first_cluster(&entry);
    if (cluster >= 2 && !free_cluster_chain(cluster)) return 0;

    if (!ata_read_sector(dir_lba, sector)) return 0;
    fat32_dirent_t *entries = (fat32_dirent_t*)sector;
    memset(&entries[dir_index], 0, sizeof(fat32_dirent_t));
    entries[dir_index].name[0] = (char)0xE5;
    return ata_write_sector(dir_lba, sector);
}

int fs_rename_file(const char *old_name, const char *new_name) {
    if (!mounted || !old_name || !new_name) return 0;

    char new_short_name[11];
    if (!make_short_name_checked(new_name, new_short_name)) return 0;

    fat32_dirent_t entry;
    uint32_t dir_lba = 0;
    uint32_t dir_index = 0;
    if (!find_root_entry_at(old_name, &entry, &dir_lba, &dir_index)) return 0;
    if (entry.attr & FAT_ATTR_DIRECTORY) return 0;

    fat32_dirent_t existing;
    if (find_root_entry(new_name, &existing)) return 0;

    if (!ata_read_sector(dir_lba, sector)) return 0;
    fat32_dirent_t *entries = (fat32_dirent_t*)sector;
    memcpy(entries[dir_index].name, new_short_name, 11);
    return ata_write_sector(dir_lba, sector);
}
