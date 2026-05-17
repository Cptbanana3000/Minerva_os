#include <stdint.h>
#include "scheduler.h"
#include "interrupts.h"
#include "libc.h"

typedef struct {
    uint32_t id;
    const char *name;
    sched_task_fn_t entry;
    void *ctx;
    uint32_t esp;
    uint32_t irq_esp;
    uint32_t runs;
    uint8_t active;
} sched_task_t;

static sched_task_t tasks[SCHED_MAX_TASKS];
static uint32_t task_stacks[SCHED_MAX_TASKS][256];
static uint32_t irq_stacks[SCHED_MAX_TASKS][256];
static uint32_t task_count = 0;
static int current_task = -1;
static uint32_t main_esp = 0;
static uint32_t ticks_in_quantum = 0;
static uint32_t switch_count = 0;
static uint32_t timer_request_count = 0;
static uint32_t irq_frame_count = 0;
static uint32_t irq_context_count = 0;
static uint32_t irq_candidate_count = 0;
static uint32_t irq_preempt_switch_count = 0;
static uint32_t irq_preempt_blocked_count = 0;
static uint32_t last_irq_eip = 0;
static uint8_t switch_pending = 0;
static uint8_t irq_candidate_ready = 0;
static uint8_t preemptive_enabled = SCHED_PREEMPTIVE_ENABLED;
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

static uint32_t make_initial_irq_esp(uint32_t id) {
    interrupt_frame_t *frame =
        (interrupt_frame_t*)(irq_stacks[id] + 256) - 1;

    memset(frame, 0, sizeof(interrupt_frame_t));
    frame->gs = 0x10;
    frame->fs = 0x10;
    frame->es = 0x10;
    frame->ds = 0x10;
    frame->int_no = 32;
    frame->eip = (uint32_t)scheduler_task_trampoline;
    frame->cs = 0x08;
    frame->eflags = 0x202;
    return (uint32_t)frame;
}

static int scheduler_next_active_irq_task(void) {
    if (task_count == 0) return -1;

    int next = current_task;
    for (uint32_t i = 0; i < task_count; i++) {
        next++;
        if (next >= (int)task_count) next = 0;

        if (tasks[next].active && tasks[next].entry && tasks[next].irq_esp) {
            return next;
        }
    }

    return -1;
}

void scheduler_init(void) {
    memset(tasks, 0, sizeof(tasks));
    memset(task_stacks, 0, sizeof(task_stacks));
    memset(irq_stacks, 0, sizeof(irq_stacks));
    task_count = 0;
    current_task = -1;
    main_esp = 0;
    ticks_in_quantum = 0;
    switch_count = 0;
    timer_request_count = 0;
    irq_frame_count = 0;
    irq_context_count = 0;
    irq_candidate_count = 0;
    irq_preempt_switch_count = 0;
    irq_preempt_blocked_count = 0;
    last_irq_eip = 0;
    switch_pending = 0;
    irq_candidate_ready = 0;
    preemptive_enabled = SCHED_PREEMPTIVE_ENABLED;
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
    tasks[id].irq_esp = make_initial_irq_esp(id);
    tasks[id].runs = 0;
    tasks[id].active = 1;
    irq_context_count++;
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
        irq_candidate_ready = 1;
        timer_request_count++;
    }
}

void scheduler_note_interrupt_frame(const struct interrupt_frame *frame) {
    if (!frame || frame->int_no != 32) return;
    irq_frame_count++;
    last_irq_eip = frame->eip;
}

uint32_t scheduler_on_timer_interrupt(const struct interrupt_frame *frame) {
    int next;

    scheduler_note_interrupt_frame(frame);

    if (!switch_pending || !irq_candidate_ready || task_count == 0) return 0;

    next = scheduler_next_active_irq_task();
    if (next < 0) return 0;

    irq_candidate_ready = 0;
    irq_candidate_count++;

    if (!preemptive_enabled) return 0;

    if (!in_task || current_task < 0 || !tasks[current_task].active) {
        irq_preempt_blocked_count++;
        return 0;
    }

    switch_pending = 0;
    tasks[current_task].irq_esp = (uint32_t)frame;
    current_task = next;
    switch_count++;
    irq_preempt_switch_count++;
    return tasks[next].irq_esp;
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

void scheduler_set_preemptive_enabled(int enabled) {
    preemptive_enabled = enabled ? 1 : 0;
}

uint32_t scheduler_preemptive_enabled(void) {
    return preemptive_enabled;
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

uint32_t scheduler_irq_frame_count(void) {
    return irq_frame_count;
}

uint32_t scheduler_irq_context_count(void) {
    return irq_context_count;
}

uint32_t scheduler_irq_candidate_count(void) {
    return irq_candidate_count;
}

uint32_t scheduler_irq_preempt_switch_count(void) {
    return irq_preempt_switch_count;
}

uint32_t scheduler_irq_preempt_blocked_count(void) {
    return irq_preempt_blocked_count;
}

uint32_t scheduler_last_irq_eip(void) {
    return last_irq_eip;
}

void scheduler_list(sched_list_cb_t cb, void *ctx) {
    if (!cb) return;

    for (uint32_t i = 0; i < task_count; i++) {
        if (tasks[i].active) cb(tasks[i].id, tasks[i].name, tasks[i].runs, ctx);
    }
}
