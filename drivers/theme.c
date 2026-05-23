#include "theme.h"
#include "libc.h"

typedef struct {
    const char *name;
    uint8_t color[THEME_COLOR_COUNT];
} theme_palette_t;

static const theme_palette_t themes[] = {
    {
        "classic",
        {
            1, 7, 0, 15, 0, 0, 7,
            7, 1, 15, 14, 8, 4, 15, 6, 0,
            15, 15
        }
    },
    {
        "night",
        {
            0, 8, 7, 1, 15, 0, 7,
            0, 8, 15, 3, 7, 4, 15, 6, 0,
            15, 15
        }
    }
};

static uint8_t current_theme = 0;

uint8_t theme_count(void) {
    return (uint8_t)(sizeof(themes) / sizeof(themes[0]));
}

uint32_t theme_color(theme_color_t color) {
    if ((uint32_t)color >= THEME_COLOR_COUNT) return 0;
    return themes[current_theme].color[color];
}

const char *theme_name(void) {
    return themes[current_theme].name;
}

const char *theme_name_at(uint8_t index) {
    if (index >= theme_count()) return "";
    return themes[index].name;
}

int theme_set(const char *name) {
    if (!name || !name[0]) return 0;
    for (uint8_t i = 0; i < (uint8_t)(sizeof(themes) / sizeof(themes[0])); i++) {
        if (strcmp(name, themes[i].name) == 0) {
            current_theme = i;
            return 1;
        }
    }
    return 0;
}

void theme_next(void) {
    current_theme++;
    if (current_theme >= theme_count()) current_theme = 0;
}
