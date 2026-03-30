/*
 * trace_internal.h
 * Internal legacy API for iSH tracing subsystem.
 *
 * This header contains all legacy trace declarations that are preserved
 * for kernel compatibility. Kernel files (task.c, exec.c, mmap.c, init.c)
 * cannot be modified per mission constraints, so they continue to access
 * these APIs through trace.h (which includes this header).
 *
 * NEW CODE SHOULD NOT USE THESE APIs DIRECTLY.
 * Use the minimal semantic API in trace.h instead:
 *   - trace_bootstrap()
 *   - trace_activate()
 *   - trace_is_active()
 *   - trace_record_event()
 *   - trace_begin_interval()
 *   - trace_end_interval()
 */

#ifndef TRACE_INTERNAL_H
#define TRACE_INTERNAL_H

#include "trace/trace_types.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================
 * Legacy Event Emission API (DEPRECATED)
 * ============================================
 *
 * These functions are preserved for kernel compatibility.
 * They are called from kernel/task.c, kernel/exec.c, kernel/mmap.c
 * which cannot be modified per the mission constraints.
 * New code should use the semantic API in trace.h.
 */

/* Core event emission */
void trace_emit(trace_event_id_t event, uint64_t pc);
void trace_emit_u64(trace_event_id_t event, uint64_t pc, uint64_t val);
void trace_emit_u32(trace_event_id_t event, uint64_t pc, uint32_t val);

/* Fault events */
void trace_emit_fault(uint64_t pc, uint64_t fault_addr, int is_write, int reason);

/* Syscall events */
void trace_emit_syscall_enter(uint64_t pc, uint64_t num, uint64_t x0, uint64_t x1, uint64_t x2);
void trace_emit_syscall_return(uint64_t pc, uint64_t retval);

/* Process and task events */
void trace_emit_process_entry(uint64_t entry_pc, uint64_t sp, uint64_t at_entry, uint64_t at_base);
void trace_emit_task_create(uint32_t pid, uint32_t parent_pid);
void trace_emit_task_start(uint32_t pid);
void trace_emit_app_task_start_runloop(void);

/* Task diagnostic events */
void trace_emit_task_thread_entry(uint64_t task_ptr);
void trace_emit_task_thread_current_set(uint32_t pid, uint64_t mm, uint64_t mem);
void trace_emit_task_run_current_entry(uint64_t current_ptr, uint32_t pid);
void trace_emit_task_run_current_mem_check(uint64_t mm, uint64_t mem);
void trace_emit_task_thread_before_set(uint64_t task_ptr, uint64_t current_before);
void trace_emit_task_thread_after_set(uint64_t task_ptr, uint64_t current_after);
void trace_emit_task_run_current_entry_check(uint64_t current_ptr);
void trace_emit_task_create_return(uint32_t pid, uint64_t task_ptr);
void trace_emit_construct_task_done(uint32_t pid, uint64_t task_ptr, uint64_t mm, uint64_t mem);
void trace_emit_task_start_pointer(uint64_t task_ptr);

/* Task handoff proof events */
void trace_emit_task_handoff_parent_pre_create(uint64_t task_ptr, uint64_t mm, uint64_t mem,
                                               uint32_t pid, uint64_t host_thread_id);
void trace_emit_task_handoff_child_entry(uint64_t task_arg_ptr, uint64_t task_arg_mm,
                                         uint64_t task_arg_mem, uint32_t task_arg_pid,
                                         uint64_t host_thread_id);
void trace_emit_task_handoff_child_post_current(uint64_t current_ptr, uint64_t current_mm,
                                                uint64_t current_mem, uint32_t current_pid,
                                                uint64_t host_thread_id);
void trace_emit_task_handoff_child_pre_run(uint64_t current_ptr, uint64_t current_mm,
                                           uint64_t current_mem, uint32_t current_pid,
                                           uint64_t host_thread_id);

/* Task initialization proof events */
void trace_emit_task_init_after_pid_write(uint64_t task_ptr, uint32_t pid, uint64_t host_thread_id);
void trace_emit_task_init_after_mm_write(uint64_t task_ptr, uint64_t mm, uint64_t host_thread_id);
void trace_emit_task_init_after_mem_write(uint64_t task_ptr, uint64_t mem, uint64_t host_thread_id);
void trace_emit_task_init_pre_pthread_create(uint64_t task_ptr, uint64_t mm, uint64_t mem,
                                             uint32_t pid, uint64_t host_thread_id);

/* Task field write tracking */
#define TASK_FIELD_PID 0
#define TASK_FIELD_MM  1
#define TASK_FIELD_MEM 2

void trace_emit_task_field_write(uint64_t task_ptr, uint8_t field_id, uint8_t site_id,
                                 uint64_t old_val, uint64_t new_val, uint64_t host_thread_id);

/* Task checkpoint */
void trace_emit_task_checkpoint(uint64_t task_ptr, uint32_t checkpoint_id, uint32_t pid,
                                uint64_t mm, uint64_t mem, uint64_t host_thread_id);

/* Exec path investigation events */
#define EXEC_MM_OP_BEFORE_MM_RELEASE  0
#define EXEC_MM_OP_AFTER_MM_RELEASE   1
#define EXEC_MM_OP_BEFORE_TASK_SET_MM 2
#define EXEC_MM_OP_AFTER_TASK_SET_MM  3

void trace_emit_exec_mm_boundary(uint64_t current_ptr, uint32_t pid, uint64_t mm, uint64_t mem,
                                 uint64_t old_mm, uint64_t new_mm, uint8_t operation, int err);

#define EXEC_PATH_DO_EXECVE_ENTRY        0
#define EXEC_PATH_DO_EXECVE_ENTRY_RET    1
#define EXEC_PATH_BEFORE_FORMAT_EXEC     2
#define EXEC_PATH_AFTER_FORMAT_EXEC      3
#define EXEC_PATH_ELF_EXEC_ENTRY         4
#define EXEC_PATH_BEFORE_ELF_EXEC_RETURN 5
#define EXEC_PATH_AFTER_DO_EXECVE_RETURN 6

void trace_emit_exec_path_boundary(uint64_t current_ptr, uint32_t pid, uint64_t mm, uint64_t mem,
                                   uint8_t point, int err);

/* Memory snapshot and bulk write detection */
#define TASK_SNAP_PARENT_PRE_CREATE  0
#define TASK_SNAP_CHILD_ENTRY        1
#define TASK_SNAP_CHILD_POST_CURRENT 2
#define TASK_SNAP_CHILD_PRE_RUN      3

void trace_emit_task_mem_snapshot(uint64_t task_ptr, uint8_t snapshot_id, uint64_t canary_value,
                                  uint64_t host_thread_id);

#define TASK_BULK_MEMSET        0
#define TASK_BULK_MEMCPY        1
#define TASK_BULK_STRUCT_ASSIGN 2

void trace_emit_task_bulk_write(uint64_t target_ptr, uint64_t source_ptr, uint32_t size,
                                uint8_t operation, uint64_t host_thread_id);

/* Task canary events */
void trace_emit_task_canary(uint64_t task_ptr, uint64_t canary_value, uint8_t operation,
                            uint64_t host_thread_id);

/* Block events */
void trace_emit_block_compile_start(uint64_t pc);
void trace_emit_block_compile_end(uint64_t pc, uint64_t end_pc, uint32_t insn_count);
void trace_emit_block_entry(uint64_t pc, uint32_t gadget_count);
void trace_emit_block_exit(uint64_t pc, int exit_reason, uint64_t next_pc);
void trace_emit_block_cache_hit(uint64_t pc, int cache_level);
void trace_emit_block_cache_miss(uint64_t pc);

/* Register and PSTATE snapshots */
void trace_emit_register_snapshot(uint64_t pc, const uint64_t *regs, uint32_t reg_mask);
void trace_emit_pstate_snapshot(uint64_t pc, uint64_t pstate, uint64_t nzcv);

/* Emulation limitation events */
void trace_emit_unhandled_mrs(uint64_t pc, uint32_t sysreg);
void trace_emit_unhandled_msr(uint64_t pc, uint32_t sysreg);

/* COMPLEX exit error events */
void trace_emit_complex_unknown(uint64_t pc, int cat, int subtype);
void trace_emit_complex_decode_fail(uint64_t pc, uint32_t insn);
void trace_emit_complex_fetch_fail(uint64_t pc, int reason);

/* MM lifecycle events */
void trace_emit_mm_new(uint64_t mm_ptr);
void trace_emit_mm_copy(uint64_t src_mm, uint64_t new_mm);
void trace_emit_mm_retain(uint64_t mm_ptr, uint32_t new_refcount);
void trace_emit_mm_release(uint64_t mm_ptr, uint32_t old_refcount);
void trace_emit_mm_release_freed(uint64_t mm_ptr);
void trace_emit_task_set_mm(uint64_t task_ptr, uint64_t new_mm);

/* ============================================
 * Configuration and Lifecycle (Legacy)
 * ============================================ */

int trace_init(trace_config_t *config);
void trace_shutdown(void);
trace_ctx_t *trace_get_global(void);
bool trace_is_enabled(void);
trace_level_t trace_get_level(void);
void trace_config_set_level(trace_level_t level);
void trace_config_set_pc_range(uint64_t start, uint64_t end);
void trace_config_enable_event(trace_event_id_t event);
void trace_config_disable_event(trace_event_id_t event);
void trace_config_enable_category(trace_category_t category);
bool trace_event_enabled(trace_event_id_t event, uint64_t pc);

/* Parse configuration from environment variables (DEPRECATED - returns safe defaults) */
int trace_config_from_env(trace_config_t *config);

/* ============================================
 * Output and Utility (Legacy)
 * ============================================ */

void trace_flush(void);
int trace_dump_ring(const char *path);
void trace_dump_ring_stderr(void);
void trace_dump_on_fault(uint64_t fault_pc, uint64_t fault_addr, int is_write);
const char *trace_event_name(trace_event_id_t event);
const trace_event_desc_t *trace_event_desc(trace_event_id_t event);

/* ============================================
 * Crash Recovery (Legacy)
 * ============================================ */

int trace_persist_ring(const char *path);
int trace_recover_previous_run(const char *marker_path, const char *ring_path);
int trace_mark_run_started(const char *path);
int trace_mark_run_completed(const char *path);

/* ============================================
 * Sidecar Management (Legacy)
 * ============================================ */

trace_block_sidecar_t *trace_sidecar_create(uint64_t start_pc, uint64_t end_pc);
void trace_sidecar_add_insn(trace_block_sidecar_t *sidecar, uint64_t pc, uint32_t raw_insn,
                            const char *mnemonic);
void trace_sidecar_set_regs(trace_block_sidecar_t *sidecar, int insn_idx, const uint8_t *dst_regs,
                            int num_dsts, const uint8_t *src_regs, int num_srcs);
void trace_sidecar_set_gadget_count(trace_block_sidecar_t *sidecar, uint32_t count);
void trace_sidecar_dump(trace_block_sidecar_t *sidecar, FILE *fp);
bool trace_sidecar_enabled(void);

/* ============================================
 * Convenience Macros (Legacy)
 * ============================================ */

#define TRACE_EVENT_EMIT(event_enum, pc_val)                                                       \
    do {                                                                                           \
        if (trace_event_enabled(event_enum, pc_val)) {                                             \
            trace_emit(event_enum, pc_val);                                                        \
        }                                                                                          \
    } while (0)

/* ============================================
 * Backend Operations (Legacy)
 * ============================================ */

const trace_backend_ops_t *trace_backend_get_ops(trace_backend_t backend);
const trace_backend_ops_t *trace_ring_backend_get_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* TRACE_INTERNAL_H */
