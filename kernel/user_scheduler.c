#include <stdint.h>
#include "user_scheduler.h"
#include "process.h"
#include "usermode.h"
#include "interrupts.h"
#include "scheduler.h"

static uint8_t armed = 0;
static uint32_t runs = 0;
static uint32_t launches = 0;
static uint32_t idle_count = 0;
static uint32_t no_ready_count = 0;
static uint32_t last_pid = 0;
static uint32_t last_result = 0;

void user_scheduler_init(void) {
    armed = 0;
    runs = 0;
    launches = 0;
    idle_count = 0;
    no_ready_count = 0;
    last_pid = 0;
    last_result = 0;
}

void user_scheduler_task(void *ctx) {
    (void)ctx;
    runs++;

    if (!armed) {
        idle_count++;
        return;
    }

    const char *name = 0;
    uint32_t pid = 0;
    user_context_t user_context;
    if (!process_get_ready_user(&name, &pid, &user_context)) {
        armed = 0;
        idle_count++;
        no_ready_count++;
        return;
    }

    armed = 0;
    process_record_user(name, &user_context, PROCESS_RUNNING);
    uint32_t was_preemptive = scheduler_preemptive_enabled();
    scheduler_set_preemptive_enabled(0);
    last_result = usermode_run_context(&user_context);
    scheduler_set_preemptive_enabled((int)was_preemptive);
    usermode_fill_test_context(&user_context,
                               syscall_get_last_eax(),
                               syscall_get_last_cs(),
                               syscall_get_user_count());
    process_record_user(name, &user_context, PROCESS_ZOMBIE);
    last_pid = pid;
    launches++;
}

int user_scheduler_arm(void) {
    const char *name = 0;
    uint32_t pid = 0;
    user_context_t user_context;
    if (!process_get_ready_user(&name, &pid, &user_context)) {
        armed = 0;
        no_ready_count++;
        return 0;
    }

    armed = 1;
    return 1;
}

uint32_t user_scheduler_armed(void) {
    return armed;
}

uint32_t user_scheduler_runs(void) {
    return runs;
}

uint32_t user_scheduler_launches(void) {
    return launches;
}

uint32_t user_scheduler_idle_count(void) {
    return idle_count;
}

uint32_t user_scheduler_no_ready_count(void) {
    return no_ready_count;
}

uint32_t user_scheduler_last_pid(void) {
    return last_pid;
}

uint32_t user_scheduler_last_result(void) {
    return last_result;
}
