#include <stdint.h>
#include "speaker.h"
#include "io.h"
#include "interrupts.h"

#define PIT_BASE_HZ 1193182u

void speaker_set_frequency(uint32_t frequency_hz) {
    if (frequency_hz < 20) frequency_hz = 20;

    uint32_t divisor = PIT_BASE_HZ / frequency_hz;
    if (divisor == 0) divisor = 1;
    if (divisor > 0xFFFFu) divisor = 0xFFFFu;

    outb(0x43, 0xB6);
    outb(0x42, (uint8_t)(divisor & 0xFF));
    outb(0x42, (uint8_t)((divisor >> 8) & 0xFF));
}

void speaker_on(void) {
    uint8_t value = inb(0x61);
    outb(0x61, (uint8_t)(value | 0x03));
}

void speaker_off(void) {
    uint8_t value = inb(0x61);
    outb(0x61, (uint8_t)(value & ~0x03));
}

void speaker_tone_ticks(uint32_t frequency_hz, uint32_t ticks) {
    if (ticks == 0) return;

    speaker_set_frequency(frequency_hz);
    speaker_on();

    uint32_t start = pit_get_ticks();
    while (pit_get_ticks() - start < ticks) {
        __asm__ volatile ("pause");
    }
}
