#include <stdint.h>
#include "term_window.h"
#include "window.h"
#include "graphics.h"
#include "libc.h"
#include "serial.h"
#include "pmm.h"
#include "io.h"
#include "fs.h"
#include "scheduler.h"

static term_window_t g_tw;
static uint8_t file_buffer[512];

static void tw_scroll(term_window_t *t) {
    for (int r = 0; r < TWIN_ROWS - 1; r++)
        memcpy(t->buf[r], t->buf[r + 1], TWIN_COLS);
    memset(t->buf[TWIN_ROWS - 1], 0, TWIN_COLS);
    t->cur_row = TWIN_ROWS - 1;
    t->cur_col = 0;
}

void term_window_putc(term_window_t *t, char c) {
    if (c == '\n') {
        t->cur_col = 0;
        t->cur_row++;
        if (t->cur_row >= TWIN_ROWS) tw_scroll(t);
    } else if (c == '\b') {
        if (t->cur_col > 0) {
            t->cur_col--;
            t->buf[t->cur_row][t->cur_col] = 0;
        }
    } else {
        if (t->cur_col >= TWIN_COLS) {
            t->cur_col = 0;
            t->cur_row++;
            if (t->cur_row >= TWIN_ROWS) tw_scroll(t);
        }
        t->buf[t->cur_row][t->cur_col++] = c;
    }
}

void term_window_print(term_window_t *t, const char *s) {
    while (*s) term_window_putc(t, *s++);
}

static void tw_print_num(term_window_t *t, uint32_t n) {
    char tmp[12];
    int i = 11;
    tmp[i] = 0;
    if (n == 0) { term_window_putc(t, '0'); return; }
    while (n > 0) { tmp[--i] = '0' + (char)(n % 10); n /= 10; }
    term_window_print(t, tmp + i);
}

static int starts_with(const char *s, const char *prefix) {
    while (*prefix) {
        if (*s++ != *prefix++) return 0;
    }
    return 1;
}

static void tw_ls_entry(const char *name, uint32_t size, void *ctx) {
    term_window_t *t = (term_window_t*)ctx;
    term_window_print(t, name);
    term_window_putc(t, ' ');
    tw_print_num(t, size);
    term_window_putc(t, '\n');
}

static void tw_task_entry(uint32_t id, const char *name, uint32_t runs, void *ctx) {
    term_window_t *t = (term_window_t*)ctx;
    tw_print_num(t, id);
    term_window_putc(t, ' ');
    term_window_print(t, name);
    term_window_putc(t, ' ');
    tw_print_num(t, runs);
    term_window_putc(t, '\n');
}

static void tw_exec(term_window_t *t) {
    const char *cmd = t->input;
    serial_write(cmd);
    serial_write("\n");

    if (strcmp(cmd, "help") == 0) {
        term_window_print(t, "help clear about\n");
        term_window_print(t, "echo meminfo tasks\n");
        term_window_print(t, "ls cat touch write\n");
        term_window_print(t, "append truncate delete\n");
        term_window_print(t, "rename preempt reboot\n");
    } else if (strcmp(cmd, "clear") == 0) {
        memset(t->buf, 0, sizeof(t->buf));
        t->cur_col = 0;
        t->cur_row = 0;
    } else if (strcmp(cmd, "about") == 0) {
        term_window_print(t, "MinervaOS v0.3\n");
        term_window_print(t, "Phase 3: Desktop\n");
        term_window_print(t, "x86 32-bit, 320x200\n");
    } else if (strcmp(cmd, "echo") == 0) {
        term_window_print(t, "Hello, MinervaOS!\n");
    } else if (strcmp(cmd, "meminfo") == 0) {
        term_window_print(t, "Total:");
        tw_print_num(t, pmm_total_pages() * 4);
        term_window_print(t, "K\n");
        term_window_print(t, "Used:");
        tw_print_num(t, pmm_used_pages() * 4);
        term_window_print(t, "K\n");
        term_window_print(t, "Free:");
        tw_print_num(t, pmm_free_pages() * 4);
        term_window_print(t, "K\n");
    } else if (strcmp(cmd, "tasks") == 0) {
        term_window_print(t, "Switches:");
        tw_print_num(t, scheduler_switch_count());
        term_window_putc(t, '\n');
        term_window_print(t, "TimerReq:");
        tw_print_num(t, scheduler_timer_request_count());
        term_window_putc(t, '\n');
        term_window_print(t, "IRQFrames:");
        tw_print_num(t, scheduler_irq_frame_count());
        term_window_putc(t, '\n');
        term_window_print(t, "IRQCtx:");
        tw_print_num(t, scheduler_irq_context_count());
        term_window_putc(t, '\n');
        term_window_print(t, "Cand:");
        tw_print_num(t, scheduler_irq_candidate_count());
        term_window_putc(t, '\n');
        term_window_print(t, "PMode:");
        tw_print_num(t, scheduler_preemptive_enabled());
        term_window_putc(t, '\n');
        term_window_print(t, "IRQSw:");
        tw_print_num(t, scheduler_irq_preempt_switch_count());
        term_window_putc(t, '\n');
        term_window_print(t, "Block:");
        tw_print_num(t, scheduler_irq_preempt_blocked_count());
        term_window_putc(t, '\n');
        scheduler_list(tw_task_entry, t);
    } else if (strcmp(cmd, "preempt") == 0) {
        term_window_print(t, "PMode:");
        tw_print_num(t, scheduler_preemptive_enabled());
        term_window_putc(t, '\n');
    } else if (strcmp(cmd, "preempt on") == 0) {
        scheduler_set_preemptive_enabled(1);
        term_window_print(t, "PMode:1\n");
    } else if (strcmp(cmd, "preempt off") == 0) {
        scheduler_set_preemptive_enabled(0);
        term_window_print(t, "PMode:0\n");
    } else if (strcmp(cmd, "ls") == 0) {
        if (!fs_is_ready()) {
            term_window_print(t, "No filesystem\n");
        } else if (!fs_list_root(tw_ls_entry, t)) {
            term_window_print(t, "ls failed\n");
        }
    } else if (starts_with(cmd, "cat ")) {
        const char *name = cmd + 4;
        fs_file_t file;
        uint32_t size = 0;
        uint32_t total = 0;
        char last = 0;
        if (!fs_is_ready()) {
            term_window_print(t, "No filesystem\n");
        } else if (!fs_open(name, &file)) {
            term_window_print(t, "cat failed\n");
        } else {
            size = fs_file_size(&file);
            while (total < size) {
                uint32_t got = fs_read(&file, file_buffer, sizeof(file_buffer) - 1);
                if (got == 0) break;
                file_buffer[got] = 0;
                last = (char)file_buffer[got - 1];
                total += got;
                term_window_print(t, (const char*)file_buffer);
            }
            fs_close(&file);
            if (total != size) {
                term_window_print(t, "cat failed\n");
            } else if (size == 0 || last != '\n') {
                term_window_putc(t, '\n');
            }
        }
    } else if (starts_with(cmd, "touch ")) {
        const char *name = cmd + 6;
        if (!fs_is_ready()) {
            term_window_print(t, "No filesystem\n");
        } else if (!fs_create(name)) {
            term_window_print(t, "touch failed\n");
        }
    } else if (starts_with(cmd, "write ")) {
        char *name = t->input + 6;
        char *text = name;
        while (*text && *text != ' ') text++;
        if (*text == ' ') {
            *text++ = 0;
        }

        if (!fs_is_ready()) {
            term_window_print(t, "No filesystem\n");
        } else if (!*name || !*text ||
                   !fs_write(name, (const uint8_t*)text, strlen(text),
                             FS_WRITE_CREATE | FS_WRITE_TRUNCATE)) {
            term_window_print(t, "write failed\n");
        }
    } else if (starts_with(cmd, "append ")) {
        char *name = t->input + 7;
        char *text = name;
        while (*text && *text != ' ') text++;
        if (*text == ' ') {
            *text++ = 0;
        }

        if (!fs_is_ready()) {
            term_window_print(t, "No filesystem\n");
        } else if (!*name || !*text ||
                   !fs_write(name, (const uint8_t*)text, strlen(text), FS_WRITE_APPEND)) {
            term_window_print(t, "append failed\n");
        }
    } else if (starts_with(cmd, "truncate ")) {
        const char *name = cmd + 9;
        if (!fs_is_ready()) {
            term_window_print(t, "No filesystem\n");
        } else if (!fs_truncate_file(name)) {
            term_window_print(t, "truncate failed\n");
        }
    } else if (starts_with(cmd, "delete ")) {
        const char *name = cmd + 7;
        if (!fs_is_ready()) {
            term_window_print(t, "No filesystem\n");
        } else if (!fs_delete_file(name)) {
            term_window_print(t, "delete failed\n");
        }
    } else if (starts_with(cmd, "rename ")) {
        char *old_name = t->input + 7;
        char *new_name = old_name;
        while (*new_name && *new_name != ' ') new_name++;
        if (*new_name == ' ') {
            *new_name++ = 0;
        }

        if (!fs_is_ready()) {
            term_window_print(t, "No filesystem\n");
        } else if (!*old_name || !*new_name || !fs_rename_file(old_name, new_name)) {
            term_window_print(t, "rename failed\n");
        }
    } else if (strcmp(cmd, "reboot") == 0) {
        while (inb(0x64) & 0x02) {}
        outb(0x64, 0xFE);
        __asm__ volatile("cli; hlt");
    } else if (cmd[0] != 0) {
        term_window_print(t, "Unknown: ");
        term_window_print(t, cmd);
        term_window_putc(t, '\n');
    }
}

void term_window_handle_key(term_window_t *t, char c) {
    if (c == '\n') {
        t->input[t->input_len] = 0;
        term_window_putc(t, '\n');
        tw_exec(t);
        t->input_len = 0;
        term_window_print(t, "$ ");
    } else if (c == '\b') {
        if (t->input_len > 0) {
            t->input_len--;
            term_window_putc(t, '\b');
        }
    } else if (t->input_len < (int)sizeof(t->input) - 1) {
        t->input[t->input_len++] = c;
        term_window_putc(t, c);
    }
}

void term_window_render(term_window_t *t) {
    if (!t || !t->win || window_is_minimized(t->win)) return;

    /* Black background — window_render() already filled it, but clear
       the client area so old chars don't ghost when the cursor moves back */
    window_clear(t->win, 0);

    for (int r = 0; r < TWIN_ROWS; r++) {
        for (int c = 0; c < TWIN_COLS; c++) {
            char ch = t->buf[r][c];
            if (ch >= 32)
                window_draw_char(t->win,
                                 (uint32_t)(c * 8), (uint32_t)(r * 8),
                                 ch, 10);  /* bright green on black */
        }
    }
    /* Solid block cursor */
    window_fill_rect(t->win,
                     (uint32_t)(t->cur_col * 8), (uint32_t)(t->cur_row * 8),
                     8, 8, 10);
}

term_window_t *term_window_create(uint32_t x, uint32_t y) {
    term_window_t *t = &g_tw;
    /* 200 wide, 100 tall: client = 198 x 82 → 24 cols x 10 rows */
    t->win = window_create(x, y, 200, 100, "Terminal");
    if (!t->win) return NULL;

    window_set_bg_color(t->win, 0);       /* black interior */
    window_set_title_color(t->win, 8, 10); /* dark-gray bar, green label */

    memset(t->buf, 0, sizeof(t->buf));
    t->cur_col   = 0;
    t->cur_row   = 0;
    t->input_len = 0;

    term_window_print(t, "MinervaOS Terminal\n");
    term_window_print(t, "Type 'help'\n");
    term_window_print(t, "$ ");
    return t;
}
