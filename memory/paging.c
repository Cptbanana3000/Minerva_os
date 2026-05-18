#include <stdint.h>
#include "paging.h"
#include "pmm.h"
#include "libc.h"

/* Page directory and the one page table that identity-maps the first 4 MB.
   Both must be 4 KB-aligned so CR3 / PDE addresses are valid. */
static uint32_t page_dir   [1024] __attribute__((aligned(4096)));
static uint32_t page_table0[1024] __attribute__((aligned(4096)));

void paging_init(void) {
    /* Clear directory */
    for (int i = 0; i < 1024; i++)
        page_dir[i] = 0;

    /* Identity-map 0x00000000 – 0x003FFFFF (4 MB).
       Covers: kernel (0x8000+), heap, back_buffer, stack (0x90000), VGA (0xA0000). */
    for (int i = 0; i < 1024; i++)
        page_table0[i] = (uint32_t)(i * 4096) | PAGE_PRESENT | PAGE_WRITABLE;

    page_dir[0] = (uint32_t)page_table0 | PAGE_PRESENT | PAGE_WRITABLE;

    /* Load CR3 with page-directory physical address, then set CR0.PG */
    __asm__ volatile (
        "mov %0,    %%cr3\n\t"
        "mov %%cr0, %%eax\n\t"
        "or  $0x80000000, %%eax\n\t"
        "mov %%eax, %%cr0\n\t"
        : : "r"(page_dir) : "eax"
    );
}

/* Map one virtual page to a physical page.
   Allocates a new page table from PMM if the PDE is not present yet.
   Only works while the new PT itself is within the identity-mapped region. */
void paging_map(uint32_t virt, uint32_t phys, uint32_t flags) {
    uint32_t pd_i = virt >> 22;
    uint32_t pt_i = (virt >> 12) & 0x3FF;

    if (!(page_dir[pd_i] & PAGE_PRESENT)) {
        uint32_t new_pt = pmm_alloc_page();
        if (!new_pt) return;
        memset((void*)new_pt, 0, 4096);
        page_dir[pd_i] = new_pt | PAGE_PRESENT | PAGE_WRITABLE;
    }
    if (flags & PAGE_USER) {
        page_dir[pd_i] |= PAGE_USER;
    }

    uint32_t* pt = (uint32_t*)(page_dir[pd_i] & ~0xFFFu);
    pt[pt_i] = (phys & ~0xFFFu) | (flags & 0xFFFu) | PAGE_PRESENT;
}
