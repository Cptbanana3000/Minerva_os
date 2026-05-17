#include <stdint.h>

#include "io.h"
#include "vga.h"

#define VGA_MEM   ((volatile uint16_t*)0xB8000)
#define VGA_COLS  80
#define VGA_ROWS  25

static int cursor_row = 0;
static int cursor_col = 0;
static unsigned char text_color = 0x0F;

static void update_cursor(void) {
    uint16_t position = (uint16_t)(cursor_row * VGA_COLS + cursor_col);

    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(position & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((position >> 8) & 0xFF));
}

static void scroll(void) {
    for (int row = 1; row < VGA_ROWS; row++) {
        for (int col = 0; col < VGA_COLS; col++) {
            VGA_MEM[(row - 1) * VGA_COLS + col] = VGA_MEM[row * VGA_COLS + col];
        }
    }

    for (int col = 0; col < VGA_COLS; col++) {
        VGA_MEM[(VGA_ROWS - 1) * VGA_COLS + col] = ((uint16_t)text_color << 8) | ' ';
    }

    cursor_row = VGA_ROWS - 1;
}

void vga_set_color(unsigned char color) {
    text_color = color;
}

void vga_clear_screen(void) {
    for (int index = 0; index < VGA_COLS * VGA_ROWS; index++) {
        VGA_MEM[index] = ((uint16_t)text_color << 8) | ' ';
    }

    cursor_row = 0;
    cursor_col = 0;
    update_cursor();
}

void vga_putc(char character) {
    if (character == '\n') {
        cursor_col = 0;
        cursor_row++;
    } else if (character == '\b') {
        if (cursor_col > 0) {
            cursor_col--;
            VGA_MEM[cursor_row * VGA_COLS + cursor_col] = ((uint16_t)text_color << 8) | ' ';
        }
    } else {
        VGA_MEM[cursor_row * VGA_COLS + cursor_col] = ((uint16_t)text_color << 8) | (uint8_t)character;
        cursor_col++;

        if (cursor_col >= VGA_COLS) {
            cursor_col = 0;
            cursor_row++;
        }
    }

    if (cursor_row >= VGA_ROWS) {
        scroll();
    }

    update_cursor();
}

void vga_print(const char* string) {
    while (*string != '\0') {
        vga_putc(*string++);
    }
}