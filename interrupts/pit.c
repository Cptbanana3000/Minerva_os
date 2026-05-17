#include <stdint.h>

#include "interrupts.h"
#include "io.h"

static volatile uint32_t timer_ticks = 0;

uint32_t pit_get_ticks(void) {
    return timer_ticks;
}

void pit_init(uint32_t frequency_hz) {
    if (frequency_hz == 0) {
        frequency_hz = 100;
    }

    uint32_t divisor = 1193182u / frequency_hz;

    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
}

void pit_tick(void) {
    timer_ticks++;
}