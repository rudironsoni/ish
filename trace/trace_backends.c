/*
 * trace_backends.c
 * Backend implementations: NOP, RING, STDERR, OS_LOG (iOS/macOS)
 */

#include "trace/trace.h"
#include "trace/trace_types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __APPLE__
#include <os/log.h>
#endif

/* Forward declaration - defined in trace_ring.c */
extern const trace_backend_ops_t *trace_ring_backend_get_ops(void);

/* ============================================
 * NOP Backend - Zero overhead when disabled
 * ============================================ */

static int nop_init(void **ctx, trace_config_t *config)
{
    (void)ctx;
    (void)config;
    return 0;
}

static void nop_shutdown(void *ctx)
{
    (void)ctx;
}

static void nop_emit(void *ctx, trace_record_t *record)
{
    (void)ctx;
    (void)record;
}

static void nop_flush(void *ctx)
{
    (void)ctx;
}

static int nop_dump(void *ctx, const char *path)
{
    (void)ctx;
    (void)path;
    return 0;
}

static const trace_backend_ops_t nop_ops = {
    .init = nop_init,
    .shutdown = nop_shutdown,
    .emit = nop_emit,
    .flush = nop_flush,
    .dump = nop_dump,
};

/* ============================================
 * STDERR Backend - Human-readable output
 * ============================================ */

static int stderr_init(void **ctx, trace_config_t *config)
{
    (void)ctx;
    (void)config;
    fprintf(stderr, "[TRACE] Tracing enabled (stderr backend)\n");
    return 0;
}

static void stderr_shutdown(void *ctx)
{
    (void)ctx;
}

static void stderr_emit(void *ctx, trace_record_t *record)
{
    (void)ctx;

    const char *event_name = trace_event_name(record->header.event_id);

    fprintf(stderr, "[TRACE] seq=%llu event=%s pc=0x%016llx",
            (unsigned long long)record->header.seq, event_name,
            (unsigned long long)record->header.pc);

    /* Add event-specific details based on payload */
    switch (record->header.event_id) {
    case TRACE_EVENT_BLOCK_COMPILE_START:
        fprintf(stderr, " (compile start)");
        break;

    case TRACE_EVENT_BLOCK_COMPILE_END: {
        uint64_t end_pc;
        uint32_t insn_count;
        memcpy(&end_pc, record->payload, 8);
        memcpy(&insn_count, record->payload + 8, 4);
        fprintf(stderr, " end_pc=0x%llx insns=%u", (unsigned long long)end_pc, insn_count);
        break;
    }

    case TRACE_EVENT_BLOCK_ENTRY: {
        uint32_t gadget_count;
        memcpy(&gadget_count, record->payload, 4);
        fprintf(stderr, " gadgets=%u", gadget_count);
        break;
    }

    case TRACE_EVENT_BLOCK_EXIT: {
        int exit_reason;
        uint64_t next_pc;
        memcpy(&exit_reason, record->payload, 4);
        memcpy(&next_pc, record->payload + 8, 8);
        fprintf(stderr, " reason=%d next_pc=0x%llx", exit_reason, (unsigned long long)next_pc);
        break;
    }

    case TRACE_EVENT_BLOCK_CACHE_HIT: {
        uint32_t cache_level;
        memcpy(&cache_level, record->payload, 4);
        fprintf(stderr, " cache_level=%u", cache_level);
        break;
    }

    case TRACE_EVENT_FAULT: {
        uint64_t fault_addr;
        int is_write;
        memcpy(&fault_addr, record->payload, 8);
        memcpy(&is_write, record->payload + 8, 4);
        fprintf(stderr, " fault_addr=0x%llx is_write=%d", (unsigned long long)fault_addr, is_write);
        break;
    }

    case TRACE_EVENT_SYSCALL_ENTER: {
        uint64_t num, x0, x1, x2;
        memcpy(&num, record->payload, 8);
        memcpy(&x0, record->payload + 8, 8);
        memcpy(&x1, record->payload + 16, 8);
        memcpy(&x2, record->payload + 24, 8);
        fprintf(stderr, " syscall=%llu x0=0x%llx x1=0x%llx x2=0x%llx", (unsigned long long)num,
                (unsigned long long)x0, (unsigned long long)x1, (unsigned long long)x2);
        break;
    }

    case TRACE_EVENT_SYSCALL_RETURN: {
        uint64_t retval;
        memcpy(&retval, record->payload, 8);
        fprintf(stderr, " retval=0x%llx", (unsigned long long)retval);
        break;
    }

    case TRACE_EVENT_PROCESS_ENTRY: {
        uint64_t sp, at_entry, at_base;
        memcpy(&sp, record->payload, 8);
        memcpy(&at_entry, record->payload + 8, 8);
        memcpy(&at_base, record->payload + 16, 8);
        fprintf(stderr, " sp=0x%llx at_entry=0x%llx at_base=0x%llx", (unsigned long long)sp,
                (unsigned long long)at_entry, (unsigned long long)at_base);
        break;
    }

    case TRACE_EVENT_DECODE_FAILURE:
    case TRACE_EVENT_UNSUPPORTED_INSTRUCTION: {
        uint32_t raw_insn;
        memcpy(&raw_insn, record->payload, 4);
        fprintf(stderr, " insn=0x%08x", raw_insn);
        break;
    }

    case TRACE_EVENT_REGISTER_SNAPSHOT: {
        fprintf(stderr, " regs=");
        for (int i = 0; i < 6; i++) {
            uint64_t reg;
            memcpy(&reg, record->payload + i * 8, 8);
            fprintf(stderr, "x%d=0x%llx ", i, (unsigned long long)reg);
        }
        break;
    }

    case TRACE_EVENT_PSTATE_SNAPSHOT: {
        uint64_t pstate, nzcv;
        memcpy(&pstate, record->payload, 8);
        memcpy(&nzcv, record->payload + 8, 8);
        fprintf(stderr, " pstate=0x%llx nzcv=0x%llx", (unsigned long long)pstate,
                (unsigned long long)nzcv);
        break;
    }

    case TRACE_EVENT_TASK_START: {
        uint32_t pid;
        memcpy(&pid, record->payload, 4);
        fprintf(stderr, " pid=%u", pid);
        break;
    }

    case TRACE_EVENT_APP_TASK_START_RUNLOOP: {
        fprintf(stderr, " (app entering runloop)");
        break;
    }

    case TRACE_EVENT_APP_TRACE_BOOTSTRAP_STARTED: {
        fprintf(stderr, " (app trace bootstrap started)");
        break;
    }

    case TRACE_EVENT_APP_BOOT_STARTED: {
        fprintf(stderr, " (app boot started)");
        break;
    }

    case TRACE_EVENT_APP_UI_SESSION_STARTED: {
        fprintf(stderr, " (app UI session started)");
        break;
    }

    case TRACE_EVENT_APP_LAUNCH_COMPLETED: {
        fprintf(stderr, " (app launch completed)");
        break;
    }

    case TRACE_EVENT_TASK_THREAD_ENTRY: {
        uint64_t task_ptr;
        memcpy(&task_ptr, record->payload, 8);
        fprintf(stderr, " task=0x%llx", (unsigned long long)task_ptr);
        break;
    }

    case TRACE_EVENT_TASK_THREAD_CURRENT_SET: {
        uint32_t pid;
        uint64_t mm, mem;
        memcpy(&pid, record->payload, 4);
        memcpy(&mm, record->payload + 4, 8);
        memcpy(&mem, record->payload + 12, 8);
        fprintf(stderr, " pid=%u mm=0x%llx mem=0x%llx", pid, (unsigned long long)mm,
                (unsigned long long)mem);
        break;
    }

    case TRACE_EVENT_TASK_RUN_CURRENT_ENTRY: {
        uint64_t current_ptr;
        uint32_t pid;
        memcpy(&current_ptr, record->payload, 8);
        memcpy(&pid, record->payload + 8, 4);
        fprintf(stderr, " current=0x%llx pid=%u", (unsigned long long)current_ptr, pid);
        break;
    }

    case TRACE_EVENT_TASK_RUN_CURRENT_MEM_CHECK: {
        uint64_t mm, mem;
        memcpy(&mm, record->payload, 8);
        memcpy(&mem, record->payload + 8, 8);
        fprintf(stderr, " mm=0x%llx mem=0x%llx", (unsigned long long)mm, (unsigned long long)mem);
        break;
    }

    case TRACE_EVENT_TASK_CREATE: {
        uint32_t pid, parent_pid;
        memcpy(&pid, record->payload, 4);
        memcpy(&parent_pid, record->payload + 4, 4);
        fprintf(stderr, " pid=%u parent_pid=%u", pid, parent_pid);
        break;
    }

    case TRACE_EVENT_TASK_THREAD_BEFORE_SET: {
        uint64_t task_ptr, current_before;
        memcpy(&task_ptr, record->payload, 8);
        memcpy(&current_before, record->payload + 8, 8);
        fprintf(stderr, " task=0x%llx current_before=0x%llx", (unsigned long long)task_ptr,
                (unsigned long long)current_before);
        break;
    }

    case TRACE_EVENT_TASK_THREAD_AFTER_SET: {
        uint64_t task_ptr, current_after;
        memcpy(&task_ptr, record->payload, 8);
        memcpy(&current_after, record->payload + 8, 8);
        fprintf(stderr, " task=0x%llx current_after=0x%llx", (unsigned long long)task_ptr,
                (unsigned long long)current_after);
        break;
    }

    case TRACE_EVENT_TASK_RUN_CURRENT_ENTRY_CHECK: {
        uint64_t current_ptr;
        memcpy(&current_ptr, record->payload, 8);
        fprintf(stderr, " current=0x%llx", (unsigned long long)current_ptr);
        break;
    }

    case TRACE_EVENT_TASK_HANDOFF_PARENT_PRE_CREATE:
    case TRACE_EVENT_TASK_HANDOFF_CHILD_ENTRY:
    case TRACE_EVENT_TASK_HANDOFF_CHILD_POST_CURRENT:
    case TRACE_EVENT_TASK_HANDOFF_CHILD_PRE_RUN: {
        uint64_t task_ptr, mm, mem;
        uint32_t pid;
        memcpy(&task_ptr, record->payload, 8);
        memcpy(&mm, record->payload + 8, 8);
        memcpy(&mem, record->payload + 16, 8);
        memcpy(&pid, record->payload + 24, 4);
        fprintf(stderr, " task=0x%llx mm=0x%llx mem=0x%llx pid=%u", (unsigned long long)task_ptr,
                (unsigned long long)mm, (unsigned long long)mem, pid);
        break;
    }

    case TRACE_EVENT_TASK_MEM_SNAPSHOT: {
        uint64_t task_ptr, canary, mm, mem;
        uint32_t pid;
        uint64_t first_8, bytes_8_16, bytes_16_24, bytes_24_32;
        memcpy(&task_ptr, record->payload + 0, 8);
        memcpy(&canary, record->payload + 16, 8);
        memcpy(&pid, record->payload + 32, 4);
        memcpy(&mm, record->payload + 40, 8);
        memcpy(&mem, record->payload + 48, 8);
        memcpy(&first_8, record->payload + 56, 8);
        memcpy(&bytes_8_16, record->payload + 64, 8);
        memcpy(&bytes_16_24, record->payload + 72, 8);
        memcpy(&bytes_24_32, record->payload + 80, 8);
        fprintf(stderr,
                " task=0x%llx canary=0x%llx pid=%u mm=0x%llx mem=0x%llx raw=[%016llx %016llx "
                "%016llx %016llx]",
                (unsigned long long)task_ptr, (unsigned long long)canary, pid,
                (unsigned long long)mm, (unsigned long long)mem, (unsigned long long)first_8,
                (unsigned long long)bytes_8_16, (unsigned long long)bytes_16_24,
                (unsigned long long)bytes_24_32);
        break;
    }

    case TRACE_EVENT_TASK_CANARY: {
        uint64_t task_ptr, canary_value;
        uint8_t operation;
        memcpy(&task_ptr, record->payload, 8);
        memcpy(&canary_value, record->payload + 8, 8);
        memcpy(&operation, record->payload + 16, 1);
        fprintf(stderr, " task=0x%llx canary=0x%llx op=%s", (unsigned long long)task_ptr,
                (unsigned long long)canary_value, operation == 0 ? "write" : "read");
        break;
    }

    case TRACE_EVENT_TASK_CHECKPOINT: {
        uint64_t task_ptr;
        uint32_t checkpoint_id, pid;
        uint64_t mm, mem;
        memcpy(&task_ptr, record->payload + 0, 8);
        memcpy(&checkpoint_id, record->payload + 8, 4);
        memcpy(&pid, record->payload + 12, 4);
        memcpy(&mm, record->payload + 16, 8);
        memcpy(&mem, record->payload + 24, 8);
        fprintf(stderr, " checkpoint=%u task=0x%llx pid=%u mm=0x%llx mem=0x%llx", checkpoint_id,
                (unsigned long long)task_ptr, pid, (unsigned long long)mm, (unsigned long long)mem);
        break;
    }

    case TRACE_EVENT_EXEC_MM_BOUNDARY: {
        uint64_t current_ptr, mm, mem, old_mm, new_mm;
        uint32_t pid;
        uint8_t operation;
        int err;
        memcpy(&current_ptr, record->payload + 0, 8);
        memcpy(&pid, record->payload + 8, 4);
        memcpy(&mm, record->payload + 16, 8);
        memcpy(&mem, record->payload + 24, 8);
        memcpy(&old_mm, record->payload + 32, 8);
        memcpy(&new_mm, record->payload + 40, 8);
        memcpy(&operation, record->payload + 48, 1);
        memcpy(&err, record->payload + 49, 4);
        const char *op_name;
        switch (operation) {
        case 0:
            op_name = "before_mm_release";
            break;
        case 1:
            op_name = "after_mm_release";
            break;
        case 2:
            op_name = "before_task_set_mm";
            break;
        case 3:
            op_name = "after_task_set_mm";
            break;
        default:
            op_name = "unknown";
            break;
        }
        fprintf(
            stderr,
            " current=0x%llx pid=%u mm=0x%llx mem=0x%llx old_mm=0x%llx new_mm=0x%llx op=%s err=%d",
            (unsigned long long)current_ptr, pid, (unsigned long long)mm, (unsigned long long)mem,
            (unsigned long long)old_mm, (unsigned long long)new_mm, op_name, err);
        break;
    }

    case TRACE_EVENT_EXEC_PATH_BOUNDARY: {
        uint64_t current_ptr, mm, mem;
        uint32_t pid;
        uint8_t point;
        int err;
        memcpy(&current_ptr, record->payload + 0, 8);
        memcpy(&pid, record->payload + 8, 4);
        memcpy(&mm, record->payload + 16, 8);
        memcpy(&mem, record->payload + 24, 8);
        memcpy(&point, record->payload + 32, 1);
        memcpy(&err, record->payload + 33, 4);
        const char *point_name;
        switch (point) {
        case 0:
            point_name = "do_execve_entry";
            break;
        case 1:
            point_name = "do_execve_entry_ret";
            break;
        case 2:
            point_name = "before_format_exec";
            break;
        case 3:
            point_name = "after_format_exec";
            break;
        case 4:
            point_name = "elf_exec_entry";
            break;
        case 5:
            point_name = "before_elf_exec_return";
            break;
        case 6:
            point_name = "after_do_execve_return";
            break;
        default:
            point_name = "unknown";
            break;
        }
        fprintf(stderr, " current=0x%llx pid=%u mm=0x%llx mem=0x%llx point=%s err=%d",
                (unsigned long long)current_ptr, pid, (unsigned long long)mm,
                (unsigned long long)mem, point_name, err);
        break;
    }

    default:
        break;
    }

    fprintf(stderr, "\n");
}

static void stderr_flush(void *ctx)
{
    (void)ctx;
    fflush(stderr);
}

static int stderr_dump(void *ctx, const char *path)
{
    (void)ctx;
    (void)path;
    /* Nothing to dump for stderr backend */
    return 0;
}

static const trace_backend_ops_t stderr_ops = {
    .init = stderr_init,
    .shutdown = stderr_shutdown,
    .emit = stderr_emit,
    .flush = stderr_flush,
    .dump = stderr_dump,
};

/* ============================================
 * OS_LOG Backend - iOS/macOS Unified Logging
 * ============================================ */

#ifdef __APPLE__

static os_log_t g_os_log;

static int os_log_init(void **ctx, trace_config_t *config)
{
    (void)ctx;
    (void)config;
    g_os_log = os_log_create("com.rudironsoni.ish", "Trace");
    return 0;
}

static void os_log_shutdown(void *ctx)
{
    (void)ctx;
    if (g_os_log) {
        os_release(g_os_log);
        g_os_log = NULL;
    }
}

static void os_log_emit(void *ctx, trace_record_t *record)
{
    (void)ctx;
    if (!g_os_log || record->header.event_id >= TRACE_EVENT_MAX)
        return;

    const char *event_name = trace_event_name(record->header.event_id);

    switch (record->header.event_id) {
        /* ============================================
         * Compile and Decode Events
         * ============================================ */

    case TRACE_EVENT_BLOCK_COMPILE_START: {
        uint64_t end_pc;
        memcpy(&end_pc, record->payload, 8);
        os_log(g_os_log, "[TRACE] %s seq=%llu pc=0x%llx end_pc=0x%llx", event_name,
               (unsigned long long)record->header.seq, (unsigned long long)record->header.pc,
               (unsigned long long)end_pc);
        break;
    }

    case TRACE_EVENT_BLOCK_COMPILE_END: {
        uint64_t end_pc;
        uint32_t insn_count;
        memcpy(&end_pc, record->payload, 8);
        memcpy(&insn_count, record->payload + 8, 4);
        os_log(g_os_log, "[TRACE] %s seq=%llu pc=0x%llx end_pc=0x%llx insns=%u", event_name,
               (unsigned long long)record->header.seq, (unsigned long long)record->header.pc,
               (unsigned long long)end_pc, insn_count);
        break;
    }

    case TRACE_EVENT_DECODE_FAILURE:
    case TRACE_EVENT_UNSUPPORTED_INSTRUCTION: {
        uint32_t raw_insn;
        memcpy(&raw_insn, record->payload, 4);
        os_log(g_os_log, "[TRACE] %s seq=%llu pc=0x%llx insn=0x%08x", event_name,
               (unsigned long long)record->header.seq, (unsigned long long)record->header.pc,
               raw_insn);
        break;
    }

        /* ============================================
         * Runtime Boundary Events
         * ============================================ */

    case TRACE_EVENT_BLOCK_ENTRY: {
        uint32_t gadget_count;
        memcpy(&gadget_count, record->payload, 4);
        os_log(g_os_log, "[TRACE] %s seq=%llu pc=0x%llx gadgets=%u", event_name,
               (unsigned long long)record->header.seq, (unsigned long long)record->header.pc,
               gadget_count);
        break;
    }

    case TRACE_EVENT_BLOCK_EXIT: {
        int exit_reason;
        uint64_t next_pc;
        memcpy(&exit_reason, record->payload, 4);
        memcpy(&next_pc, record->payload + 8, 8);
        os_log(g_os_log, "[TRACE] %s seq=%llu pc=0x%llx reason=%d next_pc=0x%llx", event_name,
               (unsigned long long)record->header.seq, (unsigned long long)record->header.pc,
               exit_reason, (unsigned long long)next_pc);
        break;
    }

    case TRACE_EVENT_BLOCK_CACHE_HIT: {
        uint32_t cache_level;
        memcpy(&cache_level, record->payload, 4);
        os_log(g_os_log, "[TRACE] %s seq=%llu pc=0x%llx cache_level=%u", event_name,
               (unsigned long long)record->header.seq, (unsigned long long)record->header.pc,
               cache_level);
        break;
    }

    case TRACE_EVENT_BLOCK_CACHE_MISS: {
        os_log(g_os_log, "[TRACE] %s seq=%llu pc=0x%llx", event_name,
               (unsigned long long)record->header.seq, (unsigned long long)record->header.pc);
        break;
    }

    case TRACE_EVENT_FAULT: {
        uint64_t fault_addr;
        int is_write;
        int reason;
        memcpy(&fault_addr, record->payload, 8);
        memcpy(&is_write, record->payload + 8, 4);
        memcpy(&reason, record->payload + 16, 4);
        os_log(g_os_log, "[TRACE] %s seq=%llu pc=0x%llx fault_addr=0x%llx is_write=%d reason=%d",
               event_name, (unsigned long long)record->header.seq,
               (unsigned long long)record->header.pc, (unsigned long long)fault_addr, is_write,
               reason);
        break;
    }

    case TRACE_EVENT_SYSCALL_ENTER: {
        uint64_t num, x0, x1, x2;
        memcpy(&num, record->payload, 8);
        memcpy(&x0, record->payload + 8, 8);
        memcpy(&x1, record->payload + 16, 8);
        memcpy(&x2, record->payload + 24, 8);
        os_log(g_os_log, "[TRACE] %s seq=%llu pc=0x%llx syscall=%llu x0=0x%llx x1=0x%llx x2=0x%llx",
               event_name, (unsigned long long)record->header.seq,
               (unsigned long long)record->header.pc, (unsigned long long)num,
               (unsigned long long)x0, (unsigned long long)x1, (unsigned long long)x2);
        break;
    }

    case TRACE_EVENT_SYSCALL_RETURN: {
        uint64_t retval;
        memcpy(&retval, record->payload, 8);
        os_log(g_os_log, "[TRACE] %s seq=%llu pc=0x%llx retval=0x%llx", event_name,
               (unsigned long long)record->header.seq, (unsigned long long)record->header.pc,
               (unsigned long long)retval);
        break;
    }

    case TRACE_EVENT_PROCESS_ENTRY: {
        uint64_t sp, at_entry, at_base;
        memcpy(&sp, record->payload, 8);
        memcpy(&at_entry, record->payload + 8, 8);
        memcpy(&at_base, record->payload + 16, 8);
        os_log(g_os_log, "[TRACE] %s seq=%llu pc=0x%llx sp=0x%llx at_entry=0x%llx at_base=0x%llx",
               event_name, (unsigned long long)record->header.seq,
               (unsigned long long)record->header.pc, (unsigned long long)sp,
               (unsigned long long)at_entry, (unsigned long long)at_base);
        break;
    }

        /* ============================================
         * Block Detail Events
         * ============================================ */

    case TRACE_EVENT_REGISTER_SNAPSHOT: {
        uint64_t regs[6];
        for (int i = 0; i < 6; i++) {
            memcpy(&regs[i], record->payload + i * 8, 8);
        }
        os_log(g_os_log,
               "[TRACE] %s seq=%llu pc=0x%llx x0=0x%llx x1=0x%llx x2=0x%llx x3=0x%llx "
               "x4=0x%llx x5=0x%llx",
               event_name, (unsigned long long)record->header.seq,
               (unsigned long long)record->header.pc, (unsigned long long)regs[0],
               (unsigned long long)regs[1], (unsigned long long)regs[2],
               (unsigned long long)regs[3], (unsigned long long)regs[4],
               (unsigned long long)regs[5]);
        break;
    }

    case TRACE_EVENT_PSTATE_SNAPSHOT: {
        uint64_t pstate, nzcv;
        memcpy(&pstate, record->payload, 8);
        memcpy(&nzcv, record->payload + 8, 8);
        os_log(g_os_log, "[TRACE] %s seq=%llu pc=0x%llx pstate=0x%llx nzcv=0x%llx", event_name,
               (unsigned long long)record->header.seq, (unsigned long long)record->header.pc,
               (unsigned long long)pstate, (unsigned long long)nzcv);
        break;
    }

        /* ============================================
         * Emulation Limitation Events
         * ============================================ */

    case TRACE_EVENT_UNHANDLED_MRS:
    case TRACE_EVENT_UNHANDLED_MSR: {
        uint32_t sysreg;
        memcpy(&sysreg, record->payload, 4);
        os_log(g_os_log, "[TRACE] %s seq=%llu pc=0x%llx sysreg=0x%08x", event_name,
               (unsigned long long)record->header.seq, (unsigned long long)record->header.pc,
               sysreg);
        break;
    }

        /* ============================================
         * Runtime Error Events
         * ============================================ */

    case TRACE_EVENT_COMPLEX_UNKNOWN: {
        uint16_t cat, subtype;
        memcpy(&cat, record->payload, 2);
        memcpy(&subtype, record->payload + 2, 2);
        os_log(g_os_log, "[TRACE] %s seq=%llu pc=0x%llx cat=%u subtype=%u", event_name,
               (unsigned long long)record->header.seq, (unsigned long long)record->header.pc, cat,
               subtype);
        break;
    }

    case TRACE_EVENT_COMPLEX_DECODE_FAIL: {
        uint32_t raw_insn;
        memcpy(&raw_insn, record->payload, 4);
        os_log(g_os_log, "[TRACE] %s seq=%llu pc=0x%llx insn=0x%08x", event_name,
               (unsigned long long)record->header.seq, (unsigned long long)record->header.pc,
               raw_insn);
        break;
    }

    case TRACE_EVENT_COMPLEX_FETCH_FAIL: {
        uint32_t fault_reason;
        memcpy(&fault_reason, record->payload, 4);
        os_log(g_os_log, "[TRACE] %s seq=%llu pc=0x%llx reason=%u", event_name,
               (unsigned long long)record->header.seq, (unsigned long long)record->header.pc,
               fault_reason);
        break;
    }

        /* ============================================
         * Task Diagnostics
         * ============================================ */

    case TRACE_EVENT_TASK_THREAD_BEFORE_SET: {
        uint64_t task_ptr, current_before;
        memcpy(&task_ptr, record->payload, 8);
        memcpy(&current_before, record->payload + 8, 8);
        os_log(g_os_log, "[TRACE] %s seq=%llu task=%p current_before=%p", event_name,
               (unsigned long long)record->header.seq, (void *)task_ptr, (void *)current_before);
        break;
    }

    case TRACE_EVENT_TASK_THREAD_AFTER_SET: {
        uint64_t task_ptr, current_after;
        memcpy(&task_ptr, record->payload, 8);
        memcpy(&current_after, record->payload + 8, 8);
        os_log(g_os_log, "[TRACE] %s seq=%llu task=%p current_after=%p", event_name,
               (unsigned long long)record->header.seq, (void *)task_ptr, (void *)current_after);
        break;
    }

    case TRACE_EVENT_TASK_RUN_CURRENT_ENTRY_CHECK: {
        uint64_t current_ptr;
        memcpy(&current_ptr, record->payload, 8);
        os_log(g_os_log, "[TRACE] %s seq=%llu current=%p", event_name,
               (unsigned long long)record->header.seq, (void *)current_ptr);
        break;
    }

        /* ============================================
         * Task Lifecycle Events
         * ============================================ */

    case TRACE_EVENT_TASK_CREATE: {
        uint32_t pid, parent_pid;
        memcpy(&pid, record->payload, 4);
        memcpy(&parent_pid, record->payload + 4, 4);
        os_log(g_os_log, "[TRACE] %s seq=%llu pid=%u parent_pid=%u", event_name,
               (unsigned long long)record->header.seq, pid, parent_pid);
        break;
    }

    case TRACE_EVENT_TASK_START: {
        uint32_t pid;
        memcpy(&pid, record->payload, 4);
        os_log(g_os_log, "[TRACE] %s seq=%llu pid=%u", event_name,
               (unsigned long long)record->header.seq, pid);
        break;
    }

    case TRACE_EVENT_APP_TASK_START_RUNLOOP: {
        os_log(g_os_log, "[TRACE] %s seq=%llu (app entering runloop)", event_name,
               (unsigned long long)record->header.seq);
        break;
    }

    case TRACE_EVENT_APP_TRACE_BOOTSTRAP_STARTED: {
        os_log(g_os_log, "[TRACE] %s seq=%llu (app trace bootstrap started)", event_name,
               (unsigned long long)record->header.seq);
        break;
    }

    case TRACE_EVENT_APP_BOOT_STARTED: {
        os_log(g_os_log, "[TRACE] %s seq=%llu (app boot started)", event_name,
               (unsigned long long)record->header.seq);
        break;
    }

    case TRACE_EVENT_APP_UI_SESSION_STARTED: {
        os_log(g_os_log, "[TRACE] %s seq=%llu (app UI session started)", event_name,
               (unsigned long long)record->header.seq);
        break;
    }

    case TRACE_EVENT_APP_LAUNCH_COMPLETED: {
        os_log(g_os_log, "[TRACE] %s seq=%llu (app launch completed)", event_name,
               (unsigned long long)record->header.seq);
        break;
    }

        /* ============================================
         * Task Thread Diagnostics
         * ============================================ */

    case TRACE_EVENT_TASK_THREAD_ENTRY: {
        uint64_t task_ptr;
        memcpy(&task_ptr, record->payload, 8);
        os_log(g_os_log, "[TRACE] %s seq=%llu task=%p", event_name,
               (unsigned long long)record->header.seq, (void *)task_ptr);
        break;
    }

    case TRACE_EVENT_TASK_THREAD_CURRENT_SET: {
        uint32_t pid;
        uint64_t mm, mem;
        memcpy(&pid, record->payload, 4);
        memcpy(&mm, record->payload + 4, 8);
        memcpy(&mem, record->payload + 12, 8);
        os_log(g_os_log, "[TRACE] %s seq=%llu pid=%u mm=%p mem=%p", event_name,
               (unsigned long long)record->header.seq, pid, (void *)mm, (void *)mem);
        break;
    }

    case TRACE_EVENT_TASK_RUN_CURRENT_ENTRY: {
        uint64_t current_ptr;
        uint32_t pid;
        memcpy(&current_ptr, record->payload, 8);
        memcpy(&pid, record->payload + 8, 4);
        os_log(g_os_log, "[TRACE] %s seq=%llu current=%p pid=%u", event_name,
               (unsigned long long)record->header.seq, (void *)current_ptr, pid);
        break;
    }

    case TRACE_EVENT_TASK_RUN_CURRENT_MEM_CHECK: {
        uint64_t mm, mem;
        memcpy(&mm, record->payload, 8);
        memcpy(&mem, record->payload + 8, 8);
        os_log(g_os_log, "[TRACE] %s seq=%llu mm=%p mem=%p", event_name,
               (unsigned long long)record->header.seq, (void *)mm, (void *)mem);
        break;
    }

        /* ============================================
         * Additional Task Diagnostics
         * ============================================ */

    case TRACE_EVENT_TASK_CREATE_RETURN: {
        uint32_t pid;
        uint64_t task_ptr;
        memcpy(&pid, record->payload, 4);
        memcpy(&task_ptr, record->payload + 8, 8);
        os_log(g_os_log, "[TRACE] %s seq=%llu pid=%u task=%p", event_name,
               (unsigned long long)record->header.seq, pid, (void *)task_ptr);
        break;
    }

    case TRACE_EVENT_CONSTRUCT_TASK_DONE: {
        uint32_t pid;
        uint64_t task_ptr, mm, mem;
        memcpy(&pid, record->payload, 4);
        memcpy(&task_ptr, record->payload + 8, 8);
        memcpy(&mm, record->payload + 16, 8);
        memcpy(&mem, record->payload + 24, 8);
        os_log(g_os_log, "[TRACE] %s seq=%llu pid=%u task=%p mm=%p mem=%p", event_name,
               (unsigned long long)record->header.seq, pid, (void *)task_ptr, (void *)mm,
               (void *)mem);
        break;
    }

    case TRACE_EVENT_TASK_START_POINTER: {
        uint64_t task_ptr;
        memcpy(&task_ptr, record->payload, 8);
        os_log(g_os_log, "[TRACE] %s seq=%llu task=%p", event_name,
               (unsigned long long)record->header.seq, (void *)task_ptr);
        break;
    }

        /* ============================================
         * MM Lifecycle Events
         * ============================================ */

    case TRACE_EVENT_MM_NEW: {
        uint64_t mm_ptr;
        memcpy(&mm_ptr, record->payload, 8);
        os_log(g_os_log, "[TRACE] %s seq=%llu mm=%p", event_name,
               (unsigned long long)record->header.seq, (void *)mm_ptr);
        break;
    }

    case TRACE_EVENT_MM_COPY: {
        uint64_t src_mm, new_mm;
        memcpy(&src_mm, record->payload, 8);
        memcpy(&new_mm, record->payload + 8, 8);
        os_log(g_os_log, "[TRACE] %s seq=%llu src_mm=%p new_mm=%p", event_name,
               (unsigned long long)record->header.seq, (void *)src_mm, (void *)new_mm);
        break;
    }

    case TRACE_EVENT_MM_RETAIN: {
        uint64_t mm_ptr;
        uint32_t new_refcount;
        memcpy(&mm_ptr, record->payload, 8);
        memcpy(&new_refcount, record->payload + 8, 4);
        os_log(g_os_log, "[TRACE] %s seq=%llu mm=%p refcount=%u", event_name,
               (unsigned long long)record->header.seq, (void *)mm_ptr, new_refcount);
        break;
    }

    case TRACE_EVENT_MM_RELEASE: {
        uint64_t mm_ptr;
        uint32_t old_refcount;
        memcpy(&mm_ptr, record->payload, 8);
        memcpy(&old_refcount, record->payload + 8, 4);
        os_log(g_os_log, "[TRACE] %s seq=%llu mm=%p refcount=%u", event_name,
               (unsigned long long)record->header.seq, (void *)mm_ptr, old_refcount);
        break;
    }

    case TRACE_EVENT_MM_RELEASE_FREED: {
        uint64_t mm_ptr;
        memcpy(&mm_ptr, record->payload, 8);
        os_log(g_os_log, "[TRACE] %s seq=%llu mm=%p", event_name,
               (unsigned long long)record->header.seq, (void *)mm_ptr);
        break;
    }

    case TRACE_EVENT_TASK_SET_MM: {
        uint64_t task_ptr, new_mm;
        memcpy(&task_ptr, record->payload, 8);
        memcpy(&new_mm, record->payload + 8, 8);
        os_log(g_os_log, "[TRACE] %s seq=%llu task=%p mm=%p", event_name,
               (unsigned long long)record->header.seq, (void *)task_ptr, (void *)new_mm);
        break;
    }

        /* ============================================
         * Task Handoff Events
         * ============================================ */

    case TRACE_EVENT_TASK_HANDOFF_PARENT_PRE_CREATE: {
        uint64_t task_ptr, mm, mem, host_thread_id;
        uint32_t pid;
        memcpy(&task_ptr, record->payload, 8);
        memcpy(&mm, record->payload + 8, 8);
        memcpy(&mem, record->payload + 16, 8);
        memcpy(&pid, record->payload + 24, 4);
        memcpy(&host_thread_id, record->payload + 32, 8);
        os_log(g_os_log, "[TRACE] %s seq=%llu task=%p mm=%p mem=%p pid=%u host_tid=%llu",
               event_name, (unsigned long long)record->header.seq, (void *)task_ptr, (void *)mm,
               (void *)mem, pid, (unsigned long long)host_thread_id);
        break;
    }

    case TRACE_EVENT_TASK_HANDOFF_CHILD_ENTRY: {
        uint64_t task_arg_ptr, task_arg_mm, task_arg_mem, host_thread_id;
        uint32_t task_arg_pid;
        memcpy(&task_arg_ptr, record->payload, 8);
        memcpy(&task_arg_mm, record->payload + 8, 8);
        memcpy(&task_arg_mem, record->payload + 16, 8);
        memcpy(&task_arg_pid, record->payload + 24, 4);
        memcpy(&host_thread_id, record->payload + 32, 8);
        os_log(g_os_log, "[TRACE] %s seq=%llu task=%p mm=%p mem=%p pid=%u host_tid=%llu",
               event_name, (unsigned long long)record->header.seq, (void *)task_arg_ptr,
               (void *)task_arg_mm, (void *)task_arg_mem, task_arg_pid,
               (unsigned long long)host_thread_id);
        break;
    }

    case TRACE_EVENT_TASK_HANDOFF_CHILD_POST_CURRENT: {
        uint64_t current_ptr, current_mm, current_mem, host_thread_id;
        uint32_t current_pid;
        memcpy(&current_ptr, record->payload, 8);
        memcpy(&current_mm, record->payload + 8, 8);
        memcpy(&current_mem, record->payload + 16, 8);
        memcpy(&current_pid, record->payload + 24, 4);
        memcpy(&host_thread_id, record->payload + 32, 8);
        os_log(g_os_log, "[TRACE] %s seq=%llu current=%p mm=%p mem=%p pid=%u host_tid=%llu",
               event_name, (unsigned long long)record->header.seq, (void *)current_ptr,
               (void *)current_mm, (void *)current_mem, current_pid,
               (unsigned long long)host_thread_id);
        break;
    }

    case TRACE_EVENT_TASK_HANDOFF_CHILD_PRE_RUN: {
        uint64_t current_ptr, current_mm, current_mem, host_thread_id;
        uint32_t current_pid;
        memcpy(&current_ptr, record->payload, 8);
        memcpy(&current_mm, record->payload + 8, 8);
        memcpy(&current_mem, record->payload + 16, 8);
        memcpy(&current_pid, record->payload + 24, 4);
        memcpy(&host_thread_id, record->payload + 32, 8);
        os_log(g_os_log, "[TRACE] %s seq=%llu current=%p mm=%p mem=%p pid=%u host_tid=%llu",
               event_name, (unsigned long long)record->header.seq, (void *)current_ptr,
               (void *)current_mm, (void *)current_mem, current_pid,
               (unsigned long long)host_thread_id);
        break;
    }

        /* ============================================
         * Task Initialization Events
         * ============================================ */

    case TRACE_EVENT_TASK_INIT_AFTER_PID_WRITE: {
        uint64_t task_ptr, host_thread_id;
        uint32_t pid;
        memcpy(&task_ptr, record->payload, 8);
        memcpy(&pid, record->payload + 8, 4);
        memcpy(&host_thread_id, record->payload + 12, 8);
        os_log(g_os_log, "[TRACE] %s seq=%llu task=%p pid=%u host_tid=%llu", event_name,
               (unsigned long long)record->header.seq, (void *)task_ptr, pid,
               (unsigned long long)host_thread_id);
        break;
    }

    case TRACE_EVENT_TASK_INIT_AFTER_MM_WRITE: {
        uint64_t task_ptr, mm, host_thread_id;
        memcpy(&task_ptr, record->payload, 8);
        memcpy(&mm, record->payload + 8, 8);
        memcpy(&host_thread_id, record->payload + 16, 8);
        os_log(g_os_log, "[TRACE] %s seq=%llu task=%p mm=%p host_tid=%llu", event_name,
               (unsigned long long)record->header.seq, (void *)task_ptr, (void *)mm,
               (unsigned long long)host_thread_id);
        break;
    }

    case TRACE_EVENT_TASK_INIT_AFTER_MEM_WRITE: {
        uint64_t task_ptr, mem, host_thread_id;
        memcpy(&task_ptr, record->payload, 8);
        memcpy(&mem, record->payload + 8, 8);
        memcpy(&host_thread_id, record->payload + 16, 8);
        os_log(g_os_log, "[TRACE] %s seq=%llu task=%p mem=%p host_tid=%llu", event_name,
               (unsigned long long)record->header.seq, (void *)task_ptr, (void *)mem,
               (unsigned long long)host_thread_id);
        break;
    }

    case TRACE_EVENT_TASK_INIT_PRE_PTHREAD_CREATE: {
        uint64_t task_ptr, mm, mem, host_thread_id;
        uint32_t pid;
        memcpy(&task_ptr, record->payload, 8);
        memcpy(&mm, record->payload + 8, 8);
        memcpy(&mem, record->payload + 16, 8);
        memcpy(&pid, record->payload + 24, 4);
        memcpy(&host_thread_id, record->payload + 32, 8);
        os_log(g_os_log, "[TRACE] %s seq=%llu task=%p mm=%p mem=%p pid=%u host_tid=%llu",
               event_name, (unsigned long long)record->header.seq, (void *)task_ptr, (void *)mm,
               (void *)mem, pid, (unsigned long long)host_thread_id);
        break;
    }

        /* ============================================
         * Task Field Write
         * ============================================ */

    case TRACE_EVENT_TASK_FIELD_WRITE: {
        uint64_t task_ptr, ptr_val, host_thread_id;
        uint8_t field_id, site_id;
        uint32_t pid_val;
        memcpy(&task_ptr, record->payload, 8);
        memcpy(&field_id, record->payload + 8, 1);
        memcpy(&site_id, record->payload + 9, 1);
        memcpy(&pid_val, record->payload + 10, 4);
        memcpy(&ptr_val, record->payload + 14, 8);
        memcpy(&host_thread_id, record->payload + 24, 8);
        const char *field_name = (field_id == 0) ? "pid" : (field_id == 1) ? "mm" : "mem";
        os_log(g_os_log, "[TRACE] %s seq=%llu task=%p field=%s site=%u pid=%u ptr=%p host_tid=%llu",
               event_name, (unsigned long long)record->header.seq, (void *)task_ptr, field_name,
               site_id, pid_val, (void *)ptr_val, (unsigned long long)host_thread_id);
        break;
    }

        /* ============================================
         * Task Checkpoint
         * ============================================ */

    case TRACE_EVENT_TASK_CHECKPOINT: {
        uint64_t task_ptr, mm, mem, host_thread_id;
        uint32_t checkpoint_id, pid;
        memcpy(&task_ptr, record->payload, 8);
        memcpy(&checkpoint_id, record->payload + 8, 4);
        memcpy(&pid, record->payload + 12, 4);
        memcpy(&mm, record->payload + 16, 8);
        memcpy(&mem, record->payload + 24, 8);
        memcpy(&host_thread_id, record->payload + 32, 8);
        os_log(g_os_log,
               "[TRACE] %s seq=%llu checkpoint=%u task=%p pid=%u mm=%p mem=%p host_tid=%llu",
               event_name, (unsigned long long)record->header.seq, checkpoint_id, (void *)task_ptr,
               pid, (void *)mm, (void *)mem, (unsigned long long)host_thread_id);
        break;
    }

        /* ============================================
         * Memory Snapshot
         * ============================================ */

    case TRACE_EVENT_TASK_MEM_SNAPSHOT: {
        uint64_t task_ptr, snapshot_id, canary_value, host_thread_id;
        uint64_t mm_field, mem_field, first_8, bytes_8_16, bytes_16_24, bytes_24_32;
        uint32_t pid_field;
        memcpy(&task_ptr, record->payload + 0, 8);
        memcpy(&snapshot_id, record->payload + 8, 8);
        memcpy(&canary_value, record->payload + 16, 8);
        memcpy(&host_thread_id, record->payload + 24, 8);
        memcpy(&pid_field, record->payload + 32, 4);
        memcpy(&mm_field, record->payload + 40, 8);
        memcpy(&mem_field, record->payload + 48, 8);
        memcpy(&first_8, record->payload + 56, 8);
        memcpy(&bytes_8_16, record->payload + 64, 8);
        memcpy(&bytes_16_24, record->payload + 72, 8);
        memcpy(&bytes_24_32, record->payload + 80, 8);
        os_log(g_os_log,
               "[TRACE] %s seq=%llu task=%p snap_id=%llu canary=0x%llx host_tid=%llu "
               "pid=%u mm=%p mem=%p raw=[%016llx %016llx %016llx %016llx]",
               event_name, (unsigned long long)record->header.seq, (void *)task_ptr,
               (unsigned long long)snapshot_id, (unsigned long long)canary_value,
               (unsigned long long)host_thread_id, pid_field, (void *)mm_field, (void *)mem_field,
               (unsigned long long)first_8, (unsigned long long)bytes_8_16,
               (unsigned long long)bytes_16_24, (unsigned long long)bytes_24_32);
        break;
    }

        /* ============================================
         * Bulk Write
         * ============================================ */

    case TRACE_EVENT_TASK_BULK_WRITE: {
        uint64_t target_ptr, source_ptr, host_thread_id;
        uint32_t size;
        uint8_t operation;
        memcpy(&target_ptr, record->payload, 8);
        memcpy(&source_ptr, record->payload + 8, 8);
        memcpy(&size, record->payload + 16, 4);
        memcpy(&operation, record->payload + 20, 1);
        memcpy(&host_thread_id, record->payload + 24, 8);
        const char *op_name = (operation == 0)   ? "memset"
                              : (operation == 1) ? "memcpy"
                                                 : "struct_assign";
        os_log(g_os_log, "[TRACE] %s seq=%llu target=%p source=%p size=%u op=%s host_tid=%llu",
               event_name, (unsigned long long)record->header.seq, (void *)target_ptr,
               (void *)source_ptr, size, op_name, (unsigned long long)host_thread_id);
        break;
    }

        /* ============================================
         * Canary
         * ============================================ */

    case TRACE_EVENT_TASK_CANARY: {
        uint64_t task_ptr, canary_value, host_thread_id;
        uint8_t operation;
        memcpy(&task_ptr, record->payload, 8);
        memcpy(&canary_value, record->payload + 8, 8);
        memcpy(&operation, record->payload + 16, 1);
        memcpy(&host_thread_id, record->payload + 17, 8);
        const char *op_name = (operation == 0) ? "write" : "read";
        os_log(g_os_log, "[TRACE] %s seq=%llu task=%p canary=0x%llx op=%s host_tid=%llu",
               event_name, (unsigned long long)record->header.seq, (void *)task_ptr,
               (unsigned long long)canary_value, op_name, (unsigned long long)host_thread_id);
        break;
    }

        /* ============================================
         * Exec Path Investigation Events
         * ============================================ */

    case TRACE_EVENT_EXEC_MM_BOUNDARY: {
        uint64_t current_ptr, mm, mem, old_mm, new_mm;
        uint32_t pid;
        uint8_t operation;
        int err;
        memcpy(&current_ptr, record->payload + 0, 8);
        memcpy(&pid, record->payload + 8, 4);
        memcpy(&mm, record->payload + 16, 8);
        memcpy(&mem, record->payload + 24, 8);
        memcpy(&old_mm, record->payload + 32, 8);
        memcpy(&new_mm, record->payload + 40, 8);
        memcpy(&operation, record->payload + 48, 1);
        memcpy(&err, record->payload + 49, 4);
        const char *op_name;
        switch (operation) {
        case 0:
            op_name = "before_mm_release";
            break;
        case 1:
            op_name = "after_mm_release";
            break;
        case 2:
            op_name = "before_task_set_mm";
            break;
        case 3:
            op_name = "after_task_set_mm";
            break;
        default:
            op_name = "unknown";
            break;
        }
        os_log(
            g_os_log,
            "[TRACE] %s seq=%llu current=%p pid=%u mm=%p mem=%p old_mm=%p new_mm=%p op=%s err=%d",
            event_name, (unsigned long long)record->header.seq, (void *)current_ptr, pid,
            (void *)mm, (void *)mem, (void *)old_mm, (void *)new_mm, op_name, err);
        break;
    }

    case TRACE_EVENT_EXEC_PATH_BOUNDARY: {
        uint64_t current_ptr, mm, mem;
        uint32_t pid;
        uint8_t point;
        int err;
        memcpy(&current_ptr, record->payload + 0, 8);
        memcpy(&pid, record->payload + 8, 4);
        memcpy(&mm, record->payload + 16, 8);
        memcpy(&mem, record->payload + 24, 8);
        memcpy(&point, record->payload + 32, 1);
        memcpy(&err, record->payload + 33, 4);
        const char *point_name;
        switch (point) {
        case 0:
            point_name = "do_execve_entry";
            break;
        case 1:
            point_name = "do_execve_entry_ret";
            break;
        case 2:
            point_name = "before_format_exec";
            break;
        case 3:
            point_name = "after_format_exec";
            break;
        case 4:
            point_name = "elf_exec_entry";
            break;
        case 5:
            point_name = "before_elf_exec_return";
            break;
        case 6:
            point_name = "after_do_execve_return";
            break;
        default:
            point_name = "unknown";
            break;
        }
        os_log(g_os_log, "[TRACE] %s seq=%llu current=%p pid=%u mm=%p mem=%p point=%s err=%d",
               event_name, (unsigned long long)record->header.seq, (void *)current_ptr, pid,
               (void *)mm, (void *)mem, point_name, err);
        break;
    }

    default:
        os_log(g_os_log, "[TRACE] %s seq=%llu pc=0x%llx", event_name,
               (unsigned long long)record->header.seq, (unsigned long long)record->header.pc);
        break;
    }
}

static void os_log_flush(void *ctx)
{
    (void)ctx;
    /* os_log is asynchronous, no explicit flush needed */
}

static int os_log_dump(void *ctx, const char *path)
{
    (void)ctx;
    (void)path;
    return 0;
}

static const trace_backend_ops_t os_log_backend_ops = {
    .init = os_log_init,
    .shutdown = os_log_shutdown,
    .emit = os_log_emit,
    .flush = os_log_flush,
    .dump = os_log_dump,
};

#endif /* __APPLE__ */

/* ============================================
 * Backend Selection
 * ============================================ */

const trace_backend_ops_t *trace_backend_get_ops(trace_backend_t backend)
{
    switch (backend) {
    case TRACE_BACKEND_NOP:
        return &nop_ops;
    case TRACE_BACKEND_RING:
        return trace_ring_backend_get_ops();
    case TRACE_BACKEND_STDERR:
        return &stderr_ops;
#ifdef __APPLE__
    case TRACE_BACKEND_OS_LOG:
        return &os_log_backend_ops;
#endif
    default:
        return &nop_ops;
    }
}
