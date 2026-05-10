#include <stdint.h>

#include "io.h"
#include "serial.h"

#define SERIAL_PORT 0x3F8

#define SERIAL_DATA_PORT (SERIAL_PORT + 0)
#define SERIAL_FIFO_PORT (SERIAL_PORT + 2)
#define SERIAL_LINE_CONTROL_PORT (SERIAL_PORT + 3)
#define SERIAL_MODEM_CONTROL_PORT (SERIAL_PORT + 4)
#define SERIAL_LINE_STATUS_PORT (SERIAL_PORT + 5)
#define SERIAL_DIVISOR_LOW_PORT (SERIAL_PORT + 0)
#define SERIAL_DIVISOR_HIGH_PORT (SERIAL_PORT + 1)

#define SERIAL_FIFO_ENABLE 0x01
#define SERIAL_FIFO_CLEAR_RECEIVE 0x02
#define SERIAL_FIFO_CLEAR_TRANSMIT 0x04
#define SERIAL_FIFO_INTERRUPT_LEVEL_14 0xC0

#define SERIAL_LINE_CONTROL_DLAB 0x80
#define SERIAL_LINE_CONTROL_8BIT 0x03

#define SERIAL_MODEM_CONTROL_DTR 0x01
#define SERIAL_MODEM_CONTROL_RTS 0x02

#define SERIAL_LINE_STATUS_TRANSMIT_EMPTY 0x20

void serial_init(void) {
    uint8_t dlab = inb(SERIAL_LINE_CONTROL_PORT) | SERIAL_LINE_CONTROL_DLAB;
    outb(SERIAL_LINE_CONTROL_PORT, dlab);

    outb(SERIAL_DIVISOR_LOW_PORT, 1);
    outb(SERIAL_DIVISOR_HIGH_PORT, 0);

    outb(SERIAL_LINE_CONTROL_PORT, SERIAL_LINE_CONTROL_8BIT);
    outb(SERIAL_FIFO_PORT, (uint8_t)(SERIAL_FIFO_ENABLE | SERIAL_FIFO_CLEAR_RECEIVE | SERIAL_FIFO_CLEAR_TRANSMIT | SERIAL_FIFO_INTERRUPT_LEVEL_14));
    outb(SERIAL_MODEM_CONTROL_PORT, (uint8_t)(SERIAL_MODEM_CONTROL_DTR | SERIAL_MODEM_CONTROL_RTS));
}

static void serial_wait_transmit(void) {
    while ((inb(SERIAL_LINE_STATUS_PORT) & SERIAL_LINE_STATUS_TRANSMIT_EMPTY) == 0) {
    }
}

void serial_putc(char character) {
    serial_wait_transmit();
    outb(SERIAL_DATA_PORT, (uint8_t)character);
}

void serial_write(const char* string) {
    while (*string != '\0') {
        serial_putc(*string++);
    }
}
