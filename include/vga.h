#ifndef MINERVA_VGA_H
#define MINERVA_VGA_H

void vga_clear_screen(void);
void vga_putc(char character);
void vga_print(const char* string);
void vga_set_color(unsigned char color);

#endif