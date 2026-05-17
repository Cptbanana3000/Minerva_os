; boot.asm - Minimal bootloader
; Loads kernel from disk, switches to 32-bit protected mode, jumps to kernel.

[BITS 16]
[ORG 0x7C00]

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    ; Save boot drive
    mov [boot_drive], dl

    ; Print boot message
    mov si, msg_boot
    call print_string

    ; Load kernel from disk to 0x8000.
    ; Read one sector at a time so BIOSes that reject cross-track reads
    ; still load the whole kernel reliably.
    mov word [load_seg], 0x0800
    mov word [load_dest], 0x0000
    mov word [load_count], KERNEL_SECTORS
    mov byte [load_cyl], 0
    mov byte [load_head], 0
    mov byte [load_sec], 2       ; sector 1 is the boot sector

.load_kernel:
    mov ah, 0x02                 ; BIOS read sectors
    mov al, 1
    mov ch, [load_cyl]
    mov cl, [load_sec]
    mov dh, [load_head]
    mov dl, [boot_drive]
    mov bx, [load_seg]
    mov es, bx
    mov bx, [load_dest]          ; ES:BX = current load address
    int 0x13
    jc disk_error

    add word [load_dest], 512
    jnc .advance_chs

    mov ax, [load_seg]
    add ax, 0x1000
    mov [load_seg], ax

.advance_chs:
    inc byte [load_sec]
    cmp byte [load_sec], 19      ; 1.44M floppy: sectors 1..18
    jb .next_sector

    mov byte [load_sec], 1
    inc byte [load_head]
    cmp byte [load_head], 2
    jb .next_sector

    mov byte [load_head], 0
    inc byte [load_cyl]

.next_sector:
    dec word [load_count]
    jnz .load_kernel

    mov si, msg_loaded
    call print_string

     ; Switch to VGA mode 13h (320x200, 256 colors)
    mov ax, 0x0013
    int 0x10

    ; Switch to protected mode
    cli
    lgdt [gdt_descriptor]

    mov eax, cr0
    or eax, 1
    mov cr0, eax

    jmp CODE_SEG:init_pm

disk_error:
    mov si, msg_disk_err
    call print_string
    jmp $

; Print null-terminated string in SI
print_string:
    pusha
    mov ah, 0x0E
.next:
    lodsb
    test al, al
    jz .done
    int 0x10
    jmp .next
.done:
    popa
    ret

[BITS 32]
init_pm:
    mov ax, DATA_SEG
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ebp, 0x90000
    mov esp, ebp

    ; Jump to kernel
    jmp 0x8000

; ==== GDT ====
gdt_start:
    dq 0x0          ; Null descriptor

gdt_code:           ; Code segment: base=0, limit=4GB, exec/read
    dw 0xFFFF
    dw 0x0
    db 0x0
    db 10011010b
    db 11001111b
    db 0x0

gdt_data:           ; Data segment: base=0, limit=4GB, read/write
    dw 0xFFFF
    dw 0x0
    db 0x0
    db 10010010b
    db 11001111b
    db 0x0

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start

; ==== Data ====
boot_drive:   db 0
load_seg:     dw 0
load_dest:    dw 0
load_count:   dw 0
load_cyl:     db 0
load_head:    db 0
load_sec:     db 0
msg_boot:     db "Booting MyOS...", 13, 10, 0
msg_loaded:   db "Kernel loaded.", 13, 10, 0
msg_disk_err: db "Disk error!", 13, 10, 0

; Load up to 128 KiB at 0x8000. This stays below the 0x90000 stack.
KERNEL_SECTORS equ 256

; Pad to 510 bytes and add boot signature
times 510 - ($ - $$) db 0
dw 0xAA55
