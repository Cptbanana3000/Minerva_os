#include <stdint.h>
#include "term_window.h"
#include "window.h"
#include "graphics.h"
#include "libc.h"
#include "serial.h"
#include "pmm.h"
#include "io.h"
#include "fs.h"
#include "scheduler.h"
#include "process.h"
#include "gdt.h"
#include "interrupts.h"
#include "usermode.h"
#include "user_scheduler.h"

static term_window_t g_tw;
static uint8_t file_buffer[512];

static void tw_scroll(term_window_t *t) {
    for (int r = 0; r < TWIN_ROWS - 1; r++)
        memcpy(t->buf[r], t->buf[r + 1], TWIN_COLS);
    memset(t->buf[TWIN_ROWS - 1], 0, TWIN_COLS);
    t->cur_row = TWIN_ROWS - 1;
    t->cur_col = 0;
}

void term_window_putc(term_window_t *t, char c) {
    if (c == '\n') {
        t->cur_col = 0;
        t->cur_row++;
        if (t->cur_row >= TWIN_ROWS) tw_scroll(t);
    } else if (c == '\b') {
        if (t->cur_col > 0) {
            t->cur_col--;
            t->buf[t->cur_row][t->cur_col] = 0;
        }
    } else {
        if (t->cur_col >= TWIN_COLS) {
            t->cur_col = 0;
            t->cur_row++;
            if (t->cur_row >= TWIN_ROWS) tw_scroll(t);
        }
        t->buf[t->cur_row][t->cur_col++] = c;
    }
}

void term_window_print(term_window_t *t, const char *s) {
    while (*s) term_window_putc(t, *s++);
}

static void tw_print_num(term_window_t *t, uint32_t n) {
    char tmp[12];
    int i = 11;
    tmp[i] = 0;
    if (n == 0) { term_window_putc(t, '0'); return; }
    while (n > 0) { tmp[--i] = '0' + (char)(n % 10); n /= 10; }
    term_window_print(t, tmp + i);
}

static void tw_print_hex32(term_window_t *t, uint32_t n) {
    const char *digits = "0123456789ABCDEF";
    for (int shift = 28; shift >= 0; shift -= 4) {
        term_window_putc(t, digits[(n >> shift) & 0xFu]);
    }
}

static int starts_with(const char *s, const char *prefix) {
    while (*prefix) {
        if (*s++ != *prefix++) return 0;
    }
    return 1;
}

static void tw_ls_entry(const char *name, uint32_t size, void *ctx) {
    term_window_t *t = (term_window_t*)ctx;
    term_window_print(t, name);
    term_window_putc(t, ' ');
    tw_print_num(t, size);
    term_window_putc(t, '\n');
}

static void tw_task_entry(uint32_t id, const char *name, uint32_t runs, void *ctx) {
    term_window_t *t = (term_window_t*)ctx;
    tw_print_num(t, id);
    term_window_putc(t, ' ');
    term_window_print(t, name);
    term_window_putc(t, ' ');
    tw_print_num(t, runs);
    term_window_putc(t, '\n');
}

static void tw_process_task_id(term_window_t *t, uint32_t task_id) {
    if (task_id == PROCESS_NO_TASK) {
        term_window_putc(t, '-');
    } else {
        tw_print_num(t, task_id);
    }
}

static void tw_process_entry(const process_t *process, void *ctx) {
    term_window_t *t = (term_window_t*)ctx;
    uint32_t running_task = scheduler_running_task_id();
    const char *state = process_state_name(process->state);
    if (process->task_id != PROCESS_NO_TASK && process->task_id == running_task) {
        state = process_state_name(PROCESS_RUNNING);
    }

    tw_print_num(t, process->pid);
    term_window_putc(t, ' ');
    term_window_print(t, process->kernel ? "ker" : "usr");
    term_window_putc(t, ' ');
    tw_process_task_id(t, process->task_id);
    term_window_putc(t, ' ');
    term_window_print(t, state);
    term_window_putc(t, ' ');
    term_window_print(t, process->name);
    term_window_putc(t, '\n');
}

static void tw_context_entry(uint32_t id,
                             const char *name,
                             uint32_t esp,
                             uint32_t irq_esp,
                             uint32_t resume_irq_esp,
                             uint32_t stack_base,
                             uint32_t stack_top,
                             void *ctx) {
    term_window_t *t = (term_window_t*)ctx;
    (void)name;
    (void)resume_irq_esp;
    (void)stack_base;
    (void)stack_top;
    tw_print_num(t, id);
    term_window_putc(t, ' ');
    tw_print_hex32(t, esp);
    term_window_putc(t, ' ');
    tw_print_hex32(t, irq_esp);
    term_window_putc(t, '\n');
}

static void tw_user_process_entry(const process_t *process, void *ctx) {
    if (process->kernel) return;

    term_window_t *t = (term_window_t*)ctx;
    term_window_print(t, "PID:");
    tw_print_num(t, process->pid);
    term_window_putc(t, ' ');
    term_window_print(t, process_state_name(process->state));
    term_window_putc(t, '\n');
    term_window_print(t, "ENT:");
    tw_print_hex32(t, process->user_context.eip);
    term_window_putc(t, '\n');
    term_window_print(t, "STK:");
    tw_print_hex32(t, process->user_context.esp);
    term_window_putc(t, '\n');
    term_window_print(t, "SYS:");
    tw_print_num(t, process->user_context.syscall_count);
    term_window_putc(t, '\n');
}

static void tw_user_context_entry(const process_t *process, void *ctx) {
    if (process->kernel) return;

    term_window_t *t = (term_window_t*)ctx;
    term_window_print(t, "EIP:");
    tw_print_hex32(t, process->user_context.eip);
    term_window_putc(t, '\n');
    term_window_print(t, "ESP:");
    tw_print_hex32(t, process->user_context.esp);
    term_window_putc(t, '\n');
    term_window_print(t, "CS:");
    tw_print_hex32(t, process->user_context.cs);
    term_window_putc(t, ' ');
    term_window_print(t, "SS:");
    tw_print_hex32(t, process->user_context.ss);
    term_window_putc(t, '\n');
    term_window_print(t, "FL:");
    tw_print_hex32(t, process->user_context.eflags);
    term_window_putc(t, '\n');
    term_window_print(t, "AX:");
    tw_print_hex32(t, process->user_context.last_syscall_eax);
    term_window_putc(t, '\n');
    term_window_print(t, "SC:");
    tw_print_num(t, process->user_context.syscall_count);
    term_window_putc(t, '\n');
    if (process->user_context.fault_vector) {
        term_window_print(t, "FV:");
        tw_print_num(t, process->user_context.fault_vector);
        term_window_putc(t, ' ');
        term_window_print(t, "FE:");
        tw_print_hex32(t, process->user_context.fault_error);
        term_window_putc(t, '\n');
        term_window_print(t, "FA:");
        tw_print_hex32(t, process->user_context.fault_address);
        term_window_putc(t, '\n');
        term_window_print(t, "FI:");
        tw_print_hex32(t, process->user_context.fault_eip);
        term_window_putc(t, '\n');
    }
}

static void tw_prepare_user_test(term_window_t *t, const char *label) {
    user_context_t user_context;
    usermode_fill_test_context(&user_context, 0, 0, 0);
    int pid = process_record_user("usertest", &user_context, PROCESS_READY);
    term_window_print(t, label);
    if (pid < 0) {
        term_window_putc(t, '-');
    } else {
        tw_print_num(t, (uint32_t)pid);
    }
    term_window_putc(t, '\n');
}

static void tw_exec(term_window_t *t) {
    const char *cmd = t->input;
    serial_write(cmd);
    serial_write("\n");

    if (strcmp(cmd, "help") == 0) {
        term_window_print(t, "help clear about echo\n");
        term_window_print(t, "meminfo ps ctx uctx\n");
        term_window_print(t, "tss tasks syscall\n");
        term_window_print(t, "usertest userprep\n");
        term_window_print(t, "userreset usersched\n");
        term_window_print(t, "userfault ufault\n");
        term_window_print(t, "userrun usched uproc\n");
        term_window_print(t, "ls cat touch write\n");
        term_window_print(t, "append truncate delete\n");
        term_window_print(t, "rename preempt reboot\n");
    } else if (strcmp(cmd, "clear") == 0) {
        memset(t->buf, 0, sizeof(t->buf));
        t->cur_col = 0;
        t->cur_row = 0;
    } else if (strcmp(cmd, "about") == 0) {
        term_window_print(t, "MinervaOS v0.3\n");
        term_window_print(t, "Phase 3: Desktop\n");
        term_window_print(t, "x86 32-bit, 320x200\n");
    } else if (strcmp(cmd, "echo") == 0) {
        term_window_print(t, "Hello, MinervaOS!\n");
    } else if (strcmp(cmd, "meminfo") == 0) {
        term_window_print(t, "Total:");
        tw_print_num(t, pmm_total_pages() * 4);
        term_window_print(t, "K\n");
        term_window_print(t, "Used:");
        tw_print_num(t, pmm_used_pages() * 4);
        term_window_print(t, "K\n");
        term_window_print(t, "Free:");
        tw_print_num(t, pmm_free_pages() * 4);
        term_window_print(t, "K\n");
    } else if (strcmp(cmd, "ps") == 0) {
        term_window_print(t, "PID K TID STATE NAME\n");
        process_list(tw_process_entry, t);
    } else if (strcmp(cmd, "ctx") == 0) {
        term_window_print(t, "ID ESP IRQ\n");
        scheduler_list_contexts(tw_context_entry, t);
    } else if (strcmp(cmd, "tss") == 0) {
        term_window_print(t, "UC:");
        tw_print_hex32(t, gdt_user_code_selector());
        term_window_putc(t, '\n');
        term_window_print(t, "UD:");
        tw_print_hex32(t, gdt_user_data_selector());
        term_window_putc(t, '\n');
        term_window_print(t, "TSS:");
        tw_print_hex32(t, gdt_tss_selector());
        term_window_putc(t, '\n');
        term_window_print(t, "LD:");
        tw_print_num(t, gdt_tss_loaded());
        term_window_putc(t, '\n');
        term_window_print(t, "SS0:");
        tw_print_hex32(t, gdt_tss_ss0());
        term_window_putc(t, '\n');
        term_window_print(t, "ESP0:");
        tw_print_hex32(t, gdt_tss_esp0());
        term_window_putc(t, '\n');
    } else if (strcmp(cmd, "syscall") == 0) {
        term_window_print(t, "Count:");
        tw_print_num(t, syscall_get_count());
        term_window_putc(t, '\n');
        term_window_print(t, "EAX:");
        tw_print_hex32(t, syscall_get_last_eax());
        term_window_putc(t, '\n');
        term_window_print(t, "CS:");
        tw_print_hex32(t, syscall_get_last_cs());
        term_window_putc(t, '\n');
        term_window_print(t, "User:");
        tw_print_num(t, syscall_get_user_count());
        term_window_putc(t, '\n');
        term_window_print(t, "Ret:");
        tw_print_hex32(t, syscall_get_last_result());
        term_window_putc(t, '\n');
    } else if (strcmp(cmd, "syscall test") == 0) {
        uint32_t result = syscall_test_interrupt();
        term_window_print(t, "Ret:");
        tw_print_hex32(t, result);
        term_window_putc(t, '\n');
        term_window_print(t, "Count:");
        tw_print_num(t, syscall_get_count());
        term_window_putc(t, '\n');
    } else if (strcmp(cmd, "usertest") == 0) {
        uint32_t result = usermode_run_test();
        user_context_t user_context;
        usermode_fill_test_context(&user_context,
                                   syscall_get_last_eax(),
                                   syscall_get_last_cs(),
                                   syscall_get_user_count());
        process_record_user("usertest", &user_context, PROCESS_ZOMBIE);
        term_window_print(t, "UserRet:");
        tw_print_hex32(t, result);
        term_window_putc(t, '\n');
        term_window_print(t, "CS:");
        tw_print_hex32(t, syscall_get_last_cs());
        term_window_putc(t, '\n');
        term_window_print(t, "EAX:");
        tw_print_hex32(t, syscall_get_last_eax());
        term_window_putc(t, '\n');
        term_window_print(t, "User:");
        tw_print_num(t, syscall_get_user_count());
        term_window_putc(t, '\n');
    } else if (strcmp(cmd, "userprep") == 0) {
        tw_prepare_user_test(t, "Ready PID:");
    } else if (strcmp(cmd, "userreset") == 0) {
        tw_prepare_user_test(t, "Reset PID:");
    } else if (strcmp(cmd, "userrun") == 0) {
        user_context_t user_context;
        if (!process_get_user_context("usertest", &user_context)) {
            term_window_print(t, "No user process\n");
        } else {
            process_record_user("usertest", &user_context, PROCESS_RUNNING);
            uint32_t result = usermode_run_context(&user_context);
            usermode_fill_test_context(&user_context,
                                       syscall_get_last_eax(),
                                       syscall_get_last_cs(),
                                       syscall_get_user_count());
            process_record_user("usertest", &user_context, PROCESS_ZOMBIE);
            term_window_print(t, "UserRet:");
            tw_print_hex32(t, result);
            term_window_putc(t, '\n');
            term_window_print(t, "User:");
            tw_print_num(t, syscall_get_user_count());
            term_window_putc(t, '\n');
        }
    } else if (strcmp(cmd, "userfault") == 0) {
        user_context_t user_context;
        usermode_fill_fault_context(&user_context, 0, 0, 0, 0);
        process_record_user("userfault", &user_context, PROCESS_RUNNING);
        uint32_t result = usermode_run_context(&user_context);
        usermode_fill_fault_context(&user_context,
                                    user_fault_get_vector(),
                                    user_fault_get_eip(),
                                    user_fault_get_address(),
                                    user_fault_get_error());
        process_record_user("userfault", &user_context, PROCESS_ZOMBIE);
        term_window_print(t, "FaultRet:");
        tw_print_hex32(t, result);
        term_window_putc(t, '\n');
        term_window_print(t, "Vec:");
        tw_print_num(t, user_fault_get_vector());
        term_window_putc(t, '\n');
        term_window_print(t, "Addr:");
        tw_print_hex32(t, user_fault_get_address());
        term_window_putc(t, '\n');
        term_window_print(t, "Err:");
        tw_print_hex32(t, user_fault_get_error());
        term_window_putc(t, '\n');
    } else if (strcmp(cmd, "ufault") == 0) {
        term_window_print(t, "Count:");
        tw_print_num(t, user_fault_get_count());
        term_window_putc(t, '\n');
        term_window_print(t, "Vec:");
        tw_print_num(t, user_fault_get_vector());
        term_window_putc(t, '\n');
        term_window_print(t, "CS:");
        tw_print_hex32(t, user_fault_get_cs());
        term_window_putc(t, '\n');
        term_window_print(t, "EIP:");
        tw_print_hex32(t, user_fault_get_eip());
        term_window_putc(t, '\n');
        term_window_print(t, "Addr:");
        tw_print_hex32(t, user_fault_get_address());
        term_window_putc(t, '\n');
        term_window_print(t, "Err:");
        tw_print_hex32(t, user_fault_get_error());
        term_window_putc(t, '\n');
    } else if (strcmp(cmd, "usersched") == 0) {
        if (user_scheduler_arm()) {
            term_window_print(t, "USched armed\n");
        } else {
            term_window_print(t, "No READY user\n");
        }
    } else if (strcmp(cmd, "usched") == 0) {
        term_window_print(t, "Armed:");
        tw_print_num(t, user_scheduler_armed());
        term_window_putc(t, '\n');
        term_window_print(t, "Runs:");
        tw_print_num(t, user_scheduler_runs());
        term_window_putc(t, '\n');
        term_window_print(t, "Launch:");
        tw_print_num(t, user_scheduler_launches());
        term_window_putc(t, '\n');
        term_window_print(t, "Idle:");
        tw_print_num(t, user_scheduler_idle_count());
        term_window_putc(t, '\n');
        term_window_print(t, "NoReady:");
        tw_print_num(t, user_scheduler_no_ready_count());
        term_window_putc(t, '\n');
        term_window_print(t, "PID:");
        tw_print_num(t, user_scheduler_last_pid());
        term_window_putc(t, '\n');
        term_window_print(t, "Ret:");
        tw_print_hex32(t, user_scheduler_last_result());
        term_window_putc(t, '\n');
    } else if (strcmp(cmd, "uproc") == 0) {
        process_list(tw_user_process_entry, t);
    } else if (strcmp(cmd, "uctx") == 0) {
        process_list(tw_user_context_entry, t);
    } else if (strcmp(cmd, "tasks") == 0) {
        term_window_print(t, "Switches:");
        tw_print_num(t, scheduler_switch_count());
        term_window_putc(t, '\n');
        term_window_print(t, "TimerReq:");
        tw_print_num(t, scheduler_timer_request_count());
        term_window_putc(t, '\n');
        term_window_print(t, "IRQFrames:");
        tw_print_num(t, scheduler_irq_frame_count());
        term_window_putc(t, '\n');
        term_window_print(t, "IRQCtx:");
        tw_print_num(t, scheduler_irq_context_count());
        term_window_putc(t, '\n');
        term_window_print(t, "Cand:");
        tw_print_num(t, scheduler_irq_candidate_count());
        term_window_putc(t, '\n');
        term_window_print(t, "PMode:");
        tw_print_num(t, scheduler_preemptive_enabled());
        term_window_putc(t, '\n');
        term_window_print(t, "IRQSw:");
        tw_print_num(t, scheduler_irq_preempt_switch_count());
        term_window_putc(t, '\n');
        term_window_print(t, "Block:");
        tw_print_num(t, scheduler_irq_preempt_blocked_count());
        term_window_putc(t, '\n');
        term_window_print(t, "MainCap:");
        tw_print_num(t, scheduler_main_capture_count());
        term_window_putc(t, '\n');
        term_window_print(t, "M->T:");
        tw_print_num(t, scheduler_main_to_task_count());
        term_window_putc(t, '\n');
        term_window_print(t, "I->M:");
        tw_print_num(t, scheduler_irq_to_main_count());
        term_window_putc(t, '\n');
        term_window_print(t, "Y->M:");
        tw_print_num(t, scheduler_yield_to_main_count());
        term_window_putc(t, '\n');
        term_window_print(t, "MSw:");
        tw_print_num(t, scheduler_main_switch_enabled());
        term_window_putc(t, '\n');
        scheduler_list(tw_task_entry, t);
    } else if (strcmp(cmd, "preempt") == 0) {
        term_window_print(t, "PMode:");
        tw_print_num(t, scheduler_preemptive_enabled());
        term_window_putc(t, '\n');
    } else if (strcmp(cmd, "preempt on") == 0) {
        scheduler_set_preemptive_enabled(1);
        term_window_print(t, "PMode:1\n");
    } else if (strcmp(cmd, "preempt off") == 0) {
        scheduler_set_preemptive_enabled(0);
        term_window_print(t, "PMode:0\n");
    } else if (strcmp(cmd, "mainsw on") == 0) {
        scheduler_set_main_switch_enabled(1);
        term_window_print(t, "MSw:1\n");
    } else if (strcmp(cmd, "mainsw off") == 0) {
        scheduler_set_main_switch_enabled(0);
        term_window_print(t, "MSw:0\n");
    } else if (strcmp(cmd, "ls") == 0) {
        if (!fs_is_ready()) {
            term_window_print(t, "No filesystem\n");
        } else if (!fs_list_root(tw_ls_entry, t)) {
            term_window_print(t, "ls failed\n");
        }
    } else if (starts_with(cmd, "cat ")) {
        const char *name = cmd + 4;
        fs_file_t file;
        uint32_t size = 0;
        uint32_t total = 0;
        char last = 0;
        if (!fs_is_ready()) {
            term_window_print(t, "No filesystem\n");
        } else if (!fs_open(name, &file)) {
            term_window_print(t, "cat failed\n");
        } else {
            size = fs_file_size(&file);
            while (total < size) {
                uint32_t got = fs_read(&file, file_buffer, sizeof(file_buffer) - 1);
                if (got == 0) break;
                file_buffer[got] = 0;
                last = (char)file_buffer[got - 1];
                total += got;
                term_window_print(t, (const char*)file_buffer);
            }
            fs_close(&file);
            if (total != size) {
                term_window_print(t, "cat failed\n");
            } else if (size == 0 || last != '\n') {
                term_window_putc(t, '\n');
            }
        }
    } else if (starts_with(cmd, "touch ")) {
        const char *name = cmd + 6;
        if (!fs_is_ready()) {
            term_window_print(t, "No filesystem\n");
        } else if (!fs_create(name)) {
            term_window_print(t, "touch failed\n");
        }
    } else if (starts_with(cmd, "write ")) {
        char *name = t->input + 6;
        char *text = name;
        while (*text && *text != ' ') text++;
        if (*text == ' ') {
            *text++ = 0;
        }

        if (!fs_is_ready()) {
            term_window_print(t, "No filesystem\n");
        } else if (!*name || !*text ||
                   !fs_write(name, (const uint8_t*)text, strlen(text),
                             FS_WRITE_CREATE | FS_WRITE_TRUNCATE)) {
            term_window_print(t, "write failed\n");
        }
    } else if (starts_with(cmd, "append ")) {
        char *name = t->input + 7;
        char *text = name;
        while (*text && *text != ' ') text++;
        if (*text == ' ') {
            *text++ = 0;
        }

        if (!fs_is_ready()) {
            term_window_print(t, "No filesystem\n");
        } else if (!*name || !*text ||
                   !fs_write(name, (const uint8_t*)text, strlen(text), FS_WRITE_APPEND)) {
            term_window_print(t, "append failed\n");
        }
    } else if (starts_with(cmd, "truncate ")) {
        const char *name = cmd + 9;
        if (!fs_is_ready()) {
            term_window_print(t, "No filesystem\n");
        } else if (!fs_truncate_file(name)) {
            term_window_print(t, "truncate failed\n");
        }
    } else if (starts_with(cmd, "delete ")) {
        const char *name = cmd + 7;
        if (!fs_is_ready()) {
            term_window_print(t, "No filesystem\n");
        } else if (!fs_delete_file(name)) {
            term_window_print(t, "delete failed\n");
        }
    } else if (starts_with(cmd, "rename ")) {
        char *old_name = t->input + 7;
        char *new_name = old_name;
        while (*new_name && *new_name != ' ') new_name++;
        if (*new_name == ' ') {
            *new_name++ = 0;
        }

        if (!fs_is_ready()) {
            term_window_print(t, "No filesystem\n");
        } else if (!*old_name || !*new_name || !fs_rename_file(old_name, new_name)) {
            term_window_print(t, "rename failed\n");
        }
    } else if (strcmp(cmd, "reboot") == 0) {
        while (inb(0x64) & 0x02) {}
        outb(0x64, 0xFE);
        __asm__ volatile("cli; hlt");
    } else if (cmd[0] != 0) {
        term_window_print(t, "Unknown: ");
        term_window_print(t, cmd);
        term_window_putc(t, '\n');
    }
}

void term_window_handle_key(term_window_t *t, char c) {
    if (c == '\n') {
        t->input[t->input_len] = 0;
        term_window_putc(t, '\n');
        tw_exec(t);
        t->input_len = 0;
        term_window_print(t, "$ ");
    } else if (c == '\b') {
        if (t->input_len > 0) {
            t->input_len--;
            term_window_putc(t, '\b');
        }
    } else if (t->input_len < (int)sizeof(t->input) - 1) {
        t->input[t->input_len++] = c;
        term_window_putc(t, c);
    }
}

void term_window_render(term_window_t *t) {
    if (!t || !t->win || window_is_minimized(t->win)) return;

    /* Black background — window_render() already filled it, but clear
       the client area so old chars don't ghost when the cursor moves back */
    window_clear(t->win, 0);

    for (int r = 0; r < TWIN_ROWS; r++) {
        for (int c = 0; c < TWIN_COLS; c++) {
            char ch = t->buf[r][c];
            if (ch >= 32)
                window_draw_char(t->win,
                                 (uint32_t)(c * 8), (uint32_t)(r * 8),
                                 ch, 10);  /* bright green on black */
        }
    }
    /* Solid block cursor */
    window_fill_rect(t->win,
                     (uint32_t)(t->cur_col * 8), (uint32_t)(t->cur_row * 8),
                     8, 8, 10);
}

term_window_t *term_window_create(uint32_t x, uint32_t y) {
    term_window_t *t = &g_tw;
    /* 200 wide, 100 tall: client = 198 x 82 → 24 cols x 10 rows */
    t->win = window_create(x, y, 200, 100, "Terminal");
    if (!t->win) return NULL;

    window_set_bg_color(t->win, 0);       /* black interior */
    window_set_title_color(t->win, 8, 10); /* dark-gray bar, green label */

    memset(t->buf, 0, sizeof(t->buf));
    t->cur_col   = 0;
    t->cur_row   = 0;
    t->input_len = 0;

    term_window_print(t, "MinervaOS Terminal\n");
    term_window_print(t, "Type 'help'\n");
    term_window_print(t, "$ ");
    return t;
}
