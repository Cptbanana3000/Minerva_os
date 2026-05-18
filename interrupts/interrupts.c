#include "keyboard.h"   /* add this at the top */
#include <stdint.h>
#include "mouse.h"
#include "interrupts.h"
#include "scheduler.h"
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

uint32_t interrupt_handler(interrupt_frame_t* frame) {
    if (frame->int_no == 32) {        /* IRQ0 — timer */
        pit_tick();
        uint32_t next_esp = scheduler_on_timer_interrupt(frame);
        pic_send_eoi(0);
        return next_esp;
    }

    if (frame->int_no == 33) {        /* IRQ1 — keyboard */
        keyboard_irq_handler();
        pic_send_eoi(1);
        return 0;
    }

    if (frame->int_no == 44) {        /* IRQ12 — mouse */
        mouse_irq_handler();
        pic_send_eoi(12);
        return 0;
    }

    if (frame->int_no >= 32 && frame->int_no < 48) {
        pic_send_eoi((uint8_t)(frame->int_no - 32));
        return 0;
    }

    /* In mode 13h, VGA text buffer (0xB8000) is invisible. Write directly
       to the graphics framebuffer so the crash is actually visible. */
    uint8_t *fb = (uint8_t*)0xA0000;
    /* Fill top half red (color 12), bottom half dark red (color 4) */
    for (uint32_t i = 0; i < 320 * 100; i++) fb[i] = 12;
    for (uint32_t i = 320 * 100; i < 320 * 200; i++) fb[i] = 4;
    /* Draw white bars encoding the interrupt number (one bar per bit) */
    for (uint8_t bit = 0; bit < 8; bit++) {
        if (frame->int_no & (1u << bit)) {
            uint32_t bx = 10 + bit * 20;
            for (uint32_t row = 10; row < 60; row++)
                for (uint32_t col = bx; col < bx + 12; col++)
                    fb[row * 320 + col] = 15;  /* white */
        }
    }
    /* Bottom half: 32 yellow bars encoding the faulting EIP, bit 0 leftmost. */
    for (uint8_t bit = 0; bit < 32; bit++) {
        if (frame->eip & (1u << bit)) {
            uint32_t bx = 4 + bit * 10;
            for (uint32_t row = 110; row < 160; row++)
                for (uint32_t col = bx; col < bx + 6; col++)
                    fb[row * 320 + col] = 14;  /* yellow */
        }
    }
    /* Error code in 32 white bars below EIP. */
    for (uint8_t bit = 0; bit < 32; bit++) {
        if (frame->err_code & (1u << bit)) {
            uint32_t bx = 4 + bit * 10;
            for (uint32_t row = 162; row < 178; row++)
                for (uint32_t col = bx; col < bx + 6; col++)
                    fb[row * 320 + col] = 15;  /* white */
        }
    }
    /* Scheduler debug marker (32 cyan bars). */
    extern uint32_t sched_debug_marker;
    for (uint8_t bit = 0; bit < 32; bit++) {
        if (sched_debug_marker & (1u << bit)) {
            uint32_t bx = 4 + bit * 10;
            for (uint32_t row = 182; row < 190; row++)
                for (uint32_t col = bx; col < bx + 6; col++)
                    fb[row * 320 + col] = 11;  /* cyan */
        }
    }
    /* CS at fault (16 green bars, bits 0-15 in screen left half). */
    for (uint8_t bit = 0; bit < 16; bit++) {
        if (frame->cs & (1u << bit)) {
            uint32_t bx = 4 + bit * 10;
            for (uint32_t row = 192; row < 198; row++)
                for (uint32_t col = bx; col < bx + 6; col++)
                    fb[row * 320 + col] = 2;  /* green */
        }
    }
    /* DS at fault (16 magenta bars, bits 0-15 in screen right half). */
    for (uint8_t bit = 0; bit < 16; bit++) {
        if (frame->ds & (1u << bit)) {
            uint32_t bx = 164 + bit * 10;
            for (uint32_t row = 192; row < 198; row++)
                for (uint32_t col = bx; col < bx + 6; col++)
                    fb[row * 320 + col] = 5;  /* magenta */
        }
    }
    while (1) { __asm__ volatile ("hlt"); }
}
