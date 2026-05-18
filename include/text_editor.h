#ifndef MINERVA_TEXT_EDITOR_H
#define MINERVA_TEXT_EDITOR_H

#include <stdint.h>
#include "window.h"

#define EDITOR_COLS 26
#define EDITOR_ROWS 12

typedef struct {
    window_t *win;
    char lines[EDITOR_ROWS][EDITOR_COLS];
    char filename[13];
    uint32_t cursor_col;
    uint32_t cursor_row;
    const char *status;
    uint8_t dirty;
    uint8_t truncated;
} text_editor_t;

text_editor_t *text_editor_create(uint32_t x, uint32_t y);
text_editor_t *text_editor_open_file(uint32_t x, uint32_t y, const char *filename);
text_editor_t *text_editor_active(void);
void text_editor_handle_key(text_editor_t *editor, char c);
void text_editor_render(text_editor_t *editor);
void text_editor_window_closed(text_editor_t *editor, window_t *win);

#endif
