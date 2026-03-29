/*
 * iSH Profiling Infrastructure
 *
 * Provides performance profiling capabilities for the TCTI emulator.
 * Supports multiple backends: statistical counters, flame graphs, and
 * continuous profiling for development builds.
 *
 * Usage:
 *   - Build with -Denable_profiling=true for profiling support
 *   - Set ISH_PROFILE_OUTPUT=/path/to/profile.json to capture profiles
 *   - Use ish-profile-tool to analyze captured profiles
 */

#ifndef ISH_PROF_H
#define ISH_PROF_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

// Profiling configuration
#define PROF_MAX_STACK_DEPTH 128
#define PROF_MAX_THREADS 64
#define PROF_SAMPLE_INTERVAL_US 1000  // 1ms default sampling
#define PROF_FLAMEGRAPH_MAX_FRAMES 4096

// Profile event types
typedef enum {
    PROF_EVENT_SAMPLE,      // Periodic CPU sample
    PROF_EVENT_ALLOC,       // Memory allocation
    PROF_EVENT_FREE,        // Memory free
    PROF_EVENT_SYSCALL,     // System call entry/exit
    PROF_EVENT_TB_COMPILE,  // Translation block compilation
    PROF_EVENT_TB_EXECUTE,  // Translation block execution
    PROF_EVENT_INTERRUPT,   // Interrupt handling
    PROF_EVENT_TLB_MISS,    // TLB miss
    PROF_EVENT_BLOCK_CACHE, // Block cache operation
} prof_event_type_t;

// Stack frame for profiling
typedef struct prof_frame {
    const char *name;           // Function/symbol name
    const char *file;           // Source file
    uint32_t line;              // Line number
    uint64_t pc;                // Program counter (guest)
    void *host_pc;              // Host PC for native frames
} prof_frame_t;

// Profile sample
typedef struct prof_sample {
    uint64_t timestamp_ns;      // Nanosecond timestamp
    uint64_t thread_id;         // Thread identifier
    prof_event_type_t type;     // Event type
    uint64_t duration_ns;       // Event duration (for timed events)
    int stack_depth;            // Number of valid frames
    prof_frame_t stack[PROF_MAX_STACK_DEPTH];
    union {
        struct {
            uint64_t size;      // Allocation size
            void *ptr;          // Allocation address
        } alloc;
        struct {
            int syscall_no;     // System call number
            uint64_t arg0;      // First argument
        } syscall;
        struct {
            uint64_t guest_ip;  // Guest instruction pointer
            uint32_t insn_count; // Instructions in block
        } tb;
        struct {
            uint64_t vaddr;     // Virtual address
            int level;          // TLB level (L0/L1)
        } tlb;
    } data;
} prof_sample_t;

// Profile writer state
typedef struct prof_writer {
    int fd;                     // Output file descriptor
    char *buffer;               // Write buffer
    size_t buf_size;            // Buffer size
    size_t buf_used;            // Bytes used in buffer
    uint64_t sample_count;      // Total samples written
    bool enabled;               // Profiling active
    struct timespec start_time; // Profile start time
} prof_writer_t;

// Global profiling state
extern prof_writer_t g_prof_writer;
extern _Thread_local int prof_thread_id;

// Core profiling API

/**
 * Initialize profiling system
 * @param output_path Path to output file (NULL for default)
 * @return true on success
 */
bool prof_init(const char *output_path);

/**
 * Shutdown profiling and flush buffers
 */
void prof_shutdown(void);

/**
 * Check if profiling is enabled
 */
static inline bool prof_enabled(void) {
    return g_prof_writer.enabled;
}

/**
 * Capture a profile sample with current stack
 * @param type Event type
 */
void prof_capture_sample(prof_event_type_t type);

/**
 * Begin a timed event
 * @param type Event type
 * @return Event handle for prof_end
 */
uint64_t prof_begin(prof_event_type_t type);

/**
 * End a timed event
 * @param handle Handle from prof_begin
 * @param type Event type (must match prof_begin)
 */
void prof_end(uint64_t handle, prof_event_type_t type);

/**
 * Record allocation event
 * @param ptr Allocated pointer
 * @param size Allocation size
 * @param name Allocator function name
 */
void prof_record_alloc(void *ptr, size_t size, const char *name);

/**
 * Record free event
 * @param ptr Freed pointer
 */
void prof_record_free(void *ptr);

/**
 * Record TLB miss
 * @param vaddr Virtual address
 * @param is_write Write access
 * @param level TLB level (0=L0, 1=L1)
 */
void prof_record_tlb_miss(uint64_t vaddr, bool is_write, int level);

/**
 * Record translation block compilation
 * @param guest_ip Guest instruction pointer
 * @param insn_count Number of instructions
 * @param host_code_size Generated code size
 */
void prof_record_tb_compile(uint64_t guest_ip, uint32_t insn_count, size_t host_code_size);

/**
 * Record translation block execution
 * @param guest_ip Guest instruction pointer
 * @param cycles Executed cycles (approximate)
 */
void prof_record_tb_execute(uint64_t guest_ip, uint64_t cycles);

/**
 * Dump current stats to log
 */
void prof_dump_stats(void);

// Convenience macros for profiling

#ifdef ENABLE_PROFILING

#define PROF_CAPTURE(type) prof_capture_sample(type)
#define PROF_BEGIN(type) prof_begin(type)
#define PROF_END(handle, type) prof_end(handle, type)
#define PROF_RECORD_ALLOC(ptr, size) prof_record_alloc(ptr, size, __func__)
#define PROF_RECORD_FREE(ptr) prof_record_free(ptr)
#define PROF_RECORD_TLB_MISS(vaddr, is_write, level) prof_record_tlb_miss(vaddr, is_write, level)
#define PROF_RECORD_TB_COMPILE(ip, count, size) prof_record_tb_compile(ip, count, size)
#define PROF_RECORD_TB_EXECUTE(ip, cycles) prof_record_tb_execute(ip, cycles)

// Scoped profiling - automatically times a block
#define PROF_SCOPE(name) \
    __attribute__((cleanup(prof_scope_cleanup))) \
    uint64_t _prof_scope_handle = prof_begin_scoped(name, __FILE__, __LINE__)

// Function entry/exit profiling
#define PROF_FUNC_ENTER() prof_capture_sample(PROF_EVENT_SAMPLE)

#else  // !ENABLE_PROFILING

#define PROF_CAPTURE(type) ((void)0)
#define PROF_BEGIN(type) 0
#define PROF_END(handle, type) ((void)(handle))
#define PROF_RECORD_ALLOC(ptr, size) ((void)0)
#define PROF_RECORD_FREE(ptr) ((void)0)
#define PROF_RECORD_TLB_MISS(vaddr, is_write, level) ((void)0)
#define PROF_RECORD_TB_COMPILE(ip, count, size) ((void)0)
#define PROF_RECORD_TB_EXECUTE(ip, cycles) ((void)0)
#define PROF_SCOPE(name) ((void)0)
#define PROF_FUNC_ENTER() ((void)0)

#endif  // ENABLE_PROFILING

// Internal helpers (don't use directly)
uint64_t prof_begin_scoped(const char *name, const char *file, int line);
void prof_scope_cleanup(uint64_t *handle);

// Flame graph generation

/**
 * Write flame graph data in folded format
 * @param output_path Output file path
 * @return true on success
 */
bool prof_write_flamegraph(const char *output_path);

// Continuous profiling integration

/**
 * Start continuous profiling with periodic dumps
 * @param interval_ms Dump interval in milliseconds
 * @return true on success
 */
bool prof_start_continuous(uint32_t interval_ms);

/**
 * Stop continuous profiling
 */
void prof_stop_continuous(void);

// Profile analysis helpers

/**
 * Get total sample count
 */
uint64_t prof_get_sample_count(void);

/**
 * Get profiling duration in nanoseconds
 */
uint64_t prof_get_duration_ns(void);

/**
 * Format profile stats as JSON
 * @param buf Output buffer
 * @param buf_size Buffer size
 * @return Bytes written (or needed if buf is NULL)
 */
int prof_format_stats_json(char *buf, size_t buf_size);

#endif  // ISH_PROF_H
