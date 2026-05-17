#ifndef MINERVA_SCHEDULER_H
#define MINERVA_SCHEDULER_H

#include <stdint.h>

#define SCHED_MAX_TASKS 8

typedef void (*sched_task_fn_t)(void *ctx);
typedef void (*sched_list_cb_t)(uint32_t id, const char *name, uint32_t runs, void *ctx);

void scheduler_init(void);
int scheduler_create_kernel_task(const char *name, sched_task_fn_t entry, void *ctx);
void scheduler_tick(void);
void scheduler_poll(void);
void scheduler_yield(void);
uint32_t scheduler_task_count(void);
uint32_t scheduler_current_task_id(void);
const char *scheduler_current_task_name(void);
uint32_t scheduler_switch_count(void);
uint32_t scheduler_timer_request_count(void);
void scheduler_list(sched_list_cb_t cb, void *ctx);

#endif
