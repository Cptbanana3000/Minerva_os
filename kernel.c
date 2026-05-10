#include "io.h"
#include "keyboard.h"
#include "interrupts.h"
#include "libc.h"
#include "memory.h"
#include "serial.h"
#include "vga.h"
#include "graphics.h"
#include "window.h"

static void print(const char* string) {
    vga_print(string);
    serial_write(string);
}

static void read_line(char* buf, int max) {
    int len = 0;
    while (1) {
        char c = keyboard_read_key();
        if (c == '\n') {
            vga_putc('\n');
            buf[len] = 0;
            return;
        } else if (c == '\b') {
            if (len > 0) {
                len--;
                vga_putc('\b');
            }
        } else if (len < max - 1) {
            buf[len++] = c;
            vga_putc(c);
        }
    }
}

static void cmd_help(void) {
    print("Available commands:\n");
    print("  help   - show this message\n");
    print("  clear  - clear the screen\n");
    print("  about  - about this OS\n");
    print("  echo   - print 'Hello from MyOS!'\n");
    print("  reboot - reboot the system\n");
}

static void cmd_about(void) {
    print("MyOS - a tiny x86 kernel\n");
    print("Bootloader + 32-bit protected mode kernel\n");
    print("VGA text output, PS/2 keyboard input\n");
}

static void reboot(void) {
    /* Pulse keyboard controller reset line */
    while (inb(0x64) & 0x02) {
    }
    outb(0x64, 0xFE);

    /* Fallback: halt if reset does not occur */
    __asm__ volatile ("cli; hlt");
}

void kernel_main(void) {
    heap_init();
    serial_init();
    serial_write("Serial console initialized.\n");

    vga_clear_screen();
    vga_set_color(0x0A); /* light green */
    print("==============================\n");
    print("   Welcome to MyOS v0.1\n");
    print("==============================\n");
    vga_set_color(0x0F);
    print("Type 'help' for available commands.\n\n");

    interrupts_init();
    pit_init(100);
    interrupts_enable();

    print("Interrupts online.\n");
    print("Window manager initializing...\n\n");

    /* Initialize graphics and window manager */
    graphics_init();
    graphics_clear(graphics_rgb(30, 30, 60));  /* dark blue background */
    window_manager_init();

    /* Create test windows */
    window_t* win1 = window_create(10, 10, 150, 80, "System");
    window_set_bg_color(win1, graphics_rgb(180, 180, 180));
    window_manager_add(win1);

    window_t* win2 = window_create(170, 10, 150, 100, "Status");
    window_set_bg_color(win2, graphics_rgb(200, 200, 180));
    window_manager_add(win2);

    /* Populate windows with test content */
    window_clear(win1, graphics_rgb(180, 180, 180));
    window_draw_string(win1, 10, 10, "OK", graphics_rgb(0, 0, 0));
    window_draw_string(win1, 10, 30, "Ready", graphics_rgb(0, 128, 0));

    window_clear(win2, graphics_rgb(200, 200, 180));
    window_draw_string(win2, 10, 10, "Running", graphics_rgb(0, 128, 0));

    window_set_focus(win1);
    window_manager_render_all();

    /* Shell loop continues in text mode */
    char line[128];
    while (1) {
        vga_set_color(0x0E); /* yellow */
        print("myos> ");
        vga_set_color(0x0F);
        read_line(line, sizeof(line));

        if (line[0] == 0) continue;
        else if (strcmp(line, "help") == 0)   cmd_help();
        else if (strcmp(line, "clear") == 0)  vga_clear_screen();
        else if (strcmp(line, "about") == 0)  cmd_about();
        else if (strcmp(line, "echo") == 0)   print("Hello from MyOS!\n");
        else if (strcmp(line, "reboot") == 0) reboot();
        else {
            print("Unknown command: ");
            print(line);
            print("\n");
        }
    }
}
