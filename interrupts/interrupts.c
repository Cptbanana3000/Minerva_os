#include "keyboard.h"   /* add this at the top */
#include <stdint.h>
#include "mouse.h"
#include "interrupts.h"
#include "io.h"
#include "vga.h"

typedef struct __attribute__((packed)) idt_entry {
    uint16_t base_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t flags;
    uint16_t base_high;
} idt_entry_t;

typedef struct __attribute__((packed)) idt_ptr {
    uint16_t limit;
    uint32_t base;
} idt_ptr_t;

struct __attribute__((packed)) interrupt_frame {
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp_dummy;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint32_t gs;
    uint32_t fs;
    uint32_t es;
    uint32_t ds;
    uint32_t int_no;
    uint32_t err_code;
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
    uint32_t useresp;
    uint32_t ss;
};

extern uint32_t isr_stub_table[];
extern void pit_tick(void);

static idt_entry_t idt[256];
static idt_ptr_t idt_descriptor;

static void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) {
        outb(0xA0, 0x20);
    }
    outb(0x20, 0x20);
}

static void pic_remap(void) {
    uint8_t master_mask = inb(0x21);
    uint8_t slave_mask = inb(0xA1);

    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 0x20);
    outb(0xA1, 0x28);
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);

    outb(0x21, master_mask);
    outb(0xA1, slave_mask);

    /* Enable IRQ0 (timer), IRQ1 (keyboard), IRQ2 (cascade to slave). */
    outb(0x21, (uint8_t)(master_mask & 0xF8));
    /* Enable IRQ12 (mouse) on slave. */
    outb(0xA1, (uint8_t)(slave_mask & 0xEF));
}

static void idt_set_gate(uint8_t interrupt_number, uint32_t base, uint16_t selector, uint8_t flags) {
    idt[interrupt_number].base_low = (uint16_t)(base & 0xFFFF);
    idt[interrupt_number].selector = selector;
    idt[interrupt_number].zero = 0;
    idt[interrupt_number].flags = flags;
    idt[interrupt_number].base_high = (uint16_t)((base >> 16) & 0xFFFF);
}

static void idt_load(void) {
    idt_descriptor.limit = (uint16_t)(sizeof(idt_entry_t) * 256 - 1);
    idt_descriptor.base = (uint32_t)&idt;

    __asm__ volatile ("lidt %0" : : "m"(idt_descriptor));
}

void interrupts_init(void) {
    for (uint8_t interrupt_number = 0; interrupt_number < 48; interrupt_number++) {
        idt_set_gate(interrupt_number, isr_stub_table[interrupt_number], 0x08, 0x8E);
    }

    pic_remap();
    idt_load();
}

void interrupts_enable(void) {
    __asm__ volatile ("sti");
}

void interrupt_handler(interrupt_frame_t* frame) {
    if (frame->int_no == 32) {        /* IRQ0 — timer */
        pit_tick();
        pic_send_eoi(0);
        return;
    }

    if (frame->int_no == 33) {        /* IRQ1 — keyboard */
        keyboard_irq_handler();
        pic_send_eoi(1);
        return;
    }

    if (frame->int_no == 44) {        /* IRQ12 — mouse */
        mouse_irq_handler();
        pic_send_eoi(12);
        return;
    }

    if (frame->int_no >= 32 && frame->int_no < 48) {
        pic_send_eoi((uint8_t)(frame->int_no - 32));
        return;
    }

    vga_set_color(0x4F);
    vga_print("CPU exception\n");
    while (1) { __asm__ volatile ("hlt"); }
}