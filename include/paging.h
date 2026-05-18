#ifndef MINERVA_PAGING_H
#define MINERVA_PAGING_H

#include <stdint.h>

#define PAGE_PRESENT  0x01
#define PAGE_WRITABLE 0x02
#define PAGE_USER     0x04

void paging_init(void);
void paging_map(uint32_t virt, uint32_t phys, uint32_t flags);
void paging_map_range(uint32_t virt, uint32_t phys, uint32_t size, uint32_t flags);

#endif
