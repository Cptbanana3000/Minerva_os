/* kernel.c - Minimal kernel: VGA text output, PS/2 keyboard, simple shell */

#include <stdint.h>

/* ===== VGA text mode (0xB8000) ===== */
#define VGA_MEM   ((volatile uint16_t*)0xB8000)
#define VGA_COLS  80
#define VGA_ROWS  25

static int cursor_row = 0;
static int cursor_col = 0;
static uint8_t text_color = 0x0F; /* white on black */

/* ===== I/O port helpers ===== */
static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void outb(uint16_t port, uint8_t v) {
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(port));
}

/* ===== Hardware cursor ===== */
static void update_cursor(void) {
    uint16_t pos = cursor_row * VGA_COLS + cursor_col;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

/* ===== Screen functions ===== */
static void clear_screen(void) {
    for (int i = 0; i < VGA_COLS * VGA_ROWS; i++) {
        VGA_MEM[i] = ((uint16_t)text_color << 8) | ' ';
    }
    cursor_row = 0;
    cursor_col = 0;
    update_cursor();
}

static void scroll(void) {
    for (int r = 1; r < VGA_ROWS; r++) {
        for (int c = 0; c < VGA_COLS; c++) {
            VGA_MEM[(r - 1) * VGA_COLS + c] = VGA_MEM[r * VGA_COLS + c];
        }
    }
    for (int c = 0; c < VGA_COLS; c++) {
        VGA_MEM[(VGA_ROWS - 1) * VGA_COLS + c] = ((uint16_t)text_color << 8) | ' ';
    }
    cursor_row = VGA_ROWS - 1;
}

static void putc(char ch) {
    if (ch == '\n') {
        cursor_col = 0;
        cursor_row++;
    } else if (ch == '\b') {
        if (cursor_col > 0) {
            cursor_col--;
            VGA_MEM[cursor_row * VGA_COLS + cursor_col] =
                ((uint16_t)text_color << 8) | ' ';
        }
    } else {
        VGA_MEM[cursor_row * VGA_COLS + cursor_col] =
            ((uint16_t)text_color << 8) | (uint8_t)ch;
        cursor_col++;
        if (cursor_col >= VGA_COLS) {
            cursor_col = 0;
            cursor_row++;
        }
    }
    if (cursor_row >= VGA_ROWS) {
        scroll();
    }
    update_cursor();
}

static void print(const char* s) {
    while (*s) putc(*s++);
}

/* ===== String helpers ===== */
static int strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (uint8_t)*a - (uint8_t)*b;
}

/* ===== Keyboard (PS/2 scancode set 1) ===== */
static const char scancode_map[128] = {
    0,    27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,    '\\','z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*',  0,   ' ',
};

static char read_key(void) {
    while (1) {
        /* Wait for a keypress: status port bit 0 = output buffer full */
        if (inb(0x64) & 0x01) {
            uint8_t sc = inb(0x60);
            /* Ignore key releases (high bit set) */
            if (sc & 0x80) continue;
            if (sc < 128) {
                char c = scancode_map[sc];
                if (c) return c;
            }
        }
    }
}

/* ===== Shell ===== */
static void read_line(char* buf, int max) {
    int len = 0;
    while (1) {
        char c = read_key();
        if (c == '\n') {
            putc('\n');
            buf[len] = 0;
            return;
        } else if (c == '\b') {
            if (len > 0) {
                len--;
                putc('\b');
            }
        } else if (len < max - 1) {
            buf[len++] = c;
            putc(c);
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
    while (inb(0x64) & 0x02);
    outb(0x64, 0xFE);
    /* Fallback: triple fault */
    __asm__ volatile ("cli; hlt");
}

void kernel_main(void) {
    clear_screen();
    text_color = 0x0A; /* light green */
    print("==============================\n");
    print("   Welcome to MyOS v0.1\n");
    print("==============================\n");
    text_color = 0x0F;
    print("Type 'help' for available commands.\n\n");

    char line[128];
    while (1) {
        text_color = 0x0E; /* yellow */
        print("myos> ");
        text_color = 0x0F;
        read_line(line, sizeof(line));

        if (line[0] == 0) continue;
        else if (strcmp(line, "help") == 0)   cmd_help();
        else if (strcmp(line, "clear") == 0)  clear_screen();
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
