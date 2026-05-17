#include "terminal.h"
#include "graphics.h"
#include "libc.h"

#define TERM_COLS     40   /* 320 / 8 pixels per char */
#define TERM_ROWS     25   /* 200 / 8 pixels per char */
#define CHAR_W        8
#define CHAR_H        8
#define BG_COLOR      0    /* black */
#define FG_COLOR      15   /* white */
#define PROMPT_COLOR  14   /* yellow */

static int cursor_x = 0;
static int cursor_y = 0;
static uint32_t current_color = 15;

void term_set_color(uint32_t color) {
    current_color = color;
}

static void term_scroll(void) {
    uint8_t* buf = graphics_get_framebuffer();
    for (int y = CHAR_H; y < TERM_ROWS * CHAR_H; y++) {
        for (int x = 0; x < TERM_COLS * CHAR_W; x++) {
            uint32_t src = (uint32_t)(y * 320 + x);
            uint32_t dst = (uint32_t)((y - CHAR_H) * 320 + x);
            buf[dst] = buf[src];
        }
    }
    for (int y = (TERM_ROWS - 1) * CHAR_H; y < TERM_ROWS * CHAR_H; y++) {
        for (int x = 0; x < TERM_COLS * CHAR_W; x++)
            buf[y * 320 + x] = BG_COLOR;
    }
    cursor_y = TERM_ROWS - 1;
}

void term_putc(char c) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
        if (cursor_y >= TERM_ROWS) term_scroll();
        return;
    }

    if (c == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
            graphics_fill_rect(
                (uint32_t)(cursor_x * CHAR_W),
                (uint32_t)(cursor_y * CHAR_H),
                CHAR_W, CHAR_H, BG_COLOR);
        }
        return;
    }

    graphics_draw_char(
        (uint32_t)(cursor_x * CHAR_W),
        (uint32_t)(cursor_y * CHAR_H),
        c, current_color, BG_COLOR);

    cursor_x++;
    if (cursor_x >= TERM_COLS) {
        cursor_x = 0;
        cursor_y++;
        if (cursor_y >= TERM_ROWS) term_scroll();
    }
}

void term_print(const char* s) {
    while (*s) term_putc(*s++);
}

void term_clear(void) {
    graphics_clear(BG_COLOR);
    cursor_x = 0;
    cursor_y = 0;
}