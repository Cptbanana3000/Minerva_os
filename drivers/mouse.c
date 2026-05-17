#include <stdint.h>
#include "io.h"
#include "mouse.h"
#include "graphics.h"

#define MOUSE_PORT      0x60
#define MOUSE_STATUS    0x64
#define MOUSE_ABIT      0x02
#define MOUSE_BBIT      0x01
#define MOUSE_WRITE     0xD4
#define MOUSE_F_BIT     0x20

static volatile int32_t mouse_x = 160;  /* start in center */
static volatile int32_t mouse_y = 100;
static volatile uint8_t mouse_buttons = 0;

static uint8_t mouse_cycle = 0;
static uint8_t mouse_byte[3];
static volatile uint8_t mouse_prev_buttons = 0;
static volatile uint8_t mouse_clicked = 0;

static int32_t cursor_px = 160, cursor_py = 100;
static uint8_t cursor_bg[9];

static void cursor_save(int32_t x, int32_t y) {
    uint8_t* buf = graphics_get_framebuffer();
    for (int32_t dy = 0; dy < 3; dy++)
        for (int32_t dx = 0; dx < 3; dx++) {
            int32_t px = x + dx, py = y + dy;
            cursor_bg[dy * 3 + dx] = (px < 320 && py < 200)
                ? buf[py * 320 + px] : 0;
        }
}

static void cursor_restore(int32_t x, int32_t y) {
    uint8_t* buf = graphics_get_framebuffer();
    for (int32_t dy = 0; dy < 3; dy++)
        for (int32_t dx = 0; dx < 3; dx++) {
            int32_t px = x + dx, py = y + dy;
            if (px < 320 && py < 200)
                buf[py * 320 + px] = cursor_bg[dy * 3 + dx];
        }
}

static void cursor_draw(int32_t x, int32_t y) {
    for (int32_t dy = 0; dy < 3; dy++)
        for (int32_t dx = 0; dx < 3; dx++)
            if (x + dx < 320 && y + dy < 200)
                graphics_put_pixel((uint32_t)(x + dx), (uint32_t)(y + dy), 15);
}

void mouse_draw_cursor(void) {
    cursor_save(cursor_px, cursor_py);
    cursor_draw(cursor_px, cursor_py);
    graphics_flip();
}

uint8_t mouse_get_clicked(void) {
    uint8_t c = mouse_clicked;
    mouse_clicked = 0;
    return c;
}

static void mouse_wait(uint8_t type) {
    uint32_t timeout = 100000;
    if (type == 0) {
        while (timeout--) {
            if (inb(MOUSE_STATUS) & MOUSE_BBIT) return;
        }
    } else {
        while (timeout--) {
            if (!(inb(MOUSE_STATUS) & MOUSE_ABIT)) return;
        }
    }
}

static void mouse_write(uint8_t data) {
    mouse_wait(1);
    outb(MOUSE_STATUS, MOUSE_WRITE);
    mouse_wait(1);
    outb(MOUSE_PORT, data);
}

static uint8_t mouse_read(void) {
    mouse_wait(0);
    return inb(MOUSE_PORT);
}

void mouse_init(void) {
    uint8_t status;

    /* Enable auxiliary mouse device */
    mouse_wait(1);
    outb(MOUSE_STATUS, 0xA8);

    /* Enable interrupts */
    mouse_wait(1);
    outb(MOUSE_STATUS, 0x20);
    mouse_wait(0);
    status = inb(MOUSE_PORT) | 0x02;
    mouse_wait(1);
    outb(MOUSE_STATUS, 0x60);
    mouse_wait(1);
    outb(MOUSE_PORT, status);

    /* Use default settings */
    mouse_write(0xF6);
    mouse_read();  /* ack */

    /* Enable mouse */
    mouse_write(0xF4);
    mouse_read();  /* ack */
}

void mouse_irq_handler(void) {
    uint8_t status = inb(MOUSE_STATUS);
    if (!(status & MOUSE_BBIT)) return;
    if (!(status & MOUSE_F_BIT)) return;

    uint8_t data = inb(MOUSE_PORT);

    switch (mouse_cycle) {
        case 0:
            mouse_byte[0] = data;
            if (!(data & 0x08)) return;  /* bit 3 must be set */
            mouse_cycle++;
            break;
        case 1:
            mouse_byte[1] = data;
            mouse_cycle++;
            break;
        case 2:
            mouse_byte[2] = data;
            mouse_cycle = 0;

            /* Update position */
            int32_t dx = (int32_t)(int8_t)mouse_byte[1];
            int32_t dy = (int32_t)(int8_t)mouse_byte[2];

            mouse_x += dx;
            mouse_y -= dy;  /* y is inverted */

            /* Clamp to screen */
            if (mouse_x < 0) mouse_x = 0;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_x > 319) mouse_x = 319;
            if (mouse_y > 199) mouse_y = 199;

            mouse_prev_buttons = mouse_buttons;
            mouse_buttons = mouse_byte[0] & 0x07;
            mouse_clicked |= (mouse_buttons & ~mouse_prev_buttons);

            cursor_restore(cursor_px, cursor_py);
            cursor_px = mouse_x;
            cursor_py = mouse_y;
            cursor_save(cursor_px, cursor_py);
            cursor_draw(cursor_px, cursor_py);
            graphics_flip();
            break;
    }
}

int32_t mouse_get_x(void) { return mouse_x; }
int32_t mouse_get_y(void) { return mouse_y; }
uint8_t mouse_get_buttons(void) { return mouse_buttons; }