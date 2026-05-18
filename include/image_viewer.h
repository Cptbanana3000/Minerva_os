#ifndef MINERVA_IMAGE_VIEWER_H
#define MINERVA_IMAGE_VIEWER_H

#include <stdint.h>
#include "window.h"

#define IMAGE_VIEW_MAX_W 32
#define IMAGE_VIEW_MAX_H 24

typedef struct {
    window_t *win;
    char filename[13];
    const char *status;
    uint32_t pixels[IMAGE_VIEW_MAX_H][IMAGE_VIEW_MAX_W];
    uint32_t image_width;
    uint32_t image_height;
    uint32_t file_size;
    uint8_t has_file;
    uint8_t has_image;
} image_viewer_t;

image_viewer_t *image_viewer_create(uint32_t x, uint32_t y);
image_viewer_t *image_viewer_open_file(uint32_t x, uint32_t y, const char *filename);
image_viewer_t *image_viewer_active(void);
void image_viewer_render(image_viewer_t *viewer);
void image_viewer_window_closed(image_viewer_t *viewer, window_t *win);

#endif
