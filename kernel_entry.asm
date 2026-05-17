; kernel_entry.asm - Entry point that calls into C kernel
[BITS 32]
[GLOBAL _start]
[EXTERN kernel_main]

_start:
    call kernel_main
    jmp $
