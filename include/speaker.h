#ifndef MINERVA_SPEAKER_H
#define MINERVA_SPEAKER_H

#include <stdint.h>

void speaker_set_frequency(uint32_t frequency_hz);
void speaker_on(void);
void speaker_off(void);
void speaker_tone_ticks(uint32_t frequency_hz, uint32_t ticks);

#endif
