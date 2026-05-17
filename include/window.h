#ifndef MINERVA_WINDOW_H
#define MINERVA_WINDOW_H

#include <stdint.h>

#define TITLEBAR_HEIGHT 16
#define WINDOW_BORDER   1

typedef struct window window_t;

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} rect_t;

/* create / destroy */
window_t*   window_create(uint32_t x, uint32_t y, uint32_t width, uint32_t height, const char* title);
void        window_destroy(window_t* win);

/* style */
void        window_set_bg_color(window_t* win, uint32_t color);
void        window_set_title_color(window_t* win, uint32_t bg_color, uint32_t fg_color);

/* drawing inside window */
void        window_draw_char(window_t* win, uint32_t x, uint32_t y, char c, uint32_t color);
void        window_draw_string(window_t* win, uint32_t x, uint32_t y, const char* str, uint32_t color);
void        window_fill_rect(window_t* win, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void        window_clear(window_t* win, uint32_t color);

/* focus / z-order */
void        window_raise(window_t* win);
void        window_set_focus(window_t* win);

/* position */
void        window_set_pos(window_t* win, uint32_t x, uint32_t y);

/* minimize */
void        window_toggle_minimize(window_t* win);
uint8_t     window_is_minimized(const window_t* win);

/* accessors (for desktop hit-testing) */
uint32_t    window_get_x(const window_t* win);
uint32_t    window_get_y(const window_t* win);
uint32_t    window_get_width(const window_t* win);
uint32_t    window_get_height(const window_t* win);
const char* window_get_title(const window_t* win);

/* window list iteration */
window_t*   window_manager_get_head(void);
window_t*   window_get_next(const window_t* win);

/* manager */
void        window_manager_init(void);
void        window_manager_render_all(void);
void        window_manager_add(window_t* win);
void        window_manager_remove(window_t* win);

#endif
