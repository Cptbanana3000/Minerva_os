#include "io.h"
#include "keyboard.h"
#include "mouse.h"
#include "interrupts.h"
#include "libc.h"
#include "memory.h"
#include "pmm.h"
#include "paging.h"
#include "serial.h"
#include "graphics.h"
#include "window.h"
#include "desktop.h"
#include "term_window.h"
#include "fs.h"

/* ------------------------------------------------------------------ */
/* Globals                                                              */
/* ------------------------------------------------------------------ */
static term_window_t *g_tw    = NULL;
static window_t      *g_about = NULL;

static void create_terminal(void) {
    if (g_tw) return;

    g_tw = term_window_create(50, 15);
    if (g_tw) window_manager_add(g_tw->win);
}

static void create_about(uint8_t minimized) {
    if (g_about) return;

    g_about = window_create(10, 10, 130, 76, "About");
    if (!g_about) return;

    window_set_bg_color(g_about, graphics_rgb(200, 200, 200));
    window_manager_add(g_about);
    if (minimized) window_toggle_minimize(g_about);
}

/* ------------------------------------------------------------------ */
/* Render callback — called by desktop_redraw() after window frames    */
/* ------------------------------------------------------------------ */
static void render_all(void) {
    if (g_tw) term_window_render(g_tw);

    /* About window content redrawn every frame (window_render erases bg) */
    if (g_about && !window_is_minimized(g_about)) {
        uint32_t blk = graphics_rgb(0, 0, 0);
        uint32_t grn = graphics_rgb(0, 200, 0);
        window_draw_string(g_about,  4,  4, "MinervaOS v0.3",   blk);
        window_draw_string(g_about,  4, 16, "Phase 3: Desktop", blk);
        window_draw_string(g_about,  4, 28, "x86 32-bit OS",    blk);
        window_draw_string(g_about,  4, 40, "320x200 VGA",      grn);
        window_draw_string(g_about,  4, 52, "IRQ kbd + mouse",  grn);
    }
}

/* ------------------------------------------------------------------ */
/* Icon callbacks                                                       */
/* ------------------------------------------------------------------ */
static void open_terminal(void) {
    if (!g_tw) create_terminal();
    if (!g_tw) return;
    if (window_is_minimized(g_tw->win))
        window_toggle_minimize(g_tw->win);
    window_set_focus(g_tw->win);
}

static void open_about(void) {
    if (!g_about) create_about(0);
    if (!g_about) return;
    if (window_is_minimized(g_about))
        window_toggle_minimize(g_about);
    window_set_focus(g_about);
}

static void window_closed(window_t *win) {
    if (g_tw && win == g_tw->win) {
        g_tw->win = NULL;
        g_tw = NULL;
    }

    if (g_about == win) {
        g_about = NULL;
    }
}

/* ------------------------------------------------------------------ */
/* Kernel entry                                                         */
/* ------------------------------------------------------------------ */
void kernel_main(void) {
    extern uint32_t __kernel_end;
    pmm_init((uint32_t)&__kernel_end);
    paging_init();
    heap_init();
    serial_init();
    serial_write("MinervaOS booting...\n");
    if (fs_init()) {
        serial_write("FAT32 filesystem mounted\n");
    } else {
        serial_write("No FAT32 filesystem\n");
    }

    graphics_init();

/* Draw a yellow pixel at column N, row 0, then flip — lets us see
   exactly how far boot reaches when QEMU serial isn't available. */
#define STEP(n) do { \
    graphics_put_pixel((n) * 4, 0, 14); \
    graphics_put_pixel((n) * 4, 1, 14); \
    graphics_put_pixel((n) * 4 + 1, 0, 14); \
    graphics_put_pixel((n) * 4 + 1, 1, 14); \
    graphics_flip(); \
} while (0)

    STEP(1);
    interrupts_init();
    STEP(2);
    pit_init(100);
    interrupts_enable();
    STEP(3);
    mouse_init();
    STEP(4);
    desktop_init();
    window_manager_init();
    STEP(5);

    /* Terminal starts closed; the desktop icon launches it. */
    STEP(6);

    /* About window — starts minimized, opened via icon */
    create_about(1);
    STEP(7);

    if (g_tw) window_set_focus(g_tw->win);

    /* Register render callback so window content is drawn each frame */
    desktop_set_render_cb(render_all);
    desktop_set_close_cb(window_closed);

    /* Desktop icons */
    desktop_add_icon(4,  8, "Term", 10, open_terminal);  /* bright green */
    desktop_add_icon(4, 40, "Info",  3, open_about);     /* cyan */
    STEP(8);

    desktop_redraw();
#undef STEP

    /* ---- Main event loop ---- */
    while (1) {
        if (desktop_process()) desktop_redraw();

        if (keyboard_has_key()) {
            char c = keyboard_read_key();
            /* Route keyboard to terminal window when it is focused */
            if (g_tw &&
                window_manager_get_head() == g_tw->win &&
                !window_is_minimized(g_tw->win)) {
                term_window_handle_key(g_tw, c);
                desktop_redraw();
            }
        }
    }
}
