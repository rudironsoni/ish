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

#endif // GUEST_EXECUTION_TRACE_SINK_H
