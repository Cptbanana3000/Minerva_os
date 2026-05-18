#ifndef MINERVA_PROCESS_H
#define MINERVA_PROCESS_H

#include <stdint.h>

#define PROCESS_MAX 16
#define PROCESS_NO_TASK 0xFFFFFFFFu

typedef enum {
    PROCESS_UNUSED = 0,
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_BLOCKED,
    PROCESS_ZOMBIE
} process_state_t;

typedef struct {
    uint32_t eip;
    uint32_t esp;
    uint32_t cs;
    uint32_t ss;
    uint32_t eflags;
    uint32_t last_syscall_eax;
    uint32_t last_syscall_cs;
    uint32_t syscall_count;
    uint32_t fault_vector;
    uint32_t fault_eip;
    uint32_t fault_address;
    uint32_t fault_error;
} user_context_t;

typedef struct {
    uint32_t pid;
    uint32_t parent_pid;
    uint32_t task_id;
    user_context_t user_context;
    const char *name;
    process_state_t state;
    uint8_t used;
    uint8_t kernel;
} process_t;

typedef void (*process_list_cb_t)(const process_t *process, void *ctx);

void process_init(void);
int process_create_kernel(const char *name, uint32_t task_id, uint32_t parent_pid);
int process_record_user(const char *name,
                        const user_context_t *user_context,
                        process_state_t state);
int process_get_user_context(const char *name, user_context_t *out_context);
int process_get_ready_user(const char **out_name,
                           uint32_t *out_pid,
                           user_context_t *out_context);
uint32_t process_count(void);
const char *process_state_name(process_state_t state);
void process_list(process_list_cb_t cb, void *ctx);

#endif
