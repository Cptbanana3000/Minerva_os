#ifndef MINERVA_SCHEDULER_H
#define MINERVA_SCHEDULER_H

#include <stdint.h>

#define SCHED_MAX_TASKS 8
#define SCHED_PREEMPTIVE_ENABLED 0

struct interrupt_frame;

typedef void (*sched_task_fn_t)(void *ctx);
typedef void (*sched_list_cb_t)(uint32_t id, const char *name, uint32_t runs, void *ctx);
typedef void (*sched_context_list_cb_t)(uint32_t id,
                                        const char *name,
                                        uint32_t esp,
                                        uint32_t irq_esp,
                                        uint32_t resume_irq_esp,
                                        uint32_t stack_base,
                                        uint32_t stack_top,
                                        void *ctx);

void scheduler_init(void);
int scheduler_create_kernel_task(const char *name, sched_task_fn_t entry, void *ctx);
int scheduler_register_main_task(const char *name);
uint32_t scheduler_main_task_id(void);
uint32_t scheduler_main_stack_top(void);
uint32_t scheduler_main_stack_base(void);
void scheduler_run_on_main_stack(void (*entry)(void), uint32_t new_esp);
void scheduler_tick(void);
void scheduler_note_interrupt_frame(const struct interrupt_frame *frame);
uint32_t scheduler_on_timer_interrupt(const struct interrupt_frame *frame);
void scheduler_poll(void);
void scheduler_yield(void);
void scheduler_set_preemptive_enabled(int enabled);
uint32_t scheduler_preemptive_enabled(void);
void scheduler_set_main_switch_enabled(int enabled);
uint32_t scheduler_main_switch_enabled(void);
uint32_t scheduler_task_count(void);
uint32_t scheduler_current_task_id(void);
uint32_t scheduler_running_task_id(void);
const char *scheduler_current_task_name(void);
uint32_t scheduler_switch_count(void);
uint32_t scheduler_timer_request_count(void);
uint32_t scheduler_irq_frame_count(void);
uint32_t scheduler_irq_context_count(void);
uint32_t scheduler_irq_candidate_count(void);
uint32_t scheduler_irq_preempt_switch_count(void);
uint32_t scheduler_irq_preempt_blocked_count(void);
uint32_t scheduler_main_capture_count(void);
uint32_t scheduler_main_captured_esp(void);
uint32_t scheduler_main_to_task_count(void);
uint32_t scheduler_irq_to_main_count(void);
uint32_t scheduler_yield_to_main_count(void);
uint32_t scheduler_last_irq_eip(void);
void scheduler_list(sched_list_cb_t cb, void *ctx);
void scheduler_list_contexts(sched_context_list_cb_t cb, void *ctx);

#endif
