[BITS 32]

[GLOBAL scheduler_context_switch]
[GLOBAL scheduler_context_switch_iret]
[GLOBAL scheduler_context_switch_update_irq]
[GLOBAL scheduler_context_switch_iret_update_irq]
[GLOBAL scheduler_resume_from_saved_esp]

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

; void scheduler_context_switch_update_irq(uint32_t *old_esp,
;                                          uint32_t resume_irq_esp,
;                                          uint32_t new_esp)
; Cooperative switch that also refreshes the synthetic IRQ resume frame.
; The frame restores EAX into scheduler_resume_from_saved_esp, which then
; jumps back through the saved cooperative stack.
scheduler_context_switch_update_irq:
    push ebp
    push ebx
    push esi
    push edi

    mov eax, [esp + 20]
    mov edx, [esp + 24]
    mov ecx, [esp + 28]
    mov [eax], esp
    test edx, edx
    jz .no_resume_update
    mov [edx + 28], esp
.no_resume_update:
    mov esp, ecx

    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

; void scheduler_context_switch_iret(uint32_t *old_esp, uint32_t new_esp)
; Same caller-save semantics as the cooperative switch, but the destination
; ESP must point at a valid interrupt_frame_t (the layout pushed by the ISR
; common stub). Executes the ISR epilogue inline, landing back in whatever
; context that frame represents via iretd.
scheduler_context_switch_iret:
    push ebp
    push ebx
    push esi
    push edi

    mov eax, [esp + 20]
    mov edx, [esp + 24]
    mov [eax], esp
    mov esp, edx

    popa
    pop gs
    pop fs
    pop es
    pop ds
    add esp, 8
    iretd

; void scheduler_context_switch_iret_update_irq(uint32_t *old_esp,
;                                               uint32_t resume_irq_esp,
;                                               uint32_t new_esp)
; Saves the task's cooperative ESP, refreshes its synthetic IRQ resume
; frame, then restores the destination interrupt frame with iretd.
scheduler_context_switch_iret_update_irq:
    push ebp
    push ebx
    push esi
    push edi

    mov eax, [esp + 20]
    mov edx, [esp + 24]
    mov ecx, [esp + 28]
    mov [eax], esp
    test edx, edx
    jz .no_iret_resume_update
    mov [edx + 28], esp
.no_iret_resume_update:
    mov esp, ecx

    popa
    pop gs
    pop fs
    pop es
    pop ds
    add esp, 8
    iretd

; Entered by a synthetic interrupt frame. PUSHA restoration leaves the
; saved cooperative ESP in EAX.
scheduler_resume_from_saved_esp:
    mov esp, eax
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret
