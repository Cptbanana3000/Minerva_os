#include "io.h"
#include "keyboard.h"
#include "mouse.h"
#include "interrupts.h"
#include "libc.h"
#include "memory.h"
#include "pmm.h"
#include "paging.h"
#include "serial.h"
#include "graphics.h"
#include "terminal.h"
#include "window.h"

static void print(const char* s) {
    term_print(s);
    serial_write(s);
    graphics_flip();
}

static void print_num(uint32_t n) {
    char buf[12];
    int i = 11;
    buf[i] = '\0';
    if (n == 0) { print("0"); return; }
    while (n > 0) { buf[--i] = '0' + (char)(n % 10); n /= 10; }
    print(buf + i);
}

static void cmd_meminfo(void) {
    uint32_t free_kb  = pmm_free_pages()  * 4;
    uint32_t used_kb  = pmm_used_pages()  * 4;
    uint32_t total_kb = pmm_total_pages() * 4;
    print("Memory (4 KB pages, 32 MB physical):\n");
    print("  Total : "); print_num(total_kb); print(" KB\n");
    print("  Used  : "); print_num(used_kb);  print(" KB\n");
    print("  Free  : "); print_num(free_kb);  print(" KB\n");
}

static void read_line(char* buf, int max) {
    int len = 0;
    mouse_draw_cursor();
    while (1) {
        char c = keyboard_read_key();
        if (c == '\n') {
            term_putc('\n');
            buf[len] = 0;
            graphics_flip();
            return;
        } else if (c == '\b') {
            if (len > 0) {
                len--;
                term_putc('\b');
            }
        } else if (len < max - 1) {
            buf[len++] = c;
            term_putc(c);
        }
        mouse_draw_cursor();
    }
}

static void cmd_help(void) {
    print("Available commands:\n");
    print("  help    - show this message\n");
    print("  clear   - clear the screen\n");
    print("  about   - about this OS\n");
    print("  echo    - print 'Hello from MinervaOS!'\n");
    print("  meminfo - show physical memory usage\n");
    print("  reboot  - reboot the system\n");
}

static void cmd_about(void) {
    print("MinervaOS - a tiny x86 kernel\n");
    print("Bootloader + 32-bit protected mode\n");
    print("320x200 256-color graphics\n");
    print("IRQ-driven PS/2 keyboard\n");
}

static void reboot(void) {
    while (inb(0x64) & 0x02) {}
    outb(0x64, 0xFE);
    __asm__ volatile ("cli; hlt");
}

void kernel_main(void) {
    extern uint32_t __kernel_end;
    pmm_init((uint32_t)&__kernel_end);
    paging_init();

    heap_init();
    serial_init();
    serial_write("MinervaOS booting...\n");

    graphics_init();
    term_clear();

    interrupts_init();
    pit_init(100);
    interrupts_enable();
    mouse_init();

    /* Draw windows first */
    window_manager_init();

    window_t* win1 = window_create(10, 10, 120, 70, "System");
    window_set_bg_color(win1, graphics_rgb(180, 180, 180));
    window_manager_add(win1);

    window_t* win2 = window_create(140, 10, 120, 70, "Status");
    window_set_bg_color(win2, graphics_rgb(200, 200, 180));
    window_manager_add(win2);

    window_clear(win1, graphics_rgb(180, 180, 180));
    window_draw_string(win1, 4, 4, "OK", graphics_rgb(0, 0, 0));
    window_draw_string(win1, 4, 20, "Ready", graphics_rgb(0, 128, 0));

    window_clear(win2, graphics_rgb(200, 200, 180));
    window_draw_string(win2, 4, 4, "Running", graphics_rgb(0, 128, 0));

    window_set_focus(win1);
    window_manager_render_all();
    graphics_flip();

    /* Shell below windows */
    term_set_color(10);
    print("\n\n\n\n\n\n\n\n\n\n\n");
    print("  Welcome to MinervaOS\n");
    term_set_color(15);
    print("  Type 'help' for commands.\n\n");

    
    char line[128];
    while (1) {
        term_set_color(14);
        print("minerva> ");
        term_set_color(15);
        read_line(line, sizeof(line));
        if (line[0] == 0) continue;
        else if (strcmp(line, "help") == 0)    cmd_help();
        else if (strcmp(line, "clear") == 0)   term_clear();
        else if (strcmp(line, "about") == 0)   cmd_about();
        else if (strcmp(line, "echo") == 0)    print("Hello from MinervaOS!\n");
        else if (strcmp(line, "meminfo") == 0) cmd_meminfo();
        else if (strcmp(line, "reboot") == 0)  reboot();
        else {
            print("Unknown command: ");
            print(line);
            print("\n");
        }
    }
}