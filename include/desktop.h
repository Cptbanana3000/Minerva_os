#ifndef MINERVA_DESKTOP_H
#define MINERVA_DESKTOP_H

#include <stdint.h>

void desktop_init(void);
int  desktop_process(void);   /* returns 1 if scene needs redraw */
void desktop_redraw(void);

/* Register a callback invoked after windows are rendered but before the
   taskbar and cursor flip — use it to paint window content each frame. */
void desktop_set_render_cb(void (*cb)(void));

/* Add a clickable icon on the wallpaper. color is a VGA palette index. */
void desktop_add_icon(int32_t x, int32_t y, const char *label,
                      uint32_t color, void (*on_click)(void));

#endif
