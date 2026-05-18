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
    uint32_t resume_irq_esp;
    uint32_t runs;
    uint8_t active;
} sched_task_t;

static sched_task_t tasks[SCHED_MAX_TASKS];
static uint32_t task_stacks[SCHED_MAX_TASKS][256];
static uint32_t irq_stacks[SCHED_MAX_TASKS][256];
static uint32_t main_stack[2048] __attribute__((aligned(16)));
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
static uint32_t main_capture_count = 0;
static uint32_t main_captured_esp = 0;
uint32_t sched_debug_marker = 0;
static uint32_t main_to_task_count = 0;
static uint32_t irq_to_main_count = 0;
static uint32_t yield_to_main_count = 0;
static uint8_t main_paused_via_irq = 0;
static uint32_t last_irq_eip = 0;
static uint8_t switch_pending = 0;
static uint8_t irq_candidate_ready = 0;
static uint8_t preemptive_enabled = SCHED_PREEMPTIVE_ENABLED;
static uint8_t main_switch_enabled = 0;
static uint8_t in_task = 0;
static int main_task_id = -1;

extern void scheduler_context_switch(uint32_t *old_esp, uint32_t new_esp);
extern void scheduler_context_switch_iret(uint32_t *old_esp, uint32_t new_esp);
extern void scheduler_context_switch_update_irq(uint32_t *old_esp,
                                                uint32_t resume_irq_esp,
                                                uint32_t new_esp);
extern void scheduler_context_switch_iret_update_irq(uint32_t *old_esp,
                                                     uint32_t resume_irq_esp,
                                                     uint32_t new_esp);
extern void scheduler_resume_from_saved_esp(void);

static void scheduler_task_trampoline(void) {
    sched_debug_marker = 0x55550000u;
    while (1) {
        if (current_task >= 0 &&
            tasks[current_task].active &&
            tasks[current_task].entry) {
            tasks[current_task].runs++;
            sched_debug_marker = 0x66660000u | (uint32_t)current_task;
            tasks[current_task].entry(tasks[current_task].ctx);
            sched_debug_marker = 0x77770000u | (uint32_t)current_task;
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

static uint32_t make_resume_irq_esp(uint32_t id) {
    interrupt_frame_t *frame =
        (interrupt_frame_t*)(irq_stacks[id] + 256) - 1;

    memset(frame, 0, sizeof(interrupt_frame_t));
    frame->eax = tasks[id].esp;
    frame->gs = 0x10;
    frame->fs = 0x10;
    frame->es = 0x10;
    frame->ds = 0x10;
    frame->int_no = 32;
    frame->eip = (uint32_t)scheduler_resume_from_saved_esp;
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

        if (next == current_task) continue;
        if (tasks[next].active && tasks[next].irq_esp) {
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
    main_capture_count = 0;
    main_captured_esp = 0;
    main_to_task_count = 0;
    irq_to_main_count = 0;
    yield_to_main_count = 0;
    main_paused_via_irq = 0;
    last_irq_eip = 0;
    switch_pending = 0;
    irq_candidate_ready = 0;
    preemptive_enabled = SCHED_PREEMPTIVE_ENABLED;
    main_switch_enabled = 0;
    in_task = 0;
    main_task_id = -1;
}

int scheduler_create_kernel_task(const char *name, sched_task_fn_t entry, void *ctx) {
    if (!name || !entry || task_count >= SCHED_MAX_TASKS) return -1;

    uint32_t id = task_count;
    tasks[id].id = id;
    tasks[id].name = name;
    tasks[id].entry = entry;
    tasks[id].ctx = ctx;
    tasks[id].esp = make_initial_esp(id);
    tasks[id].resume_irq_esp = make_resume_irq_esp(id);
    tasks[id].irq_esp = tasks[id].resume_irq_esp;
    tasks[id].runs = 0;
    tasks[id].active = 1;
    irq_context_count++;
    task_count++;

    if (current_task < 0) current_task = 0;
    return (int)id;
}

int scheduler_register_main_task(const char *name) {
    if (!name || task_count >= SCHED_MAX_TASKS || main_task_id >= 0) return -1;

    uint32_t id = task_count;
    tasks[id].id = id;
    tasks[id].name = name;
    tasks[id].entry = 0;
    tasks[id].ctx = 0;
    tasks[id].esp = 0;
    tasks[id].irq_esp = 0;
    tasks[id].resume_irq_esp = 0;
    tasks[id].runs = 0;
    tasks[id].active = 1;
    task_count++;
    main_task_id = (int)id;
    return (int)id;
}

uint32_t scheduler_main_task_id(void) {
    if (main_task_id < 0) return 0xFFFFFFFFu;
    return (uint32_t)main_task_id;
}

uint32_t scheduler_main_stack_top(void) {
    return (uint32_t)(main_stack + 2048);
}

uint32_t scheduler_main_stack_base(void) {
    return (uint32_t)main_stack;
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

    if (!in_task) {
        if (main_task_id >= 0) {
            main_captured_esp = (uint32_t)frame;
            main_capture_count++;
        }
        if (!main_switch_enabled || main_task_id < 0) {
            irq_preempt_blocked_count++;
            return 0;
        }
        /* IRQ fired in the desktop main loop. Atomic capture-and-switch:
           save main's frame into its slot's irq_esp (preserved because main
           runs on its own dedicated stack and is about to be paused), then
           hand control to the chosen task in the same ISR call. */
        sched_debug_marker = 0x11110000u | (uint32_t)next;
        tasks[main_task_id].irq_esp = (uint32_t)frame;
        main_paused_via_irq = 1;
        switch_pending = 0;
        current_task = next;
        in_task = 1;
        switch_count++;
        irq_preempt_switch_count++;
        main_to_task_count++;
        return tasks[next].irq_esp;
    }

    if (current_task < 0 || !tasks[current_task].active) {
        irq_preempt_blocked_count++;
        return 0;
    }

    /* IRQ fired in a kernel task. Save its frame and switch to next, which
       may be another task or the (paused-via-IRQ) main loop. */
    switch_pending = 0;
    tasks[current_task].irq_esp = (uint32_t)frame;
    uint32_t resume_esp = tasks[next].irq_esp;
    if (next == main_task_id) {
        sched_debug_marker = 0x22220000u | (uint32_t)current_task;
        tasks[main_task_id].irq_esp = 0;
        main_paused_via_irq = 0;
        in_task = 0;
        irq_to_main_count++;
    } else {
        sched_debug_marker = 0x33330000u | ((uint32_t)current_task << 8) | (uint32_t)next;
    }
    current_task = next;
    switch_count++;
    irq_preempt_switch_count++;
    return resume_esp;
}

void scheduler_poll(void) {
    if (!switch_pending || task_count == 0) return;
    switch_pending = 0;

    int next = current_task;
    for (uint32_t i = 0; i < task_count; i++) {
        next++;
        if (next >= (int)task_count) next = 0;
        if (tasks[next].active && tasks[next].entry) {
            current_task = next;
            switch_count++;
            in_task = 1;
            scheduler_context_switch(&main_esp, tasks[current_task].esp);
            in_task = 0;
            return;
        }
    }
}

void scheduler_yield(void) {
    if (!in_task || current_task < 0) return;

    if (main_paused_via_irq && main_task_id >= 0 && tasks[main_task_id].irq_esp) {
        /* Main was paused by an IRQ, so its cooperative main_esp is stale.
           Return to main via iret on its saved interrupt frame. IRQs must
           stay off across the bookkeeping → switch — otherwise a PIT tick
           in this window would see in_task=0 and misclassify the task
           stack as the main loop. The iretd at the end of the asm helper
           re-enables them by popping main's captured EFLAGS. */
        __asm__ volatile ("cli");
        int old_task = current_task;
        uint32_t main_iret_esp = tasks[main_task_id].irq_esp;
        sched_debug_marker = 0x44440000u | (uint32_t)old_task;
        tasks[old_task].irq_esp = tasks[old_task].resume_irq_esp;
        tasks[main_task_id].irq_esp = 0;
        main_paused_via_irq = 0;
        current_task = main_task_id;
        in_task = 0;
        yield_to_main_count++;
        scheduler_context_switch_iret_update_irq(&tasks[old_task].esp,
                                                 tasks[old_task].resume_irq_esp,
                                                 main_iret_esp);
        return;
    }

    scheduler_context_switch_update_irq(&tasks[current_task].esp,
                                        tasks[current_task].resume_irq_esp,
                                        main_esp);
}

void scheduler_set_preemptive_enabled(int enabled) {
    preemptive_enabled = enabled ? 1 : 0;
}

uint32_t scheduler_preemptive_enabled(void) {
    return preemptive_enabled;
}

void scheduler_set_main_switch_enabled(int enabled) {
    main_switch_enabled = enabled ? 1 : 0;
}

uint32_t scheduler_main_switch_enabled(void) {
    return main_switch_enabled;
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

uint32_t scheduler_main_capture_count(void) {
    return main_capture_count;
}

uint32_t scheduler_main_captured_esp(void) {
    return main_captured_esp;
}

uint32_t scheduler_main_to_task_count(void) {
    return main_to_task_count;
}

uint32_t scheduler_irq_to_main_count(void) {
    return irq_to_main_count;
}

uint32_t scheduler_yield_to_main_count(void) {
    return yield_to_main_count;
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
