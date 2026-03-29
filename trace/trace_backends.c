/*
 * trace_backends.c
 * Backend implementations: NOP, RING, STDERR
 */

#include "trace/trace.h"
#include "trace/trace_types.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/* Forward declaration - defined in trace_ring.c */
extern const trace_backend_ops_t* trace_ring_backend_get_ops(void);

/* ============================================
 * NOP Backend - Zero overhead when disabled
 * ============================================ */

static int nop_init(void **ctx, trace_config_t *config) {
    (void)ctx;
    (void)config;
    return 0;
}

static void nop_shutdown(void *ctx) {
    (void)ctx;
}

static void nop_emit(void *ctx, trace_record_t *record) {
    (void)ctx;
    (void)record;
}

static void nop_flush(void *ctx) {
    (void)ctx;
}

static int nop_dump(void *ctx, const char *path) {
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

static int stderr_init(void **ctx, trace_config_t *config) {
    (void)ctx;
    (void)config;
    fprintf(stderr, "[TRACE] Tracing enabled (stderr backend)\n");
    return 0;
}

static void stderr_shutdown(void *ctx) {
    (void)ctx;
}

static void stderr_emit(void *ctx, trace_record_t *record) {
    (void)ctx;
    
    const char *event_name = trace_event_name(record->header.event_id);
    
    fprintf(stderr, "[TRACE] seq=%llu event=%s pc=0x%016llx",
            (unsigned long long)record->header.seq,
            event_name,
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
            fprintf(stderr, " end_pc=0x%llx insns=%u",
                    (unsigned long long)end_pc, insn_count);
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
            fprintf(stderr, " reason=%d next_pc=0x%llx",
                    exit_reason, (unsigned long long)next_pc);
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
            fprintf(stderr, " fault_addr=0x%llx is_write=%d",
                    (unsigned long long)fault_addr, is_write);
            break;
        }
            
        case TRACE_EVENT_SYSCALL_ENTER: {
            uint64_t num, x0, x1, x2;
            memcpy(&num, record->payload, 8);
            memcpy(&x0, record->payload + 8, 8);
            memcpy(&x1, record->payload + 16, 8);
            memcpy(&x2, record->payload + 24, 8);
            fprintf(stderr, " syscall=%llu x0=0x%llx x1=0x%llx x2=0x%llx",
                    (unsigned long long)num,
                    (unsigned long long)x0,
                    (unsigned long long)x1,
                    (unsigned long long)x2);
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
            fprintf(stderr, " sp=0x%llx at_entry=0x%llx at_base=0x%llx",
                    (unsigned long long)sp,
                    (unsigned long long)at_entry,
                    (unsigned long long)at_base);
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
            fprintf(stderr, " pstate=0x%llx nzcv=0x%llx",
                    (unsigned long long)pstate,
                    (unsigned long long)nzcv);
            break;
        }

        case TRACE_EVENT_TASK_START: {
            uint32_t pid;
            memcpy(&pid, record->payload, 4);
            fprintf(stderr, " pid=%u", pid);
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
            fprintf(stderr, " pid=%u mm=0x%llx mem=0x%llx",
                    pid, (unsigned long long)mm, (unsigned long long)mem);
            break;
        }

        case TRACE_EVENT_TASK_RUN_CURRENT_ENTRY: {
            uint64_t current_ptr;
            uint32_t pid;
            memcpy(&current_ptr, record->payload, 8);
            memcpy(&pid, record->payload + 8, 4);
            fprintf(stderr, " current=0x%llx pid=%u",
                    (unsigned long long)current_ptr, pid);
            break;
        }

        case TRACE_EVENT_TASK_RUN_CURRENT_MEM_CHECK: {
            uint64_t mm, mem;
            memcpy(&mm, record->payload, 8);
            memcpy(&mem, record->payload + 8, 8);
            fprintf(stderr, " mm=0x%llx mem=0x%llx",
                    (unsigned long long)mm, (unsigned long long)mem);
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
            fprintf(stderr, " task=0x%llx current_before=0x%llx",
                    (unsigned long long)task_ptr, (unsigned long long)current_before);
            break;
        }

        case TRACE_EVENT_TASK_THREAD_AFTER_SET: {
            uint64_t task_ptr, current_after;
            memcpy(&task_ptr, record->payload, 8);
            memcpy(&current_after, record->payload + 8, 8);
            fprintf(stderr, " task=0x%llx current_after=0x%llx",
                    (unsigned long long)task_ptr, (unsigned long long)current_after);
            break;
        }

        case TRACE_EVENT_TASK_RUN_CURRENT_ENTRY_CHECK: {
            uint64_t current_ptr;
            memcpy(&current_ptr, record->payload, 8);
            fprintf(stderr, " current=0x%llx", (unsigned long long)current_ptr);
            break;
        }
            
        default:
            break;
    }
    
    fprintf(stderr, "\n");
}

static void stderr_flush(void *ctx) {
    (void)ctx;
    fflush(stderr);
}

static int stderr_dump(void *ctx, const char *path) {
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
 * Backend Selection
 * ============================================ */

const trace_backend_ops_t* trace_backend_get_ops(trace_backend_t backend) {
    switch (backend) {
        case TRACE_BACKEND_NOP:
            return &nop_ops;
        case TRACE_BACKEND_RING:
            return trace_ring_backend_get_ops();
        case TRACE_BACKEND_STDERR:
            return &stderr_ops;
        default:
            return &nop_ops;
    }
}
