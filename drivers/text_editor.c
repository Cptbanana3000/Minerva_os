#include <stdint.h>
#include "text_editor.h"
#include "window.h"
#include "graphics.h"
#include "libc.h"
#include "fs.h"

static text_editor_t g_editor;
static uint8_t editor_file_buffer[512];

static void editor_set_filename(text_editor_t *editor, const char *filename) {
    uint32_t i = 0;
    const char *fallback = "NOTE.TXT";
    const char *src = (filename && filename[0]) ? filename : fallback;

    while (src[i] && i < sizeof(editor->filename) - 1) {
        char c = src[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        editor->filename[i] = c;
        i++;
    }
    editor->filename[i] = 0;
}

static void editor_clear_buffer(text_editor_t *editor) {
    memset(editor->lines, 0, sizeof(editor->lines));
    editor->cursor_col = 0;
    editor->cursor_row = 0;
    editor->truncated = 0;
}

static void editor_load_bytes(text_editor_t *editor,
                              const uint8_t *buffer,
                              uint32_t size) {
    editor_clear_buffer(editor);

    for (uint32_t i = 0; i < size; i++) {
        char c = (char)buffer[i];
        if (c == '\r') continue;
        if (c == '\n') {
            if (editor->cursor_row + 1 >= EDITOR_ROWS) {
                editor->truncated = 1;
                break;
            }
            editor->cursor_row++;
            editor->cursor_col = 0;
            continue;
        }
        if (c < 32 || c >= 127) continue;
        if (editor->cursor_col >= EDITOR_COLS - 1) {
            editor->truncated = 1;
            continue;
        }

        editor->lines[editor->cursor_row][editor->cursor_col] = c;
        editor->cursor_col++;
    }
}

static void editor_load_file(text_editor_t *editor) {
    if (!fs_is_ready()) {
        editor_clear_buffer(editor);
        editor->status = "NO FS";
        editor->dirty = 0;
        return;
    }

    fs_file_t file;
    if (!fs_open(editor->filename, &file)) {
        editor_clear_buffer(editor);
        editor->status = "NEW";
        editor->dirty = 0;
        return;
    }

    uint32_t size = fs_file_size(&file);
    uint32_t read = fs_read(&file, editor_file_buffer, sizeof(editor_file_buffer));
    fs_close(&file);

    editor_load_bytes(editor, editor_file_buffer, read);
    editor->dirty = 0;
    if (read < size || editor->truncated) {
        editor->status = "TRUNC";
    } else {
        editor->status = "LOADED";
    }
}

static uint32_t editor_build_save_buffer(text_editor_t *editor) {
    uint32_t last_row = 0;
    for (uint32_t row = 0; row < EDITOR_ROWS; row++) {
        for (uint32_t col = 0; col < EDITOR_COLS - 1; col++) {
            if (editor->lines[row][col]) last_row = row;
        }
    }
    if (editor->cursor_row > last_row) last_row = editor->cursor_row;

    uint32_t out = 0;
    for (uint32_t row = 0; row <= last_row && out < sizeof(editor_file_buffer); row++) {
        uint32_t line_end = 0;
        for (uint32_t col = 0; col < EDITOR_COLS - 1; col++) {
            if (editor->lines[row][col]) line_end = col + 1;
        }

        for (uint32_t col = 0; col < line_end && out < sizeof(editor_file_buffer); col++) {
            editor_file_buffer[out++] = (uint8_t)editor->lines[row][col];
        }
        if (row < last_row && out < sizeof(editor_file_buffer)) {
            editor_file_buffer[out++] = '\n';
        }
    }

    return out;
}

static void editor_save_file(text_editor_t *editor) {
    if (!fs_is_ready()) {
        editor->status = "NO FS";
        return;
    }

    uint32_t size = editor_build_save_buffer(editor);
    if (fs_write(editor->filename,
                 editor_file_buffer,
                 size,
                 FS_WRITE_CREATE | FS_WRITE_TRUNCATE)) {
        editor->dirty = 0;
        editor->status = "SAVED";
    } else {
        editor->status = "SAVE ERR";
    }
}

static void editor_insert_char(text_editor_t *editor, char c) {
    if (editor->cursor_row >= EDITOR_ROWS) return;
    if (editor->cursor_col >= EDITOR_COLS - 1) return;

    editor->lines[editor->cursor_row][editor->cursor_col] = c;
    editor->cursor_col++;
    editor->dirty = 1;
    editor->status = "EDIT";
}

static void editor_newline(text_editor_t *editor) {
    if (editor->cursor_row + 1 >= EDITOR_ROWS) return;

    editor->cursor_row++;
    editor->cursor_col = 0;
    editor->dirty = 1;
    editor->status = "EDIT";
}

static void editor_backspace(text_editor_t *editor) {
    if (editor->cursor_col > 0) {
        editor->cursor_col--;
        editor->lines[editor->cursor_row][editor->cursor_col] = 0;
        editor->dirty = 1;
        editor->status = "EDIT";
        return;
    }

    if (editor->cursor_row > 0) {
        editor->cursor_row--;
        editor->cursor_col = 0;
        while (editor->cursor_col < EDITOR_COLS - 1 &&
               editor->lines[editor->cursor_row][editor->cursor_col]) {
            editor->cursor_col++;
        }
        editor->dirty = 1;
        editor->status = "EDIT";
    }
}

text_editor_t *text_editor_create(uint32_t x, uint32_t y) {
    text_editor_t *editor = &g_editor;
    if (editor->win) return editor;

    editor->win = window_create(x, y, 230, 136, "Editor");
    if (!editor->win) return 0;

    window_set_bg_color(editor->win, graphics_rgb(238, 238, 230));
    window_set_title_color(editor->win,
                           graphics_rgb(32, 72, 96),
                           graphics_rgb(255, 255, 255));

    editor_set_filename(editor, "NOTE.TXT");
    editor->status = "NEW";
    editor_clear_buffer(editor);
    editor_load_file(editor);

    return editor;
}

text_editor_t *text_editor_open_file(uint32_t x, uint32_t y, const char *filename) {
    text_editor_t *editor = &g_editor;
    uint8_t had_window = editor->win ? 1 : 0;

    if (!editor->win) {
        editor = text_editor_create(x, y);
        if (!editor) return 0;
    }

    editor_set_filename(editor, filename);
    editor_load_file(editor);

    if (!had_window) window_manager_add(editor->win);
    if (window_is_minimized(editor->win)) window_toggle_minimize(editor->win);
    window_set_focus(editor->win);
    return editor;
}

text_editor_t *text_editor_active(void) {
    return g_editor.win ? &g_editor : 0;
}

void text_editor_handle_key(text_editor_t *editor, char c) {
    if (!editor || !editor->win || window_is_minimized(editor->win)) return;

    if (c == 27) {
        editor_save_file(editor);
    } else if (c == '\b') {
        editor_backspace(editor);
    } else if (c == '\n') {
        editor_newline(editor);
    } else if (c >= 32 && c < 127) {
        editor_insert_char(editor, c);
    }
}

void text_editor_render(text_editor_t *editor) {
    if (!editor || !editor->win || window_is_minimized(editor->win)) return;

    uint32_t paper = graphics_rgb(238, 238, 230);
    uint32_t ink = graphics_rgb(20, 24, 28);
    uint32_t muted = graphics_rgb(70, 74, 78);
    uint32_t cursor = graphics_rgb(20, 120, 150);

    window_clear(editor->win, paper);

    for (uint32_t row = 0; row < EDITOR_ROWS; row++) {
        for (uint32_t col = 0; col < EDITOR_COLS; col++) {
            char ch = editor->lines[row][col];
            if (ch >= 32) {
                window_draw_char(editor->win, col * 8 + 4, row * 8 + 4, ch, ink);
            }
        }
    }

    window_fill_rect(editor->win,
                     editor->cursor_col * 8 + 4,
                     editor->cursor_row * 8 + 12,
                     8,
                     2,
                     cursor);

    window_draw_string(editor->win, 4, EDITOR_ROWS * 8 + 10,
                       editor->filename, muted);
    if (editor->dirty) {
        window_draw_string(editor->win, 108, EDITOR_ROWS * 8 + 10, "*", muted);
    }
    window_draw_string(editor->win, 124, EDITOR_ROWS * 8 + 10, "ESC", muted);
    window_draw_string(editor->win, 156, EDITOR_ROWS * 8 + 10,
                       editor->status ? editor->status : "", muted);
}

void text_editor_window_closed(text_editor_t *editor, window_t *win) {
    if (editor && editor->win == win) {
        editor->win = 0;
    }
}
