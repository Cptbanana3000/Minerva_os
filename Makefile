# Makefile for MyOS
# Requires: nasm, gcc (with i386 support), ld, qemu-system-i386

CC      = gcc
LD      = ld
ASM     = nasm
QEMU    = qemu-system-i386
QEMU_PATH = /usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
QEMU_ENV = env -i HOME="$(HOME)" USER="$(USER)" PATH="$(QEMU_PATH)" DISPLAY="$(DISPLAY)" WAYLAND_DISPLAY="$(WAYLAND_DISPLAY)" XAUTHORITY="$(XAUTHORITY)" XDG_RUNTIME_DIR="$(XDG_RUNTIME_DIR)" DBUS_SESSION_BUS_ADDRESS="$(DBUS_SESSION_BUS_ADDRESS)"
QEMU_NET = -netdev user,id=net0 -device e1000,netdev=net0

CFLAGS  = -m32 -Os -ffreestanding -fno-pie -fno-stack-protector -nostdlib -Wall -Wextra -c
LDFLAGS = -m elf_i386 -T linker.ld --oformat binary -nostdlib
INCLUDES = -Iinclude

all: os-image.bin fs.img

boot.bin: boot.asm
	$(ASM) -f bin boot.asm -o boot.bin

kernel_entry.o: kernel_entry.asm
	$(ASM) -f elf32 kernel_entry.asm -o kernel_entry.o

libc/string.o: libc/string.c include/libc.h
	$(CC) $(CFLAGS) $(INCLUDES) libc/string.c -o libc/string.o

drivers/vga.o: drivers/vga.c include/io.h include/vga.h
	$(CC) $(CFLAGS) $(INCLUDES) drivers/vga.c -o drivers/vga.o

drivers/keyboard.o: drivers/keyboard.c include/io.h include/keyboard.h
	$(CC) $(CFLAGS) $(INCLUDES) drivers/keyboard.c -o drivers/keyboard.o

drivers/serial.o: drivers/serial.c include/io.h include/serial.h
	$(CC) $(CFLAGS) $(INCLUDES) drivers/serial.c -o drivers/serial.o

drivers/graphics.o: drivers/graphics.c include/graphics.h
	$(CC) $(CFLAGS) $(INCLUDES) drivers/graphics.c -o drivers/graphics.o

drivers/theme.o: drivers/theme.c include/theme.h include/libc.h
	$(CC) $(CFLAGS) $(INCLUDES) drivers/theme.c -o drivers/theme.o

drivers/window.o: drivers/window.c include/window.h include/graphics.h include/memory.h include/libc.h include/theme.h
	$(CC) $(CFLAGS) $(INCLUDES) drivers/window.c -o drivers/window.o

drivers/terminal.o: drivers/terminal.c include/terminal.h include/graphics.h
	$(CC) $(CFLAGS) $(INCLUDES) drivers/terminal.c -o drivers/terminal.o

drivers/mouse.o: drivers/mouse.c include/mouse.h include/io.h include/graphics.h
	$(CC) $(CFLAGS) $(INCLUDES) drivers/mouse.c -o drivers/mouse.o

drivers/ata.o: drivers/ata.c include/ata.h include/io.h
	$(CC) $(CFLAGS) $(INCLUDES) drivers/ata.c -o drivers/ata.o

drivers/desktop.o: drivers/desktop.c include/desktop.h include/graphics.h include/window.h include/mouse.h include/theme.h
	$(CC) $(CFLAGS) $(INCLUDES) drivers/desktop.c -o drivers/desktop.o

drivers/term_window.o: drivers/term_window.c include/term_window.h include/window.h include/graphics.h include/libc.h include/serial.h include/pmm.h include/io.h include/fs.h include/text_editor.h include/image_viewer.h include/audio_player.h include/browser.h include/scheduler.h include/process.h include/gdt.h include/interrupts.h include/usermode.h include/user_scheduler.h include/e1000.h include/pci.h include/net.h include/rtc.h include/p256.h include/theme.h
	$(CC) $(CFLAGS) $(INCLUDES) drivers/term_window.c -o drivers/term_window.o

drivers/text_editor.o: drivers/text_editor.c include/text_editor.h include/window.h include/graphics.h include/libc.h include/fs.h
	$(CC) $(CFLAGS) $(INCLUDES) drivers/text_editor.c -o drivers/text_editor.o

drivers/image_viewer.o: drivers/image_viewer.c include/image_viewer.h include/window.h include/graphics.h include/libc.h include/fs.h
	$(CC) $(CFLAGS) $(INCLUDES) drivers/image_viewer.c -o drivers/image_viewer.o

drivers/audio_player.o: drivers/audio_player.c include/audio_player.h include/window.h include/graphics.h include/libc.h include/fs.h include/speaker.h
	$(CC) $(CFLAGS) $(INCLUDES) drivers/audio_player.c -o drivers/audio_player.o

drivers/browser.o: drivers/browser.c include/browser.h include/window.h include/graphics.h include/net.h
	$(CC) $(CFLAGS) $(INCLUDES) drivers/browser.c -o drivers/browser.o

drivers/speaker.o: drivers/speaker.c include/speaker.h include/io.h include/interrupts.h
	$(CC) $(CFLAGS) $(INCLUDES) drivers/speaker.c -o drivers/speaker.o

drivers/pci.o: drivers/pci.c include/pci.h include/io.h
	$(CC) $(CFLAGS) $(INCLUDES) drivers/pci.c -o drivers/pci.o

drivers/e1000.o: drivers/e1000.c include/e1000.h include/pci.h include/paging.h include/serial.h include/libc.h
	$(CC) $(CFLAGS) $(INCLUDES) drivers/e1000.c -o drivers/e1000.o

drivers/net.o: drivers/net.c include/net.h include/e1000.h include/libc.h include/sha256.h include/p256.h
	$(CC) $(CFLAGS) $(INCLUDES) drivers/net.c -o drivers/net.o

drivers/sha256.o: drivers/sha256.c include/sha256.h
	$(CC) $(CFLAGS) $(INCLUDES) drivers/sha256.c -o drivers/sha256.o

drivers/p256.o: drivers/p256.c include/p256.h include/libc.h
	$(CC) $(CFLAGS) $(INCLUDES) drivers/p256.c -o drivers/p256.o

drivers/rtc.o: drivers/rtc.c include/rtc.h include/io.h
	$(CC) $(CFLAGS) $(INCLUDES) drivers/rtc.c -o drivers/rtc.o

fs/fat32.o: fs/fat32.c include/fs.h include/ata.h include/libc.h
	$(CC) $(CFLAGS) $(INCLUDES) fs/fat32.c -o fs/fat32.o

kernel/scheduler.o: kernel/scheduler.c include/scheduler.h include/libc.h
	$(CC) $(CFLAGS) $(INCLUDES) kernel/scheduler.c -o kernel/scheduler.o

kernel/process.o: kernel/process.c include/process.h include/libc.h
	$(CC) $(CFLAGS) $(INCLUDES) kernel/process.c -o kernel/process.o

kernel/gdt.o: kernel/gdt.c include/gdt.h include/libc.h
	$(CC) $(CFLAGS) $(INCLUDES) kernel/gdt.c -o kernel/gdt.o

kernel/gdt_asm.o: kernel/gdt.asm
	$(ASM) -f elf32 kernel/gdt.asm -o kernel/gdt_asm.o

kernel/usermode.o: kernel/usermode.c include/usermode.h include/paging.h
	$(CC) $(CFLAGS) $(INCLUDES) kernel/usermode.c -o kernel/usermode.o

kernel/user_scheduler.o: kernel/user_scheduler.c include/user_scheduler.h include/process.h include/usermode.h include/interrupts.h include/scheduler.h
	$(CC) $(CFLAGS) $(INCLUDES) kernel/user_scheduler.c -o kernel/user_scheduler.o

kernel/usermode_asm.o: kernel/usermode.asm
	$(ASM) -f elf32 kernel/usermode.asm -o kernel/usermode_asm.o

kernel/switch.o: kernel/switch.asm
	$(ASM) -f elf32 kernel/switch.asm -o kernel/switch.o

kernel/main_stack.o: kernel/main_stack.asm
	$(ASM) -f elf32 kernel/main_stack.asm -o kernel/main_stack.o

memory/allocator.o: memory/allocator.c include/memory.h
	$(CC) $(CFLAGS) $(INCLUDES) memory/allocator.c -o memory/allocator.o

memory/pmm.o: memory/pmm.c include/pmm.h
	$(CC) $(CFLAGS) $(INCLUDES) memory/pmm.c -o memory/pmm.o

memory/paging.o: memory/paging.c include/paging.h include/pmm.h include/libc.h
	$(CC) $(CFLAGS) $(INCLUDES) memory/paging.c -o memory/paging.o

interrupts/interrupts.o: interrupts/interrupts.c include/interrupts.h include/io.h include/vga.h include/gdt.h include/usermode.h
	$(CC) $(CFLAGS) $(INCLUDES) interrupts/interrupts.c -o interrupts/interrupts.o

interrupts/pit.o: interrupts/pit.c include/interrupts.h include/io.h include/scheduler.h
	$(CC) $(CFLAGS) $(INCLUDES) interrupts/pit.c -o interrupts/pit.o

kernel.o: kernel.c include/io.h include/keyboard.h include/interrupts.h include/libc.h include/memory.h include/serial.h include/graphics.h include/window.h include/desktop.h include/term_window.h include/text_editor.h include/image_viewer.h include/audio_player.h include/browser.h include/mouse.h include/pmm.h include/paging.h include/fs.h include/scheduler.h include/process.h include/gdt.h include/usermode.h include/user_scheduler.h include/e1000.h include/net.h
	$(CC) $(CFLAGS) $(INCLUDES) kernel.c -o kernel.o

interrupts/isr.o: interrupts/isr.asm
	$(ASM) -f elf32 interrupts/isr.asm -o interrupts/isr.o

kernel.bin: kernel_entry.o interrupts/isr.o libc/string.o drivers/vga.o drivers/keyboard.o drivers/serial.o drivers/graphics.o drivers/theme.o drivers/window.o drivers/terminal.o drivers/desktop.o drivers/term_window.o drivers/text_editor.o drivers/image_viewer.o drivers/audio_player.o drivers/browser.o drivers/speaker.o drivers/pci.o drivers/e1000.o drivers/net.o drivers/sha256.o drivers/p256.o drivers/rtc.o drivers/ata.o fs/fat32.o kernel/scheduler.o kernel/process.o kernel/gdt.o kernel/gdt_asm.o kernel/usermode.o kernel/user_scheduler.o kernel/usermode_asm.o kernel/switch.o kernel/main_stack.o memory/allocator.o memory/pmm.o memory/paging.o interrupts/interrupts.o interrupts/pit.o kernel.o linker.ld drivers/mouse.o
	$(LD) $(LDFLAGS) -o kernel.bin kernel_entry.o interrupts/isr.o libc/string.o drivers/vga.o drivers/keyboard.o drivers/serial.o drivers/graphics.o drivers/theme.o drivers/window.o drivers/terminal.o drivers/desktop.o drivers/term_window.o drivers/text_editor.o drivers/image_viewer.o drivers/audio_player.o drivers/browser.o drivers/speaker.o drivers/pci.o drivers/e1000.o drivers/net.o drivers/sha256.o drivers/p256.o drivers/rtc.o drivers/ata.o fs/fat32.o kernel/scheduler.o kernel/process.o kernel/gdt.o kernel/gdt_asm.o kernel/usermode.o kernel/user_scheduler.o kernel/usermode_asm.o kernel/switch.o kernel/main_stack.o memory/allocator.o memory/pmm.o memory/paging.o interrupts/interrupts.o interrupts/pit.o kernel.o drivers/mouse.o
	$(LD) -m elf_i386 -T linker.ld -nostdlib -o kernel.elf kernel_entry.o interrupts/isr.o libc/string.o drivers/vga.o drivers/keyboard.o drivers/serial.o drivers/graphics.o drivers/theme.o drivers/window.o drivers/terminal.o drivers/desktop.o drivers/term_window.o drivers/text_editor.o drivers/image_viewer.o drivers/audio_player.o drivers/browser.o drivers/speaker.o drivers/pci.o drivers/e1000.o drivers/net.o drivers/sha256.o drivers/p256.o drivers/rtc.o drivers/ata.o fs/fat32.o kernel/scheduler.o kernel/process.o kernel/gdt.o kernel/gdt_asm.o kernel/usermode.o kernel/user_scheduler.o kernel/usermode_asm.o kernel/switch.o kernel/main_stack.o memory/allocator.o memory/pmm.o memory/paging.o interrupts/interrupts.o interrupts/pit.o kernel.o drivers/mouse.o
	@test $$(wc -c < kernel.bin) -le 196608 || { echo "kernel.bin too large for bootloader load window"; exit 1; }
	@python3 -c 'import subprocess, sys; syms = {p[2]: int(p[0], 16) for p in (line.split() for line in subprocess.check_output(["nm", "kernel.elf"], text=True).splitlines()) if len(p) >= 3}; end = syms.get("__kernel_end", 0); print("kernel end: 0x%05x" % end); sys.exit(0 if end <= 0x98000 else "kernel image+bss too close to bootstrap stack")'

os-image.bin: boot.bin kernel.bin
	cat boot.bin kernel.bin > os-image.bin
	# Pad to a 1.44 MB floppy image so BIOS CHS geometry is predictable.
	truncate -s 1474560 os-image.bin

fs.img: scripts/make_fat32_image.py
	python3 scripts/make_fat32_image.py fs.img

run: os-image.bin fs.img
	$(QEMU_ENV) $(QEMU) -drive file=os-image.bin,format=raw,if=floppy -drive file=fs.img,format=raw,if=ide,index=0 $(QEMU_NET) -boot a -no-reboot -no-shutdown

smoke: os-image.bin fs.img
	sh scripts/smoke_qemu.sh

clean:
	rm -f *.bin *.o libc/*.o drivers/*.o fs/*.o kernel/*.o interrupts/*.o memory/*.o os-image.bin fs.img

.PHONY: all run smoke clean
