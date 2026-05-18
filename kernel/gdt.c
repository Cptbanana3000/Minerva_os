#include <stdint.h>
#include "gdt.h"
#include "libc.h"

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed)) gdt_entry_t;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) gdt_ptr_t;

typedef struct {
    uint32_t prev_tss;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t es;
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
} __attribute__((packed)) tss_entry_t;

extern void gdt_load(gdt_ptr_t *gdt_ptr);
extern void tss_load(uint16_t selector);

static gdt_entry_t gdt[6];
static gdt_ptr_t gdt_ptr;
static tss_entry_t tss;
static uint32_t tss_stack[1024] __attribute__((aligned(16)));
static uint8_t tss_loaded = 0;

static void gdt_set_entry(uint32_t index,
                          uint32_t base,
                          uint32_t limit,
                          uint8_t access,
                          uint8_t granularity) {
    gdt[index].limit_low = (uint16_t)(limit & 0xFFFFu);
    gdt[index].base_low = (uint16_t)(base & 0xFFFFu);
    gdt[index].base_mid = (uint8_t)((base >> 16) & 0xFFu);
    gdt[index].access = access;
    gdt[index].granularity = (uint8_t)(((limit >> 16) & 0x0Fu) |
                                       (granularity & 0xF0u));
    gdt[index].base_high = (uint8_t)((base >> 24) & 0xFFu);
}

void gdt_init(void) {
    memset(gdt, 0, sizeof(gdt));
    memset(&tss, 0, sizeof(tss));

    gdt_set_entry(1, 0, 0xFFFFFu, 0x9Au, 0xCFu);
    gdt_set_entry(2, 0, 0xFFFFFu, 0x92u, 0xCFu);
    gdt_set_entry(3, 0, 0xFFFFFu, 0xFAu, 0xCFu);
    gdt_set_entry(4, 0, 0xFFFFFu, 0xF2u, 0xCFu);

    tss.ss0 = GDT_KERNEL_DATA;
    tss.esp0 = (uint32_t)(tss_stack + 1024);
    tss.iomap_base = sizeof(tss_entry_t);
    gdt_set_entry(5, (uint32_t)&tss, sizeof(tss_entry_t) - 1, 0x89u, 0x00u);

    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base = (uint32_t)&gdt;

    gdt_load(&gdt_ptr);
    tss_load(GDT_TSS);
    tss_loaded = 1;
}

uint32_t gdt_user_code_selector(void) {
    return GDT_USER_CODE;
}

uint32_t gdt_user_data_selector(void) {
    return GDT_USER_DATA;
}

uint32_t gdt_tss_selector(void) {
    return GDT_TSS;
}

uint32_t gdt_tss_loaded(void) {
    return tss_loaded;
}

uint32_t gdt_tss_esp0(void) {
    return tss.esp0;
}

uint32_t gdt_tss_ss0(void) {
    return tss.ss0;
}
