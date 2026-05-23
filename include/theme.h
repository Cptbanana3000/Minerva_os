#ifndef MINERVA_THEME_H
#define MINERVA_THEME_H

#include <stdint.h>

typedef enum {
    THEME_WALLPAPER = 0,
    THEME_TASKBAR,
    THEME_TASKBAR_BORDER,
    THEME_TASK_ACTIVE_BG,
    THEME_TASK_ACTIVE_FG,
    THEME_TASK_MIN_BG,
    THEME_TASK_MIN_FG,

    THEME_WINDOW_BG,
    THEME_TITLE_BG,
    THEME_TITLE_FG,
    THEME_BORDER_FOCUS,
    THEME_BORDER_IDLE,
    THEME_CLOSE_BG,
    THEME_CLOSE_FG,
    THEME_MIN_BG,
    THEME_MIN_FG,

    THEME_ICON_BORDER,
    THEME_ICON_TEXT,
    THEME_COLOR_COUNT
} theme_color_t;

uint32_t theme_color(theme_color_t color);
const char *theme_name(void);
uint8_t theme_count(void);
const char *theme_name_at(uint8_t index);
int theme_set(const char *name);
void theme_next(void);

#endif
