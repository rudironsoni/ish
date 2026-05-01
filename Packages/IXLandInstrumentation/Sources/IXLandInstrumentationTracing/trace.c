/*
 * trace.c
 * Thin lifecycle shim forwarding to IXLandInstrumentation.
 *
 * All startup markers, ring persistence, recovery, backend policy,
 * environment configuration, and proof scaffolding have been removed.
 * Only semantic forwarding to ixland_instrumentation_* remains.
 */

#include "trace.h"

#include "trace_internal.h"
#include "trace_types.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* ============================================
 * Instrumentation API (provided by IXLandInstrumentation package)
 * ============================================ */

#include <IXLandInstrumentation/IXLandInstrumentation.h>

/* ============================================
 * Semantic API - forwards to ISHInstrumentation
 * ============================================ */

void trace_bootstrap(void)
{
    ixland_instrumentation_bootstrap();
}

void trace_activate(void)
{
    ixland_instrumentation_activate();
}

bool trace_is_active(void)
{
    return ixland_instrumentation_is_active() && trace_get_level() > TRACE_LEVEL_OFF;
}

static bool trace_event_has_prefix(const char *event_name, const char *prefix)
{
    if (!event_name || !prefix)
        return false;
    return strncmp(event_name, prefix, strlen(prefix)) == 0;
}

typedef struct {
    const char *prefix;
    trace_level_t level;
} trace_level_rule_t;

static const trace_level_rule_t g_trace_level_rules[] = {
    { "task.proof.", TRACE_LEVEL_DEBUG },
    { "boot.generic_openat.", TRACE_LEVEL_DEBUG },
    { "boot.mount_find.", TRACE_LEVEL_DEBUG },
    { "boot.construct_task.", TRACE_LEVEL_DEBUG },
    { "tcti.block.", TRACE_LEVEL_DEBUG },
    { "tcti.compile.instruction", TRACE_LEVEL_DEBUG },
    { "gadget.", TRACE_LEVEL_DEBUG },
    { "mem.translate.", TRACE_LEVEL_DEBUG },
    { "mem.pgdir.", TRACE_LEVEL_DEBUG },
    { "tcti.", TRACE_LEVEL_DEBUG },
    { "guest.handle_interrupt", TRACE_LEVEL_DEBUG },
    { "guest.receive_signals", TRACE_LEVEL_DEBUG },
    { "guest.syscall", TRACE_LEVEL_DEBUG },
    { "guest.first_fault", TRACE_LEVEL_DEBUG },
};

static trace_level_t trace_level_for_event_name(const char *event_name)
{
    for (size_t i = 0; i < sizeof(g_trace_level_rules) / sizeof(g_trace_level_rules[0]); i++) {
        if (trace_event_has_prefix(event_name, g_trace_level_rules[i].prefix))
            return g_trace_level_rules[i].level;
    }

    return TRACE_LEVEL_INFO;
}

bool trace_should_emit_event(const char *event_name)
{
    return trace_is_active() && trace_get_level() >= trace_level_for_event_name(event_name);
}

static void trace_record_event_at_level(trace_origin_t origin, trace_level_t level,
                                        const char *event_name)
{
    if (!event_name || !trace_is_active() || trace_get_level() < level)
        return;
    ixland_instrumentation_record_event((ixland_instrumentation_origin_t)origin, event_name);
}

void trace_record_event(int origin, const char *event_name)
{
    if (!trace_should_emit_event(event_name))
        return;
    ixland_instrumentation_record_event((ixland_instrumentation_origin_t)origin, event_name);
}

uint64_t trace_begin_interval(int origin, const char *interval_name, const void *attrs,
                              uint32_t attr_count)
{
    if (!trace_should_emit_event(interval_name))
        return 0;

    return ixland_instrumentation_begin_interval(
        (ixland_instrumentation_origin_t)origin, interval_name,
        (const ixland_instrumentation_attribute_t *)attrs, attr_count);
}

void trace_end_interval(uint64_t interval_id, const void *attrs, uint32_t attr_count)
{
    if (interval_id == 0)
        return;
    ixland_instrumentation_end_interval(
        interval_id, (const ixland_instrumentation_attribute_t *)attrs, attr_count);
}

/* ============================================
 * Legacy Event Emission API - STUBBED
 * ============================================
 *
 * These are preserved for kernel compatibility but are no-ops.
 * Kernel code in task.c, exec.c, mmap.c cannot be modified.
 */

/* Global trace context - minimal, no heavy state */
trace_ctx_t *g_trace_ctx = NULL;

/* Global crash ring path - DEPRECATED */
const char *g_crash_ring_path = NULL;

/* Event descriptor table - minimal, no payload logic */
static const trace_event_desc_t event_descriptors[TRACE_EVENT_MAX] = {
    [TRACE_EVENT_NONE] = { "NONE", TRACE_LEVEL_OFF, 0, 0 },

#define TRACE_EVENT(name, level, category, payload_size)                                           \
    [TRACE_EVENT_##name] = { #name, level, category, payload_size },
#include "trace_events.def"
#undef TRACE_EVENT
};

/* Event filtering - always disabled */
bool trace_event_enabled(trace_event_id_t event, uint64_t pc)
{
    (void)event;
    (void)pc;
    return false;
}

/* Legacy init - minimal */
int trace_init(trace_config_t *config)
{
    (void)config;
    if (!g_trace_ctx) {
        g_trace_ctx = calloc(1, sizeof(trace_ctx_t));
    }

    return 0;
}

/* Legacy shutdown - minimal */
void trace_shutdown(void)
{
    if (g_trace_ctx) {
        free(g_trace_ctx);
        g_trace_ctx = NULL;
    }
}

/* Get global context */
trace_ctx_t *trace_get_global(void)
{
    return g_trace_ctx;
}

/* Check if tracing is enabled - forwards to ISHInstrumentation */
bool trace_is_enabled(void)
{
    return trace_is_active();
}

static int g_trace_level_initialized = 0;
static trace_level_t g_trace_level = TRACE_LEVEL_OFF;

static trace_level_t default_trace_level(void)
{
#if DEBUG
    return TRACE_LEVEL_DEBUG;
#else
    return TRACE_LEVEL_OFF;
#endif
}

static trace_level_t parse_trace_level_value(const char *value)
{
    if (!value || value[0] == '\0')
        return TRACE_LEVEL_OFF;

    if (strcmp(value, "0") == 0 || strcasecmp(value, "off") == 0 ||
        strcasecmp(value, "none") == 0)
        return TRACE_LEVEL_OFF;
    if (strcmp(value, "1") == 0 || strcasecmp(value, "info") == 0 ||
        strcasecmp(value, "summary") == 0)
        return TRACE_LEVEL_INFO;
    if (strcmp(value, "2") == 0 || strcasecmp(value, "boundary") == 0)
        return TRACE_LEVEL_BOUNDARY;
    if (strcmp(value, "3") == 0 || strcasecmp(value, "debug") == 0)
        return TRACE_LEVEL_DEBUG;

    return TRACE_LEVEL_OFF;
}

/* Get current trace level */
trace_level_t trace_get_level(void)
{
    if (!g_trace_level_initialized) {
        const char *value = getenv("IXLAND_TRACE_LEVEL");
        if (!value || value[0] == '\0')
            value = getenv("ISH_TRACE_LEVEL");
        g_trace_level = value && value[0] != '\0' ? parse_trace_level_value(value)
                                                  : default_trace_level();
        g_trace_level_initialized = 1;
    }

    return g_trace_level;
}

void trace_config_set_level(trace_level_t level)
{
    g_trace_level = level;
    g_trace_level_initialized = 1;
}

void trace_config_set_level_from_string(const char *value)
{
    trace_config_set_level(parse_trace_level_value(value));
}

/* Set PC range filter - no-op */
void trace_config_set_pc_range(uint64_t start, uint64_t end)
{
    (void)start;
    (void)end;
}

/* Enable event - no-op */
void trace_config_enable_event(trace_event_id_t event)
{
    (void)event;
}

/* Disable event - no-op */
void trace_config_disable_event(trace_event_id_t event)
{
    (void)event;
}

/* Enable category - no-op */
void trace_config_enable_category(trace_category_t category)
{
    (void)category;
}

/* trace_config_from_env is defined in trace_config.c */

/* ============================================
 * Legacy Event Emitters - STUBBED
 * ============================================ */

void trace_emit(trace_event_id_t event, uint64_t pc)
{
    (void)event;
    (void)pc;
}

void trace_emit_u64(trace_event_id_t event, uint64_t pc, uint64_t val)
{
    (void)event;
    (void)pc;
    (void)val;
}

void trace_emit_u32(trace_event_id_t event, uint64_t pc, uint32_t val)
{
    (void)event;
    (void)pc;
    (void)val;
}

void trace_emit_fault(uint64_t pc, uint64_t fault_addr, int is_write, int reason)
{
    (void)pc;
    (void)fault_addr;
    (void)is_write;
    (void)reason;
}

void trace_emit_syscall_enter(uint64_t pc, uint64_t num, uint64_t x0, uint64_t x1, uint64_t x2)
{
    (void)pc;
    (void)num;
    (void)x0;
    (void)x1;
    (void)x2;
}

void trace_emit_syscall_return(uint64_t pc, uint64_t retval)
{
    (void)pc;
    (void)retval;
}

void trace_emit_process_entry(uint64_t entry_pc, uint64_t sp, uint64_t at_entry, uint64_t at_base)
{
    (void)entry_pc;
    (void)sp;
    (void)at_entry;
    (void)at_base;
}

void trace_emit_task_create(uint32_t pid, uint32_t parent_pid)
{
    (void)pid;
    (void)parent_pid;
}

void trace_emit_task_start(uint32_t pid)
{
    (void)pid;
}

void trace_emit_app_task_start_runloop(void) {}

void trace_emit_block_compile_start(uint64_t pc)
{
    (void)pc;
}

void trace_emit_block_compile_end(uint64_t pc, uint64_t end_pc, uint32_t insn_count)
{
    (void)pc;
    (void)end_pc;
    (void)insn_count;
}

void trace_emit_block_entry(uint64_t pc, uint32_t gadget_count)
{
    (void)pc;
    (void)gadget_count;
}

void trace_emit_block_exit(uint64_t pc, int exit_reason, uint64_t next_pc)
{
    (void)pc;
    (void)exit_reason;
    (void)next_pc;
}

void trace_emit_block_cache_hit(uint64_t pc, int cache_level)
{
    (void)pc;
    (void)cache_level;
}

void trace_emit_block_cache_miss(uint64_t pc)
{
    (void)pc;
}

void trace_emit_register_snapshot(uint64_t pc, const uint64_t *regs, uint32_t reg_mask)
{
    (void)pc;
    (void)regs;
    (void)reg_mask;
}

void trace_emit_pstate_snapshot(uint64_t pc, uint64_t pstate, uint64_t nzcv)
{
    (void)pc;
    (void)pstate;
    (void)nzcv;
}

void trace_emit_unhandled_mrs(uint64_t pc, uint32_t sysreg)
{
    (void)pc;
    (void)sysreg;
}

void trace_emit_unhandled_msr(uint64_t pc, uint32_t sysreg)
{
    (void)pc;
    (void)sysreg;
}

void trace_emit_complex_unknown(uint64_t pc, int cat, int subtype)
{
    (void)pc;
    (void)cat;
    (void)subtype;
}

void trace_emit_complex_decode_fail(uint64_t pc, uint32_t insn)
{
    (void)pc;
    (void)insn;
}

void trace_emit_complex_fetch_fail(uint64_t pc, int reason)
{
    (void)pc;
    (void)reason;
}

void trace_emit_task_thread_entry(uint64_t task_ptr)
{
    (void)task_ptr;
}

void trace_emit_task_thread_current_set(uint32_t pid, uint64_t mm, uint64_t mem)
{
    (void)pid;
    (void)mm;
    (void)mem;
}

void trace_emit_task_run_current_entry(uint64_t current_ptr, uint32_t pid)
{
    (void)current_ptr;
    (void)pid;
}

void trace_emit_task_run_current_mem_check(uint64_t mm, uint64_t mem)
{
    (void)mm;
    (void)mem;
}

void trace_emit_task_create_return(uint32_t pid, uint64_t task_ptr)
{
    (void)pid;
    (void)task_ptr;
}

void trace_emit_construct_task_done(uint32_t pid, uint64_t task_ptr, uint64_t mm, uint64_t mem)
{
    (void)pid;
    (void)task_ptr;
    (void)mm;
    (void)mem;
}

void trace_emit_task_start_pointer(uint64_t task_ptr)
{
    (void)task_ptr;
}

void trace_emit_mm_new(uint64_t mm_ptr)
{
    (void)mm_ptr;
}

void trace_emit_mm_copy(uint64_t src_mm, uint64_t new_mm)
{
    (void)src_mm;
    (void)new_mm;
}

void trace_emit_mm_retain(uint64_t mm_ptr, uint32_t new_refcount)
{
    (void)mm_ptr;
    (void)new_refcount;
}

void trace_emit_mm_release(uint64_t mm_ptr, uint32_t old_refcount)
{
    (void)mm_ptr;
    (void)old_refcount;
}

void trace_emit_mm_release_freed(uint64_t mm_ptr)
{
    (void)mm_ptr;
}

void trace_emit_task_set_mm(uint64_t task_ptr, uint64_t new_mm)
{
    (void)task_ptr;
    (void)new_mm;
}

void trace_emit_task_handoff_parent_pre_create(uint64_t task_ptr, uint64_t mm, uint64_t mem,
                                               uint32_t pid, uint64_t host_thread_id)
{
    (void)task_ptr;
    (void)mm;
    (void)mem;
    (void)pid;
    (void)host_thread_id;
}

void trace_emit_task_handoff_child_entry(uint64_t task_arg_ptr, uint64_t task_arg_mm,
                                         uint64_t task_arg_mem, uint32_t task_arg_pid,
                                         uint64_t host_thread_id)
{
    (void)task_arg_ptr;
    (void)task_arg_mm;
    (void)task_arg_mem;
    (void)task_arg_pid;
    (void)host_thread_id;
}

void trace_emit_task_handoff_child_post_current(uint64_t current_ptr, uint64_t current_mm,
                                                uint64_t current_mem, uint32_t current_pid,
                                                uint64_t host_thread_id)
{
    (void)current_ptr;
    (void)current_mm;
    (void)current_mem;
    (void)current_pid;
    (void)host_thread_id;
}

void trace_emit_task_handoff_child_pre_run(uint64_t current_ptr, uint64_t current_mm,
                                           uint64_t current_mem, uint32_t current_pid,
                                           uint64_t host_thread_id)
{
    (void)current_ptr;
    (void)current_mm;
    (void)current_mem;
    (void)current_pid;
    (void)host_thread_id;
}

void trace_emit_task_init_after_pid_write(uint64_t task_ptr, uint32_t pid, uint64_t host_thread_id)
{
    (void)task_ptr;
    (void)pid;
    (void)host_thread_id;
}

void trace_emit_task_init_after_mm_write(uint64_t task_ptr, uint64_t mm, uint64_t host_thread_id)
{
    (void)task_ptr;
    (void)mm;
    (void)host_thread_id;
}

void trace_emit_task_init_after_mem_write(uint64_t task_ptr, uint64_t mem, uint64_t host_thread_id)
{
    (void)task_ptr;
    (void)mem;
    (void)host_thread_id;
}

void trace_emit_task_init_pre_pthread_create(uint64_t task_ptr, uint64_t mm, uint64_t mem,
                                             uint32_t pid, uint64_t host_thread_id)
{
    (void)task_ptr;
    (void)mm;
    (void)mem;
    (void)pid;
    (void)host_thread_id;
}

void trace_emit_task_field_write(uint64_t task_ptr, uint8_t field_id, uint8_t site_id,
                                 uint64_t old_val, uint64_t new_val, uint64_t host_thread_id)
{
    (void)task_ptr;
    (void)field_id;
    (void)site_id;
    (void)old_val;
    (void)new_val;
    (void)host_thread_id;
}

void trace_emit_task_checkpoint(uint64_t task_ptr, uint32_t checkpoint_id, uint32_t pid,
                                uint64_t mm, uint64_t mem, uint64_t host_thread_id)
{
    (void)task_ptr;
    (void)checkpoint_id;
    (void)pid;
    (void)mm;
    (void)mem;
    (void)host_thread_id;
}

void trace_emit_task_mem_snapshot(uint64_t task_ptr, uint8_t snapshot_id, uint64_t canary_value,
                                  uint64_t host_thread_id)
{
    (void)task_ptr;
    (void)snapshot_id;
    (void)canary_value;
    (void)host_thread_id;
}

void trace_emit_task_bulk_write(uint64_t target_ptr, uint64_t source_ptr, uint32_t size,
                                uint8_t operation, uint64_t host_thread_id)
{
    (void)target_ptr;
    (void)source_ptr;
    (void)size;
    (void)operation;
    (void)host_thread_id;
}

void trace_emit_task_canary(uint64_t task_ptr, uint64_t canary_value, uint8_t operation,
                            uint64_t host_thread_id)
{
    (void)task_ptr;
    (void)canary_value;
    (void)operation;
    (void)host_thread_id;
}

void trace_emit_exec_mm_boundary(uint64_t current_ptr, uint32_t pid, uint64_t mm, uint64_t mem,
                                 uint64_t old_mm, uint64_t new_mm, uint8_t operation, int err)
{
    (void)current_ptr;
    (void)pid;
    (void)mm;
    (void)mem;
    (void)old_mm;
    (void)new_mm;
    (void)operation;
    (void)err;
}

void trace_emit_exec_path_boundary(uint64_t current_ptr, uint32_t pid, uint64_t mm, uint64_t mem,
                                   uint8_t point, int err)
{
    (void)current_ptr;
    (void)pid;
    (void)mm;
    (void)mem;
    (void)point;
    (void)err;
}

void trace_emit_task_thread_before_set(uint64_t task_ptr, uint64_t current_before)
{
    (void)task_ptr;
    (void)current_before;
}

void trace_emit_task_thread_after_set(uint64_t task_ptr, uint64_t current_after)
{
    (void)task_ptr;
    (void)current_after;
}

void trace_emit_task_run_current_entry_check(uint64_t current_ptr)
{
    (void)current_ptr;
}

/* ============================================
 * Task Proof Points for narrowing task_start() failure
 * ============================================ */
void trace_emit_task_proof_point(task_proof_point_t point, uint32_t pid)
{
    switch (point) {
    case TASK_PROOF_START_ENTER:
        trace_record_event_at_level(TRACE_ORIGIN_TASK, TRACE_LEVEL_DEBUG,
                                    "task.proof.start.enter");
        break;
    case TASK_PROOF_BEFORE_PTHREAD:
        trace_record_event_at_level(TRACE_ORIGIN_TASK, TRACE_LEVEL_DEBUG,
                                    "task.proof.before_pthread");
        break;
    case TASK_PROOF_AFTER_PTHREAD:
        trace_record_event_at_level(TRACE_ORIGIN_TASK, TRACE_LEVEL_DEBUG,
                                    "task.proof.after_pthread");
        break;
    case TASK_PROOF_THREAD_ENTRY:
        trace_record_event_at_level(TRACE_ORIGIN_TASK, TRACE_LEVEL_DEBUG,
                                    "task.proof.thread_entry");
        break;
    case TASK_PROOF_BEFORE_CURRENT_SET:
        trace_record_event_at_level(TRACE_ORIGIN_TASK, TRACE_LEVEL_DEBUG,
                                    "task.proof.before_current_set");
        break;
    case TASK_PROOF_AFTER_CURRENT_SET:
        trace_record_event_at_level(TRACE_ORIGIN_TASK, TRACE_LEVEL_DEBUG,
                                    "task.proof.after_current_set");
        break;
    case TASK_PROOF_RUN_CURRENT_ENTER:
        trace_record_event_at_level(TRACE_ORIGIN_TASK, TRACE_LEVEL_DEBUG,
                                    "task.proof.run_current_enter");
        break;
    case TASK_PROOF_BEFORE_GUEST_CPU:
        trace_record_event_at_level(TRACE_ORIGIN_TASK, TRACE_LEVEL_DEBUG,
                                    "task.proof.before_guest_cpu");
        break;
    case TASK_PROOF_AFTER_THREAD_ENTRY:
        trace_record_event_at_level(TRACE_ORIGIN_TASK, TRACE_LEVEL_DEBUG,
                                    "task.proof.after_thread_entry");
        break;
    case TASK_PROOF_BEFORE_TASK_RUN_CURRENT:
        trace_record_event_at_level(TRACE_ORIGIN_TASK, TRACE_LEVEL_DEBUG,
                                    "task.proof.before_task_run_current");
        break;
    case TASK_PROOF_TASK_RUN_CURRENT_ENTRY:
        trace_record_event_at_level(TRACE_ORIGIN_TASK, TRACE_LEVEL_DEBUG,
                                    "task.proof.task_run_current_entry");
        break;
    }
    (void)pid;
}

/* ============================================
 * TCTI Boundary Instrumentation - Spill-First Trace Events
 * ============================================
 *
 * These functions capture exact runtime values at the TCTI boundary
 * for runtime analysis of the spill-first execution model.
 */

void trace_emit_tcti_entry_x28_before(uint64_t x28_value)
{
    trace_record_event_at_level(TRACE_ORIGIN_TCTI, TRACE_LEVEL_DEBUG,
                                "tcti.entry.x28.before");
    (void)x28_value;
}

void trace_emit_tcti_entry_qword0(uint64_t qword0)
{
    trace_record_event_at_level(TRACE_ORIGIN_TCTI, TRACE_LEVEL_DEBUG, "tcti.entry.qword0");
    (void)qword0;
}

void trace_emit_tcti_entry_x27_after(uint64_t x27_value)
{
    trace_record_event_at_level(TRACE_ORIGIN_TCTI, TRACE_LEVEL_DEBUG,
                                "tcti.entry.x27.after");
    (void)x27_value;
}

void trace_emit_tcti_entry_x28_after(uint64_t x28_value)
{
    trace_record_event_at_level(TRACE_ORIGIN_TCTI, TRACE_LEVEL_DEBUG,
                                "tcti.entry.x28.after");
    (void)x28_value;
}

void trace_emit_tcti_entry_qword1(uint64_t qword1)
{
    trace_record_event_at_level(TRACE_ORIGIN_TCTI, TRACE_LEVEL_DEBUG, "tcti.entry.qword1");
    (void)qword1;
}

void trace_emit_gadget_entry_x28(uint64_t x28_value)
{
    trace_record_event_at_level(TRACE_ORIGIN_TCTI, TRACE_LEVEL_DEBUG, "gadget.entry.x28");
    (void)x28_value;
}

void trace_emit_gadget_fault_addr(uint64_t fault_addr)
{
    trace_record_event_at_level(TRACE_ORIGIN_TCTI, TRACE_LEVEL_DEBUG, "gadget.fault_addr");
    (void)fault_addr;
}

/* ============================================
 * Memory Translation Boundary Trace Events
 * ============================================
 *
 * These functions capture the memory translation boundary between
 * TCTI gadgets and the kernel memory subsystem for runtime proof.
 */

void trace_emit_gadget_ldr_fault_pc(uint64_t fault_pc)
{
    trace_record_event_at_level(TRACE_ORIGIN_TCTI, TRACE_LEVEL_DEBUG,
                                "gadget.ldr.fault_pc");
    (void)fault_pc;
}

void trace_emit_gadget_ldr_rn_value(uint64_t rn_value)
{
    trace_record_event_at_level(TRACE_ORIGIN_TCTI, TRACE_LEVEL_DEBUG,
                                "gadget.ldr.rn_value");
    (void)rn_value;
}

void trace_emit_gadget_ldr_imm_value(uint64_t imm_value)
{
    trace_record_event_at_level(TRACE_ORIGIN_TCTI, TRACE_LEVEL_DEBUG,
                                "gadget.ldr.imm_value");
    (void)imm_value;
}

void trace_emit_gadget_ldr_idx_mode(uint64_t idx_mode)
{
    trace_record_event_at_level(TRACE_ORIGIN_TCTI, TRACE_LEVEL_DEBUG,
                                "gadget.ldr.idx_mode");
    (void)idx_mode;
}

void trace_emit_gadget_ldr_guest_vaddr(uint64_t guest_vaddr)
{
    trace_record_event_at_level(TRACE_ORIGIN_TCTI, TRACE_LEVEL_DEBUG,
                                "gadget.ldr.guest_vaddr");
    (void)guest_vaddr;
}

void trace_emit_gadget_ldr_host_ptr(uint64_t host_ptr)
{
    trace_record_event_at_level(TRACE_ORIGIN_TCTI, TRACE_LEVEL_DEBUG,
                                "gadget.ldr.host_ptr");
    (void)host_ptr;
}

void trace_emit_mem_translate_attempt(uint64_t guest_addr, uint64_t size)
{
    trace_record_event_at_level(TRACE_ORIGIN_KERNEL, TRACE_LEVEL_DEBUG,
                                "mem.translate.attempt");
    (void)guest_addr;
    (void)size;
}

void trace_emit_mem_translate_result(uint64_t host_ptr, int success)
{
    trace_record_event_at_level(TRACE_ORIGIN_KERNEL, TRACE_LEVEL_DEBUG,
                                "mem.translate.result");
    (void)host_ptr;
    (void)success;
}

void trace_emit_mem_pgdir_lookup(uint64_t page, uint64_t pgdir_slot)
{
    trace_record_event_at_level(TRACE_ORIGIN_KERNEL, TRACE_LEVEL_DEBUG, "mem.pgdir.lookup");
    (void)page;
    (void)pgdir_slot;
}

/* ============================================
 * AArch64 Detailed Load Analysis - Phase 1 Fault Isolation
 * ============================================
 *
 * This function captures all 13 required diagnostic items for the first
 * failing AArch64 load to determine if EA is wrong before translation
 * or translation is wrong after correct EA.
 */
void trace_emit_a64_ldr_full_analysis(uint64_t fault_pc, uint32_t raw_insn, uint64_t base_reg_val,
                                      uint64_t offset_reg_val, int rn, int rm, int rt,
                                      int extend_type, int shift, uint64_t computed_offset,
                                      uint64_t guest_ea, uint64_t host_ptr, int translation_success,
                                      int is_reg_offset, int idx_mode)
{
    /* Buffers for attribute values */
    char fault_pc_buf[24];
    char raw_insn_buf[16];
    char base_buf[24];
    char offset_buf[24];
    char rn_buf[8];
    char rm_buf[8];
    char rt_buf[8];
    char extend_buf[8];
    char shift_buf[8];
    char computed_off_buf[24];
    char guest_ea_buf[24];
    char host_ptr_buf[24];
    char trans_success_buf[8];
    char is_reg_off_buf[8];
    char idx_mode_buf[8];

    snprintf(fault_pc_buf, sizeof(fault_pc_buf), "0x%llx", (unsigned long long)fault_pc);
    snprintf(raw_insn_buf, sizeof(raw_insn_buf), "0x%08x", raw_insn);
    snprintf(base_buf, sizeof(base_buf), "0x%llx", (unsigned long long)base_reg_val);
    snprintf(offset_buf, sizeof(offset_buf), "0x%llx", (unsigned long long)offset_reg_val);
    snprintf(rn_buf, sizeof(rn_buf), "%d", rn);
    snprintf(rm_buf, sizeof(rm_buf), "%d", rm);
    snprintf(rt_buf, sizeof(rt_buf), "%d", rt);
    snprintf(extend_buf, sizeof(extend_buf), "%d", extend_type);
    snprintf(shift_buf, sizeof(shift_buf), "%d", shift);
    snprintf(computed_off_buf, sizeof(computed_off_buf), "0x%llx",
             (unsigned long long)computed_offset);
    snprintf(guest_ea_buf, sizeof(guest_ea_buf), "0x%llx", (unsigned long long)guest_ea);
    snprintf(host_ptr_buf, sizeof(host_ptr_buf), "0x%llx", (unsigned long long)host_ptr);
    snprintf(trans_success_buf, sizeof(trans_success_buf), "%d", translation_success);
    snprintf(is_reg_off_buf, sizeof(is_reg_off_buf), "%d", is_reg_offset);
    snprintf(idx_mode_buf, sizeof(idx_mode_buf), "%d", idx_mode);

    trace_attribute_t attrs[] = {
        { "fault_pc", fault_pc_buf },
        { "raw_insn", raw_insn_buf },
        { "base_reg_val", base_buf },
        { "offset_reg_val", offset_buf },
        { "rn", rn_buf },
        { "rm", rm_buf },
        { "rt", rt_buf },
        { "extend_type", extend_buf },
        { "shift", shift_buf },
        { "computed_offset", computed_off_buf },
        { "guest_ea", guest_ea_buf },
        { "host_ptr", host_ptr_buf },
        { "translation_success", trans_success_buf },
        { "is_reg_offset", is_reg_off_buf },
        { "idx_mode", idx_mode_buf },
    };

    (void)trace_begin_interval(TRACE_ORIGIN_EXEC, "a64.ldr.full_analysis", attrs,
                               sizeof(attrs) / sizeof(attrs[0]));
}

/* ============================================
 * Output and Utility - STUBBED
 * ============================================ */

const char *trace_event_name(trace_event_id_t event)
{
    if (event >= TRACE_EVENT_MAX) {
        return "UNKNOWN";
    }
    return event_descriptors[event].name;
}

const trace_event_desc_t *trace_event_desc(trace_event_id_t event)
{
    if (event >= TRACE_EVENT_MAX) {
        return NULL;
    }
    return &event_descriptors[event];
}

void trace_flush(void) {}

/* ============================================
 * Crash Recovery - REMOVED (stubbed)
 * ============================================ */

int trace_persist_ring(const char *path)
{
    (void)path;
    return 0;
}

int trace_recover_previous_run(const char *marker_path, const char *ring_path)
{
    (void)marker_path;
    (void)ring_path;
    return 0;
}

int trace_mark_run_started(const char *path)
{
    (void)path;
    return 0;
}

int trace_mark_run_completed(const char *path)
{
    (void)path;
    return 0;
}

/* Legacy dump/sidecar APIs are implemented as no-op compatibility shims
 * in trace_dump.c to preserve source compatibility without legacy sinks. */
