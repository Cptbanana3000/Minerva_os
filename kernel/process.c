#include <stdint.h>
#include "process.h"
#include "libc.h"

static process_t processes[PROCESS_MAX];
static uint32_t next_pid = 1;
static uint32_t live_count = 0;

static process_t *process_find_user(const char *name) {
    if (!name) return 0;

    for (uint32_t i = 0; i < PROCESS_MAX; i++) {
        if (processes[i].used &&
            !processes[i].kernel &&
            strcmp(processes[i].name, name) == 0) {
            return &processes[i];
        }
    }

    return 0;
}

static process_t *process_alloc(void) {
    for (uint32_t i = 0; i < PROCESS_MAX; i++) {
        if (!processes[i].used) return &processes[i];
    }

    return 0;
}

void process_init(void) {
    memset(processes, 0, sizeof(processes));
    next_pid = 1;
    live_count = 0;
}

int process_create_kernel(const char *name, uint32_t task_id, uint32_t parent_pid) {
    if (!name) return -1;

    process_t *process = process_alloc();
    if (!process) return -1;

    process->pid = next_pid++;
    process->parent_pid = parent_pid;
    process->task_id = task_id;
    memset(&process->user_context, 0, sizeof(process->user_context));
    process->name = name;
    process->state = PROCESS_READY;
    process->used = 1;
    process->kernel = 1;
    live_count++;
    return (int)process->pid;
}

int process_record_user(const char *name,
                        const user_context_t *user_context,
                        process_state_t state) {
    if (!name || !user_context) return -1;

    process_t *process = process_find_user(name);
    if (!process) {
        process = process_alloc();
        if (!process) return -1;
        process->pid = next_pid++;
        process->parent_pid = 0;
        process->task_id = PROCESS_NO_TASK;
        process->name = name;
        process->used = 1;
        process->kernel = 0;
        live_count++;
    }

    process->user_context = *user_context;
    process->state = state;
    return (int)process->pid;
}

int process_get_user_context(const char *name, user_context_t *out_context) {
    if (!name || !out_context) return 0;

    process_t *process = process_find_user(name);
    if (!process) return 0;

    *out_context = process->user_context;
    return 1;
}

int process_get_ready_user(const char **out_name,
                           uint32_t *out_pid,
                           user_context_t *out_context) {
    if (!out_name || !out_pid || !out_context) return 0;

    for (uint32_t i = 0; i < PROCESS_MAX; i++) {
        if (processes[i].used &&
            !processes[i].kernel &&
            processes[i].state == PROCESS_READY) {
            *out_name = processes[i].name;
            *out_pid = processes[i].pid;
            *out_context = processes[i].user_context;
            return 1;
        }
    }

    return 0;
}

uint32_t process_count(void) {
    return live_count;
}

const char *process_state_name(process_state_t state) {
    switch (state) {
        case PROCESS_READY: return "ready";
        case PROCESS_RUNNING: return "run";
        case PROCESS_BLOCKED: return "block";
        case PROCESS_ZOMBIE: return "zombie";
        default: return "unused";
    }
}

void process_list(process_list_cb_t cb, void *ctx) {
    if (!cb) return;

    for (uint32_t i = 0; i < PROCESS_MAX; i++) {
        if (processes[i].used) cb(&processes[i], ctx);
    }
}
