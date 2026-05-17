; kernel_entry.asm - Entry point that calls into C kernel
[BITS 32]
[GLOBAL _start]
[EXTERN kernel_main]
[EXTERN __bss_start]
[EXTERN __bss_end]

_start:
    lgdt [kernel_gdt_descriptor]
    jmp 0x08:.reload_segments

.reload_segments:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    cld
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb

    call kernel_main
    jmp $

align 8
kernel_gdt_start:
    dq 0x0000000000000000
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
kernel_gdt_end:

kernel_gdt_descriptor:
    dw kernel_gdt_end - kernel_gdt_start - 1
    dd kernel_gdt_start
