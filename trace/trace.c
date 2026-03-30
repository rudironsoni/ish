/*
 * trace.c
 * Trace subsystem implementation with minimal semantic API and legacy support.
 *
 * This file contains:
 * 1. The NEW minimal semantic API that forwards to ISHInstrumentation
 * 2. The LEGACY event emission API preserved for kernel compatibility
 */

#include "trace/trace.h"
#include "trace/trace_types.h"
#include "app/Instrumentation/ISHInstrumentationBridge.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================
 * NEW Semantic API Implementation
 * ============================================
 *
 * These functions forward to the ISHInstrumentationBridge C API,
 * which in turn calls the Objective-C ISHInstrumentation class.
 */

void trace_bootstrap(void)
{
    ish_instrumentation_bootstrap();
}

void trace_activate(void)
{
    ish_instrumentation_activate();
}

bool trace_is_active(void)
{
    return ish_instrumentation_is_active();
}

void trace_record_event(int origin, const char *event_name)
{
    ish_instrumentation_record_event((ish_instrumentation_origin_t)origin, event_name);
}

uint64_t trace_begin_interval(int origin, const char *interval_name,
                               const void *attrs, uint32_t attr_count)
{
    return ish_instrumentation_begin_interval((ish_instrumentation_origin_t)origin, interval_name,
                                               (const ish_instrumentation_attribute_t *)attrs,
                                               attr_count);
}

void trace_end_interval(uint64_t interval_id, const void *attrs, uint32_t attr_count)
{
    ish_instrumentation_end_interval(interval_id,
                                      (const ish_instrumentation_attribute_t *)attrs,
                                      attr_count);
}

/* ============================================
 * LEGACY Support - Global Context
 * ============================================
 *
 * The global context is maintained for backward compatibility
 * with existing kernel code that relies on trace being initialized.
 */

/* Global trace context */
trace_ctx_t *g_trace_ctx = NULL;

/* Global crash ring path - set by app layer */
const char *g_crash_ring_path = NULL;

/* Event descriptor table - populated from trace_events.def */
static const trace_event_desc_t event_descriptors[TRACE_EVENT_MAX] = {
    [TRACE_EVENT_NONE] = { "NONE", TRACE_LEVEL_OFF, 0, 0 },

#define TRACE_EVENT(name, level, category, payload_size)                                           \
    [TRACE_EVENT_##name] = { #name, level, category, payload_size },
#include "trace/trace_events.def"
#undef TRACE_EVENT
};

/* Check if event should be emitted */
bool trace_event_enabled(trace_event_id_t event, uint64_t pc)
{
    (void)pc;
    if (!g_trace_ctx || !g_trace_ctx->enabled) {
        return false;
    }

    if (event >= TRACE_EVENT_MAX) {
        return false;
    }

    trace_config_t *cfg = &g_trace_ctx->config;
    const trace_event_desc_t *desc = &event_descriptors[event];

    /* Check level */
    if (desc->level > cfg->level) {
        return false;
    }

    /* Check event mask - if 0, allow all events */
    if (cfg->event_mask != 0 && !(cfg->event_mask & (1ULL << event))) {
        return false;
    }

    /* Check category mask */
    if (!(cfg->category_mask & desc->category)) {
        return false;
    }

    /* Check PC range filter */
    if (cfg->pc_start != 0 || cfg->pc_end != 0) {
        if (pc < cfg->pc_start || pc > cfg->pc_end) {
            return false;
        }
    }

    return true;
}

/* Initialize tracing - delegates to new semantic API */
int trace_init(trace_config_t *config)
{
    /* Once-only: if already initialized, return success and reuse context */
    if (g_trace_ctx) {
        return 0;
    }

    g_trace_ctx = calloc(1, sizeof(trace_ctx_t));
    if (!g_trace_ctx) {
        return -1;
    }

    /* Copy configuration */
    memcpy(&g_trace_ctx->config, config, sizeof(trace_config_t));

    /* Get backend operations */
    g_trace_ctx->backend_ops = trace_backend_get_ops(config->backend);
    if (!g_trace_ctx->backend_ops) {
        free(g_trace_ctx);
        g_trace_ctx = NULL;
        return -1;
    }

    /* Initialize backend */
    if (g_trace_ctx->backend_ops->init) {
        int ret = g_trace_ctx->backend_ops->init(&g_trace_ctx->backend_ctx, config);
        if (ret != 0) {
            free(g_trace_ctx);
            g_trace_ctx = NULL;
            return ret;
        }
    }

    /* If ring backend, store ring reference for dumping */
    if (config->backend == TRACE_BACKEND_RING) {
        /* backend_ctx is ring_ctx_t*, ring is inside it */
        typedef struct ring_ctx {
            trace_ring_t ring;
            trace_config_t *config;
        } ring_ctx_t;
        ring_ctx_t *rctx = (ring_ctx_t *)g_trace_ctx->backend_ctx;
        g_trace_ctx->ring = &rctx->ring;
    }

    g_trace_ctx->enabled = (config->level > TRACE_LEVEL_OFF);

    return 0;
}

/* Shutdown tracing */
void trace_shutdown(void)
{
    if (!g_trace_ctx) {
        return;
    }

    if (g_trace_ctx->backend_ops && g_trace_ctx->backend_ops->shutdown) {
        g_trace_ctx->backend_ops->shutdown(g_trace_ctx->backend_ctx);
    }

    free(g_trace_ctx);
    g_trace_ctx = NULL;
}

/* Get global context */
trace_ctx_t *trace_get_global(void)
{
    return g_trace_ctx;
}

/* Get current trace level */
trace_level_t trace_get_level(void)
{
    if (!g_trace_ctx) {
        return TRACE_LEVEL_OFF;
    }
    return g_trace_ctx->config.level;
}

/* Set trace level */
void trace_config_set_level(trace_level_t level)
{
    if (!g_trace_ctx) {
        return;
    }
    g_trace_ctx->config.level = level;
    g_trace_ctx->enabled = (level > TRACE_LEVEL_OFF);
}

/* Set PC range filter */
void trace_config_set_pc_range(uint64_t start, uint64_t end)
{
    if (!g_trace_ctx) {
        return;
    }
    g_trace_ctx->config.pc_start = start;
    g_trace_ctx->config.pc_end = end;
}

/* Enable event by ID */
void trace_config_enable_event(trace_event_id_t event)
{
    if (!g_trace_ctx || event >= TRACE_EVENT_MAX) {
        return;
    }
    g_trace_ctx->config.event_mask |= (1ULL << event);
}

/* Disable event by ID */
void trace_config_disable_event(trace_event_id_t event)
{
    if (!g_trace_ctx || event >= TRACE_EVENT_MAX) {
        return;
    }
    g_trace_ctx->config.event_mask &= ~(1ULL << event);
}

/* Enable category */
void trace_config_enable_category(trace_category_t category)
{
    if (!g_trace_ctx) {
        return;
    }
    g_trace_ctx->config.category_mask |= category;
}

/* Parse configuration from environment variables (DEPRECATED) */
int trace_config_from_env(trace_config_t *config)
{
    /* This function is deprecated. Returns safe minimal defaults. */
    memset(config, 0, sizeof(trace_config_t));
    config->backend = TRACE_BACKEND_NOP;
    config->level = TRACE_LEVEL_OFF;
    config->category_mask = 0;
    config->event_mask = 0;
    return 0;
}

/* ============================================
 * LEGACY Event Emission API
 * ============================================
 *
 * These implementations are preserved for kernel compatibility.
 * They emit to the new bridge backend which forwards to ISHInstrumentation.
 */

/* Emit simple event */
void trace_emit(trace_event_id_t event, uint64_t pc)
{
    if (!g_trace_ctx || !g_trace_ctx->backend_ops || !g_trace_ctx->backend_ops->emit) {
        return;
    }

    if (!trace_event_enabled(event, pc)) {
        return;
    }

    trace_record_t record;
    memset(&record, 0, sizeof(record));

    record.header.seq = g_trace_ctx->emitted_count++;
    record.header.event_id = event;
    record.header.level = event_descriptors[event].level;
    record.header.cpu_id = 0;
    record.header.pc = pc;
    record.header.payload_size = 0;

    g_trace_ctx->backend_ops->emit(g_trace_ctx->backend_ctx, &record);
}

/* Emit with 8-byte payload */
void trace_emit_u64(trace_event_id_t event, uint64_t pc, uint64_t val)
{
    if (!g_trace_ctx || !g_trace_ctx->backend_ops || !g_trace_ctx->backend_ops->emit) {
        return;
    }

    if (!trace_event_enabled(event, pc)) {
        return;
    }

    trace_record_t record;
    memset(&record, 0, sizeof(record));

    record.header.seq = g_trace_ctx->emitted_count++;
    record.header.event_id = event;
    record.header.level = event_descriptors[event].level;
    record.header.cpu_id = 0;
    record.header.pc = pc;
    record.header.payload_size = 8;

    memcpy(record.payload, &val, 8);

    g_trace_ctx->backend_ops->emit(g_trace_ctx->backend_ctx, &record);
}

/* Emit with 4-byte payload */
void trace_emit_u32(trace_event_id_t event, uint64_t pc, uint32_t val)
{
    if (!g_trace_ctx || !g_trace_ctx->backend_ops || !g_trace_ctx->backend_ops->emit) {
        return;
    }

    if (!trace_event_enabled(event, pc)) {
        return;
    }

    trace_record_t record;
    memset(&record, 0, sizeof(record));

    record.header.seq = g_trace_ctx->emitted_count++;
    record.header.event_id = event;
    record.header.level = event_descriptors[event].level;
    record.header.cpu_id = 0;
    record.header.pc = pc;
    record.header.payload_size = 4;

    memcpy(record.payload, &val, 4);

    g_trace_ctx->backend_ops->emit(g_trace_ctx->backend_ctx, &record);
}

/* Emit fault event */
void trace_emit_fault(uint64_t pc, uint64_t fault_addr, int is_write, int reason)
{
    if (!g_trace_ctx || !g_trace_ctx->backend_ops || !g_trace_ctx->backend_ops->emit) {
        return;
    }

    if (!trace_event_enabled(TRACE_EVENT_FAULT, pc)) {
        return;
    }

    trace_record_t record;
    memset(&record, 0, sizeof(record));

    record.header.seq = g_trace_ctx->emitted_count++;
    record.header.event_id = TRACE_EVENT_FAULT;
    record.header.level = TRACE_LEVEL_BOUNDARY;
    record.header.cpu_id = 0;
    record.header.pc = pc;
    record.header.payload_size = 24;

    memcpy(record.payload + 0, &fault_addr, 8);
    memcpy(record.payload + 8, &is_write, 4);
    memcpy(record.payload + 12, &reason, 4);

    g_trace_ctx->backend_ops->emit(g_trace_ctx->backend_ctx, &record);
}

/* Emit syscall enter */
void trace_emit_syscall_enter(uint64_t pc, uint64_t num, uint64_t x0, uint64_t x1, uint64_t x2)
{
    if (!g_trace_ctx || !g_trace_ctx->backend_ops || !g_trace_ctx->backend_ops->emit) {
        return;
    }

    if (!trace_event_enabled(TRACE_EVENT_SYSCALL_ENTER, pc)) {
        return;
    }

    trace_record_t record;
    memset(&record, 0, sizeof(record));

    record.header.seq = g_trace_ctx->emitted_count++;
    record.header.event_id = TRACE_EVENT_SYSCALL_ENTER;
    record.header.level = TRACE_LEVEL_BOUNDARY;
    record.header.cpu_id = 0;
    record.header.pc = pc;
    record.header.payload_size = 32;

    memcpy(record.payload + 0, &num, 8);
    memcpy(record.payload + 8, &x0, 8);
    memcpy(record.payload + 16, &x1, 8);
    memcpy(record.payload + 24, &x2, 8);

    g_trace_ctx->backend_ops->emit(g_trace_ctx->backend_ctx, &record);
}

/* Emit syscall return */
void trace_emit_syscall_return(uint64_t pc, uint64_t retval)
{
    trace_emit_u64(TRACE_EVENT_SYSCALL_RETURN, pc, retval);
}

/* Emit process entry */
void trace_emit_process_entry(uint64_t entry_pc, uint64_t sp, uint64_t at_entry, uint64_t at_base)
{
    if (!g_trace_ctx || !g_trace_ctx->backend_ops || !g_trace_ctx->backend_ops->emit) {
        return;
    }

    if (!trace_event_enabled(TRACE_EVENT_PROCESS_ENTRY, entry_pc)) {
        return;
    }

    trace_record_t record;
    memset(&record, 0, sizeof(record));

    record.header.seq = g_trace_ctx->emitted_count++;
    record.header.event_id = TRACE_EVENT_PROCESS_ENTRY;
    record.header.level = TRACE_LEVEL_SUMMARY;
    record.header.cpu_id = 0;
    record.header.pc = entry_pc;
    record.header.payload_size = 32;

    memcpy(record.payload + 0, &sp, 8);
    memcpy(record.payload + 8, &at_entry, 8);
    memcpy(record.payload + 16, &at_base, 8);

    g_trace_ctx->backend_ops->emit(g_trace_ctx->backend_ctx, &record);
}

/* Emit task creation event */
void trace_emit_task_create(uint32_t pid, uint32_t parent_pid)
{
    if (!g_trace_ctx || !g_trace_ctx->backend_ops || !g_trace_ctx->backend_ops->emit) {
        return;
    }

    if (!trace_event_enabled(TRACE_EVENT_TASK_CREATE, 0)) {
        return;
    }

    trace_record_t record;
    memset(&record, 0, sizeof(record));

    record.header.seq = g_trace_ctx->emitted_count++;
    record.header.event_id = TRACE_EVENT_TASK_CREATE;
    record.header.level = TRACE_LEVEL_SUMMARY;
    record.header.cpu_id = 0;
    record.header.pc = 0;
    record.header.payload_size = 8;

    memcpy(record.payload + 0, &pid, 4);
    memcpy(record.payload + 4, &parent_pid, 4);

    g_trace_ctx->backend_ops->emit(g_trace_ctx->backend_ctx, &record);
}

/* Emit task start event */
void trace_emit_task_start(uint32_t pid)
{
    trace_emit_u32(TRACE_EVENT_TASK_START, 0, pid);
}

/* Emit app task start runloop event */
void trace_emit_app_task_start_runloop(void)
{
    trace_emit(TRACE_EVENT_APP_TASK_START_RUNLOOP, 0);
}

/* Emit block compile start */
void trace_emit_block_compile_start(uint64_t pc)
{
    trace_emit_u64(TRACE_EVENT_BLOCK_COMPILE_START, pc, pc);
}

/* Emit block compile end */
void trace_emit_block_compile_end(uint64_t pc, uint64_t end_pc, uint32_t insn_count)
{
    if (!g_trace_ctx || !g_trace_ctx->backend_ops || !g_trace_ctx->backend_ops->emit) {
        return;
    }

    trace_record_t record;
    memset(&record, 0, sizeof(record));

    record.header.seq = g_trace_ctx->emitted_count++;
    record.header.event_id = TRACE_EVENT_BLOCK_COMPILE_END;
    record.header.level = TRACE_LEVEL_SUMMARY;
    record.header.cpu_id = 0;
    record.header.pc = pc;
    record.header.payload_size = 16;

    memcpy(record.payload + 0, &end_pc, 8);
    memcpy(record.payload + 8, &insn_count, 4);

    g_trace_ctx->backend_ops->emit(g_trace_ctx->backend_ctx, &record);
}

/* Emit block entry */
void trace_emit_block_entry(uint64_t pc, uint32_t gadget_count)
{
    trace_emit_u32(TRACE_EVENT_BLOCK_ENTRY, pc, gadget_count);
}

/* Emit block exit */
void trace_emit_block_exit(uint64_t pc, int exit_reason, uint64_t next_pc)
{
    if (!g_trace_ctx || !g_trace_ctx->backend_ops || !g_trace_ctx->backend_ops->emit) {
        return;
    }

    if (!trace_event_enabled(TRACE_EVENT_BLOCK_EXIT, pc)) {
        return;
    }

    trace_record_t record;
    memset(&record, 0, sizeof(record));

    record.header.seq = g_trace_ctx->emitted_count++;
    record.header.event_id = TRACE_EVENT_BLOCK_EXIT;
    record.header.level = TRACE_LEVEL_BOUNDARY;
    record.header.cpu_id = 0;
    record.header.pc = pc;
    record.header.payload_size = 16;

    memcpy(record.payload + 0, &exit_reason, 4);
    memcpy(record.payload + 8, &next_pc, 8);

    g_trace_ctx->backend_ops->emit(g_trace_ctx->backend_ctx, &record);
}

/* Emit block cache hit */
void trace_emit_block_cache_hit(uint64_t pc, int cache_level)
{
    trace_emit_u32(TRACE_EVENT_BLOCK_CACHE_HIT, pc, (uint32_t)cache_level);
}

/* Emit block cache miss */
void trace_emit_block_cache_miss(uint64_t pc)
{
    trace_emit(TRACE_EVENT_BLOCK_CACHE_MISS, pc);
}

/* Emit register snapshot */
void trace_emit_register_snapshot(uint64_t pc, const uint64_t *regs, uint32_t reg_mask)
{
    if (!g_trace_ctx || !g_trace_ctx->backend_ops || !g_trace_ctx->backend_ops->emit) {
        return;
    }

    trace_record_t record;
    memset(&record, 0, sizeof(record));

    record.header.seq = g_trace_ctx->emitted_count++;
    record.header.event_id = TRACE_EVENT_REGISTER_SNAPSHOT;
    record.header.level = TRACE_LEVEL_BLOCK;
    record.header.cpu_id = 0;
    record.header.pc = pc;
    record.header.payload_size = 48;

    /* Copy up to 6 registers */
    for (int i = 0; i < 6 && i < 32; i++) {
        if (reg_mask & (1U << i)) {
            memcpy(record.payload + i * 8, &regs[i], 8);
        }
    }

    g_trace_ctx->backend_ops->emit(g_trace_ctx->backend_ctx, &record);
}

/* Emit PSTATE snapshot */
void trace_emit_pstate_snapshot(uint64_t pc, uint64_t pstate, uint64_t nzcv)
{
    if (!g_trace_ctx || !g_trace_ctx->backend_ops || !g_trace_ctx->backend_ops->emit) {
        return;
    }

    trace_record_t record;
    memset(&record, 0, sizeof(record));

    record.header.seq = g_trace_ctx->emitted_count++;
    record.header.event_id = TRACE_EVENT_PSTATE_SNAPSHOT;
    record.header.level = TRACE_LEVEL_BLOCK;
    record.header.cpu_id = 0;
    record.header.pc = pc;
    record.header.payload_size = 16;

    memcpy(record.payload + 0, &pstate, 8);
    memcpy(record.payload + 8, &nzcv, 8);

    g_trace_ctx->backend_ops->emit(g_trace_ctx->backend_ctx, &record);
}

/* Get event name */
const char *trace_event_name(trace_event_id_t event)
{
    if (event >= TRACE_EVENT_MAX) {
        return "UNKNOWN";
    }
    return event_descriptors[event].name;
}

/* Get event description */
const trace_event_desc_t *trace_event_desc(trace_event_id_t event)
{
    if (event >= TRACE_EVENT_MAX) {
        return NULL;
    }
    return &event_descriptors[event];
}

/* Flush output */
void trace_flush(void)
{
    if (!g_trace_ctx || !g_trace_ctx->backend_ops || !g_trace_ctx->backend_ops->flush) {
        return;
    }
    g_trace_ctx->backend_ops->flush(g_trace_ctx->backend_ctx);
}

/* Check if sidecar should be created */
bool trace_sidecar_enabled(void)
{
    if (!g_trace_ctx || !g_trace_ctx->enabled) {
        return false;
    }
    return g_trace_ctx->config.level >= TRACE_LEVEL_BLOCK || g_trace_ctx->config.dump_on_fault;
}

/* Dump on fault */
void trace_dump_on_fault(uint64_t fault_pc, uint64_t fault_addr, int is_write)
{
    if (!g_trace_ctx) {
        return;
    }

    trace_emit_fault(fault_pc, fault_addr, is_write, 0);

    if (g_trace_ctx->config.dump_on_fault && g_trace_ctx->ring) {
        const char *path = g_trace_ctx->config.output_path[0] ? g_trace_ctx->config.output_path
                                                              : "/tmp/ish.trace.ring";
        trace_dump_ring(path);
    }

    trace_dump_ring_stderr();
}

/* Dump ring to file */
int trace_dump_ring(const char *path)
{
    if (!g_trace_ctx || !g_trace_ctx->backend_ops || !g_trace_ctx->backend_ops->dump) {
        return -1;
    }
    return g_trace_ctx->backend_ops->dump(g_trace_ctx->backend_ctx, path);
}

/* ============================================
 * Crash Recovery (Legacy - No-op now)
 * ============================================ */

int trace_persist_ring(const char *path)
{
    return trace_dump_ring(path);
}

int trace_recover_previous_run(const char *marker_path, const char *ring_path)
{
    (void)ring_path;
    FILE *marker = fopen(marker_path, "r");
    if (!marker) {
        return 0;
    }
    fclose(marker);

    trace_emit(TRACE_EVENT_APP_CRASH_RECOVERY_BUNDLE_FOUND, 0);

    remove(marker_path);

    return 1;
}

int trace_mark_run_started(const char *path)
{
    FILE *fp = fopen(path, "w");
    if (!fp)
        return -1;
    fprintf(fp, "RUN_STARTED\n");
    fclose(fp);
    return 0;
}

int trace_mark_run_completed(const char *path)
{
    remove(path);
    return 0;
}

/* Dump ring to stderr */
void trace_dump_ring_stderr(void)
{
    /* No-op: output is handled by the app layer */
}

/* ============================================
 * Additional Legacy Event Emitters
 * ============================================ */

void trace_emit_unhandled_mrs(uint64_t pc, uint32_t sysreg)
{
    trace_emit_u32(TRACE_EVENT_UNHANDLED_MRS, pc, sysreg);
}

void trace_emit_unhandled_msr(uint64_t pc, uint32_t sysreg)
{
    trace_emit_u32(TRACE_EVENT_UNHANDLED_MSR, pc, sysreg);
}

void trace_emit_complex_unknown(uint64_t pc, int cat, int subtype)
{
    if (!g_trace_ctx || !g_trace_ctx->backend_ops || !g_trace_ctx->backend_ops->emit) {
        return;
    }

    trace_record_t record;
    memset(&record, 0, sizeof(record));

    record.header.seq = g_trace_ctx->emitted_count++;
    record.header.event_id = TRACE_EVENT_COMPLEX_UNKNOWN;
    record.header.level = TRACE_LEVEL_BOUNDARY;
    record.header.cpu_id = 0;
    record.header.pc = pc;
    record.header.payload_size = 4;

    uint16_t payload[2] = { (uint16_t)cat, (uint16_t)subtype };
    memcpy(record.payload, payload, 4);

    g_trace_ctx->backend_ops->emit(g_trace_ctx->backend_ctx, &record);
}

void trace_emit_complex_decode_fail(uint64_t pc, uint32_t insn)
{
    trace_emit_u32(TRACE_EVENT_COMPLEX_DECODE_FAIL, pc, insn);
}

void trace_emit_complex_fetch_fail(uint64_t pc, int reason)
{
    trace_emit_u32(TRACE_EVENT_COMPLEX_FETCH_FAIL, pc, (uint32_t)reason);
}

void trace_emit_task_thread_entry(uint64_t task_ptr)
{
    trace_emit_u64(TRACE_EVENT_TASK_THREAD_ENTRY, 0, task_ptr);
}

void trace_emit_task_thread_current_set(uint32_t pid, uint64_t mm, uint64_t mem)
{
    if (!g_trace_ctx || !g_trace_ctx->backend_ops || !g_trace_ctx->backend_ops->emit) {
        return;
    }

    if (!trace_event_enabled(TRACE_EVENT_TASK_THREAD_CURRENT_SET, 0)) {
        return;
    }

    trace_record_t record;
    memset(&record, 0, sizeof(record));

    record.header.seq = g_trace_ctx->emitted_count++;
    record.header.event_id = TRACE_EVENT_TASK_THREAD_CURRENT_SET;
    record.header.level = TRACE_LEVEL_SUMMARY;
    record.header.cpu_id = 0;
    record.header.pc = 0;
    record.header.payload_size = 24;

    memcpy(record.payload + 0, &pid, 4);
    memcpy(record.payload + 8, &mm, 8);
    memcpy(record.payload + 16, &mem, 8);

    g_trace_ctx->backend_ops->emit(g_trace_ctx->backend_ctx, &record);
}

void trace_emit_task_run_current_entry(uint64_t current_ptr, uint32_t pid)
{
    if (!g_trace_ctx || !g_trace_ctx->backend_ops || !g_trace_ctx->backend_ops->emit) {
        return;
    }

    if (!trace_event_enabled(TRACE_EVENT_TASK_RUN_CURRENT_ENTRY, 0)) {
        return;
    }

    trace_record_t record;
    memset(&record, 0, sizeof(record));

    record.header.seq = g_trace_ctx->emitted_count++;
    record.header.event_id = TRACE_EVENT_TASK_RUN_CURRENT_ENTRY;
    record.header.level = TRACE_LEVEL_SUMMARY;
    record.header.cpu_id = 0;
    record.header.pc = 0;
    record.header.payload_size = 16;

    memcpy(record.payload + 0, &current_ptr, 8);
    memcpy(record.payload + 8, &pid, 4);

    g_trace_ctx->backend_ops->emit(g_trace_ctx->backend_ctx, &record);
}

void trace_emit_task_run_current_mem_check(uint64_t mm, uint64_t mem)
{
    if (!g_trace_ctx || !g_trace_ctx->backend_ops || !g_trace_ctx->backend_ops->emit) {
        return;
    }

    if (!trace_event_enabled(TRACE_EVENT_TASK_RUN_CURRENT_MEM_CHECK, 0)) {
        return;
    }

    trace_record_t record;
    memset(&record, 0, sizeof(record));

    record.header.seq = g_trace_ctx->emitted_count++;
    record.header.event_id = TRACE_EVENT_TASK_RUN_CURRENT_MEM_CHECK;
    record.header.level = TRACE_LEVEL_SUMMARY;
    record.header.cpu_id = 0;
    record.header.pc = 0;
    record.header.payload_size = 16;

    memcpy(record.payload + 0, &mm, 8);
    memcpy(record.payload + 8, &mem, 8);

    g_trace_ctx->backend_ops->emit(g_trace_ctx->backend_ctx, &record);
}

void trace_emit_task_create_return(uint32_t pid, uint64_t task_ptr)
{
    if (!g_trace_ctx || !g_trace_ctx->backend_ops || !g_trace_ctx->backend_ops->emit) {
        return;
    }

    if (!trace_event_enabled(TRACE_EVENT_TASK_CREATE_RETURN, 0)) {
        return;
    }

    trace_record_t record;
    memset(&record, 0, sizeof(record));

    record.header.seq = g_trace_ctx->emitted_count++;
    record.header.event_id = TRACE_EVENT_TASK_CREATE_RETURN;
    record.header.level = TRACE_LEVEL_SUMMARY;
    record.header.cpu_id = 0;
    record.header.pc = 0;
    record.header.payload_size = 16;

    memcpy(record.payload + 0, &pid, 4);
    memcpy(record.payload + 8, &task_ptr, 8);

    g_trace_ctx->backend_ops->emit(g_trace_ctx->backend_ctx, &record);
}

void trace_emit_construct_task_done(uint32_t pid, uint64_t task_ptr, uint64_t mm, uint64_t mem)
{
    if (!g_trace_ctx || !g_trace_ctx->backend_ops || !g_trace_ctx->backend_ops->emit) {
        return;
    }

    if (!trace_event_enabled(TRACE_EVENT_CONSTRUCT_TASK_DONE, 0)) {
        return;
    }

    trace_record_t record;
    memset(&record, 0, sizeof(record));

    record.header.seq = g_trace_ctx->emitted_count++;
    record.header.event_id = TRACE_EVENT_CONSTRUCT_TASK_DONE;
    record.header.level = TRACE_LEVEL_SUMMARY;
    record.header.cpu_id = 0;
    record.header.pc = 0;
    record.header.payload_size = 32;

    memcpy(record.payload + 0, &pid, 4);
    memcpy(record.payload + 8, &task_ptr, 8);
    memcpy(record.payload + 16, &mm, 8);
    memcpy(record.payload + 24, &mem, 8);

    g_trace_ctx->backend_ops->emit(g_trace_ctx->backend_ctx, &record);
}

void trace_emit_task_start_pointer(uint64_t task_ptr)
{
    trace_emit_u64(TRACE_EVENT_TASK_START_POINTER, 0, task_ptr);
}

void trace_emit_mm_new(uint64_t mm_ptr)
{
    trace_emit_u64(TRACE_EVENT_MM_NEW, 0, mm_ptr);
}

void trace_emit_mm_copy(uint64_t src_mm, uint64_t new_mm)
{
    if (!g_trace_ctx || !g_trace_ctx->backend_ops || !g_trace_ctx->backend_ops->emit) {
        return;
    }

    if (!trace_event_enabled(TRACE_EVENT_MM_COPY, 0)) {
        return;
    }

    trace_record_t record;
    memset(&record, 0, sizeof(record));

    record.header.seq = g_trace_ctx->emitted_count++;
    record.header.event_id = TRACE_EVENT_MM_COPY;
    record.header.level = TRACE_LEVEL_SUMMARY;
    record.header.cpu_id = 0;
    record.header.pc = 0;
    record.header.payload_size = 16;

    memcpy(record.payload + 0, &src_mm, 8);
    memcpy(record.payload + 8, &new_mm, 8);

    g_trace_ctx->backend_ops->emit(g_trace_ctx->backend_ctx, &record);
}

void trace_emit_mm_retain(uint64_t mm_ptr, uint32_t new_refcount)
{
    if (!g_trace_ctx || !g_trace_ctx->backend_ops || !g_trace_ctx->backend_ops->emit) {
        return;
    }

    if (!trace_event_enabled(TRACE_EVENT_MM_RETAIN, 0)) {
        return;
    }

    trace_record_t record;
    memset(&record, 0, sizeof(record));

    record.header.seq = g_trace_ctx->emitted_count++;
    record.header.event_id = TRACE_EVENT_MM_RETAIN;
    record.header.level = TRACE_LEVEL_SUMMARY;
    record.header.cpu_id = 0;
    record.header.pc = 0;
    record.header.payload_size = 16;

    memcpy(record.payload + 0, &mm_ptr, 8);
    memcpy(record.payload + 8, &new_refcount, 4);

    g_trace_ctx->backend_ops->emit(g_trace_ctx->backend_ctx, &record);
}

void trace_emit_mm_release(uint64_t mm_ptr, uint32_t old_refcount)
{
    if (!g_trace_ctx || !g_trace_ctx->backend_ops || !g_trace_ctx->backend_ops->emit) {
        return;
    }

    if (!trace_event_enabled(TRACE_EVENT_MM_RELEASE, 0)) {
        return;
    }

    trace_record_t record;
    memset(&record, 0, sizeof(record));

    record.header.seq = g_trace_ctx->emitted_count++;
    record.header.event_id = TRACE_EVENT_MM_RELEASE;
    record.header.level = TRACE_LEVEL_SUMMARY;
    record.header.cpu_id = 0;
    record.header.pc = 0;
    record.header.payload_size = 16;

    memcpy(record.payload + 0, &mm_ptr, 8);
    memcpy(record.payload + 8, &old_refcount, 4);

    g_trace_ctx->backend_ops->emit(g_trace_ctx->backend_ctx, &record);
}

void trace_emit_mm_release_freed(uint64_t mm_ptr)
{
    trace_emit_u64(TRACE_EVENT_MM_RELEASE_FREED, 0, mm_ptr);
}

void trace_emit_task_set_mm(uint64_t task_ptr, uint64_t new_mm)
{
    if (!g_trace_ctx || !g_trace_ctx->backend_ops || !g_trace_ctx->backend_ops->emit) {
        return;
    }

    if (!trace_event_enabled(TRACE_EVENT_TASK_SET_MM, 0)) {
        return;
    }

    trace_record_t record;
    memset(&record, 0, sizeof(record));

    record.header.seq = g_trace_ctx->emitted_count++;
    record.header.event_id = TRACE_EVENT_TASK_SET_MM;
    record.header.level = TRACE_LEVEL_SUMMARY;
    record.header.cpu_id = 0;
    record.header.pc = 0;
    record.header.payload_size = 16;

    memcpy(record.payload + 0, &task_ptr, 8);
    memcpy(record.payload + 8, &new_mm, 8);

    g_trace_ctx->backend_ops->emit(g_trace_ctx->backend_ctx, &record);
}

void trace_emit_exec_mm_boundary(uint64_t current_ptr, uint32_t pid, uint64_t mm, uint64_t mem,
                                 uint64_t old_mm, uint64_t new_mm, uint8_t operation, int err)
{
    if (!g_trace_ctx || !g_trace_ctx->backend_ops || !g_trace_ctx->backend_ops->emit) {
        return;
    }

    if (!trace_event_enabled(TRACE_EVENT_EXEC_MM_BOUNDARY, 0)) {
        return;
    }

    trace_record_t record;
    memset(&record, 0, sizeof(record));

    record.header.seq = g_trace_ctx->emitted_count++;
    record.header.event_id = TRACE_EVENT_EXEC_MM_BOUNDARY;
    record.header.level = TRACE_LEVEL_SUMMARY;
    record.header.cpu_id = 0;
    record.header.pc = 0;
    record.header.payload_size = 48;

    memcpy(record.payload + 0, &current_ptr, 8);
    memcpy(record.payload + 8, &pid, 4);
    memcpy(record.payload + 16, &mm, 8);
    memcpy(record.payload + 24, &mem, 8);
    memcpy(record.payload + 32, &old_mm, 8);
    memcpy(record.payload + 40, &new_mm, 8);
    memcpy(record.payload + 48, &operation, 1);
    memcpy(record.payload + 49, &err, 4);

    g_trace_ctx->backend_ops->emit(g_trace_ctx->backend_ctx, &record);

    if (g_trace_ctx->backend_ops->flush) {
        g_trace_ctx->backend_ops->flush(g_trace_ctx->backend_ctx);
    }
}

void trace_emit_exec_path_boundary(uint64_t current_ptr, uint32_t pid, uint64_t mm, uint64_t mem,
                                   uint8_t point, int err)
{
    if (!g_trace_ctx || !g_trace_ctx->backend_ops || !g_trace_ctx->backend_ops->emit) {
        return;
    }

    if (!trace_event_enabled(TRACE_EVENT_EXEC_PATH_BOUNDARY, 0)) {
        return;
    }

    trace_record_t record;
    memset(&record, 0, sizeof(record));

    record.header.seq = g_trace_ctx->emitted_count++;
    record.header.event_id = TRACE_EVENT_EXEC_PATH_BOUNDARY;
    record.header.level = TRACE_LEVEL_SUMMARY;
    record.header.cpu_id = 0;
    record.header.pc = 0;
    record.header.payload_size = 32;

    memcpy(record.payload + 0, &current_ptr, 8);
    memcpy(record.payload + 8, &pid, 4);
    memcpy(record.payload + 16, &mm, 8);
    memcpy(record.payload + 24, &mem, 8);
    memcpy(record.payload + 32, &point, 1);
    memcpy(record.payload + 33, &err, 4);

    g_trace_ctx->backend_ops->emit(g_trace_ctx->backend_ctx, &record);

    if (g_trace_ctx->backend_ops->flush) {
        g_trace_ctx->backend_ops->flush(g_trace_ctx->backend_ctx);
    }
}

void trace_emit_task_thread_before_set(uint64_t task_ptr, uint64_t current_before)
{
    if (!g_trace_ctx || !g_trace_ctx->backend_ops || !g_trace_ctx->backend_ops->emit) {
        return;
    }

    if (!trace_event_enabled(TRACE_EVENT_TASK_THREAD_BEFORE_SET, 0)) {
        return;
    }

    trace_record_t record;
    memset(&record, 0, sizeof(record));

    record.header.seq = g_trace_ctx->emitted_count++;
    record.header.event_id = TRACE_EVENT_TASK_THREAD_BEFORE_SET;
    record.header.level = TRACE_LEVEL_SUMMARY;
    record.header.cpu_id = 0;
    record.header.pc = 0;
    record.header.payload_size = 16;

    memcpy(record.payload + 0, &task_ptr, 8);
    memcpy(record.payload + 8, &current_before, 8);

    g_trace_ctx->backend_ops->emit(g_trace_ctx->backend_ctx, &record);
}

void trace_emit_task_thread_after_set(uint64_t task_ptr, uint64_t current_after)
{
    if (!g_trace_ctx || !g_trace_ctx->backend_ops || !g_trace_ctx->backend_ops->emit) {
        return;
    }

    if (!trace_event_enabled(TRACE_EVENT_TASK_THREAD_AFTER_SET, 0)) {
        return;
    }

    trace_record_t record;
    memset(&record, 0, sizeof(record));

    record.header.seq = g_trace_ctx->emitted_count++;
    record.header.event_id = TRACE_EVENT_TASK_THREAD_AFTER_SET;
    record.header.level = TRACE_LEVEL_SUMMARY;
    record.header.cpu_id = 0;
    record.header.pc = 0;
    record.header.payload_size = 16;

    memcpy(record.payload + 0, &task_ptr, 8);
    memcpy(record.payload + 8, &current_after, 8);

    g_trace_ctx->backend_ops->emit(g_trace_ctx->backend_ctx, &record);
}

void trace_emit_task_run_current_entry_check(uint64_t current_ptr)
{
    trace_emit_u64(TRACE_EVENT_TASK_RUN_CURRENT_ENTRY_CHECK, 0, current_ptr);
}

/* Task handoff proof events */
static void trace_emit_task_handoff_record(trace_event_id_t event_id, uint64_t task_ptr,
                                           uint64_t mm, uint64_t mem, uint32_t pid,
                                           uint64_t host_thread_id)
{
    if (!g_trace_ctx || !g_trace_ctx->backend_ops || !g_trace_ctx->backend_ops->emit) {
        return;
    }

    if (!trace_event_enabled(event_id, 0)) {
        return;
    }

    trace_record_t record;
    memset(&record, 0, sizeof(record));

    record.header.seq = g_trace_ctx->emitted_count++;
    record.header.event_id = event_id;
    record.header.level = TRACE_LEVEL_SUMMARY;
    record.header.cpu_id = 0;
    record.header.pc = 0;
    record.header.payload_size = 40;

    memcpy(record.payload + 0, &task_ptr, 8);
    memcpy(record.payload + 8, &mm, 8);
    memcpy(record.payload + 16, &mem, 8);
    memcpy(record.payload + 24, &pid, 4);
    memcpy(record.payload + 32, &host_thread_id, 8);

    g_trace_ctx->backend_ops->emit(g_trace_ctx->backend_ctx, &record);
}

void trace_emit_task_handoff_parent_pre_create(uint64_t task_ptr, uint64_t mm, uint64_t mem,
                                               uint32_t pid, uint64_t host_thread_id)
{
    trace_emit_task_handoff_record(TRACE_EVENT_TASK_HANDOFF_PARENT_PRE_CREATE, task_ptr, mm, mem,
                                   pid, host_thread_id);
    trace_flush();
}

void trace_emit_task_handoff_child_entry(uint64_t task_arg_ptr, uint64_t task_arg_mm,
                                         uint64_t task_arg_mem, uint32_t task_arg_pid,
                                         uint64_t host_thread_id)
{
    trace_emit_task_handoff_record(TRACE_EVENT_TASK_HANDOFF_CHILD_ENTRY, task_arg_ptr, task_arg_mm,
                                   task_arg_mem, task_arg_pid, host_thread_id);
    trace_flush();
}

void trace_emit_task_handoff_child_post_current(uint64_t current_ptr, uint64_t current_mm,
                                                uint64_t current_mem, uint32_t current_pid,
                                                uint64_t host_thread_id)
{
    trace_emit_task_handoff_record(TRACE_EVENT_TASK_HANDOFF_CHILD_POST_CURRENT, current_ptr,
                                   current_mm, current_mem, current_pid, host_thread_id);
    trace_flush();
}

void trace_emit_task_handoff_child_pre_run(uint64_t current_ptr, uint64_t current_mm,
                                           uint64_t current_mem, uint32_t current_pid,
                                           uint64_t host_thread_id)
{
    trace_emit_task_handoff_record(TRACE_EVENT_TASK_HANDOFF_CHILD_PRE_RUN, current_ptr, current_mm,
                                   current_mem, current_pid, host_thread_id);
    trace_flush();
}

/* Task initialization proof events */
static void trace_emit_task_init_simple(trace_event_id_t event_id, uint64_t task_ptr,
                                        uint64_t value, uint64_t host_thread_id)
{
    if (!g_trace_ctx || !g_trace_ctx->backend_ops || !g_trace_ctx->backend_ops->emit) {
        return;
    }

    if (!trace_event_enabled(event_id, 0)) {
        return;
    }

    trace_record_t record;
    memset(&record, 0, sizeof(record));

    record.header.seq = g_trace_ctx->emitted_count++;
    record.header.event_id = event_id;
    record.header.level = TRACE_LEVEL_SUMMARY;
    record.header.pc = 0;

    if (event_id == TRACE_EVENT_TASK_INIT_AFTER_PID_WRITE) {
        record.header.payload_size = 16;
        uint32_t pid = (uint32_t)value;
        memcpy(record.payload + 0, &task_ptr, 8);
        memcpy(record.payload + 8, &pid, 4);
    } else {
        record.header.payload_size = 24;
        memcpy(record.payload + 0, &task_ptr, 8);
        memcpy(record.payload + 8, &value, 8);
    }
    memcpy(record.payload + 16, &host_thread_id, 8);

    g_trace_ctx->backend_ops->emit(g_trace_ctx->backend_ctx, &record);
}

void trace_emit_task_init_after_pid_write(uint64_t task_ptr, uint32_t pid, uint64_t host_thread_id)
{
    trace_emit_task_init_simple(TRACE_EVENT_TASK_INIT_AFTER_PID_WRITE, task_ptr, pid,
                                host_thread_id);
}

void trace_emit_task_init_after_mm_write(uint64_t task_ptr, uint64_t mm, uint64_t host_thread_id)
{
    trace_emit_task_init_simple(TRACE_EVENT_TASK_INIT_AFTER_MM_WRITE, task_ptr, mm, host_thread_id);
}

void trace_emit_task_init_after_mem_write(uint64_t task_ptr, uint64_t mem, uint64_t host_thread_id)
{
    trace_emit_task_init_simple(TRACE_EVENT_TASK_INIT_AFTER_MEM_WRITE, task_ptr, mem,
                                host_thread_id);
}

void trace_emit_task_init_pre_pthread_create(uint64_t task_ptr, uint64_t mm, uint64_t mem,
                                             uint32_t pid, uint64_t host_thread_id)
{
    trace_emit_task_handoff_record(TRACE_EVENT_TASK_INIT_PRE_PTHREAD_CREATE, task_ptr, mm, mem, pid,
                                   host_thread_id);
}

/* Task field write tracking */
void trace_emit_task_field_write(uint64_t task_ptr, uint8_t field_id, uint8_t site_id,
                                 uint64_t old_val, uint64_t new_val, uint64_t host_thread_id)
{
    if (!g_trace_ctx || !g_trace_ctx->backend_ops || !g_trace_ctx->backend_ops->emit) {
        return;
    }

    if (!trace_event_enabled(TRACE_EVENT_TASK_FIELD_WRITE, 0)) {
        return;
    }

    trace_record_t record;
    memset(&record, 0, sizeof(record));

    record.header.seq = g_trace_ctx->emitted_count++;
    record.header.event_id = TRACE_EVENT_TASK_FIELD_WRITE;
    record.header.level = TRACE_LEVEL_SUMMARY;
    record.header.pc = 0;
    record.header.payload_size = 32;

    memcpy(record.payload + 0, &task_ptr, 8);
    memcpy(record.payload + 8, &field_id, 1);
    memcpy(record.payload + 9, &site_id, 1);
    memcpy(record.payload + 16, &old_val, 8);
    memcpy(record.payload + 24, &new_val, 8);
    memcpy(record.payload + 12, &host_thread_id, 8);

    g_trace_ctx->backend_ops->emit(g_trace_ctx->backend_ctx, &record);
}

/* Task checkpoint */
void trace_emit_task_checkpoint(uint64_t task_ptr, uint32_t checkpoint_id, uint32_t pid,
                                uint64_t mm, uint64_t mem, uint64_t host_thread_id)
{
    if (!g_trace_ctx || !g_trace_ctx->backend_ops || !g_trace_ctx->backend_ops->emit) {
        return;
    }

    if (!trace_event_enabled(TRACE_EVENT_TASK_CHECKPOINT, 0)) {
        return;
    }

    trace_record_t record;
    memset(&record, 0, sizeof(record));

    record.header.seq = g_trace_ctx->emitted_count++;
    record.header.event_id = TRACE_EVENT_TASK_CHECKPOINT;
    record.header.level = TRACE_LEVEL_SUMMARY;
    record.header.pc = 0;
    record.header.payload_size = 32;

    memcpy(record.payload + 0, &task_ptr, 8);
    memcpy(record.payload + 8, &checkpoint_id, 4);
    memcpy(record.payload + 12, &pid, 4);
    memcpy(record.payload + 16, &mm, 8);
    memcpy(record.payload + 24, &mem, 8);
    memcpy(record.payload + 28, &host_thread_id, 8);

    g_trace_ctx->backend_ops->emit(g_trace_ctx->backend_ctx, &record);
}

/* Task mem snapshot */
void trace_emit_task_mem_snapshot(uint64_t task_ptr, uint8_t snapshot_id, uint64_t canary_value,
                                  uint64_t host_thread_id)
{
    if (!g_trace_ctx || !g_trace_ctx->backend_ops || !g_trace_ctx->backend_ops->emit) {
        return;
    }

    if (!trace_event_enabled(TRACE_EVENT_TASK_MEM_SNAPSHOT, 0)) {
        return;
    }

    trace_record_t record;
    memset(&record, 0, sizeof(record));

    record.header.seq = g_trace_ctx->emitted_count++;
    record.header.event_id = TRACE_EVENT_TASK_MEM_SNAPSHOT;
    record.header.level = TRACE_LEVEL_SUMMARY;
    record.header.pc = 0;
    record.header.payload_size = 24;

    memcpy(record.payload + 0, &task_ptr, 8);
    memcpy(record.payload + 8, &snapshot_id, 1);
    memcpy(record.payload + 9, &canary_value, 8);
    memcpy(record.payload + 17, &host_thread_id, 8);

    g_trace_ctx->backend_ops->emit(g_trace_ctx->backend_ctx, &record);
}

/* Task bulk write */
void trace_emit_task_bulk_write(uint64_t target_ptr, uint64_t source_ptr, uint32_t size,
                                uint8_t operation, uint64_t host_thread_id)
{
    if (!g_trace_ctx || !g_trace_ctx->backend_ops || !g_trace_ctx->backend_ops->emit) {
        return;
    }

    if (!trace_event_enabled(TRACE_EVENT_TASK_BULK_WRITE, 0)) {
        return;
    }

    trace_record_t record;
    memset(&record, 0, sizeof(record));

    record.header.seq = g_trace_ctx->emitted_count++;
    record.header.event_id = TRACE_EVENT_TASK_BULK_WRITE;
    record.header.level = TRACE_LEVEL_SUMMARY;
    record.header.pc = 0;
    record.header.payload_size = 32;

    memcpy(record.payload + 0, &target_ptr, 8);
    memcpy(record.payload + 8, &source_ptr, 8);
    memcpy(record.payload + 16, &size, 4);
    memcpy(record.payload + 20, &operation, 1);
    memcpy(record.payload + 24, &host_thread_id, 8);

    g_trace_ctx->backend_ops->emit(g_trace_ctx->backend_ctx, &record);
}

/* Task canary */
void trace_emit_task_canary(uint64_t task_ptr, uint64_t canary_value, uint8_t operation,
                            uint64_t host_thread_id)
{
    if (!g_trace_ctx || !g_trace_ctx->backend_ops || !g_trace_ctx->backend_ops->emit) {
        return;
    }

    if (!trace_event_enabled(TRACE_EVENT_TASK_CANARY, 0)) {
        return;
    }

    trace_record_t record;
    memset(&record, 0, sizeof(record));

    record.header.seq = g_trace_ctx->emitted_count++;
    record.header.event_id = TRACE_EVENT_TASK_CANARY;
    record.header.level = TRACE_LEVEL_SUMMARY;
    record.header.pc = 0;
    record.header.payload_size = 24;

    memcpy(record.payload + 0, &task_ptr, 8);
    memcpy(record.payload + 8, &canary_value, 8);
    memcpy(record.payload + 16, &operation, 1);
    memcpy(record.payload + 17, &host_thread_id, 8);

    g_trace_ctx->backend_ops->emit(g_trace_ctx->backend_ctx, &record);
}
