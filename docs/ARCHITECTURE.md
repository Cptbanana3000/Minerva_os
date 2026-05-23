# Architecture Overview

MinervaOS currently targets 32-bit x86 in QEMU.

## Boot Path

- `boot.asm` is a 512-byte BIOS boot sector.
- The bootloader reads the kernel image from the floppy image into `0x8000`.
- It switches to VGA mode 13h, enables protected mode, and jumps to the kernel.
- `kernel_entry.asm` reloads the kernel GDT, clears `.bss`, then calls
  `kernel_main()`.

## Kernel Core

- `kernel.c` initializes memory, interrupts, drivers, filesystem, networking,
  scheduler state, and the desktop loop.
- `interrupts/` owns ISR stubs, IDT setup, PIC remapping, and PIT ticks.
- `kernel/` owns scheduling, process metadata, GDT/TSS, user-mode transition
  experiments, and context-switch helpers.
- `memory/` owns the early heap, physical page bitmap, and paging setup.

## Drivers And UI

- `drivers/graphics.c` provides mode 13h drawing primitives and double buffering.
- `drivers/window.c` is the window manager: focus, Z-order, dragging, minimize,
  close, taskbar integration, and content click dispatch.
- `drivers/desktop.c` owns wallpaper, icons, taskbar, and app routing.
- `drivers/term_window.c` is the terminal shell and diagnostics surface.
- `drivers/theme.c` owns runtime theme palettes.

## Storage And Apps

- `fs/fat32.c` provides FAT32 root-directory file operations.
- Built-in apps live in `drivers/text_editor.c`, `drivers/image_viewer.c`,
  `drivers/audio_player.c`, and `drivers/browser.c`.
- `scripts/make_fat32_image.py` creates `fs.img` with seed files, package
  metadata, app metadata, and sample assets.

## Networking

- `drivers/pci.c` discovers PCI devices.
- `drivers/e1000.c` initializes the QEMU-compatible Intel e1000 NIC.
- `drivers/net.c` layers Ethernet, ARP, IPv4, UDP, DNS, TCP, HTTP, TLS
  diagnostics, certificate checks, P-256/ECDSA support, AES-GCM records, and
  browser fetch glue.

## Current Constraints

- QEMU-first hardware support.
- 320x200 8-bit VGA mode.
- Root-only FAT32 8.3 file workflows.
- TLS/browser support is a narrow educational path, not a general web platform.
- App registry entries launch known built-in handlers, not arbitrary binaries.
