#ifndef MINERVA_WINDOW_H
#define MINERVA_WINDOW_H

#include <stdint.h>

typedef struct window window_t;

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} rect_t;

window_t* window_create(uint32_t x, uint32_t y, uint32_t width, uint32_t height, const char* title);
void window_destroy(window_t* win);

void window_set_bg_color(window_t* win, uint32_t color);
void window_set_title_color(window_t* win, uint32_t bg_color, uint32_t fg_color);

void window_draw_char(window_t* win, uint32_t x, uint32_t y, char c, uint32_t color);
void window_draw_string(window_t* win, uint32_t x, uint32_t y, const char* str, uint32_t color);
void window_fill_rect(window_t* win, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void window_clear(window_t* win, uint32_t color);

void window_raise(window_t* win);
void window_set_focus(window_t* win);

void window_manager_init(void);
void window_manager_render_all(void);
void window_manager_add(window_t* win);
void window_manager_remove(window_t* win);

#endif
