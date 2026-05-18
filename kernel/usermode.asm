[BITS 32]

[GLOBAL usermode_run_test]
[GLOBAL usermode_run_context]
[GLOBAL usermode_syscall_return]
[GLOBAL usermode_fault_return]
[GLOBAL user_test_entry]
[GLOBAL user_fault_entry]
[GLOBAL user_stack]
[GLOBAL user_stack_top]

USER_CODE equ 0x1B
USER_DATA equ 0x23
KERNEL_DATA equ 0x10
USER_MAGIC equ 0x55534552

section .text

; uint32_t usermode_run_test(void)
; Enters a tiny ring-3 stub. The syscall handler redirects int 0x80 back to
; usermode_syscall_return, which restores this saved ESP and returns to C.
usermode_run_test:
    cli
    mov [saved_kernel_esp], esp

    mov ax, USER_DATA
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push dword USER_DATA
    push dword user_stack_top
    push dword 0x202
    push dword USER_CODE
    push dword user_test_entry
    iretd

; uint32_t usermode_run_context(const user_context_t *context)
; Restores a prepared ring-3 iret frame. Field offsets are defined by
; user_context_t: eip, esp, cs, ss, eflags.
usermode_run_context:
    cli
    mov [saved_kernel_esp], esp
    mov edi, [esp + 4]

    mov ax, [edi + 12]
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push dword [edi + 12]
    push dword [edi + 4]
    push dword [edi + 16]
    push dword [edi + 8]
    push dword [edi]
    iretd

; Entered by the ISR epilogue after the syscall handler rewrites the saved
; frame to kernel CS:EIP. Interrupts are off until the original kernel stack is
; restored, closing the tiny landing-pad race.
usermode_syscall_return:
    mov ax, KERNEL_DATA
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov esp, [saved_kernel_esp]
    mov eax, 1
    sti
    ret

usermode_fault_return:
    mov ax, KERNEL_DATA
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov esp, [saved_kernel_esp]
    mov eax, 2
    sti
    ret

section .usertext align=4096
user_test_entry:
    mov eax, USER_MAGIC
    int 0x80
.hang:
    jmp .hang

user_fault_entry:
    mov eax, [0x00008000]
    mov eax, USER_MAGIC
    int 0x80
.fault_hang:
    jmp .fault_hang

section .bss
alignb 16
saved_kernel_esp:
    resd 1
alignb 4096
user_stack:
    resb 4096
user_stack_top:
