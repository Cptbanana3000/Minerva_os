#include <stdint.h>
#include "scheduler.h"
#include "libc.h"

typedef struct {
    uint32_t id;
    const char *name;
    sched_task_fn_t entry;
    void *ctx;
    uint32_t esp;
    uint32_t runs;
    uint8_t active;
} sched_task_t;

static sched_task_t tasks[SCHED_MAX_TASKS];
static uint32_t task_stacks[SCHED_MAX_TASKS][256];
static uint32_t task_count = 0;
static int current_task = -1;
static uint32_t main_esp = 0;
static uint32_t ticks_in_quantum = 0;
static uint32_t switch_count = 0;
static uint32_t timer_request_count = 0;
static uint8_t switch_pending = 0;
static uint8_t in_task = 0;

extern void scheduler_context_switch(uint32_t *old_esp, uint32_t new_esp);

static void scheduler_task_trampoline(void) {
    while (1) {
        if (current_task >= 0 &&
            tasks[current_task].active &&
            tasks[current_task].entry) {
            tasks[current_task].runs++;
            tasks[current_task].entry(tasks[current_task].ctx);
        }

        scheduler_yield();
    }
}

static uint32_t make_initial_esp(uint32_t id) {
    uint32_t *sp = task_stacks[id] + 256;

    *--sp = (uint32_t)scheduler_task_trampoline;
    *--sp = 0;  /* ebp */
    *--sp = 0;  /* ebx */
    *--sp = 0;  /* esi */
    *--sp = 0;  /* edi */
    return (uint32_t)sp;
}

void scheduler_init(void) {
    memset(tasks, 0, sizeof(tasks));
    memset(task_stacks, 0, sizeof(task_stacks));
    task_count = 0;
    current_task = -1;
    main_esp = 0;
    ticks_in_quantum = 0;
    switch_count = 0;
    timer_request_count = 0;
    switch_pending = 0;
    in_task = 0;
}

int scheduler_create_kernel_task(const char *name, sched_task_fn_t entry, void *ctx) {
    if (!name || !entry || task_count >= SCHED_MAX_TASKS) return -1;

    uint32_t id = task_count;
    tasks[id].id = id;
    tasks[id].name = name;
    tasks[id].entry = entry;
    tasks[id].ctx = ctx;
    tasks[id].esp = make_initial_esp(id);
    tasks[id].runs = 0;
    tasks[id].active = 1;
    task_count++;

    if (current_task < 0) current_task = 0;
    return (int)id;
}

void scheduler_tick(void) {
    if (task_count == 0) return;

    ticks_in_quantum++;
    if (ticks_in_quantum >= 10) {
        ticks_in_quantum = 0;
        switch_pending = 1;
        timer_request_count++;
    }
}

void scheduler_poll(void) {
    if (!switch_pending || task_count == 0) return;
    switch_pending = 0;

    current_task++;
    if (current_task >= (int)task_count) current_task = 0;

    if (!tasks[current_task].active || !tasks[current_task].entry) return;
    switch_count++;
    in_task = 1;
    scheduler_context_switch(&main_esp, tasks[current_task].esp);
    in_task = 0;
}

void scheduler_yield(void) {
    if (!in_task || current_task < 0) return;
    scheduler_context_switch(&tasks[current_task].esp, main_esp);
}

uint32_t scheduler_task_count(void) {
    return task_count;
}

uint32_t scheduler_current_task_id(void) {
    if (current_task < 0) return 0xFFFFFFFFu;
    return (uint32_t)current_task;
}

const char *scheduler_current_task_name(void) {
    if (current_task < 0) return "none";
    return tasks[current_task].name;
}

uint32_t scheduler_switch_count(void) {
    return switch_count;
}

uint32_t scheduler_timer_request_count(void) {
    return timer_request_count;
}

void scheduler_list(sched_list_cb_t cb, void *ctx) {
    if (!cb) return;

    for (uint32_t i = 0; i < task_count; i++) {
        if (tasks[i].active) cb(tasks[i].id, tasks[i].name, tasks[i].runs, ctx);
    }
}
