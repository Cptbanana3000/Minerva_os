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
- [x] Heap allocator (kmalloc / kfree, 224 KB, best-fit)
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

## Phase 6 — Applications `COMPLETE`

> Built-in software ecosystem.

- [x] Text editor app skeleton
- [x] Text editor in-memory editing
- [x] Text editor fixed-file open/save (`NOTE.TXT`)
- [x] Text editor terminal-driven file open (`edit NAME`)
- [x] Image viewer app skeleton
- [x] Image viewer terminal-driven file open (`view NAME`)
- [x] BMP test asset in FAT32 image
- [x] Image viewer 24-bit BMP render path
- [x] WAV test asset in FAT32 image
- [x] Audio player app skeleton
- [x] Audio player terminal-driven file open (`play NAME`)
- [x] WAV metadata parser
- [x] PC speaker driver
- [x] Audio player PC-speaker WAV preview
- [x] Terminal emulator app (window-based, full shell)
- [x] Text editor (open, edit, save files)
- [x] Image viewer (BMP support)
- [x] Audio player (WAV playback)

---

## Phase 7 — Networking

> Internet access.

- [x] PCI config-space access
- [x] e1000 PCI discovery (QEMU-compatible)
- [x] `net` terminal diagnostics
- [x] Intel e1000 Ethernet driver register mapping
- [x] e1000 MAC address readout
- [x] e1000 transmit descriptor ring
- [x] Raw Ethernet test-frame transmit (`net tx`)
- [x] e1000 receive descriptor ring
- [x] Raw receive poll diagnostics (`net rx`)
- [x] Packet receive/transmit API
- [x] Static QEMU user-net IPv4 identity (`10.0.2.15`)
- [x] ARP request/reply handling (`net arp`)
- [x] Minimal IPv4 packet transmit/receive
- [x] Minimal UDP packet transmit/receive
- [x] DNS resolver smoke test (`net dns`)
- [x] TCP SYN/SYN-ACK/ACK handshake smoke test (`net tcp`)
- [x] Local QEMU TCP handshake probe (`net tcp dns`)
- [x] TCP payload send/receive smoke test
- [x] HTTP GET smoke test (`net http`)
- [x] HTTP response capture and terminal preview (`net page`)
- [x] HTTP response viewer/browser integration
- [x] Basic arbitrary HTTP host/path fetch (`net http host/path`, `browser host/path`)

---

## Phase 8 — Browser

> Web browsing capability.

- [x] Browser window shell with HTTP response preview
- [x] Basic URL target support for plain HTTP hosts/paths
- [x] HTML renderer (basic tags)
- [x] CSS engine (basic styling)
- [x] First-link click navigation (plain HTTP, HTTPS guarded)
- [x] Multi-link hit testing
- [x] HTTPS/TLS TCP probe (`net tls`)
- [x] TLS ClientHello transmit
- [x] TLS ServerHello header parse
- [x] TLS ServerHello fields parse
- [x] TLS Certificate message capture
- [x] TLS fragmented handshake byte tracking
- [x] TLS Certificate fragment completion wait
- [x] TLS Certificate list header parse
- [x] TLS first certificate DER envelope parse
- [x] TLS X.509 TBS/serial envelope parse
- [x] TLS X.509 signature algorithm OID parse
- [x] TLS X.509 validity envelope parse
- [x] TLS X.509 validity date decode
- [x] TLS X.509 public-key algorithm OID parse
- [x] Dedicated certificate diagnostics command (`net cert`)
- [x] TLS X.509 issuer/subject Common Name parse
- [x] TLS X.509 first SAN DNS parse
- [x] TLS X.509 SAN hostname match diagnostic
- [x] TLS X.509 date validity diagnostic via RTC
- [x] TLS X.509 Basic Constraints CA diagnostic
- [x] TLS X.509 Key Usage diagnostic
- [x] TLS X.509 Extended Key Usage diagnostic
- [x] TLS leaf trust summary command (`net trust`)
- [x] TLS known issuer diagnostic
- [x] TLS leaf-to-intermediate chain link diagnostic
- [x] TLS leaf certificate signature-value diagnostic
- [x] SHA-256 digest over leaf TBSCertificate
- [x] Compact TLS signature diagnostics command (`net sig`)
- [x] TLS intermediate EC public-key diagnostic
- [x] TLS ECDSA signature r/s diagnostic
- [x] TLS P-256 verifier operand capture
- [x] P-256 field arithmetic self-test (`net p256`)
- [x] P-256 modular inverse self-test
- [x] P-256 base-point/on-curve and doubling self-test
- [x] P-256 point addition and small scalar-multiply self-test
- [x] P-256 256-bit scalar input self-test
- [x] P-256 group-order scalar arithmetic self-test
- [x] ECDSA verifier scalar derivation diagnostic (`w`, `u1`, `u2`)
- [x] ECDSA issuer public-key P-256 point validation diagnostic
- [x] P-256 projective scalar multiplication self-test
- [x] ECDSA P-256 signature equation diagnostic (`u1*G + u2*Q`)
- [x] P-256 optimized/projective scalar multiplication for ECDSA
- [x] ECDSA signature result integrated into TLS trust summary
- [x] TLS ServerKeyExchange capture and ECDHE parameter diagnostic (`net kex`)
- [x] TLS ServerKeyExchange signature verification
- [x] TLS ClientKeyExchange transmit (`net cke`)
- [x] TLS ECDHE shared-secret derivation diagnostic (`net keys`)
- [x] TLS 1.2 master-secret PRF diagnostic (`net keys`)
- [x] TLS key-block expansion diagnostic (`net keys`)
- [x] TLS handshake transcript hash and Finished verify-data diagnostic (`net fin`)
- [x] TLS AES-128-GCM client Finished record encryption
- [x] TLS ChangeCipherSpec + encrypted Finished transmit (`net finish`)
- [x] TLS server Finished receive/decrypt/verify (`net finish`)
- [x] TLS application-data record crypto (`net app`)
- [x] HTTPS GET over encrypted TLS records (`net app`)
- [x] Browser HTTPS fetch integration (`browser https://example.com/`)
- [x] Browser editable address bar
- [x] Browser HTTP redirect following (`Location:` 301/302)
- [x] Browser readable-text fallback for modern pages
- [x] Browser HTTP header diagnostics for blank pages
- [x] Browser source/raw preview toggle
- [x] Browser source/raw preview scrolling
- [x] Browser source marker jumps
- [x] Browser source text search
- [x] Browser document title/meta summary fallback
- [x] Browser head metadata summary fallback
- [x] Browser compact page-info fallback
- [x] Browser one-step history/back navigation
- [x] Browser reload action
- [x] Browser home action
- [ ] JavaScript engine (long-term goal)

---

## Phase 9 — Polish & Ecosystem

> Community-ready OS.

- [x] Package manager seed (`pkg list/info/install`)
- [ ] Theming system (colors, icons, UI styles)
- [x] Theme palette foundation
- [x] Theme terminal diagnostics (`theme`)
- [x] Runtime theme switching (`theme classic`, `theme night`)
- [x] Theme-linked default window chrome
- [x] Theme listing/cycling commands
- [x] Kernel image/BSS headroom guard
- [x] FAT32 package manifest seed (`PKGS.TXT`)
- [x] Package install marker receipts (`*.PKG`)
- [x] Package status/remove commands
- [x] App registry manifest seed (`APPS.TXT`)
- [x] App launcher command (`app list/info/run`)
- [x] Expanded bootloader kernel load window
- [x] App manifest launch targets
- [x] Runtime app registry append (`app add`)
- [x] Runtime app registry removal (`app remove`)
- [x] App registry validation (`app check`)
- [x] In-OS App SDK reference (`sdk`, `SDK.TXT`)
- [x] Open-source documentation refresh
- [x] Architecture/contribution/release docs
- [x] Issue/PR templates
- [x] Roadmap guide and license decision note
- [x] Changelog/devlog/support/security scaffolds
- [x] Automated headless QEMU smoke target (`make smoke`)
- [x] MIT license selected and added
- [ ] App SDK (third-party executable/app-bundle support)
- [x] Open source release prep (docs, license, templates, smoke)
- [ ] Public GitHub release (repo publish, screenshots, labels, milestones)

---

## Long-Term Roadmap — Full Open Source OS

> These phases describe the bigger ambition: MinervaOS becoming a real,
> community-built general-purpose operating system with modern visuals,
> hardware support, applications, developer tooling, and a sustainable ecosystem.

---

## Phase 10 — Modern Display & Visual System

> Move beyond 320x200 VGA into crisp high-resolution graphics.

- [x] VBE / linear framebuffer implementation plan
- [ ] VESA BIOS Extensions / linear framebuffer boot mode
- [ ] 800x600 and 1024x768 graphics modes
- [ ] 1280x720 HD mode
- [ ] 1920x1080 Full HD mode
- [ ] 24/32-bit true-color framebuffer
- [ ] Resolution detection and mode selection
- [ ] Scalable graphics primitives
- [ ] Dirty-rectangle compositor
- [ ] Alpha blending and transparency
- [ ] Smooth window movement without full-screen redraws
- [ ] Bitmap font loading
- [ ] Anti-aliased font rendering
- [ ] High-DPI aware UI scaling
- [ ] Icon format and icon loader
- [ ] Crisp desktop icon set
- [ ] Window shadows and modern frame styling
- [ ] Wallpaper image support
- [ ] Theme engine for colors, fonts, spacing, and icons

---

## Phase 11 — Hardware Platform Support

> Boot and run beyond the narrow QEMU demo environment.

- [ ] PCI bus enumeration
- [ ] PCI driver registry
- [ ] ACPI table discovery
- [ ] APIC / IOAPIC groundwork
- [x] Real-time clock driver
- [ ] PS/2 compatibility cleanup
- [ ] USB controller discovery
- [ ] USB keyboard support
- [ ] USB mouse support
- [ ] AHCI / SATA storage driver
- [ ] VirtIO block driver
- [ ] VirtIO input support
- [ ] Real hardware boot smoke tests
- [ ] Hardware compatibility matrix

---

## Phase 12 — Real Userland & Executables

> Turn ring-3 tests into a normal application/runtime model.

- [ ] ELF executable loader
- [ ] Per-process address spaces
- [ ] User heap / brk-style allocation
- [ ] User stack allocation
- [ ] Process creation API
- [ ] Process exit/wait API
- [ ] File descriptor table
- [ ] Standard input/output/error
- [ ] Userland C runtime startup
- [ ] Userland libc subset
- [ ] Shell launches user programs
- [ ] App bundles or executable metadata
- [ ] Crash handling and process cleanup

---

## Phase 13 — System Call & POSIX-Like API

> Provide stable APIs so real programs can be ported or written cleanly.

- [ ] Syscall ABI versioning
- [ ] File syscalls: open/read/write/close/stat
- [ ] Directory syscalls: opendir/readdir/mkdir/rmdir
- [ ] Process syscalls: spawn/exit/wait/sleep
- [ ] Time syscalls
- [ ] Memory syscalls: map/unmap/protect
- [ ] Event or poll syscall
- [ ] Pipes
- [ ] Signals or structured process events
- [ ] Socket syscall surface
- [ ] Error codes and errno-compatible layer
- [ ] POSIX compatibility decision document

---

## Phase 14 — Filesystem Evolution

> Move from root-only FAT32 demos to a robust storage model.

- [ ] FAT32 subdirectory support
- [ ] Long filename support
- [ ] File metadata timestamps
- [ ] File permissions model
- [ ] Multi-file handle write safety
- [ ] Path resolver (`/`, `.`, `..`)
- [ ] Mount table
- [ ] Initrd or system volume layout
- [ ] Larger disk images
- [ ] Filesystem cache
- [ ] Journaling filesystem research
- [ ] Native Minerva filesystem design

---

## Phase 15 — Networking & Internet

> Build from local networking to usable internet applications.

- [ ] e1000 driver
- [ ] VirtIO network driver
- [ ] Ethernet frame layer
- [ ] ARP
- [ ] IPv4
- [ ] ICMP ping
- [ ] UDP
- [ ] TCP
- [ ] DHCP client
- [ ] DNS resolver
- [ ] HTTP client
- [ ] TLS research path
- [ ] Network diagnostics app

---

## Phase 16 — Application Platform

> Make Minerva useful day-to-day, not just bootable.

- [ ] Terminal app improvements
- [ ] File manager
- [ ] Settings app
- [ ] Text editor polish
- [ ] Image viewer polish
- [ ] Audio player
- [ ] Calculator
- [ ] Process/task manager
- [ ] Package browser
- [ ] App launcher/menu
- [ ] Clipboard
- [ ] App preferences storage
- [ ] Native UI toolkit
- [ ] App SDK documentation

---

## Phase 17 — Browser & Web Platform

> Long-term web browsing capability.

- [ ] URL parser
- [ ] HTTP integration
- [ ] HTML tokenizer
- [ ] DOM tree
- [ ] Basic layout engine
- [ ] Text rendering integration
- [ ] Image loading
- [ ] CSS parser
- [ ] CSS cascade and box model
- [ ] Forms and input controls
- [ ] JavaScript engine research
- [ ] Browser app shell

---

## Phase 18 — Security & Multi-User

> Build the trust model needed for a serious open OS.

- [ ] Users and groups
- [ ] Login/session model
- [ ] File ownership and permissions
- [ ] Kernel/user pointer validation
- [ ] Capability or permission model research
- [ ] Process isolation hardening
- [ ] Secure syscall validation
- [ ] Random number source
- [ ] Audit/debug logs
- [ ] Sandboxed app model

---

## Phase 19 — Developer Experience & Open Source

> Make the project easy to build, test, contribute to, and release.

- [ ] Public contribution guide
- [ ] Coding style guide
- [ ] Architecture documentation
- [ ] Build instructions for Linux/macOS/Windows/WSL
- [ ] Automated QEMU smoke tests
- [ ] Unit tests for libc/fs/kernel helpers
- [ ] CI build pipeline
- [ ] Release image generation
- [ ] Issue and PR templates
- [ ] Roadmap labels/milestones
- [ ] Devlog/release notes
- [ ] Website or project page
- [ ] License audit

---

## Phase 20 — Distribution & Installer

> Turn MinervaOS into something people can try and install.

- [ ] Bootable ISO image
- [ ] Boot menu
- [ ] Live environment
- [ ] Installer app
- [ ] Disk partitioning support
- [ ] Installed system layout
- [ ] First-run setup
- [ ] Update mechanism
- [ ] Versioned releases
- [ ] Recovery mode

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
| 6 | Applications | ✅ Complete |
| 7 | Networking | ✅ Complete |
| 8 | Browser | ✅ Complete |
| 9 | Polish & Ecosystem | 🟡 In progress |
| 10 | Modern Display & Visual System | 🟡 Started |
| 11 | Hardware Platform Support | ⬜ Long-term |
| 12 | Real Userland & Executables | ⬜ Long-term |
| 13 | System Call & POSIX-Like API | ⬜ Long-term |
| 14 | Filesystem Evolution | ⬜ Long-term |
| 15 | Networking & Internet | ⬜ Long-term |
| 16 | Application Platform | ⬜ Long-term |
| 17 | Browser & Web Platform | ⬜ Long-term |
| 18 | Security & Multi-User | ⬜ Long-term |
| 19 | Developer Experience & Open Source | ⬜ Long-term |
| 20 | Distribution & Installer | ⬜ Long-term |
