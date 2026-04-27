#ifndef GUEST_EXECUTION_TRACE_SINK_H
#define GUEST_EXECUTION_TRACE_SINK_H

// Test-only trace sink for capturing guest exit events
// Owner: Tests/Support/GuestExecutionHarness
// This is NOT product code - exists only to make exit observation possible

#include <stdbool.h>
#include <stdint.h>

typedef void (^guest_execution_trace_sink_callback_t)(bool exit_observed, int exit_code);

// Initialize trace sink for test harness
void guest_execution_trace_sink_init(void);

// Check if exit has been observed since last reset
bool guest_execution_trace_sink_exit_observed(void);

// Get exit code from last observed exit
int guest_execution_trace_sink_get_exit_code(void);

// Set callback to be invoked when exit is observed
void guest_execution_trace_sink_set_completion_callback(guest_execution_trace_sink_callback_t callback);

// Reset sink state
void guest_execution_trace_sink_reset(void);

// Milestone B: Dynamic ELF loader observation API
// These return true when the corresponding loader boundary was observed via trace events

// B1: PT_INTERP path resolved (interpreter string read from ELF)
bool guest_execution_trace_sink_interp_path_resolved(void);

// M1: Main ELF header accepted (emitted after read_header succeeds for main binary)
bool guest_execution_trace_sink_main_elf_header_accepted(void);

// DIAGNOSTIC: elf_exec was reached
bool guest_execution_trace_sink_elf_exec_reached(void);

// B1: Interpreter header loaded and validated
bool guest_execution_trace_sink_interp_header_loaded(void);

// B2: Interpreter PT_LOAD mappings exist
bool guest_execution_trace_sink_interp_mappings_exist(void);

// B2: Main image loaded (entry point reached)
bool guest_execution_trace_sink_main_image_loaded(void);

// B3: Auxv initialized with AT_BASE for dynamic loader
bool guest_execution_trace_sink_auxv_initialized(void);

// Get the resolved interpreter path (e.g., "/lib/ld-linux-aarch64.so.1")
const char *guest_execution_trace_sink_get_interp_path(void);

// Get the last loader event observed (for debugging)
const char *guest_execution_trace_sink_get_last_loader_event(void);

// D2.0: Interp open state accessors
// These provide explicit D2.0 boundary classification (interpreter open attempt)
bool guest_execution_trace_sink_interp_open_attempted(void);
bool guest_execution_trace_sink_interp_open_succeeded(void);
int guest_execution_trace_sink_interp_open_errno(void);

// Pre-elf_exec diagnostic ladder accessors (X0, X1, X2, X3)
bool guest_execution_trace_sink_do_execve_entered(void);
bool guest_execution_trace_sink_format_exec_entered(void);
bool guest_execution_trace_sink_before_elf_exec_entered(void);
bool guest_execution_trace_sink_elf_exec_entered(void);

// S0: Sink diagnostic accessors - prove callbacks are being invoked
uint64_t guest_execution_trace_sink_begin_interval_calls_count(void);
bool guest_execution_trace_sink_any_interval_received(void);

// --- New proof event accessors (thread-safe) ---
// LDRH probe at 0x6d1c0: addr/read_val/mem_ret/host_ptr
bool guest_execution_trace_sink_has_ldrh_6d1c0(void);
void guest_execution_trace_sink_get_ldrh_6d1c0(uint64_t *addr, uint16_t *read_val, int *mem_ret, uint64_t *host_ptr);

// Writeback probe at 0x6d1c0: x0 after write/value/rt/size/is_64bit
bool guest_execution_trace_sink_has_6d1c0_writeback(void);
void guest_execution_trace_sink_get_6d1c0_writeback(uint64_t *x0_after_write, uint64_t *value, unsigned long *rt, unsigned long *size, int *is_64bit);

// X0 mutation probes: query by PC or ask if any were observed
bool guest_execution_trace_sink_any_x0_mutation(void);
bool guest_execution_trace_sink_has_x0_mutation_at(uint64_t pc);
bool guest_execution_trace_sink_get_x0_mutation_at(uint64_t pc, uint64_t *new_x0, uint64_t *old_x0, uint64_t *value, unsigned long *size, int *is_64bit);

#endif // GUEST_EXECUTION_TRACE_SINK_H
