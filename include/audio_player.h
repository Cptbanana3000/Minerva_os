#ifndef MINERVA_AUDIO_PLAYER_H
#define MINERVA_AUDIO_PLAYER_H

#include <stdint.h>
#include "window.h"

typedef struct {
    window_t *win;
    char filename[13];
    const char *status;
    uint32_t file_size;
    uint32_t sample_rate;
    uint32_t data_offset;
    uint32_t data_size;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint8_t has_file;
    uint8_t has_wav;
} audio_player_t;

audio_player_t *audio_player_create(uint32_t x, uint32_t y);
audio_player_t *audio_player_open_file(uint32_t x, uint32_t y, const char *filename);
audio_player_t *audio_player_active(void);
int audio_player_play_preview(audio_player_t *player);
void audio_player_render(audio_player_t *player);
void audio_player_window_closed(audio_player_t *player, window_t *win);

#endif
