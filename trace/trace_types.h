/*
 * trace_types.h
 * Core type definitions for the iSH tracing subsystem.
 */

#ifndef TRACE_TYPES_H
#define TRACE_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Trace levels - explicit integer levels */
typedef enum {
    TRACE_LEVEL_OFF = 0,      /* Tracing disabled */
    TRACE_LEVEL_SUMMARY = 1,  /* Counters, compile summaries */
    TRACE_LEVEL_BOUNDARY = 2, /* Block entry/exit, syscall boundaries */
    TRACE_LEVEL_BLOCK = 3,    /* Block details, instruction lists */
    TRACE_LEVEL_INSTR = 4,    /* Per-instruction execution (scaffolded) */
    TRACE_LEVEL_FORENSIC = 5, /* Deep capture for one bug (scaffolded) */
} trace_level_t;

/* Backend types */
typedef enum {
    TRACE_BACKEND_NOP = 0,
    TRACE_BACKEND_RING = 1,
    TRACE_BACKEND_STDERR = 2,
    TRACE_BACKEND_OS_LOG = 3, /* iOS/macOS Unified Logging */
} trace_backend_t;

/* Event IDs - core event taxonomy for phase 1 */
typedef enum {
    TRACE_EVENT_NONE = 0,

    /* Compile and decode events (level 1-2) */
    TRACE_EVENT_BLOCK_COMPILE_START,     /* Block compilation began */
    TRACE_EVENT_BLOCK_COMPILE_END,       /* Block compilation completed */
    TRACE_EVENT_DECODE_FAILURE,          /* Instruction decode failed */
    TRACE_EVENT_UNSUPPORTED_INSTRUCTION, /* Unsupported instruction encountered */

    /* Runtime boundary events (level 2) */
    TRACE_EVENT_BLOCK_ENTRY,      /* Block execution started */
    TRACE_EVENT_BLOCK_EXIT,       /* Block execution ended */
    TRACE_EVENT_BLOCK_CACHE_HIT,  /* Block found in cache */
    TRACE_EVENT_BLOCK_CACHE_MISS, /* Block not in cache */
    TRACE_EVENT_FAULT,            /* Memory fault or exception */
    TRACE_EVENT_SYSCALL_ENTER,    /* System call entered */
    TRACE_EVENT_SYSCALL_RETURN,   /* System call returned */
    TRACE_EVENT_PROCESS_ENTRY,    /* Process execution started */

    /* Block detail events (level 3) */
    TRACE_EVENT_REGISTER_SNAPSHOT, /* Register state captured */
    TRACE_EVENT_PSTATE_SNAPSHOT,   /* PSTATE captured */

    /* Emulation limitation events (level 1) */
    TRACE_EVENT_UNHANDLED_MRS, /* Unhandled MRS system register */
    TRACE_EVENT_UNHANDLED_MSR, /* Unhandled MSR system register */

    /* COMPLEX exit error events (level 2) */
    TRACE_EVENT_COMPLEX_UNKNOWN,     /* Unknown COMPLEX exit */
    TRACE_EVENT_COMPLEX_DECODE_FAIL, /* COMPLEX decode failure */
    TRACE_EVENT_COMPLEX_FETCH_FAIL,  /* COMPLEX fetch failure */

    /* NULL current Crash Diagnostics (NEW) */
    TRACE_EVENT_TASK_THREAD_BEFORE_SET,       /* Task thread before set */
    TRACE_EVENT_TASK_THREAD_AFTER_SET,        /* Task thread after set */
    TRACE_EVENT_TASK_RUN_CURRENT_ENTRY_CHECK, /* Task run_current entry check */

    /* Task lifecycle events (level 1) */
    TRACE_EVENT_TASK_CREATE, /* Task created */
    TRACE_EVENT_TASK_START,  /* Task started execution */

    /* Task thread diagnostics (level 1) */
    TRACE_EVENT_TASK_THREAD_ENTRY,          /* Task thread entry */
    TRACE_EVENT_TASK_THREAD_CURRENT_SET,    /* Task thread current set */
    TRACE_EVENT_TASK_RUN_CURRENT_ENTRY,     /* Task run_current entry */
    TRACE_EVENT_TASK_RUN_CURRENT_MEM_CHECK, /* Task run_current mem check */

    /* Additional task diagnostics (level 1) */
    TRACE_EVENT_TASK_CREATE_RETURN,  /* Task create return */
    TRACE_EVENT_CONSTRUCT_TASK_DONE, /* Construct task done */
    TRACE_EVENT_TASK_START_POINTER,  /* Task start pointer */

    /* MM lifecycle events (level 1) */
    TRACE_EVENT_MM_NEW,           /* mm_new called */
    TRACE_EVENT_MM_COPY,          /* mm_copy called */
    TRACE_EVENT_MM_RETAIN,        /* mm_retain called */
    TRACE_EVENT_MM_RELEASE,       /* mm_release called */
    TRACE_EVENT_MM_RELEASE_FREED, /* mm_release freed mm */
    TRACE_EVENT_TASK_SET_MM,      /* task_set_mm called */

    /* Task Handoff Proof Events */
    TRACE_EVENT_TASK_HANDOFF_PARENT_PRE_CREATE,  /* Parent before pthread_create */
    TRACE_EVENT_TASK_HANDOFF_CHILD_ENTRY,        /* Child entry before current=task */
    TRACE_EVENT_TASK_HANDOFF_CHILD_POST_CURRENT, /* Child after current=task */
    TRACE_EVENT_TASK_HANDOFF_CHILD_PRE_RUN,      /* Child before task_run_current */

    /* Task Initialization Proof Events (parent-side) */
    TRACE_EVENT_TASK_INIT_AFTER_PID_WRITE,    /* Parent after task->pid write */
    TRACE_EVENT_TASK_INIT_AFTER_MM_WRITE,     /* Parent after task->mm write */
    TRACE_EVENT_TASK_INIT_AFTER_MEM_WRITE,    /* Parent after task->mem write */
    TRACE_EVENT_TASK_INIT_PRE_PTHREAD_CREATE, /* Parent pre-pthread_create full state */

    /* Task Field Write Tracking */
    TRACE_EVENT_TASK_FIELD_WRITE, /* Any write to task->pid/mm/mem */

    /* Task checkpoint for finding zero transition */
    TRACE_EVENT_TASK_CHECKPOINT, /* Checkpoint trace for field zero detection */

    /* Memory Snapshot and Bulk Write Detection */
    TRACE_EVENT_TASK_MEM_SNAPSHOT, /* Raw memory snapshot of task struct */
    TRACE_EVENT_TASK_BULK_WRITE,   /* Bulk write detection (memset, memcpy, etc) */
    TRACE_EVENT_TASK_CANARY,       /* Canary value written/read */

    /* Exec Path Investigation Events */
    TRACE_EVENT_EXEC_MM_BOUNDARY,   /* MM operation boundary in exec */
    TRACE_EVENT_EXEC_PATH_BOUNDARY, /* Exec path entry/exit points */

    TRACE_EVENT_MAX
} trace_event_id_t;

/* Event category for filtering */
typedef enum {
    TRACE_CAT_COMPILE = 0x01,  /* Compilation events */
    TRACE_CAT_DECODE = 0x02,   /* Decode events */
    TRACE_CAT_BLOCK = 0x04,    /* Block execution */
    TRACE_CAT_FAULT = 0x08,    /* Fault events */
    TRACE_CAT_SYSCALL = 0x10,  /* Syscall events */
    TRACE_CAT_REGISTER = 0x20, /* Register events */
    TRACE_CAT_MEMORY = 0x40,   /* Memory events (phase 2) */
    TRACE_CAT_GADGET = 0x80,   /* Gadget events (phase 2) */
} trace_category_t;

/* Per-event metadata */
typedef struct trace_event_desc {
    const char *name;
    trace_level_t level;
    trace_category_t category;
    uint8_t payload_size; /* Fixed payload size, 0 for variable */
} trace_event_desc_t;

/* Trace configuration - parsed from environment */
typedef struct trace_config {
    trace_backend_t backend;
    trace_level_t level;
    uint64_t event_mask;    /* Bitmask of enabled events by ID */
    uint64_t category_mask; /* Bitmask of enabled categories */
    uint64_t pc_start;      /* PC range filter start */
    uint64_t pc_end;        /* PC range filter end */
    uint32_t regs_mask;     /* Which registers to snapshot (bits 0-30, 31=SP) */
    size_t ring_size;       /* Ring buffer size in records */
    char output_path[256];  /* Output file path */
    bool dump_on_fault;     /* Auto-dump ring on fault */
    uint64_t max_events;    /* Stop after N events (0=unlimited) */
} trace_config_t;

/* Binary record header - fixed size for ring buffer */
/* Format is always little-endian */
typedef struct __attribute__((packed)) trace_record_header {
    uint64_t seq;          /* Global sequence number */
    uint8_t event_id;      /* Event ID */
    uint8_t level;         /* Trace level of this event */
    uint16_t cpu_id;       /* CPU/thread ID */
    uint64_t pc;           /* Guest PC */
    uint16_t payload_size; /* Payload size in bytes */
} trace_record_header_t;

/* Maximum payload size for fixed buffer */
#define TRACE_MAX_PAYLOAD 48

/* Complete record structure */
typedef struct trace_record {
    trace_record_header_t header;
    uint8_t payload[TRACE_MAX_PAYLOAD];
} trace_record_t;

/* Ring buffer state */
typedef struct trace_ring {
    trace_record_t *records; /* Circular buffer */
    size_t capacity;         /* Total capacity */
    size_t head;             /* Next write position */
    uint64_t seq;            /* Global sequence counter */
    uint64_t dropped;        /* Dropped event count */
    bool wrapped;            /* Has buffer wrapped? */
} trace_ring_t;

/* Ring dump header - written to output files */
typedef struct __attribute__((packed)) trace_dump_header {
    uint8_t magic[4];     /* "ISH\0" */
    uint16_t version;     /* Format version */
    uint16_t header_size; /* Size of this header */
    uint8_t endianness;   /* 1=little, 2=big */
    uint8_t record_header_size;
    uint16_t max_payload_size;
    uint64_t record_count;  /* Number of records in dump */
    uint64_t dropped_count; /* Dropped events before dump */
    uint64_t start_seq;     /* First sequence number */
} trace_dump_header_t;

#define TRACE_MAGIC         { 'I', 'S', 'H', '\0' }
#define TRACE_VERSION       1
#define TRACE_ENDIAN_LITTLE 1

/* Backend operations */
typedef struct trace_backend_ops {
    int (*init)(void **ctx, trace_config_t *config);
    void (*shutdown)(void *ctx);
    void (*emit)(void *ctx, trace_record_t *record);
    void (*flush)(void *ctx);
    int (*dump)(void *ctx, const char *path); /* Returns 0 on success */
} trace_backend_ops_t;

/* Block sidecar - per-block debug metadata */
#define TRACE_MAX_SIDECAR_INSNS 128
#define TRACE_MAX_MNEMONIC_LEN  32

typedef struct trace_sidecar_insn {
    uint64_t pc;
    uint32_t raw_insn;
    char mnemonic[TRACE_MAX_MNEMONIC_LEN];
    uint8_t dst_regs[4]; /* Destination register indices */
    uint8_t src_regs[4]; /* Source register indices */
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

/* Forward declaration - defined in cpu.h */
struct a64_block;

/* Global trace context */
typedef struct trace_ctx {
    trace_config_t config;
    const trace_backend_ops_t *backend_ops;
    void *backend_ctx;
    trace_ring_t *ring;     /* Ring buffer state if backend uses it */
    uint64_t emitted_count; /* Total events emitted */
    bool enabled;           /* Tracing active */
} trace_ctx_t;

#endif /* TRACE_TYPES_H */
