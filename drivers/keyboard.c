#include <stdint.h>
#include "io.h"
#include "keyboard.h"

#define KB_BUFFER_SIZE 64

static const char scancode_map[128] = {
    0,    27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*',  0,   ' ',
};

static char kb_buffer[KB_BUFFER_SIZE];
static volatile uint8_t kb_head = 0;
static volatile uint8_t kb_tail = 0;

/* Called from interrupt_handler (IRQ1) */
void keyboard_irq_handler(void) {
    uint8_t scancode = inb(0x60);

    if (scancode & 0x80) return;  /* key release, ignore */
    if (scancode >= 128) return;

    char c = scancode_map[scancode];
    if (c == 0) return;

    uint8_t next_head = (kb_head + 1) % KB_BUFFER_SIZE;
    if (next_head != kb_tail) {  /* drop if buffer full */
        kb_buffer[kb_head] = c;
        kb_head = next_head;
    }
}

int keyboard_has_key(void) {
    return kb_head != kb_tail;
}

/* Blocking read — waits until a key is in the buffer */
char keyboard_read_key(void) {
    while (kb_head == kb_tail) {
        // __asm__ volatile ("hlt");  /* sleep until next interrupt */
    }
    char c = kb_buffer[kb_tail];
    kb_tail = (kb_tail + 1) % KB_BUFFER_SIZE;
    return c;
}