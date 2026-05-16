# Makefile for MyOS
# Requires: nasm, gcc (with i386 support), ld, qemu-system-i386

CC      = gcc
LD      = ld
ASM     = nasm

CFLAGS  = -m32 -ffreestanding -fno-pie -fno-stack-protector -nostdlib -Wall -Wextra -c
LDFLAGS = -m elf_i386 -T linker.ld --oformat binary -nostdlib
INCLUDES = -Iinclude

all: os-image.bin

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

drivers/window.o: drivers/window.c include/window.h include/graphics.h include/memory.h include/libc.h
	$(CC) $(CFLAGS) $(INCLUDES) drivers/window.c -o drivers/window.o

drivers/terminal.o: drivers/terminal.c include/terminal.h include/graphics.h
	$(CC) $(CFLAGS) $(INCLUDES) drivers/terminal.c -o drivers/terminal.o

drivers/mouse.o: drivers/mouse.c include/mouse.h include/io.h include/graphics.h
	$(CC) $(CFLAGS) $(INCLUDES) drivers/mouse.c -o drivers/mouse.o

memory/allocator.o: memory/allocator.c include/memory.h
	$(CC) $(CFLAGS) $(INCLUDES) memory/allocator.c -o memory/allocator.o

memory/pmm.o: memory/pmm.c include/pmm.h
	$(CC) $(CFLAGS) $(INCLUDES) memory/pmm.c -o memory/pmm.o

memory/paging.o: memory/paging.c include/paging.h include/pmm.h include/libc.h
	$(CC) $(CFLAGS) $(INCLUDES) memory/paging.c -o memory/paging.o

interrupts/interrupts.o: interrupts/interrupts.c include/interrupts.h include/io.h include/vga.h
	$(CC) $(CFLAGS) $(INCLUDES) interrupts/interrupts.c -o interrupts/interrupts.o

interrupts/pit.o: interrupts/pit.c include/interrupts.h include/io.h
	$(CC) $(CFLAGS) $(INCLUDES) interrupts/pit.c -o interrupts/pit.o

kernel.o: kernel.c include/io.h include/keyboard.h include/interrupts.h include/libc.h include/memory.h include/serial.h include/vga.h include/graphics.h include/window.h include/terminal.h include/mouse.h
	$(CC) $(CFLAGS) $(INCLUDES) kernel.c -o kernel.o

interrupts/isr.o: interrupts/isr.asm
	$(ASM) -f elf32 interrupts/isr.asm -o interrupts/isr.o

kernel.bin: kernel_entry.o interrupts/isr.o libc/string.o drivers/vga.o drivers/keyboard.o drivers/serial.o drivers/graphics.o drivers/window.o drivers/terminal.o memory/allocator.o memory/pmm.o memory/paging.o interrupts/interrupts.o interrupts/pit.o kernel.o linker.ld drivers/mouse.o
	$(LD) $(LDFLAGS) -o kernel.bin kernel_entry.o interrupts/isr.o libc/string.o drivers/vga.o drivers/keyboard.o drivers/serial.o drivers/graphics.o drivers/window.o drivers/terminal.o memory/allocator.o memory/pmm.o memory/paging.o interrupts/interrupts.o interrupts/pit.o kernel.o drivers/mouse.o
	
os-image.bin: boot.bin kernel.bin
	cat boot.bin kernel.bin > os-image.bin
	# Pad to a sensible size (16 sectors after boot = ~8KB total minimum)
	truncate -s 40960 os-image.bin

run: os-image.bin
	qemu-system-i386 -drive format=raw,file=os-image.bin

clean:
	rm -f *.bin *.o libc/*.o drivers/*.o interrupts/*.o memory/*.o os-image.bin

.PHONY: all run clean
