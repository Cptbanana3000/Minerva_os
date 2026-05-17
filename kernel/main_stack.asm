[BITS 32]

[GLOBAL scheduler_run_on_main_stack]

; void scheduler_run_on_main_stack(void (*entry)(void), uint32_t new_esp);
; Switches ESP to new_esp (must be 16-byte aligned, points one past the
; highest valid word of the buffer) and calls entry. If entry returns,
; halts forever — main loop is not expected to return.
scheduler_run_on_main_stack:
    mov eax, [esp + 4]
    mov edx, [esp + 8]
    mov esp, edx
    call eax
.hang:
    cli
    hlt
    jmp .hang
