#include <stdint.h>
#include "pmm.h"

#define PAGE_SIZE     4096
#define MEM_BYTES     (32u * 1024 * 1024)   /* assume 32 MB (QEMU default) */
#define TOTAL_PAGES   (MEM_BYTES / PAGE_SIZE)
#define BITMAP_BYTES  (TOTAL_PAGES / 8)

static uint8_t  bitmap[BITMAP_BYTES];
static uint32_t used_count = 0;

static void mark_used(uint32_t p) { bitmap[p / 8] |=  (1u << (p % 8)); }
static void mark_free(uint32_t p) { bitmap[p / 8] &= ~(1u << (p % 8)); }
static int  is_used  (uint32_t p) { return (bitmap[p / 8] >> (p % 8)) & 1; }

void pmm_init(uint32_t kernel_end) {
    /* Start: every page marked used */
    for (uint32_t i = 0; i < BITMAP_BYTES; i++)
        bitmap[i] = 0xFF;
    used_count = TOTAL_PAGES;

    /* Free pages above the kernel, skipping reserved regions */
    uint32_t first_free = (kernel_end + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint32_t i = first_free; i < TOTAL_PAGES; i++) {
        /* 0xA0–0xBF: VGA framebuffer + text (0xA0000–0xBFFFF) */
        if (i >= 0xA0 && i <= 0xBF) continue;
        mark_free(i);
        used_count--;
    }
}

uint32_t pmm_alloc_page(void) {
    for (uint32_t i = 0; i < TOTAL_PAGES; i++) {
        if (!is_used(i)) {
            mark_used(i);
            used_count++;
            return i * PAGE_SIZE;
        }
    }
    return 0;   /* out of memory */
}

void pmm_free_page(uint32_t phys) {
    uint32_t p = phys / PAGE_SIZE;
    if (p < TOTAL_PAGES && is_used(p)) {
        mark_free(p);
        used_count--;
    }
}

uint32_t pmm_used_pages(void)  { return used_count; }
uint32_t pmm_free_pages(void)  { return TOTAL_PAGES - used_count; }
uint32_t pmm_total_pages(void) { return TOTAL_PAGES; }
