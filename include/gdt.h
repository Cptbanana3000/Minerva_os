#ifndef MINERVA_GDT_H
#define MINERVA_GDT_H

#include <stdint.h>

#define GDT_KERNEL_CODE 0x08u
#define GDT_KERNEL_DATA 0x10u
#define GDT_USER_CODE   0x1Bu
#define GDT_USER_DATA   0x23u
#define GDT_TSS         0x28u

void gdt_init(void);
uint32_t gdt_user_code_selector(void);
uint32_t gdt_user_data_selector(void);
uint32_t gdt_tss_selector(void);
uint32_t gdt_tss_loaded(void);
uint32_t gdt_tss_esp0(void);
uint32_t gdt_tss_ss0(void);

#endif
