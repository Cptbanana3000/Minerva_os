# Makefile for MyOS
# Requires: nasm, gcc (with i386 support), ld, qemu-system-i386

CC      = gcc
LD      = ld
ASM     = nasm

CFLAGS  = -m32 -ffreestanding -fno-pie -fno-stack-protector -nostdlib -Wall -Wextra -c
LDFLAGS = -m elf_i386 -T linker.ld --oformat binary -nostdlib

all: os-image.bin

boot.bin: boot.asm
	$(ASM) -f bin boot.asm -o boot.bin

kernel_entry.o: kernel_entry.asm
	$(ASM) -f elf32 kernel_entry.asm -o kernel_entry.o

kernel.o: kernel.c
	$(CC) $(CFLAGS) kernel.c -o kernel.o

kernel.bin: kernel_entry.o kernel.o linker.ld
	$(LD) $(LDFLAGS) -o kernel.bin kernel_entry.o kernel.o

os-image.bin: boot.bin kernel.bin
	cat boot.bin kernel.bin > os-image.bin
	# Pad to a sensible size (16 sectors after boot = ~8KB total minimum)
	truncate -s 32256 os-image.bin

run: os-image.bin
	qemu-system-i386 -drive format=raw,file=os-image.bin

clean:
	rm -f *.bin *.o

.PHONY: all run clean
