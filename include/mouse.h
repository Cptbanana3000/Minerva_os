#ifndef MINERVA_MOUSE_H
#define MINERVA_MOUSE_H

#include <stdint.h>

void    mouse_init(void);
void    mouse_irq_handler(void);
void    mouse_draw_cursor(void);
uint8_t mouse_get_clicked(void);
int32_t mouse_get_x(void);
int32_t mouse_get_y(void);
uint8_t mouse_get_buttons(void);

#endif