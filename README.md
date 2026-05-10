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
