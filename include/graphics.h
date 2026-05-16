#ifndef MINERVA_GRAPHICS_H
#define MINERVA_GRAPHICS_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t pitch;
    uint8_t* framebuffer;
} graphics_mode_t;

void graphics_init(void);
void graphics_put_pixel(uint32_t x, uint32_t y, uint32_t color);
void graphics_draw_line(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint32_t color);
void graphics_draw_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color);
void graphics_fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color);
void graphics_clear(uint32_t color);
void graphics_draw_char(uint32_t x, uint32_t y, char character, uint32_t color, uint32_t bg_color);
void graphics_draw_string(uint32_t x, uint32_t y, const char* string, uint32_t color, uint32_t bg_color);

uint32_t graphics_rgb(uint8_t r, uint8_t g, uint8_t b);
void     graphics_flip(void);
uint8_t* graphics_get_framebuffer(void);

#endif
