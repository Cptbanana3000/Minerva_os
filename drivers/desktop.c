#include <stdint.h>
#include "desktop.h"
#include "graphics.h"
#include "window.h"
#include "mouse.h"

#define TASKBAR_Y    184
#define TASKBAR_H     16
#define BTN_W         16   /* close / minimize button width == TITLEBAR_HEIGHT */
#define TASK_BTN_W    58   /* taskbar window-button width */
#define TASK_BTN_GAP   2

/* ------------------------------------------------------------------ */
/* Internal state                                                       */
/* ------------------------------------------------------------------ */
static window_t* drag_win    = NULL;
static int32_t   drag_off_x  = 0;
static int32_t   drag_off_y  = 0;
static uint8_t   prev_btns   = 0;

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */
static int in_rect(int32_t mx, int32_t my,
                   int32_t rx, int32_t ry, int32_t rw, int32_t rh) {
    return mx >= rx && mx < rx + rw && my >= ry && my < ry + rh;
}

/* ------------------------------------------------------------------ */
/* Taskbar click                                                        */
/* ------------------------------------------------------------------ */
static void taskbar_click(int32_t mx) {
    int32_t bx = TASK_BTN_GAP;
    window_t* w = window_manager_get_head();
    while (w) {
        if (mx >= bx && mx < bx + TASK_BTN_W) {
            if (window_is_minimized(w)) window_toggle_minimize(w);
            window_set_focus(w);
            return;
        }
        bx += TASK_BTN_W + TASK_BTN_GAP;
        w = window_get_next(w);
    }
}

/* ------------------------------------------------------------------ */
/* Window click / drag start                                            */
/* ------------------------------------------------------------------ */
static int window_click(int32_t mx, int32_t my) {
    window_t* w = window_manager_get_head();
    while (w) {
        if (window_is_minimized(w)) { w = window_get_next(w); continue; }

        int32_t wx = (int32_t)window_get_x(w);
        int32_t wy = (int32_t)window_get_y(w);
        int32_t ww = (int32_t)window_get_width(w);
        int32_t wh = (int32_t)window_get_height(w);

        if (!in_rect(mx, my, wx, wy, ww, wh)) { w = window_get_next(w); continue; }

        window_set_focus(w);

        /* Close button */
        if (in_rect(mx, my, wx + ww - BTN_W, wy, BTN_W, TITLEBAR_HEIGHT)) {
            if (drag_win == w) drag_win = NULL;
            window_destroy(w);
            return 1;
        }

        /* Minimize button */
        if (in_rect(mx, my, wx + ww - BTN_W * 2, wy, BTN_W, TITLEBAR_HEIGHT)) {
            window_toggle_minimize(w);
            return 1;
        }

        /* Titlebar drag */
        if (my < wy + TITLEBAR_HEIGHT) {
            drag_win   = w;
            drag_off_x = mx - wx;
            drag_off_y = my - wy;
        }

        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */
void desktop_init(void) {
    drag_win  = NULL;
    prev_btns = 0;
}

int desktop_process(void) {
    uint8_t  btns     = mouse_get_buttons();
    int32_t  mx       = mouse_get_x();
    int32_t  my       = mouse_get_y();
    uint8_t  pressed  = btns & ~prev_btns;
    uint8_t  released = (uint8_t)(~btns) & prev_btns;
    prev_btns = btns;

    int dirty = 0;

    /* Drag in progress */
    if ((btns & 0x01) && drag_win) {
        int32_t nx = mx - drag_off_x;
        int32_t ny = my - drag_off_y;
        int32_t max_x = 319 - (int32_t)window_get_width(drag_win);
        int32_t max_y = TASKBAR_Y - (int32_t)window_get_height(drag_win);
        if (nx < 0) nx = 0;
        if (ny < 0) ny = 0;
        if (nx > max_x) nx = max_x;
        if (ny > max_y) ny = max_y;
        window_set_pos(drag_win, (uint32_t)nx, (uint32_t)ny);
        dirty = 1;
    }

    if (released & 0x01) drag_win = NULL;

    if (pressed & 0x01) {
        if (my >= TASKBAR_Y) { taskbar_click(mx); dirty = 1; }
        else                  dirty |= window_click(mx, my);
    }

    return dirty;
}

void desktop_redraw(void) {
    /* Wallpaper */
    graphics_fill_rect(0, 0, 320, TASKBAR_Y, 1);   /* dark blue */

    /* Windows */
    window_manager_render_all();

    /* Taskbar */
    graphics_fill_rect(0, TASKBAR_Y, 320, TASKBAR_H, 7);  /* light gray */
    graphics_draw_line(0, TASKBAR_Y, 319, TASKBAR_Y, 0);  /* top border */

    int32_t bx = TASK_BTN_GAP;
    window_t* w = window_manager_get_head();
    while (w && bx + TASK_BTN_W < 320) {
        uint32_t bg = window_is_minimized(w) ? 0 : 15;
        uint32_t fg = window_is_minimized(w) ? 7 : 0;
        graphics_fill_rect((uint32_t)bx, TASKBAR_Y + 2, TASK_BTN_W, TASKBAR_H - 4, bg);
        graphics_draw_rect((uint32_t)bx, TASKBAR_Y + 2, TASK_BTN_W, TASKBAR_H - 4, 0);

        const char* title = window_get_title(w);
        for (int ci = 0; ci < 6 && title[ci]; ci++)
            graphics_draw_char((uint32_t)(bx + 3 + ci * 8),
                               (uint32_t)(TASKBAR_Y + 4),
                               title[ci], fg, bg);

        bx += TASK_BTN_W + TASK_BTN_GAP;
        w = window_get_next(w);
    }

    mouse_draw_cursor();   /* re-apply cursor on top, then flips */
}
