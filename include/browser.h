#ifndef MINERVA_BROWSER_H
#define MINERVA_BROWSER_H

#include <stdint.h>
#include "window.h"

#define BROWSER_MAX_LINKS 8
#define BROWSER_SOURCE_QUERY_MAX 16

typedef struct {
    window_t *win;
    const char *status;
    char url[64];
    char history_url[64];
    char link_url[BROWSER_MAX_LINKS][64];
    uint32_t link_x[BROWSER_MAX_LINKS];
    uint32_t link_y[BROWSER_MAX_LINKS];
    uint32_t link_w[BROWSER_MAX_LINKS];
    uint32_t link_h[BROWSER_MAX_LINKS];
    uint8_t link_count;
    uint8_t editing_url;
    uint8_t has_history;
    uint8_t source_view;
    uint8_t source_searching;
    uint16_t source_offset;
    char source_query[BROWSER_SOURCE_QUERY_MAX];
} browser_t;

browser_t *browser_open(uint32_t x, uint32_t y);
browser_t *browser_open_url(uint32_t x, uint32_t y, const char *url);
browser_t *browser_active(void);
int browser_refresh(browser_t *browser);
void browser_render(browser_t *browser);
int browser_handle_click(browser_t *browser, int32_t x, int32_t y);
void browser_handle_key(browser_t *browser, char c);
void browser_window_closed(browser_t *browser, window_t *win);

#endif
