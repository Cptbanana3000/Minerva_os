#ifndef MINERVA_SERIAL_H
#define MINERVA_SERIAL_H

#include <stdint.h>

void serial_init(void);
void serial_write(const char* string);
void serial_putc(char character);

#endif
