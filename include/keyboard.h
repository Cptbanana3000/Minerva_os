#ifndef MINERVA_KEYBOARD_H
#define MINERVA_KEYBOARD_H

void keyboard_irq_handler(void);
char keyboard_read_key(void);
int  keyboard_has_key(void);

#endif