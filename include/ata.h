#ifndef MINERVA_ATA_H
#define MINERVA_ATA_H

#include <stdint.h>

int ata_read_sector(uint32_t lba, void *buffer);

#endif
