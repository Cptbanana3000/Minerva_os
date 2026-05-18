#ifndef MINERVA_USER_SCHEDULER_H
#define MINERVA_USER_SCHEDULER_H

#include <stdint.h>

void user_scheduler_init(void);
void user_scheduler_task(void *ctx);
int user_scheduler_arm(void);
uint32_t user_scheduler_armed(void);
uint32_t user_scheduler_runs(void);
uint32_t user_scheduler_launches(void);
uint32_t user_scheduler_idle_count(void);
uint32_t user_scheduler_no_ready_count(void);
uint32_t user_scheduler_last_pid(void);
uint32_t user_scheduler_last_result(void);

#endif
