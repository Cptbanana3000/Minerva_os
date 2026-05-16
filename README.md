# MyOS — Minimal x86 Operating System

A tiny but real OS with a bootloader, 32-bit protected-mode kernel, VGA text
output, PS/2 keyboard input, and a simple shell.

## Files

- `boot.asm`         — Boot sector. Loads the kernel, switches to 32-bit protected mode.
- `kernel_entry.asm` — Tiny stub that calls into the C kernel.
- `kernel.c`         — The kernel: VGA driver, keyboard driver, shell.
- `linker.ld`        — Linker script (loads kernel at 0x1000).
- `Makefile`         — Build script.

## How to run on Windows 11

You need WSL (Windows Subsystem for Linux). It gives you a real Linux
environment where the OS-dev toolchain works out of the box.

### 1. Install WSL (one-time)

Open **PowerShell as Administrator** and run:

```powershell
wsl --install
```

Reboot when prompted. After reboot, Ubuntu will finish installing — set a
username and password.

### 2. Install the toolchain inside WSL

Open Ubuntu (just type "Ubuntu" in the Start menu) and run:

```bash
sudo apt update
sudo apt install -y nasm gcc make qemu-system-x86
```

### 3. Get the source files into WSL

Easiest: copy the `myos` folder into your WSL home directory. From inside
Ubuntu:

```bash
cp -r /mnt/c/Users/YOUR_WINDOWS_USERNAME/Downloads/myos ~/
cd ~/myos
```

(Adjust the path to wherever you downloaded the files.)

### 4. Build it

```bash
make
```

If it builds with no errors you'll have an `os-image.bin` file. That's your
operating system.

### 5. Run it in QEMU

```bash
make run
```

A QEMU window opens and your OS boots. You should see:

```
Booting MyOS...
Kernel loaded.
==============================
   Welcome to MyOS v0.1
==============================
Type 'help' for available commands.

myos>
```

Type commands and hit Enter:

- `help`   — list commands
- `clear`  — clear the screen
- `about`  — about the OS
- `echo`   — prints "Hello from MyOS!"
- `reboot` — reboot

To close, just close the QEMU window (or press Ctrl+Alt+G to release the mouse
on some setups, then close).

## Troubleshooting

**"command not found: make/nasm/gcc"** — re-run the apt install line in step 2.

**QEMU window doesn't open** — WSL2 on Windows 11 supports GUI apps natively
(WSLg). If nothing happens, make sure you're on Windows 11 (not 10) and that
your WSL is up to date: `wsl --update` in PowerShell.

**Black screen / boots but no text** — try `make clean && make` to rebuild
from scratch.

## What this OS actually is

- Real boot sector (512 bytes, ends in `0x55AA`) loaded by BIOS at 0x7C00.
- BIOS disk read (INT 0x13) loads the kernel from sectors 2+.
- GDT setup, switch from 16-bit real mode to 32-bit protected mode.
- C kernel writes directly to VGA text memory at 0xB8000.
- Polls the PS/2 keyboard controller (port 0x60/0x64) for input.
- ~300 lines of code total.

## What it is NOT

- No memory management, no paging, no processes, no filesystem, no
  interrupts (keyboard is polled, not IRQ-driven). Adding any of these is a
  fun next step.

## Where to go next

If you want to extend this: the standard reference is the OSDev wiki
(wiki.osdev.org). Good next steps in order: (1) interrupt handling with an
IDT, (2) a proper IRQ-driven keyboard driver, (3) a memory manager, (4) a
simple filesystem.

PHASE 1 — Foundation Stabilization
Goal:

Turn your current prototype into a cleaner architecture.

1. Refactor project structure

Move away from one big kernel.c.

Recommended structure:

MinervaOS/
├── boot/
├── kernel/
├── drivers/
├── graphics/
├── memory/
├── fs/
├── apps/
├── libc/
├── include/
├── userland/
├── Makefile
2. Build a tiny libc

Implement:

strlen
memcpy
memset
strcpy
printf-like output later

This becomes your OS utility layer.

3. Interrupt handling (VERY IMPORTANT)

Implement:

IDT
ISRs
PIC remapping

This unlocks:

keyboard interrupts
timer interrupts
real hardware event handling

Huge milestone.

4. PIT timer

Add programmable interval timer.

This enables:

timekeeping
scheduling
sleep functions
PHASE 2 — Memory Management
Goal:

Give Minerva OS real memory control.

5. Heap allocator

Implement:

kmalloc()
kfree()

Now apps/drivers can allocate memory dynamically.

6. Paging

Huge milestone.

Virtual Address→Page Tables→Physical Memory

This enables:

memory isolation
larger systems
future multitasking
7. Physical memory manager

Track:

used pages
free pages

Essential for growth.

PHASE 3 — Graphics System
Goal:

Escape VGA text mode.

8. Framebuffer graphics

Switch from:

0xB8000 text mode

to:

pixel framebuffer

This changes EVERYTHING.

9. Graphics primitives

Implement:

put_pixel()
draw_line()
draw_rect()
fill_rect()
10. Font rendering

Add bitmap fonts.

Now you can:

render GUI text
labels
windows
buttons
11. Double buffering

Important for smooth rendering.

Without this:

flickering
tearing
PHASE 4 — Input & Desktop
Goal:

Create a usable GUI.

12. Mouse driver

Implement:

PS/2 mouse
cursor rendering
click detection
13. Window manager

Create:

draggable windows
title bars
close buttons

At this point:
Minerva OS becomes a GUI OS.

14. Desktop environment

Build:

taskbar
launcher
wallpaper
icons

This is where Minerva gains personality.

PHASE 5 — Filesystem
Goal:

Persistent storage.

15. FAT32 support

Start simple.

You need:

read files
write files
directories
16. VFS layer

Virtual filesystem abstraction.

Lets you support:

FAT
ext-like systems
custom filesystem later
PHASE 6 — Applications
Goal:

Real software ecosystem.

17. Terminal emulator

Better shell experience.

18. Text editor

A HUGE milestone.

Especially if:

syntax highlighting
file saving
coding support
19. Image viewer

Start with:

BMP

Later:

PNG
JPEG
20. Audio player

Start with:

WAV playback

Later:

MP3
PHASE 7 — Multitasking
Goal:

Real operating system behavior.

21. Scheduler

Implement:

round-robin scheduling initially
22. Processes

Add:

process table
context switching
23. User mode (Ring 3)

Huge architecture milestone.

Ring 0 Kernel

=Ring 3 Userland

PHASE 8 — Networking
Goal:

Internet access.

24. Ethernet driver

Likely Intel e1000 first (easy in QEMU).

25. TCP/IP stack

Massive subsystem.

26. DNS + HTTP

Now Minerva can communicate online.

PHASE 9 — Browser
Goal:

Your custom web ecosystem.

27. HTML renderer

Start tiny:

plain HTML
no JS initially
28. CSS engine

Basic styling.

29. JavaScript engine (very advanced)

This is HARD.

Very long-term goal.

PHASE 10 — Polish & Ecosystem
Goal:

Make it memorable.

30. Package manager

Example:

pkg install editor
31. Theming system

Custom:

colors
icons
UI styles
32. App SDK

Let others build apps for Minerva OS.

33. Open source + community

Eventually:

GitHub repo
docs
screenshots
devlogs
contributors
REALISTIC milestone order

The BEST next steps for you specifically:

1. Refactor codebase
2. Interrupts + timer
3. Framebuffer graphics
4. Mouse support
5. Window manager
6. Filesystem
7. Text editor
8. Multitasking
9. Networking
10. Browser

That progression is ambitious but absolutely logical.