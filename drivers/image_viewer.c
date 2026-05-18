#include <stdint.h>
#include "image_viewer.h"
#include "window.h"
#include "graphics.h"
#include "libc.h"
#include "fs.h"

static image_viewer_t g_viewer;
static uint8_t viewer_file_buffer[1024];

static uint16_t rd16(const uint8_t *buffer, uint32_t offset) {
    return (uint16_t)buffer[offset] | ((uint16_t)buffer[offset + 1] << 8);
}

static uint32_t rd32(const uint8_t *buffer, uint32_t offset) {
    return (uint32_t)buffer[offset] |
           ((uint32_t)buffer[offset + 1] << 8) |
           ((uint32_t)buffer[offset + 2] << 16) |
           ((uint32_t)buffer[offset + 3] << 24);
}

static int32_t rd32s(const uint8_t *buffer, uint32_t offset) {
    return (int32_t)rd32(buffer, offset);
}

static void viewer_set_filename(image_viewer_t *viewer, const char *filename) {
    uint32_t i = 0;
    const char *fallback = "IMAGE.BMP";
    const char *src = (filename && filename[0]) ? filename : fallback;

    while (src[i] && i < sizeof(viewer->filename) - 1) {
        char c = src[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        viewer->filename[i] = c;
        i++;
    }
    viewer->filename[i] = 0;
}

static void viewer_probe_file(image_viewer_t *viewer) {
    viewer->file_size = 0;
    viewer->has_file = 0;
    viewer->has_image = 0;
    viewer->image_width = 0;
    viewer->image_height = 0;

    if (!fs_is_ready()) {
        viewer->status = "NO FS";
        return;
    }

    fs_file_t file;
    if (!fs_open(viewer->filename, &file)) {
        viewer->status = "MISSING";
        return;
    }

    viewer->file_size = fs_file_size(&file);
    viewer->has_file = 1;
    viewer->status = "READY";

    uint32_t read = fs_read(&file, viewer_file_buffer, sizeof(viewer_file_buffer));
    fs_close(&file);
    if (read < viewer->file_size || read < 54) {
        viewer->status = read < 54 ? "TOO SMALL" : "TOO BIG";
        return;
    }

    if (viewer_file_buffer[0] != 'B' || viewer_file_buffer[1] != 'M') {
        viewer->status = "NOT BMP";
        return;
    }

    uint32_t pixel_offset = rd32(viewer_file_buffer, 10);
    uint32_t dib_size = rd32(viewer_file_buffer, 14);
    int32_t width_signed = rd32s(viewer_file_buffer, 18);
    int32_t height_signed = rd32s(viewer_file_buffer, 22);
    uint16_t planes = rd16(viewer_file_buffer, 26);
    uint16_t bpp = rd16(viewer_file_buffer, 28);
    uint32_t compression = rd32(viewer_file_buffer, 30);

    if (dib_size < 40 || planes != 1 || bpp != 24 || compression != 0 ||
        width_signed <= 0 || height_signed == 0) {
        viewer->status = "UNSUP";
        return;
    }

    uint32_t width = (uint32_t)width_signed;
    uint32_t height = height_signed < 0 ? (uint32_t)(-height_signed)
                                        : (uint32_t)height_signed;
    if (width > IMAGE_VIEW_MAX_W || height > IMAGE_VIEW_MAX_H) {
        viewer->status = "TOO BIG";
        return;
    }

    uint32_t row_stride = ((width * 3 + 3) / 4) * 4;
    uint32_t pixel_end = pixel_offset + row_stride * height;
    if (pixel_offset < 54 || pixel_end > read) {
        viewer->status = "BAD BMP";
        return;
    }

    for (uint32_t y = 0; y < height; y++) {
        uint32_t src_y = height_signed > 0 ? (height - 1 - y) : y;
        for (uint32_t x = 0; x < width; x++) {
            uint32_t offset = pixel_offset + src_y * row_stride + x * 3;
            uint8_t b = viewer_file_buffer[offset + 0];
            uint8_t g = viewer_file_buffer[offset + 1];
            uint8_t r = viewer_file_buffer[offset + 2];
            viewer->pixels[y][x] = graphics_rgb(r, g, b);
        }
    }

    viewer->image_width = width;
    viewer->image_height = height;
    viewer->has_image = 1;
    viewer->status = "BMP24";
}

static void viewer_print_num(window_t *win, uint32_t x, uint32_t y,
                             uint32_t n, uint32_t color) {
    char tmp[12];
    int i = 11;
    tmp[i] = 0;
    if (n == 0) {
        window_draw_char(win, x, y, '0', color);
        return;
    }

    while (n > 0 && i > 0) {
        tmp[--i] = (char)('0' + (n % 10));
        n /= 10;
    }
    window_draw_string(win, x, y, tmp + i, color);
}

image_viewer_t *image_viewer_create(uint32_t x, uint32_t y) {
    image_viewer_t *viewer = &g_viewer;
    if (viewer->win) return viewer;

    viewer->win = window_create(x, y, 210, 128, "Image");
    if (!viewer->win) return 0;

    window_set_bg_color(viewer->win, graphics_rgb(28, 32, 36));
    window_set_title_color(viewer->win,
                           graphics_rgb(54, 58, 64),
                           graphics_rgb(255, 255, 255));

    viewer_set_filename(viewer, "IMAGE.BMP");
    viewer_probe_file(viewer);
    return viewer;
}

image_viewer_t *image_viewer_open_file(uint32_t x, uint32_t y, const char *filename) {
    image_viewer_t *viewer = &g_viewer;
    uint8_t had_window = viewer->win ? 1 : 0;

    if (!viewer->win) {
        viewer = image_viewer_create(x, y);
        if (!viewer) return 0;
    }

    viewer_set_filename(viewer, filename);
    viewer_probe_file(viewer);

    if (!had_window) window_manager_add(viewer->win);
    if (window_is_minimized(viewer->win)) window_toggle_minimize(viewer->win);
    window_set_focus(viewer->win);
    return viewer;
}

image_viewer_t *image_viewer_active(void) {
    return g_viewer.win ? &g_viewer : 0;
}

void image_viewer_render(image_viewer_t *viewer) {
    if (!viewer || !viewer->win || window_is_minimized(viewer->win)) return;

    uint32_t bg = graphics_rgb(28, 32, 36);
    uint32_t panel = graphics_rgb(210, 214, 218);
    uint32_t ink = graphics_rgb(18, 22, 26);
    uint32_t muted = graphics_rgb(170, 176, 184);
    uint32_t accent = viewer->has_image ? graphics_rgb(70, 170, 115)
                                        : graphics_rgb(190, 80, 80);

    window_clear(viewer->win, bg);
    window_fill_rect(viewer->win, 8, 8, 192, 76, panel);
    window_fill_rect(viewer->win, 12, 12, 184, 68, graphics_rgb(238, 240, 242));

    if (viewer->has_image) {
        uint32_t scale_x = 184 / viewer->image_width;
        uint32_t scale_y = 68 / viewer->image_height;
        uint32_t scale = scale_x < scale_y ? scale_x : scale_y;
        if (scale == 0) scale = 1;
        uint32_t draw_w = viewer->image_width * scale;
        uint32_t draw_h = viewer->image_height * scale;
        uint32_t origin_x = 12 + (184 - draw_w) / 2;
        uint32_t origin_y = 12 + (68 - draw_h) / 2;

        for (uint32_t y = 0; y < viewer->image_height; y++) {
            for (uint32_t x = 0; x < viewer->image_width; x++) {
                window_fill_rect(viewer->win,
                                 origin_x + x * scale,
                                 origin_y + y * scale,
                                 scale,
                                 scale,
                                 viewer->pixels[y][x]);
            }
        }
    } else {
        for (uint32_t y = 0; y < 56; y += 8) {
            for (uint32_t x = 0; x < 168; x += 8) {
                uint32_t color = ((x ^ y) & 8) ? graphics_rgb(220, 224, 228)
                                               : graphics_rgb(245, 246, 248);
                window_fill_rect(viewer->win, 20 + x, 18 + y, 8, 8, color);
            }
        }
    }

    window_fill_rect(viewer->win, 18, 72, 172, 4, accent);
    window_draw_string(viewer->win, 12, 90, viewer->filename, muted);
    window_draw_string(viewer->win, 112, 90, viewer->status ? viewer->status : "", muted);

    if (viewer->has_file) {
        viewer_print_num(viewer->win, 12, 104, viewer->file_size, muted);
        window_draw_string(viewer->win, 72, 104, "bytes", muted);
    } else if (!viewer->has_file) {
        window_draw_string(viewer->win, 44, 42, "BMP soon", ink);
    }
}

void image_viewer_window_closed(image_viewer_t *viewer, window_t *win) {
    if (viewer && viewer->win == win) {
        viewer->win = 0;
    }
}
