[BITS 32]

[GLOBAL gdt_load]
[GLOBAL tss_load]

; void gdt_load(gdt_ptr_t *gdt_ptr)
gdt_load:
    mov eax, [esp + 4]
    lgdt [eax]
    jmp 0x08:.reload_segments

.reload_segments:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ret

; void tss_load(uint16_t selector)
tss_load:
    mov ax, [esp + 4]
    ltr ax
    ret
