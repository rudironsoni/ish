/*
 * trace.c
 * Thin lifecycle shim forwarding to ISHInstrumentation.
 *
 * All startup markers, ring persistence, recovery, backend policy,
 * environment configuration, and proof scaffolding have been removed.
 * Only semantic forwarding to ish_instrumentation_* remains.
 */

#include "trace/trace.h"

#include "trace/trace_internal.h"
#include "trace/trace_types.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ============================================
 * Bridge Forwarding (delegates to app layer)
 * ============================================
 *
 * These functions are provided by ISHInstrumentationBridge.mm
 * when building for iOS. The trace_types.h header provides the
 * origin enum and attribute struct definitions.
 */

#include "app/Instrumentation/ISHInstrumentationBridge.h"

static int instrumentation_active = 0;
__attribute__((weak)) bool ish_instrumentation_is_active(void)
{
    return instrumentation_active;
}
__attribute__((weak)) void ish_instrumentation_record_event(ish_instrumentation_origin_t origin,
                                                            const char *event_name)
{
    (void)origin;
    (void)event_name;
}
__attribute__((weak)) uint64_t ish_instrumentation_begin_interval(
    ish_instrumentation_origin_t origin, const char *interval_name,
    const ish_instrumentation_attribute_t *attrs, uint32_t attr_count)
{
    (void)origin;
    (void)interval_name;
    (void)attrs;
    (void)attr_count;
    return 0;
}
__attribute__((weak)) void
ish_instrumentation_end_interval(uint64_t interval_id, const ish_instrumentation_attribute_t *attrs,
                                 uint32_t attr_count)
{
    (void)interval_id;
    (void)attrs;
    (void)attr_count;
}

/* ============================================
 * Semantic API - forwards to ISHInstrumentation
 * ============================================ */

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

uint64_t trace_begin_interval(int origin, const char *interval_name, const void *attrs,
                              uint32_t attr_count)
{
    return ish_instrumentation_begin_interval((ish_instrumentation_origin_t)origin, interval_name,
                                              (const ish_instrumentation_attribute_t *)attrs,
                                              attr_count);
}

void trace_end_interval(uint64_t interval_id, const void *attrs, uint32_t attr_count)
{
    ish_instrumentation_end_interval(interval_id, (const ish_instrumentation_attribute_t *)attrs,
                                     attr_count);
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
#include "trace/trace_events.def"
#undef TRACE_EVENT
};

/* Event filtering - always disabled */
bool trace_event_enabled(trace_event_id_t event, uint64_t pc)
{
    (void)event;
    (void)pc;
    return false;
}

/* Legacy init - minimal, with ring buffer for pre-crash capture */
int trace_init(trace_config_t *config)
{
    (void)config;
    /* Minimal initialization - just set the global context to a dummy state */
    if (!g_trace_ctx) {
        g_trace_ctx = calloc(1, sizeof(trace_ctx_t));
    }

    /* Enable pre-crash ring buffer capture - this is safe to call multiple times */
    trace_ring_enable_precrash_capture();

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
    return ish_instrumentation_is_active();
}

/* Get current trace level - always off */
trace_level_t trace_get_level(void)
{
    return TRACE_LEVEL_OFF;
}

/* Set trace level - no-op */
void trace_config_set_level(trace_level_t level)
{
    (void)level;
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
    /* Route through ISHInstrumentation bridge only - single observability path */
    switch (point) {
    case TASK_PROOF_START_ENTER:
        ish_instrumentation_record_event(ISH_INSTRUMENTATION_ORIGIN_TASK, "task_proof_start_enter");
        break;
    case TASK_PROOF_BEFORE_PTHREAD:
        ish_instrumentation_record_event(ISH_INSTRUMENTATION_ORIGIN_TASK,
                                         "task_proof_before_pthread");
        break;
    case TASK_PROOF_AFTER_PTHREAD:
        ish_instrumentation_record_event(ISH_INSTRUMENTATION_ORIGIN_TASK,
                                         "task_proof_after_pthread");
        break;
    case TASK_PROOF_THREAD_ENTRY:
        ish_instrumentation_record_event(ISH_INSTRUMENTATION_ORIGIN_TASK,
                                         "task_proof_thread_entry");
        break;
    case TASK_PROOF_BEFORE_CURRENT_SET:
        ish_instrumentation_record_event(ISH_INSTRUMENTATION_ORIGIN_TASK,
                                         "task_proof_before_current_set");
        break;
    case TASK_PROOF_AFTER_CURRENT_SET:
        ish_instrumentation_record_event(ISH_INSTRUMENTATION_ORIGIN_TASK,
                                         "task_proof_after_current_set");
        break;
    case TASK_PROOF_RUN_CURRENT_ENTER:
        ish_instrumentation_record_event(ISH_INSTRUMENTATION_ORIGIN_TASK,
                                         "task_proof_run_current_enter");
        break;
    case TASK_PROOF_BEFORE_GUEST_CPU:
        ish_instrumentation_record_event(ISH_INSTRUMENTATION_ORIGIN_TASK,
                                         "task_proof_before_guest_cpu");
        break;
    // Paired diagnostic proof points for narrowing failure boundary
    case TASK_PROOF_AFTER_THREAD_ENTRY:
        ish_instrumentation_record_event(ISH_INSTRUMENTATION_ORIGIN_TASK,
                                         "task_proof_after_thread_entry");
        break;
    case TASK_PROOF_BEFORE_TASK_RUN_CURRENT:
        ish_instrumentation_record_event(ISH_INSTRUMENTATION_ORIGIN_TASK,
                                         "task_proof_before_task_run_current");
        break;
    case TASK_PROOF_TASK_RUN_CURRENT_ENTRY:
        ish_instrumentation_record_event(ISH_INSTRUMENTATION_ORIGIN_TASK,
                                         "task_proof_task_run_current_entry");
        break;
    }
    (void)pid;
}

/* ============================================
 * TCTI Boundary Instrumentation - Spill-First Trace Events
 * ============================================
 *
 * These functions capture exact runtime values at the TCTI boundary
 * for forensic analysis of the spill-first execution model.
 */

/* Helper to write TCTI event to ring buffer - ALWAYS writes regardless of trace level or init state
 */
static void trace_tcti_to_ring(trace_event_id_t event, uint64_t value)
{
    /* Get the global ring buffer directly - works even before trace_init */
    trace_ring_t *ring = trace_get_global_ring();
    if (!ring || !ring->records)
        return;

    /* Calculate write position */
    size_t idx = ring->head % ring->capacity;

    /* Build record directly in ring buffer */
    trace_record_t *record = &ring->records[idx];
    memset(record, 0, sizeof(trace_record_t));

    record->header.event_id = event;
    record->header.level = TRACE_LEVEL_BOUNDARY;
    record->header.pc = 0; /* Not applicable for TCTI entry */
    record->header.payload_size = 8;
    record->header.seq = ring->seq++;

    memcpy(record->payload, &value, 8);

    /* Update ring buffer state */
    ring->head++;
    if (ring->head >= ring->capacity) {
        ring->wrapped = true;
        ring->dropped++;
    }
}

void trace_emit_tcti_entry_x28_before(uint64_t x28_value)
{
    /* Buffer retained for ring buffer logging */
    char x28_buf[24];
    snprintf(x28_buf, sizeof(x28_buf), "0x%llx", (unsigned long long)x28_value);

    ish_instrumentation_record_event(ISH_INSTRUMENTATION_ORIGIN_TCTI, "tcti.entry.x28.before");

    /* Also write to ring buffer for pre-crash capture */
    trace_tcti_to_ring(TRACE_EVENT_TCTI_ENTRY_X28_BEFORE, x28_value);
}

void trace_emit_tcti_entry_qword0(uint64_t qword0)
{
    /* Buffer retained for ring buffer logging */
    char qword0_buf[24];
    snprintf(qword0_buf, sizeof(qword0_buf), "0x%llx", (unsigned long long)qword0);

    ish_instrumentation_record_event(ISH_INSTRUMENTATION_ORIGIN_TCTI, "tcti.entry.qword0");

    /* Also write to ring buffer for pre-crash capture */
    trace_tcti_to_ring(TRACE_EVENT_TCTI_ENTRY_QWORD0, qword0);
}

void trace_emit_tcti_entry_x27_after(uint64_t x27_value)
{
    /* Buffer retained for ring buffer logging */
    char x27_buf[24];
    snprintf(x27_buf, sizeof(x27_buf), "0x%llx", (unsigned long long)x27_value);

    ish_instrumentation_record_event(ISH_INSTRUMENTATION_ORIGIN_TCTI, "tcti.entry.x27.after");

    /* Also write to ring buffer for pre-crash capture */
    trace_tcti_to_ring(TRACE_EVENT_TCTI_ENTRY_X27_AFTER, x27_value);
}

void trace_emit_tcti_entry_x28_after(uint64_t x28_value)
{
    /* Buffer retained for ring buffer logging */
    char x28_buf[24];
    snprintf(x28_buf, sizeof(x28_buf), "0x%llx", (unsigned long long)x28_value);

    ish_instrumentation_record_event(ISH_INSTRUMENTATION_ORIGIN_TCTI, "tcti.entry.x28.after");

    /* Also write to ring buffer for pre-crash capture */
    trace_tcti_to_ring(TRACE_EVENT_TCTI_ENTRY_X28_AFTER, x28_value);
}

void trace_emit_tcti_entry_qword1(uint64_t qword1)
{
    /* Buffer retained for ring buffer logging */
    char qword1_buf[24];
    snprintf(qword1_buf, sizeof(qword1_buf), "0x%llx", (unsigned long long)qword1);

    ish_instrumentation_record_event(ISH_INSTRUMENTATION_ORIGIN_TCTI, "tcti.entry.qword1");

    /* Also write to ring buffer for pre-crash capture */
    trace_tcti_to_ring(TRACE_EVENT_TCTI_ENTRY_QWORD1, qword1);
}

void trace_emit_gadget_entry_x28(uint64_t x28_value)
{
    /* Buffer retained for ring buffer logging */
    char x28_buf[24];
    snprintf(x28_buf, sizeof(x28_buf), "0x%llx", (unsigned long long)x28_value);

    ish_instrumentation_record_event(ISH_INSTRUMENTATION_ORIGIN_TCTI, "gadget.entry.x28");

    /* Also write to ring buffer for pre-crash capture */
    trace_tcti_to_ring(TRACE_EVENT_GADGET_ENTRY_X28, x28_value);
}

void trace_emit_gadget_fault_addr(uint64_t fault_addr)
{
    /* Buffer retained for ring buffer logging */
    char addr_buf[24];
    snprintf(addr_buf, sizeof(addr_buf), "0x%llx", (unsigned long long)fault_addr);

    ish_instrumentation_record_event(ISH_INSTRUMENTATION_ORIGIN_TCTI, "gadget.fault_addr");

    /* Also write to ring buffer for pre-crash capture */
    trace_tcti_to_ring(TRACE_EVENT_GADGET_FAULT_ADDR, fault_addr);
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
    /* Buffer retained for ring buffer logging */
    char buf[24];
    snprintf(buf, sizeof(buf), "0x%llx", (unsigned long long)fault_pc);

    ish_instrumentation_record_event(ISH_INSTRUMENTATION_ORIGIN_TCTI, "gadget.ldr.fault_pc");
    trace_tcti_to_ring(TRACE_EVENT_GADGET_LDR_FAULT_PC, fault_pc);
}

void trace_emit_gadget_ldr_rn_value(uint64_t rn_value)
{
    /* Buffer retained for ring buffer logging */
    char buf[24];
    snprintf(buf, sizeof(buf), "0x%llx", (unsigned long long)rn_value);

    ish_instrumentation_record_event(ISH_INSTRUMENTATION_ORIGIN_TCTI, "gadget.ldr.rn_value");
    trace_tcti_to_ring(TRACE_EVENT_GADGET_LDR_RN_VALUE, rn_value);
}

void trace_emit_gadget_ldr_imm_value(uint64_t imm_value)
{
    /* Buffer retained for ring buffer logging */
    char buf[24];
    snprintf(buf, sizeof(buf), "0x%llx", (unsigned long long)imm_value);

    ish_instrumentation_record_event(ISH_INSTRUMENTATION_ORIGIN_TCTI, "gadget.ldr.imm_value");
    trace_tcti_to_ring(TRACE_EVENT_GADGET_LDR_IMM_VALUE, imm_value);
}

void trace_emit_gadget_ldr_idx_mode(uint64_t idx_mode)
{
    /* Buffer retained for ring buffer logging */
    char buf[24];
    snprintf(buf, sizeof(buf), "0x%llx", (unsigned long long)idx_mode);

    ish_instrumentation_record_event(ISH_INSTRUMENTATION_ORIGIN_TCTI, "gadget.ldr.idx_mode");
    trace_tcti_to_ring(TRACE_EVENT_GADGET_LDR_IDX_MODE, idx_mode);
}

void trace_emit_gadget_ldr_guest_vaddr(uint64_t guest_vaddr)
{
    /* Buffer retained for ring buffer logging */
    char buf[24];
    snprintf(buf, sizeof(buf), "0x%llx", (unsigned long long)guest_vaddr);

    ish_instrumentation_record_event(ISH_INSTRUMENTATION_ORIGIN_TCTI, "gadget.ldr.guest_vaddr");
    trace_tcti_to_ring(TRACE_EVENT_GADGET_LDR_GUEST_VADDR, guest_vaddr);
}

void trace_emit_gadget_ldr_host_ptr(uint64_t host_ptr)
{
    /* Buffer retained for ring buffer logging */
    char buf[24];
    snprintf(buf, sizeof(buf), "0x%llx", (unsigned long long)host_ptr);

    ish_instrumentation_record_event(ISH_INSTRUMENTATION_ORIGIN_TCTI, "gadget.ldr.host_ptr");
    trace_tcti_to_ring(TRACE_EVENT_GADGET_LDR_HOST_PTR, host_ptr);
}

void trace_emit_mem_translate_attempt(uint64_t guest_addr, uint64_t size)
{
    /* Buffers retained for ring buffer logging */
    char addr_buf[24];
    char size_buf[24];
    snprintf(addr_buf, sizeof(addr_buf), "0x%llx", (unsigned long long)guest_addr);
    snprintf(size_buf, sizeof(size_buf), "0x%llx", (unsigned long long)size);

    ish_instrumentation_record_event(ISH_INSTRUMENTATION_ORIGIN_KERNEL, "mem.translate.attempt");
    trace_tcti_to_ring(TRACE_EVENT_MEM_TRANSLATE_ATTEMPT, guest_addr);
}

void trace_emit_mem_translate_result(uint64_t host_ptr, int success)
{
    /* Buffers retained for ring buffer logging */
    char ptr_buf[24];
    char success_buf[8];
    snprintf(ptr_buf, sizeof(ptr_buf), "0x%llx", (unsigned long long)host_ptr);
    snprintf(success_buf, sizeof(success_buf), "%d", success);

    ish_instrumentation_record_event(ISH_INSTRUMENTATION_ORIGIN_KERNEL, "mem.translate.result");
    trace_tcti_to_ring(TRACE_EVENT_MEM_TRANSLATE_RESULT, host_ptr);
}

void trace_emit_mem_pgdir_lookup(uint64_t page, uint64_t pgdir_slot)
{
    /* Buffers retained for ring buffer logging */
    char page_buf[24];
    char slot_buf[24];
    snprintf(page_buf, sizeof(page_buf), "0x%llx", (unsigned long long)page);
    snprintf(slot_buf, sizeof(slot_buf), "0x%llx", (unsigned long long)pgdir_slot);

    ish_instrumentation_record_event(ISH_INSTRUMENTATION_ORIGIN_KERNEL, "mem.pgdir.lookup");
    trace_tcti_to_ring(TRACE_EVENT_MEM_PGDIR_LOOKUP, pgdir_slot);
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

/* trace_dump_ring, trace_dump_ring_stderr, and trace_dump_on_fault
 * are implemented in trace_dump.c and trace_ring.c */

/* Weak stub for trace_dump_ring - trace_ring.c provides real implementation */
__attribute__((weak)) int trace_dump_ring(const char *path)
{
    (void)path;
    return 0;
}

/* Weak stub for trace_dump_ring_stderr - trace_ring.c provides real implementation */
__attribute__((weak)) void trace_dump_ring_stderr(void) {}

/* Weak stub for trace_dump_on_fault - trace_dump.c provides real implementation */
__attribute__((weak)) void trace_dump_on_fault(uint64_t fault_pc, uint64_t fault_addr, int is_write)
{
    (void)fault_pc;
    (void)fault_addr;
    (void)is_write;
}

/* ============================================
 * Sidecar Management - STUBBED
 * ============================================ */

/* Weak stub - trace_dump.c provides the real implementation when linked */
__attribute__((weak)) bool trace_sidecar_enabled(void)
{
    return false;
}
