#ifndef MINERVA_INTERRUPTS_H
#define MINERVA_INTERRUPTS_H

#include <stdint.h>

typedef struct __attribute__((packed)) interrupt_frame {
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp_dummy;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint32_t gs;
    uint32_t fs;
    uint32_t es;
    uint32_t ds;
    uint32_t int_no;
    uint32_t err_code;
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
    uint32_t useresp;
    uint32_t ss;
} interrupt_frame_t;

void interrupts_init(void);
void interrupts_enable(void);
uint32_t interrupt_handler(interrupt_frame_t* frame);

void pit_init(uint32_t frequency_hz);
uint32_t pit_get_ticks(void);

#endif
