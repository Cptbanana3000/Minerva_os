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
#include "text_editor.h"
#include "image_viewer.h"
#include "audio_player.h"
#include "browser.h"
#include "fs.h"
#include "scheduler.h"
#include "process.h"
#include "gdt.h"
#include "usermode.h"
#include "user_scheduler.h"
#include "e1000.h"
#include "net.h"

/* ------------------------------------------------------------------ */
/* Globals                                                              */
/* ------------------------------------------------------------------ */
static term_window_t *g_tw    = NULL;
static window_t      *g_about = NULL;
static uint32_t g_task_a_ticks = 0;
static uint32_t g_task_b_ticks = 0;

static void demo_task(void *ctx) {
    uint32_t *counter = (uint32_t*)ctx;
    (*counter)++;
}

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

static void create_editor(void) {
    text_editor_open_file(82, 20, "NOTE.TXT");
}

static void create_image_viewer(void) {
    image_viewer_open_file(96, 28, "IMAGE.BMP");
}

static void create_audio_player(void) {
    audio_player_open_file(92, 42, "AUDIO.WAV");
}

static void create_browser(void) {
    browser_open(72, 22);
}

/* ------------------------------------------------------------------ */
/* Render callback — called by desktop_redraw() after window frames    */
/* ------------------------------------------------------------------ */
static void render_all(void) {
    if (g_tw) term_window_render(g_tw);
    text_editor_t *editor = text_editor_active();
    if (editor) text_editor_render(editor);
    image_viewer_t *viewer = image_viewer_active();
    if (viewer) image_viewer_render(viewer);
    audio_player_t *player = audio_player_active();
    if (player) audio_player_render(player);
    browser_t *browser = browser_active();
    if (browser) browser_render(browser);

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

static void open_editor(void) {
    create_editor();
}

static void open_image_viewer(void) {
    create_image_viewer();
}

static void open_audio_player(void) {
    create_audio_player();
}

static void open_browser(void) {
    create_browser();
}

static void content_clicked(window_t *win, int32_t x, int32_t y) {
    browser_t *browser = browser_active();
    if (browser && win == browser->win && browser_handle_click(browser, x, y)) {
        desktop_redraw();
    }
}

static void desktop_main_loop(void) {
    while (1) {
        scheduler_poll();

        if (desktop_process()) desktop_redraw();

        if (keyboard_has_key()) {
            char c = keyboard_read_key();
            if (g_tw &&
                window_manager_get_head() == g_tw->win &&
                !window_is_minimized(g_tw->win)) {
                term_window_handle_key(g_tw, c);
                desktop_redraw();
            } else {
                text_editor_t *editor = text_editor_active();
                if (editor &&
                    window_manager_get_head() == editor->win &&
                    !window_is_minimized(editor->win)) {
                    text_editor_handle_key(editor, c);
                    desktop_redraw();
                } else {
                    browser_t *browser = browser_active();
                    if (browser &&
                        window_manager_get_head() == browser->win &&
                        !window_is_minimized(browser->win)) {
                        browser_handle_key(browser, c);
                        desktop_redraw();
                    }
                }
            }
        }
    }
}

static void window_closed(window_t *win) {
    if (g_tw && win == g_tw->win) {
        g_tw->win = NULL;
        g_tw = NULL;
    }

    text_editor_t *editor = text_editor_active();
    if (editor && win == editor->win) {
        text_editor_window_closed(editor, win);
    }

    image_viewer_t *viewer = image_viewer_active();
    if (viewer && win == viewer->win) {
        image_viewer_window_closed(viewer, win);
    }

    audio_player_t *player = audio_player_active();
    if (player && win == player->win) {
        audio_player_window_closed(player, win);
    }

    browser_t *browser = browser_active();
    if (browser && win == browser->win) {
        browser_window_closed(browser, win);
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
    process_init();
    scheduler_init();
    gdt_init();
    usermode_init();
    user_scheduler_init();
    net_init();
    int task_a = scheduler_create_kernel_task("task-a", demo_task, &g_task_a_ticks);
    if (task_a >= 0) process_create_kernel("task-a", (uint32_t)task_a, 0);
    int task_b = scheduler_create_kernel_task("task-b", demo_task, &g_task_b_ticks);
    if (task_b >= 0) process_create_kernel("task-b", (uint32_t)task_b, 0);
    int user_sched = scheduler_create_kernel_task("user-sched", user_scheduler_task, 0);
    if (user_sched >= 0) process_create_kernel("user-sched", (uint32_t)user_sched, 0);
    serial_write("MinervaOS booting...\n");
    e1000_init();
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
    desktop_set_content_click_cb(content_clicked);

    /* Desktop icons */
    desktop_add_icon(4,  8, "Term", 10, open_terminal);  /* bright green */
    desktop_add_icon(4, 40, "Info",  3, open_about);     /* cyan */
    desktop_add_icon(4, 72, "Edit", 14, open_editor);    /* yellow */
    desktop_add_icon(4, 104, "View", 13, open_image_viewer); /* magenta */
    desktop_add_icon(4, 136, "Aud",  12, open_audio_player); /* red */
    desktop_add_icon(40, 8, "Web",  11, open_browser);   /* light cyan */
    STEP(8);

    desktop_redraw();
#undef STEP

    int desktop_task = scheduler_register_main_task("desktop");
    if (desktop_task >= 0) process_create_kernel("desktop", (uint32_t)desktop_task, 0);

    /* Switch to a dedicated kernel stack for the desktop event loop and
       jump into it. Never returns — boot stack is abandoned past this point. */
    scheduler_run_on_main_stack(desktop_main_loop, scheduler_main_stack_top());
}
