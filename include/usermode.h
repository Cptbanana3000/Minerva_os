#ifndef MINERVA_USERMODE_H
#define MINERVA_USERMODE_H

#include <stdint.h>
#include "process.h"

#define USERMODE_TEST_MAGIC 0x55534552u

void usermode_init(void);
uint32_t usermode_run_test(void);
uint32_t usermode_run_context(const user_context_t *context);
void usermode_syscall_return(void);
void usermode_fault_return(void);
uint32_t usermode_test_entry(void);
uint32_t usermode_fault_entry(void);
uint32_t usermode_test_stack_top(void);
void usermode_fill_test_context(user_context_t *context,
                                uint32_t last_syscall_eax,
                                uint32_t last_syscall_cs,
                                uint32_t syscall_count);
void usermode_fill_fault_context(user_context_t *context,
                                 uint32_t fault_vector,
                                 uint32_t fault_eip,
                                 uint32_t fault_address,
                                 uint32_t fault_error);

#endif
