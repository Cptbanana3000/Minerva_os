#include <stdint.h>
#include "audio_player.h"
#include "window.h"
#include "graphics.h"
#include "libc.h"
#include "fs.h"
#include "speaker.h"

static audio_player_t g_player;
static uint8_t audio_file_buffer[1024];

static uint16_t rd16(const uint8_t *buffer, uint32_t offset) {
    return (uint16_t)buffer[offset] | ((uint16_t)buffer[offset + 1] << 8);
}

static uint32_t rd32(const uint8_t *buffer, uint32_t offset) {
    return (uint32_t)buffer[offset] |
           ((uint32_t)buffer[offset + 1] << 8) |
           ((uint32_t)buffer[offset + 2] << 16) |
           ((uint32_t)buffer[offset + 3] << 24);
}

static int tag_eq(const uint8_t *buffer, uint32_t offset, const char *tag) {
    return buffer[offset] == (uint8_t)tag[0] &&
           buffer[offset + 1] == (uint8_t)tag[1] &&
           buffer[offset + 2] == (uint8_t)tag[2] &&
           buffer[offset + 3] == (uint8_t)tag[3];
}

static void player_set_filename(audio_player_t *player, const char *filename) {
    uint32_t i = 0;
    const char *fallback = "AUDIO.WAV";
    const char *src = (filename && filename[0]) ? filename : fallback;

    while (src[i] && i < sizeof(player->filename) - 1) {
        char c = src[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        player->filename[i] = c;
        i++;
    }
    player->filename[i] = 0;
}

static void player_clear_metadata(audio_player_t *player) {
    player->file_size = 0;
    player->sample_rate = 0;
    player->data_offset = 0;
    player->data_size = 0;
    player->channels = 0;
    player->bits_per_sample = 0;
    player->has_file = 0;
    player->has_wav = 0;
}

static void player_probe_file(audio_player_t *player) {
    player_clear_metadata(player);

    if (!fs_is_ready()) {
        player->status = "NO FS";
        return;
    }

    fs_file_t file;
    if (!fs_open(player->filename, &file)) {
        player->status = "MISSING";
        return;
    }

    player->file_size = fs_file_size(&file);
    player->has_file = 1;
    uint32_t read = fs_read(&file, audio_file_buffer, sizeof(audio_file_buffer));
    fs_close(&file);

    if (read < player->file_size || read < 44) {
        player->status = read < 44 ? "TOO SMALL" : "TOO BIG";
        return;
    }

    if (!tag_eq(audio_file_buffer, 0, "RIFF") ||
        !tag_eq(audio_file_buffer, 8, "WAVE")) {
        player->status = "NOT WAV";
        return;
    }

    uint32_t offset = 12;
    uint16_t audio_format = 0;
    uint8_t saw_fmt = 0;
    uint8_t saw_data = 0;

    while (offset + 8 <= read) {
        uint32_t chunk_size = rd32(audio_file_buffer, offset + 4);
        uint32_t chunk_data = offset + 8;
        uint32_t next = chunk_data + chunk_size + (chunk_size & 1u);
        if (chunk_data + chunk_size > read || next <= offset) break;

        if (tag_eq(audio_file_buffer, offset, "fmt ")) {
            if (chunk_size < 16) {
                player->status = "BAD FMT";
                return;
            }
            audio_format = rd16(audio_file_buffer, chunk_data + 0);
            player->channels = rd16(audio_file_buffer, chunk_data + 2);
            player->sample_rate = rd32(audio_file_buffer, chunk_data + 4);
            player->bits_per_sample = rd16(audio_file_buffer, chunk_data + 14);
            saw_fmt = 1;
        } else if (tag_eq(audio_file_buffer, offset, "data")) {
            player->data_offset = chunk_data;
            player->data_size = chunk_size;
            saw_data = 1;
        }

        offset = next;
    }

    if (!saw_fmt || !saw_data) {
        player->status = "BAD WAV";
        return;
    }

    if (audio_format != 1) {
        player->status = "UNSUP";
        return;
    }

    player->has_wav = 1;
    player->status = "PCM";
}

static void player_print_num(window_t *win, uint32_t x, uint32_t y,
                             uint32_t n, uint32_t color) {
    char tmp[12];
    int i = 11;
    tmp[i] = 0;
    if (n == 0) {
        window_draw_char(win, x, y, '0', color);
        return;
    }

    while (n > 0 && i > 0) {
        tmp[--i] = (char)('0' + (n % 10));
        n /= 10;
    }
    window_draw_string(win, x, y, tmp + i, color);
}

audio_player_t *audio_player_create(uint32_t x, uint32_t y) {
    audio_player_t *player = &g_player;
    if (player->win) return player;

    player->win = window_create(x, y, 214, 118, "Audio");
    if (!player->win) return 0;

    window_set_bg_color(player->win, graphics_rgb(24, 26, 30));
    window_set_title_color(player->win,
                           graphics_rgb(72, 48, 86),
                           graphics_rgb(255, 255, 255));

    player_set_filename(player, "AUDIO.WAV");
    player_probe_file(player);
    return player;
}

audio_player_t *audio_player_open_file(uint32_t x, uint32_t y, const char *filename) {
    audio_player_t *player = &g_player;
    uint8_t had_window = player->win ? 1 : 0;

    if (!player->win) {
        player = audio_player_create(x, y);
        if (!player) return 0;
    }

    player_set_filename(player, filename);
    player_probe_file(player);

    if (!had_window) window_manager_add(player->win);
    if (window_is_minimized(player->win)) window_toggle_minimize(player->win);
    window_set_focus(player->win);
    return player;
}

audio_player_t *audio_player_active(void) {
    return g_player.win ? &g_player : 0;
}

int audio_player_play_preview(audio_player_t *player) {
    if (!player || !player->has_wav) return 0;
    if (player->channels != 1 || player->bits_per_sample != 8) {
        player->status = "NO PLAY";
        return 0;
    }

    uint32_t playable = player->data_size;
    if (player->data_offset + playable > sizeof(audio_file_buffer)) {
        playable = sizeof(audio_file_buffer) - player->data_offset;
    }
    if (playable == 0) {
        player->status = "NO DATA";
        return 0;
    }

    uint32_t steps = playable < 48 ? playable : 48;
    uint32_t stride = playable / steps;
    if (stride == 0) stride = 1;

    for (uint32_t i = 0; i < steps; i++) {
        uint8_t sample = audio_file_buffer[player->data_offset + i * stride];
        uint32_t frequency = 180 + ((uint32_t)sample * 5);
        speaker_tone_ticks(frequency, 1);
    }

    speaker_off();
    player->status = "PLAYED";
    return 1;
}

void audio_player_render(audio_player_t *player) {
    if (!player || !player->win || window_is_minimized(player->win)) return;

    uint32_t bg = graphics_rgb(24, 26, 30);
    uint32_t panel = graphics_rgb(235, 236, 238);
    uint32_t ink = graphics_rgb(18, 22, 26);
    uint32_t muted = graphics_rgb(174, 178, 186);
    uint32_t accent = player->has_wav ? graphics_rgb(90, 170, 210)
                                      : graphics_rgb(190, 80, 90);

    window_clear(player->win, bg);
    window_fill_rect(player->win, 8, 8, 196, 36, panel);

    for (uint32_t x = 0; x < 176; x += 8) {
        uint32_t h = 6 + ((x * 5) % 22);
        window_fill_rect(player->win, 18 + x, 36 - h, 4, h, accent);
    }

    window_draw_string(player->win, 12, 52, player->filename, muted);
    window_draw_string(player->win, 112, 52, player->status ? player->status : "", muted);

    if (player->has_wav) {
        window_draw_string(player->win, 12, 68, "rate", muted);
        player_print_num(player->win, 60, 68, player->sample_rate, muted);
        window_draw_string(player->win, 12, 82, "ch", muted);
        player_print_num(player->win, 60, 82, player->channels, muted);
        window_draw_string(player->win, 92, 82, "bits", muted);
        player_print_num(player->win, 140, 82, player->bits_per_sample, muted);
        window_draw_string(player->win, 12, 96, "data", muted);
        player_print_num(player->win, 60, 96, player->data_size, muted);
    } else if (player->has_file) {
        player_print_num(player->win, 12, 68, player->file_size, muted);
        window_draw_string(player->win, 72, 68, "bytes", muted);
    } else {
        window_draw_string(player->win, 52, 22, "WAV soon", ink);
    }
}

void audio_player_window_closed(audio_player_t *player, window_t *win) {
    if (player && player->win == win) {
        player->win = 0;
    }
}
