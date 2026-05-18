# MinervaOS — Development Phases

---

## Phase 0 — Foundation `COMPLETE`

> Bootable OS with graphics and a working shell.

- [x] Bootloader (real-mode, BIOS INT 0x13 disk load)
- [x] 32-bit protected mode + GDT
- [x] VGA text output (0xB8000)
- [x] VGA graphics mode 13h (320×200, 256 color)
- [x] PS/2 keyboard (IRQ-driven ring buffer)
- [x] Basic shell (help, clear, about, echo, reboot)
- [x] IDT + ISR stubs (48 vectors)
- [x] PIC remapping (8259A, IRQ0–IRQ15)
- [x] PIT timer (IRQ0, 100 Hz)
- [x] Heap allocator (kmalloc / kfree, 256 KB, best-fit)
- [x] Serial output (COM1, 115200 baud)
- [x] Graphics primitives (pixel, line, rect, fill, font, RGB)
- [x] Window manager (Z-order, focus, doubly-linked list)
- [x] libc (strlen, memcpy, memset, strcpy, strcmp)

---

## Phase 1 — Stabilize + Input `COMPLETE`

> Interrupt-driven input, working mouse, double buffering.

- [x] Fix render order bug (tail-to-head window rendering)
- [x] IRQ-driven keyboard (scancode → ASCII, 64-byte ring buffer)
- [x] PS/2 mouse driver (IRQ12, 3-byte packet state machine)
- [x] Mouse cursor rendering (3×3 block, interrupt-driven, bg save/restore)
- [x] Click detection (mouse_get_clicked — button press transitions)
- [x] Double buffering (back_buffer → graphics_flip at logical draw points)

---

## Phase 2 — Memory `COMPLETE`

> Physical memory management and virtual address space.

- [x] Physical memory manager (PMM — bitmap, 32 MB, 4 KB pages)
- [x] Paging / page tables (identity map first 4 MB, CR0.PG enabled)
- [x] Virtual address space (paging_map for future mappings)
- [x] meminfo shell command (shows used/free pages)

---

## Phase 3 — Desktop GUI `COMPLETE`

> Usable graphical desktop environment.

- [x] Wallpaper (solid color background layer)
- [x] Draggable windows (click titlebar to drag, clamped to screen)
- [x] Close button (red X — destroys window)
- [x] Minimize button (yellow — — hides window, shows in taskbar)
- [x] Taskbar (bottom bar, window buttons, click to focus/restore)
- [x] Desktop icons (clickable icons on wallpaper)
- [x] Terminal emulator app (window-based shell)

---

## Phase 4 — Filesystem `COMPLETE`

> Persistent storage and file access.

- [x] FAT32 read support
- [x] FAT32 write support
- [x] VFS layer (virtual filesystem abstraction)
- [x] Directory listing
- [x] File open / read API
- [x] Create empty FAT32 files
- [x] Write one-cluster empty FAT32 files
- [x] Overwrite one-cluster FAT32 files
- [x] Append within one-cluster FAT32 files
- [x] Extend one-cluster append to two clusters
- [x] General multi-cluster append
- [x] Truncate files and free cluster chains
- [x] File write API (create / truncate / append modes)
- [x] File delete API
- [x] File rename API

---

## Phase 5 — Multitasking `COMPLETE`

> Multiple processes running concurrently.

- [x] Kernel task table
- [x] PIT-driven scheduler ticks
- [x] Cooperative round-robin scheduler skeleton
- [x] Kernel task stacks
- [x] Cooperative context switch (ESP save/restore)
- [x] Timer-requested scheduling plumbing
- [x] Interrupt-frame scheduling metadata
- [x] ISR return-ESP switch hook
- [x] Timer interrupt scheduler decision hook
- [x] Fake interrupt-frame task contexts
- [x] Disabled preemptive switch candidate path
- [x] Runtime preemption gate
- [x] Unsafe IRQ preemption guard
- [x] Main/desktop loop registered as a scheduler slot
- [x] Main-loop IRQ frame capture (diagnostic, no switch)
- [x] Dedicated kernel stack for the desktop main loop
- [x] Atomic main↔task IRQ round-trip
- [x] Preemptive round-robin scheduler
- [x] Process table
- [x] Context switching (save/restore CPU state)
- [x] GDT/TSS user-mode groundwork
- [x] Syscall interrupt gate
- [x] Controlled ring 3 entry test
- [x] User process metadata
- [x] User context metadata
- [x] Prepared user process runner
- [x] Scheduler-owned user launch
- [x] Restartable user launch diagnostics
- [x] User-mode page fault recovery
- [x] Ring 3 userland (privilege separation)

---

## Phase 6 — Applications

> Built-in software ecosystem.

- [ ] Terminal emulator app (window-based, full shell)
- [ ] Text editor (open, edit, save files)
- [ ] Image viewer (BMP support)
- [ ] Audio player (WAV playback)

---

## Phase 7 — Networking

> Internet access.

- [ ] Intel e1000 Ethernet driver (QEMU-compatible)
- [ ] TCP/IP stack
- [ ] DNS resolver
- [ ] HTTP client

---

## Phase 8 — Browser

> Web browsing capability.

- [ ] HTML renderer (basic tags)
- [ ] CSS engine (basic styling)
- [ ] JavaScript engine (long-term goal)

---

## Phase 9 — Polish & Ecosystem

> Community-ready OS.

- [ ] Package manager (pkg install)
- [ ] Theming system (colors, icons, UI styles)
- [ ] App SDK (third-party app support)
- [ ] Open source release (GitHub, docs, devlogs)

---

## Progress

| Phase | Name | Status |
|-------|------|--------|
| 0 | Foundation | ✅ Complete |
| 1 | Stabilize + Input | ✅ Complete |
| 2 | Memory | ✅ Complete |
| 3 | Desktop GUI | ✅ Complete |
| 4 | Filesystem | ✅ Complete |
| 5 | Multitasking | ✅ Complete |
| 6 | Applications | ⬜ Not started |
| 7 | Networking | ⬜ Not started |
| 8 | Browser | ⬜ Not started |
| 9 | Polish & Ecosystem | ⬜ Not started |
