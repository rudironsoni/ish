/*
 * trace_types.h
 * Minimal type definitions for the iSH tracing subsystem.
 *
 * This file has been reduced to only the types required by the new
 * minimal semantic API. Old event-specific payload structures,
 * backend policy types, legacy recovery/marker/ring startup types,
 * and event-specific payload complexity have been removed.
 */

#ifndef TRACE_TYPES_H
#define TRACE_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ============================================
 * Origin enum (from ISHInstrumentationBridge)
 * ============================================ */
typedef enum {
    TRACE_ORIGIN_APP = 0,
    TRACE_ORIGIN_UI,
    TRACE_ORIGIN_SESSION,
    TRACE_ORIGIN_KERNEL,
    TRACE_ORIGIN_TASK,
    TRACE_ORIGIN_EXEC,
    TRACE_ORIGIN_EMULATOR,
    TRACE_ORIGIN_TCTI
} trace_origin_t;

/* ============================================
 * Lightweight attribute structure
 * ============================================ */
typedef struct {
    const char *key;
    const char *value;
} trace_attribute_t;

/* ============================================
 * Trace levels
 * ============================================ */
typedef enum {
    TRACE_LEVEL_OFF = 0,
    TRACE_LEVEL_NONE = 0,
    TRACE_LEVEL_SUMMARY = 1,
    TRACE_LEVEL_BOUNDARY = 2,
    TRACE_LEVEL_BLOCK = 3,
    TRACE_LEVEL_INSTR = 4,
    TRACE_LEVEL_FORENSIC = 5,
} trace_level_t;

/* ============================================
 * Backend types (minimal)
 * ============================================ */
typedef enum {
    TRACE_BACKEND_NOP = 0,
    TRACE_BACKEND_RING = 1,
    TRACE_BACKEND_STDERR = 2,
    TRACE_BACKEND_OS_LOG = 3,
    TRACE_BACKEND_BRIDGE = 4,
} trace_backend_t;

/* ============================================
 * Task proof points for narrowing task_start() failure
 * ============================================ */
typedef enum {
    TASK_PROOF_START_ENTER = 1,
    TASK_PROOF_BEFORE_PTHREAD,
    TASK_PROOF_AFTER_PTHREAD,
    TASK_PROOF_THREAD_ENTRY,
    TASK_PROOF_AFTER_CURRENT_SET,
    TASK_PROOF_RUN_CURRENT_ENTER,
    TASK_PROOF_BEFORE_GUEST_CPU
} task_proof_point_t;

/* ============================================
 * Event IDs - core taxonomy for phase 1
 * ============================================ */
typedef enum {
    TRACE_EVENT_NONE = 0,

    /* Compile and decode events */
    TRACE_EVENT_BLOCK_COMPILE_START,
    TRACE_EVENT_BLOCK_COMPILE_END,
    TRACE_EVENT_DECODE_FAILURE,
    TRACE_EVENT_UNSUPPORTED_INSTRUCTION,

    /* Runtime boundary events */
    TRACE_EVENT_BLOCK_ENTRY,
    TRACE_EVENT_BLOCK_EXIT,
    TRACE_EVENT_BLOCK_CACHE_HIT,
    TRACE_EVENT_BLOCK_CACHE_MISS,
    TRACE_EVENT_FAULT,
    TRACE_EVENT_SYSCALL_ENTER,
    TRACE_EVENT_SYSCALL_RETURN,
    TRACE_EVENT_PROCESS_ENTRY,

    /* Block detail events */
    TRACE_EVENT_REGISTER_SNAPSHOT,
    TRACE_EVENT_PSTATE_SNAPSHOT,

    /* Emulation limitation events */
    TRACE_EVENT_UNHANDLED_MRS,
    TRACE_EVENT_UNHANDLED_MSR,

    /* COMPLEX exit error events */
    TRACE_EVENT_COMPLEX_UNKNOWN,
    TRACE_EVENT_COMPLEX_DECODE_FAIL,
    TRACE_EVENT_COMPLEX_FETCH_FAIL,

    /* NULL current Crash Diagnostics */
    TRACE_EVENT_TASK_THREAD_BEFORE_SET,
    TRACE_EVENT_TASK_THREAD_AFTER_SET,
    TRACE_EVENT_TASK_RUN_CURRENT_ENTRY_CHECK,

    /* Task lifecycle events */
    TRACE_EVENT_TASK_CREATE,
    TRACE_EVENT_TASK_START,

    /* App-layer lifecycle events */
    TRACE_EVENT_APP_TASK_START_RUNLOOP,
    TRACE_EVENT_APP_TRACE_BOOTSTRAP_STARTED,
    TRACE_EVENT_APP_TRACE_BOOTSTRAP_READY,
    TRACE_EVENT_APP_BOOT_STARTED,
    TRACE_EVENT_APP_UI_SESSION_STARTED,
    TRACE_EVENT_APP_LAUNCH_COMPLETED,
    TRACE_EVENT_APP_CRASH_RECOVERY_BUNDLE_FOUND,

    /* Task thread diagnostics */
    TRACE_EVENT_TASK_THREAD_ENTRY,
    TRACE_EVENT_TASK_THREAD_CURRENT_SET,
    TRACE_EVENT_TASK_RUN_CURRENT_ENTRY,
    TRACE_EVENT_TASK_RUN_CURRENT_MEM_CHECK,

    /* Additional task diagnostics */
    TRACE_EVENT_TASK_CREATE_RETURN,
    TRACE_EVENT_CONSTRUCT_TASK_DONE,
    TRACE_EVENT_TASK_START_POINTER,

    /* MM lifecycle events */
    TRACE_EVENT_MM_NEW,
    TRACE_EVENT_MM_COPY,
    TRACE_EVENT_MM_RETAIN,
    TRACE_EVENT_MM_RELEASE,
    TRACE_EVENT_MM_RELEASE_FREED,
    TRACE_EVENT_TASK_SET_MM,

    /* Task Handoff Proof Events */
    TRACE_EVENT_TASK_HANDOFF_PARENT_PRE_CREATE,
    TRACE_EVENT_TASK_HANDOFF_CHILD_ENTRY,
    TRACE_EVENT_TASK_HANDOFF_CHILD_POST_CURRENT,
    TRACE_EVENT_TASK_HANDOFF_CHILD_PRE_RUN,

    /* Task Initialization Proof Events */
    TRACE_EVENT_TASK_INIT_AFTER_PID_WRITE,
    TRACE_EVENT_TASK_INIT_AFTER_MM_WRITE,
    TRACE_EVENT_TASK_INIT_AFTER_MEM_WRITE,
    TRACE_EVENT_TASK_INIT_PRE_PTHREAD_CREATE,

    /* Task Field Write Tracking */
    TRACE_EVENT_TASK_FIELD_WRITE,
    TRACE_EVENT_TASK_CHECKPOINT,

    /* Memory Snapshot and Bulk Write Detection */
    TRACE_EVENT_TASK_MEM_SNAPSHOT,
    TRACE_EVENT_TASK_BULK_WRITE,
    TRACE_EVENT_TASK_CANARY,

    /* Exec Path Investigation Events */
    TRACE_EVENT_EXEC_MM_BOUNDARY,
    TRACE_EVENT_EXEC_PATH_BOUNDARY,

    /* Contract violation events */
    TRACE_EVENT_INIT_CHILD_NO_INIT_TASK,

    TRACE_EVENT_MAX
} trace_event_id_t;

/* ============================================
 * Event category for filtering
 * ============================================ */
typedef enum {
    TRACE_CAT_COMPILE = 0x01,
    TRACE_CAT_DECODE = 0x02,
    TRACE_CAT_BLOCK = 0x04,
    TRACE_CAT_FAULT = 0x08,
    TRACE_CAT_SYSCALL = 0x10,
    TRACE_CAT_REGISTER = 0x20,
    TRACE_CAT_MEMORY = 0x40,
    TRACE_CAT_GADGET = 0x80,
} trace_category_t;

/* ============================================
 * Per-event metadata
 * ============================================ */
typedef struct trace_event_desc {
    const char *name;
    trace_level_t level;
    trace_category_t category;
    uint8_t payload_size;
} trace_event_desc_t;

/* ============================================
 * Trace configuration (minimal)
 * ============================================ */
typedef struct trace_config {
    trace_backend_t backend;
    trace_level_t level;
    uint64_t event_mask;
    uint64_t category_mask;
    uint64_t max_events;
    /* Legacy fields kept for compatibility - not used in new minimal API */
    uint64_t pc_start;
    uint64_t pc_end;
    uint32_t regs_mask;
    size_t ring_size;
    bool dump_on_fault;
    char output_path[256];
} trace_config_t;

/* ============================================
 * Binary record header (for ring buffer)
 * ============================================ */
typedef struct __attribute__((packed)) trace_record_header {
    uint64_t seq;
    uint8_t event_id;
    uint8_t level;
    uint16_t cpu_id;
    uint64_t pc;
    uint16_t payload_size;
} trace_record_header_t;

/* Maximum payload size */
#define TRACE_MAX_PAYLOAD 96

/* Complete record structure */
typedef struct trace_record {
    trace_record_header_t header;
    uint8_t payload[TRACE_MAX_PAYLOAD];
} trace_record_t;

/* ============================================
 * Ring dump header (kept for compatibility)
 * ============================================ */
typedef struct __attribute__((packed)) trace_dump_header {
    uint8_t magic[4];
    uint16_t version;
    uint16_t header_size;
    uint8_t endianness;
    uint8_t record_header_size;
    uint16_t max_payload_size;
    uint64_t record_count;
    uint64_t dropped_count;
    uint64_t start_seq;
} trace_dump_header_t;

#define TRACE_MAGIC         { 'I', 'S', 'H', '\0' }
#define TRACE_VERSION       1
#define TRACE_ENDIAN_LITTLE 1

/* ============================================
 * Ring buffer state (kept for compatibility)
 * ============================================ */
typedef struct trace_ring {
    trace_record_t *records;
    size_t capacity;
    size_t head;
    uint64_t seq;
    uint64_t dropped;
    bool wrapped;
} trace_ring_t;

/* ============================================
 * Backend operations
 * ============================================ */
typedef struct trace_backend_ops {
    int (*init)(void **ctx, trace_config_t *config);
    void (*shutdown)(void *ctx);
    void (*emit)(void *ctx, trace_record_t *record);
    void (*flush)(void *ctx);
    int (*dump)(void *ctx, const char *path);
} trace_backend_ops_t;

/* ============================================
 * Global trace context
 * ============================================ */
typedef struct trace_ctx {
    trace_config_t config;
    const trace_backend_ops_t *backend_ops;
    void *backend_ctx;
    trace_ring_t *ring;
    uint64_t emitted_count;
    bool enabled;
} trace_ctx_t;

/* ============================================
 * Block sidecar structures (kept for compatibility with existing code)
 * ============================================ */
#define TRACE_MAX_SIDECAR_INSNS 128
#define TRACE_MAX_MNEMONIC_LEN  32

typedef struct trace_sidecar_insn {
    uint64_t pc;
    uint32_t raw_insn;
    char mnemonic[TRACE_MAX_MNEMONIC_LEN];
    uint8_t dst_regs[4];
    uint8_t src_regs[4];
    uint8_t num_dsts;
    uint8_t num_srcs;
} trace_sidecar_insn_t;

typedef struct trace_block_sidecar {
    uint64_t start_pc;
    uint64_t end_pc;
    bool explicit_pc_on_exit;
    uint32_t insn_count;
    uint32_t gadget_count;
    trace_sidecar_insn_t insns[TRACE_MAX_SIDECAR_INSNS];
} trace_block_sidecar_t;

#endif /* TRACE_TYPES_H */
