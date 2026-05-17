[BITS 32]

[GLOBAL scheduler_context_switch]

; void scheduler_context_switch(uint32_t *old_esp, uint32_t new_esp)
; Saves the current callee-saved register frame, stores ESP through old_esp,
; loads new_esp, restores that task's saved frame, and returns there.
scheduler_context_switch:
    push ebp
    push ebx
    push esi
    push edi

    mov eax, [esp + 20]
    mov edx, [esp + 24]
    mov [eax], esp
    mov esp, edx

    pop edi
    pop esi
    pop ebx
    pop ebp
    ret
