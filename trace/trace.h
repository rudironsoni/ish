/*
 * trace.h
 * Public API for the iSH tracing subsystem.
 */

#ifndef TRACE_H
#define TRACE_H

#include "trace/trace_types.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================
 * Initialization and Lifecycle
 * ============================================ */

/* Initialize tracing from configuration. Returns 0 on success. */
int trace_init(trace_config_t *config);

/* Shutdown tracing and cleanup resources. */
void trace_shutdown(void);

/* Get global trace context (may be NULL if not initialized). */
trace_ctx_t* trace_get_global(void);

/* Check if tracing is enabled and active. */
static inline bool trace_is_enabled(void) {
    extern trace_ctx_t *g_trace_ctx;
    return g_trace_ctx && g_trace_ctx->enabled;
}

/* Get current trace level. */
trace_level_t trace_get_level(void);

/* ============================================
 * Configuration
 * ============================================ */

/* Parse configuration from environment variables. */
int trace_config_from_env(trace_config_t *config);

/* Set trace level at runtime. */
void trace_config_set_level(trace_level_t level);

/* Set PC range filter. Use start=0, end=0 to disable. */
void trace_config_set_pc_range(uint64_t start, uint64_t end);

/* Enable a specific event by ID. */
void trace_config_enable_event(trace_event_id_t event);

/* Disable a specific event by ID. */
void trace_config_disable_event(trace_event_id_t event);

/* Enable events by category bitmask. */
void trace_config_enable_category(trace_category_t category);

/* Check if an event should be emitted (level, filter, etc). */
bool trace_event_enabled(trace_event_id_t event, uint64_t pc);

/* ============================================
 * Core Event Emission (Hot Path)
 * ============================================ */

/* Emit a simple event with no payload. */
void trace_emit(trace_event_id_t event, uint64_t pc);

/* Emit event with 8-byte payload. */
void trace_emit_u64(trace_event_id_t event, uint64_t pc, uint64_t val);

/* Emit event with 4-byte payload. */
void trace_emit_u32(trace_event_id_t event, uint64_t pc, uint32_t val);

/* Emit fault event with full details. */
void trace_emit_fault(uint64_t pc, uint64_t fault_addr, int is_write, int reason);

/* Emit syscall enter with arguments. */
void trace_emit_syscall_enter(uint64_t pc, uint64_t num, uint64_t x0, uint64_t x1, uint64_t x2);

/* Emit syscall return with result. */
void trace_emit_syscall_return(uint64_t pc, uint64_t retval);

/* Emit process entry snapshot. */
void trace_emit_process_entry(uint64_t entry_pc, uint64_t sp, uint64_t at_entry, uint64_t at_base);

/* Emit block compilation events. */
void trace_emit_block_compile_start(uint64_t pc);
void trace_emit_block_compile_end(uint64_t pc, uint64_t end_pc, uint32_t insn_count);

/* Emit block execution events. */
void trace_emit_block_entry(uint64_t pc, uint32_t gadget_count);
void trace_emit_block_exit(uint64_t pc, int exit_reason, uint64_t next_pc);
void trace_emit_block_cache_hit(uint64_t pc, int cache_level);
void trace_emit_block_cache_miss(uint64_t pc);

/* Emit register snapshot (selected registers). */
void trace_emit_register_snapshot(uint64_t pc, const uint64_t *regs, uint32_t reg_mask);

/* Emit PSTATE snapshot. */
void trace_emit_pstate_snapshot(uint64_t pc, uint64_t pstate, uint64_t nzcv);

/* ============================================
 * Block Sidecar Management
 * ============================================ */

/* Create a sidecar for a block. Returns NULL if tracing not active. */
trace_block_sidecar_t* trace_sidecar_create(uint64_t start_pc, uint64_t end_pc);

/* Add instruction to sidecar. */
void trace_sidecar_add_insn(trace_block_sidecar_t *sidecar, uint64_t pc, 
                            uint32_t raw_insn, const char *mnemonic);

/* Set instruction register info. */
void trace_sidecar_set_regs(trace_block_sidecar_t *sidecar, int insn_idx,
                            const uint8_t *dst_regs, int num_dsts,
                            const uint8_t *src_regs, int num_srcs);

/* Set gadget count for sidecar. */
void trace_sidecar_set_gadget_count(trace_block_sidecar_t *sidecar, uint32_t count);

/* Dump sidecar to file. */
void trace_sidecar_dump(trace_block_sidecar_t *sidecar, FILE *fp);

/* Check if sidecar should be created for current trace level. */
bool trace_sidecar_enabled(void);

/* ============================================
 * Dump and Output
 * ============================================ */

/* Dump ring buffer to file. Returns 0 on success. */
int trace_dump_ring(const char *path);

/* Dump ring buffer to stderr. */
void trace_dump_ring_stderr(void);

/* Dump on fault - automatically called when dump_on_fault is enabled. */
void trace_dump_on_fault(uint64_t fault_pc, uint64_t fault_addr, int is_write);

/* Get event name string. */
const char* trace_event_name(trace_event_id_t event);

/* Get event description. */
const trace_event_desc_t* trace_event_desc(trace_event_id_t event);

/* Flush any buffered output. */
void trace_flush(void);

/* ============================================
 * Convenience Macros
 * ============================================ */

/* Main trace macro - emits event if enabled at current level */
#define TRACE_EVENT_EMIT(event_enum, pc_val) \
    do { \
        if (trace_event_enabled(event_enum, pc_val)) { \
            trace_emit(event_enum, pc_val); \
        } \
    } while(0)

/* ============================================
 * Backend Operations (Internal)
 * ============================================ */

/* Get backend operations table. */
const trace_backend_ops_t* trace_backend_get_ops(trace_backend_t backend);

/* Get ring backend operations (defined in trace_ring.c). */
const trace_backend_ops_t* trace_ring_backend_get_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* TRACE_H */
