#include <stdint.h>
#include "paging.h"
#include "usermode.h"

extern uint8_t user_test_entry;
extern uint8_t user_fault_entry;
extern uint8_t user_stack;

static uint32_t page_base(uint32_t address) {
    return address & ~0xFFFu;
}

void usermode_init(void) {
    uint32_t code_page = page_base((uint32_t)&user_test_entry);
    uint32_t fault_code_page = page_base((uint32_t)&user_fault_entry);
    uint32_t stack_page = page_base((uint32_t)&user_stack);

    paging_map(code_page, code_page, PAGE_WRITABLE | PAGE_USER);
    if (fault_code_page != code_page) {
        paging_map(fault_code_page, fault_code_page, PAGE_WRITABLE | PAGE_USER);
    }
    paging_map(stack_page, stack_page, PAGE_WRITABLE | PAGE_USER);
}

uint32_t usermode_test_entry(void) {
    return (uint32_t)&user_test_entry;
}

uint32_t usermode_fault_entry(void) {
    return (uint32_t)&user_fault_entry;
}

uint32_t usermode_test_stack_top(void) {
    extern uint8_t user_stack_top;
    return (uint32_t)&user_stack_top;
}

static void usermode_fill_context(user_context_t *context,
                                  uint32_t eip,
                                  uint32_t last_syscall_eax,
                                  uint32_t last_syscall_cs,
                                  uint32_t syscall_count,
                                  uint32_t fault_vector,
                                  uint32_t fault_eip,
                                  uint32_t fault_address,
                                  uint32_t fault_error) {
    if (!context) return;

    context->eip = eip;
    context->esp = usermode_test_stack_top();
    context->cs = 0x1Bu;
    context->ss = 0x23u;
    context->eflags = 0x202u;
    context->last_syscall_eax = last_syscall_eax;
    context->last_syscall_cs = last_syscall_cs;
    context->syscall_count = syscall_count;
    context->fault_vector = fault_vector;
    context->fault_eip = fault_eip;
    context->fault_address = fault_address;
    context->fault_error = fault_error;
}

void usermode_fill_test_context(user_context_t *context,
                                uint32_t last_syscall_eax,
                                uint32_t last_syscall_cs,
                                uint32_t syscall_count) {
    usermode_fill_context(context,
                          usermode_test_entry(),
                          last_syscall_eax,
                          last_syscall_cs,
                          syscall_count,
                          0,
                          0,
                          0,
                          0);
}

void usermode_fill_fault_context(user_context_t *context,
                                 uint32_t fault_vector,
                                 uint32_t fault_eip,
                                 uint32_t fault_address,
                                 uint32_t fault_error) {
    usermode_fill_context(context,
                          usermode_fault_entry(),
                          0,
                          0,
                          0,
                          fault_vector,
                          fault_eip,
                          fault_address,
                          fault_error);
}
