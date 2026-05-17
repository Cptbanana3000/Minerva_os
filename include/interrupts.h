#ifndef MINERVA_INTERRUPTS_H
#define MINERVA_INTERRUPTS_H

#include <stdint.h>

typedef struct interrupt_frame interrupt_frame_t;

void interrupts_init(void);
void interrupts_enable(void);
void interrupt_handler(interrupt_frame_t* frame);

void pit_init(uint32_t frequency_hz);
uint32_t pit_get_ticks(void);

#endif