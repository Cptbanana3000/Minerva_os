#ifndef MINERVA_TERMINAL_H
#define MINERVA_TERMINAL_H

#include <stdint.h>

void term_clear(void);
void term_putc(char c);
void term_print(const char* s);
void term_set_color(uint32_t color);

#endif