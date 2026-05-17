#ifndef MINERVA_PMM_H
#define MINERVA_PMM_H

#include <stdint.h>

void     pmm_init(uint32_t kernel_end);
uint32_t pmm_alloc_page(void);
void     pmm_free_page(uint32_t phys_addr);
uint32_t pmm_used_pages(void);
uint32_t pmm_free_pages(void);
uint32_t pmm_total_pages(void);

#endif
