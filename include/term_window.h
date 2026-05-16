#ifndef MINERVA_TERM_WINDOW_H
#define MINERVA_TERM_WINDOW_H

#include <stdint.h>
#include "window.h"

/* Client area: 198 x 82 px  →  24 cols × 10 rows at 8×8 */
#define TWIN_COLS 24
#define TWIN_ROWS 10

typedef struct {
    window_t *win;
    char      buf[TWIN_ROWS][TWIN_COLS];
    int       cur_col;
    int       cur_row;
    char      input[64];
    int       input_len;
} term_window_t;

term_window_t *term_window_create(uint32_t x, uint32_t y);
void           term_window_putc(term_window_t *t, char c);
void           term_window_print(term_window_t *t, const char *s);
void           term_window_handle_key(term_window_t *t, char c);
void           term_window_render(term_window_t *t);

#endif
