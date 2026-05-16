#include <stdint.h>
#include <stddef.h>
#include "graphics.h"
#include "window.h"
#include "libc.h"
#include "memory.h"

typedef struct window {
    rect_t rect;
    char title[64];
    uint32_t bg_color;
    uint32_t title_bg_color;
    uint32_t title_fg_color;
    uint8_t focused;
    uint8_t minimized;
    struct window* prev;
    struct window* next;
} window_t;

static window_t* window_list_head = NULL;
static window_t* focused_window = NULL;

void window_manager_init(void) {
    window_list_head = NULL;
    focused_window = NULL;
}

window_t* window_create(uint32_t x, uint32_t y, uint32_t width, uint32_t height, const char* title) {
    window_t* win = (window_t*)kmalloc(sizeof(window_t));
    if (!win) return NULL;

    win->rect.x = x;
    win->rect.y = y;
    win->rect.width = width;
    win->rect.height = height;
    win->bg_color = graphics_rgb(200, 200, 200);
    win->title_bg_color = graphics_rgb(0, 0, 128);
    win->title_fg_color = graphics_rgb(255, 255, 255);
    win->focused = 0;
    win->prev = NULL;
    win->next = NULL;

    strcpy(win->title, title);

    return win;
}

void window_destroy(window_t* win) {
    if (!win) return;
    window_manager_remove(win);
    kfree(win);
}

void window_set_bg_color(window_t* win, uint32_t color) {
    if (!win) return;
    win->bg_color = color;
}

void window_set_title_color(window_t* win, uint32_t bg_color, uint32_t fg_color) {
    if (!win) return;
    win->title_bg_color = bg_color;
    win->title_fg_color = fg_color;
}

void window_draw_char(window_t* win, uint32_t x, uint32_t y, char c, uint32_t color) {
    if (!win) return;
    uint32_t screen_x = win->rect.x + WINDOW_BORDER + x;
    uint32_t screen_y = win->rect.y + TITLEBAR_HEIGHT + WINDOW_BORDER + y;
    graphics_draw_char(screen_x, screen_y, c, color, win->bg_color);
}

void window_draw_string(window_t* win, uint32_t x, uint32_t y, const char* str, uint32_t color) {
    if (!win) return;
    uint32_t offset_x = 0;
    while (*str != '\0') {
        window_draw_char(win, x + offset_x, y, *str, color);
        offset_x += 8;
        str++;
    }
}

void window_fill_rect(window_t* win, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (!win) return;
    uint32_t screen_x = win->rect.x + WINDOW_BORDER + x;
    uint32_t screen_y = win->rect.y + TITLEBAR_HEIGHT + WINDOW_BORDER + y;
    graphics_fill_rect(screen_x, screen_y, w, h, color);
}

void window_clear(window_t* win, uint32_t color) {
    if (!win) return;
    window_fill_rect(win, 0, 0, win->rect.width - 2 * WINDOW_BORDER, win->rect.height - TITLEBAR_HEIGHT - WINDOW_BORDER, color);
}

void window_raise(window_t* win) {
    if (!win || !window_list_head) return;
    if (win == window_list_head) return;

    if (win->next) win->next->prev = win->prev;
    if (win->prev) win->prev->next = win->next;

    window_list_head->prev = win;
    win->next = window_list_head;
    win->prev = NULL;
    window_list_head = win;
}

void window_set_focus(window_t* win) {
    if (focused_window) focused_window->focused = 0;
    if (win) {
        focused_window = win;
        win->focused = 1;
        window_raise(win);
    }
}

void window_manager_add(window_t* win) {
    if (!win) return;
    if (!window_list_head) {
        window_list_head = win;
        win->prev = NULL;
        win->next = NULL;
    } else {
        win->next = window_list_head;
        window_list_head->prev = win;
        win->prev = NULL;
        window_list_head = win;
    }
}

void window_manager_remove(window_t* win) {
    if (!win || !window_list_head) return;

    if (win == window_list_head) {
        window_list_head = win->next;
        if (window_list_head) window_list_head->prev = NULL;
    } else {
        if (win->prev) win->prev->next = win->next;
        if (win->next) win->next->prev = win->prev;
    }

    if (focused_window == win) {
        focused_window = window_list_head;
    }
}

static void window_render(window_t* win) {
    if (!win || win->minimized) return;

    uint32_t x = win->rect.x;
    uint32_t y = win->rect.y;
    uint32_t w = win->rect.width;
    uint32_t h = win->rect.height;

    graphics_fill_rect(x, y, w, h, win->bg_color);
    graphics_fill_rect(x + WINDOW_BORDER, y + WINDOW_BORDER,
                       w - 2 * WINDOW_BORDER, TITLEBAR_HEIGHT - WINDOW_BORDER,
                       win->title_bg_color);
    graphics_draw_string(x + WINDOW_BORDER + 4, y + WINDOW_BORDER + 4,
                         win->title, win->title_fg_color, win->title_bg_color);

    /* Close button — red X, top-right 16×16 of titlebar */
    graphics_fill_rect(x + w - 16, y, 16, TITLEBAR_HEIGHT, 4);
    graphics_draw_char(x + w - 12, y + 4, 'X', 15, 4);

    /* Minimize button — yellow -, just left of close */
    graphics_fill_rect(x + w - 32, y, 16, TITLEBAR_HEIGHT, 6);
    graphics_draw_char(x + w - 28, y + 4, '-', 0, 6);

    uint32_t border_color = win->focused ? graphics_rgb(255, 200, 0)
                                         : graphics_rgb(100, 100, 100);
    graphics_draw_rect(x, y, w, h, border_color);
}

/* ---- new accessors / operations ---- */

void window_set_pos(window_t* win, uint32_t x, uint32_t y) {
    if (!win) return;
    win->rect.x = x;
    win->rect.y = y;
}

void window_toggle_minimize(window_t* win) {
    if (win) win->minimized = !win->minimized;
}

uint8_t window_is_minimized(const window_t* win) {
    return win ? win->minimized : 0;
}

uint32_t    window_get_x(const window_t* w)      { return w ? w->rect.x     : 0; }
uint32_t    window_get_y(const window_t* w)      { return w ? w->rect.y     : 0; }
uint32_t    window_get_width(const window_t* w)  { return w ? w->rect.width : 0; }
uint32_t    window_get_height(const window_t* w) { return w ? w->rect.height: 0; }
const char* window_get_title(const window_t* w)  { return w ? w->title      : ""; }

window_t* window_manager_get_head(void)          { return window_list_head; }
window_t* window_get_next(const window_t* w)     { return w ? w->next : NULL; }

void window_manager_render_all(void) {
    /* Walk to the tail */
    window_t* tail = NULL;
    window_t* win = window_list_head;
    while (win != NULL) {
        tail = win;
        win = win->next;
    }

    /* Render tail-to-head so focused window (head) draws last = on top */
    win = tail;
    while (win != NULL) {
        window_render(win);
        win = win->prev;
    }
}
