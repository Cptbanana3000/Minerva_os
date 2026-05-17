#include <stdint.h>
#include "ata.h"
#include "io.h"

#define ATA_DATA        0x1F0
#define ATA_SECCOUNT    0x1F2
#define ATA_LBA0        0x1F3
#define ATA_LBA1        0x1F4
#define ATA_LBA2        0x1F5
#define ATA_DRIVE       0x1F6
#define ATA_COMMAND     0x1F7
#define ATA_STATUS      0x1F7

#define ATA_STATUS_ERR  0x01
#define ATA_STATUS_DRQ  0x08
#define ATA_STATUS_BSY  0x80

#define ATA_CMD_READ    0x20

static int ata_wait_ready(void) {
    for (uint32_t i = 0; i < 100000; i++) {
        uint8_t status = inb(ATA_STATUS);
        if (!(status & ATA_STATUS_BSY)) {
            return (status & ATA_STATUS_ERR) ? 0 : 1;
        }
    }
    return 0;
}

static int ata_wait_drq(void) {
    for (uint32_t i = 0; i < 100000; i++) {
        uint8_t status = inb(ATA_STATUS);
        if (status & ATA_STATUS_ERR) return 0;
        if (!(status & ATA_STATUS_BSY) && (status & ATA_STATUS_DRQ)) return 1;
    }
    return 0;
}

int ata_read_sector(uint32_t lba, void *buffer) {
    if (!buffer || lba >= 0x10000000u) return 0;
    if (!ata_wait_ready()) return 0;

    outb(ATA_DRIVE, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA_SECCOUNT, 1);
    outb(ATA_LBA0, (uint8_t)(lba & 0xFF));
    outb(ATA_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA2, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_COMMAND, ATA_CMD_READ);

    if (!ata_wait_drq()) return 0;

    uint16_t *dst = (uint16_t*)buffer;
    for (uint32_t i = 0; i < 256; i++) {
        dst[i] = inw(ATA_DATA);
    }

    return 1;
}
