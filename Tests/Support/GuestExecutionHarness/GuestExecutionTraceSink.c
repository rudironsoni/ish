// GuestExecutionTraceSink.c
// Test-only trace sink for capturing guest exit events
// Owner: Tests/Support/GuestExecutionHarness
// Uses ixland_instrumentation_register_sink() to observe events externally

#include "GuestExecutionTraceSink.h"

#include "GuestExecutionProbe.h"

#include <IXLandInstrumentation/IXLandInstrumentation.h>
#include <Block.h>
#include <os/lock.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Static state for exit observation
// These MUST NOT be __thread - callback is set from XCTest thread but invoked from guest thread
// exit_event_received and last_exit_code are checked from both threads
static bool sink_registered = false;
static bool exit_event_received = false;
static int last_exit_code = -1;
static guest_execution_trace_sink_callback_t completion_callback = NULL;
static os_unfair_lock sink_state_lock = OS_UNFAIR_LOCK_INIT;

static void invoke_completion_callback(bool exit_observed, int exit_code)
{
    guest_execution_trace_sink_callback_t callback = NULL;
    os_unfair_lock_lock(&sink_state_lock);
    callback = completion_callback;
    os_unfair_lock_unlock(&sink_state_lock);

    if (callback != NULL) {
        callback(exit_observed, exit_code);
    }
}

// S0: Sink Diagnostic - proves callbacks are being invoked
static uint64_t begin_interval_calls_count = 0;
static bool any_interval_received = false;

// Milestone B: Dynamic ELF loader state
static bool interp_path_resolved = false;
static bool elf_exec_reached = false;
static bool main_elf_header_accepted = false;
static bool interp_header_loaded = false;
static bool interp_mappings_exist = false;
static bool main_image_loaded = false;
static bool auxv_initialized = false;
static char resolved_interp_path[256] = { 0 };
static char last_loader_event[256] = { 0 };

// D2.0: Interp open state tracking
static bool interp_open_attempted = false;
static bool interp_open_succeeded = false;
static int interp_open_errno = 0;

// Pre-elf_exec diagnostic ladder (X0-X3)
static bool do_execve_entered = false;
static bool format_exec_entered = false;
static bool before_elf_exec_entered = false; // X2
static bool elf_exec_entered = false;        // X3 (elf_exec returned)

// Forward declaration
static void test_sink_record_event(ixland_instrumentation_origin_t origin, const char *event_name);
static uint64_t test_sink_begin_interval(ixland_instrumentation_origin_t origin,
                                         const char *interval_name,
                                         const ixland_instrumentation_attribute_t *attrs,
                                         uint32_t attr_count);
static void test_sink_end_interval(uint64_t interval_id,
                                   const ixland_instrumentation_attribute_t *attrs,
                                   uint32_t attr_count);
static bool test_sink_is_active(void);

// Test sink implementation - captures events from runtime
static ixland_instrumentation_sink_t test_sink = { .bootstrap = NULL,
                                                   .activate = NULL,
                                                   .is_active = test_sink_is_active,
                                                   .record_event = test_sink_record_event,
                                                   .begin_interval = test_sink_begin_interval,
                                                   .end_interval = test_sink_end_interval };

static void test_sink_record_event(ixland_instrumentation_origin_t origin, const char *event_name)
{
    if (!event_name)
        return;

    // Capture guest.do_exit_group.entry event (Milestones A/B)
    if (strcmp(event_name, "guest.do_exit_group.entry") == 0) {
        exit_event_received = true;
        probe_task_exit_observed(last_exit_code);
    }
    // Capture guest.do_exit.entry
    else if (strcmp(event_name, "guest.do_exit.entry") == 0) {
        exit_event_received = true;
        probe_task_exit_observed(last_exit_code);
    }
    // Milestone B: Interp path resolved (only if it's a REAL path, not "none")
    else if (strstr(event_name, "loader.interpreter_path=path:") != NULL) {
        const char *path_start = strstr(event_name, "path:");
        if (path_start) {
            path_start += 5; // Skip "path:"
            // Only count if it's not "none" - real dynamic ELF must have a real interpreter
            if (strncmp(path_start, "none", 4) != 0) {
                interp_path_resolved = true;
                snprintf(resolved_interp_path, sizeof(resolved_interp_path), "%s", path_start);
                snprintf(last_loader_event, sizeof(last_loader_event), "%s", event_name);
                // Invoke callback for loader boundary - interp path resolved
                invoke_completion_callback(false, -1);
            }
        }
    }
    // DIAGNOSTIC: format_exec was called
    else if (strstr(event_name, "loader.format_exec.called") != NULL) {
        snprintf(last_loader_event, sizeof(last_loader_event), "%s", event_name);
    }
    // DIAGNOSTIC: elf_exec was reached
    else if (strstr(event_name, "loader.elf_exec.reached") != NULL) {
        elf_exec_reached = true;
        snprintf(last_loader_event, sizeof(last_loader_event), "%s", event_name);
    }
    // M1: Main ELF header accepted (emitted after read_header succeeds for main binary)
    else if (strstr(event_name, "loader.main_elf.header") != NULL) {
        main_elf_header_accepted = true;
        snprintf(last_loader_event, sizeof(last_loader_event), "%s", event_name);
    }
    // Milestone B: Interp pt_load mapping
    else if (strstr(event_name, "loader.interp.pt_load.map") != NULL) {
        interp_mappings_exist = true;
        snprintf(last_loader_event, sizeof(last_loader_event), "%s", event_name);
    }
    // Milestone B: Auxv initialized with AT_BASE
    else if (strstr(event_name, "loader.auxv.at_base.write") != NULL) {
        auxv_initialized = true;
        snprintf(last_loader_event, sizeof(last_loader_event), "%s", event_name);
    }
    // D2.0: Interp open result (emitted at exec.c:754 after generic_open attempt)
    else if (strstr(event_name, "loader.interp.open.result") != NULL) {
        interp_open_attempted = true;
        snprintf(last_loader_event, sizeof(last_loader_event), "%s", event_name);
        // Parse err:X from event to classify open result
        const char *err_str = strstr(event_name, "err:");
        if (err_str) {
            int err_val = atoi(err_str + 4);
            if (err_val == 0) {
                interp_open_succeeded = true;
                interp_open_errno = 0;
            } else {
                interp_open_succeeded = false;
                interp_open_errno = err_val;
            }
        }
    }
    // Milestone B: Interp header loaded (emitted after read_header succeeds)
    else if (strstr(event_name, "loader.interp_elf.header") != NULL) {
        interp_header_loaded = true;
        snprintf(last_loader_event, sizeof(last_loader_event), "%s", event_name);
    }
    // Milestone B: Interp bias compute (emitted during interpreter mapping phase, after D2.1/D2.2)
    else if (strstr(event_name, "loader.interp.bias.compute") != NULL) {
        interp_header_loaded = true;
        snprintf(last_loader_event, sizeof(last_loader_event), "%s", event_name);
    }
    // Milestone B: Main image loaded
    else if (strstr(event_name, "task.proof.exec.load_entry.reached") != NULL) {
        main_image_loaded = true;
        snprintf(last_loader_event, sizeof(last_loader_event), "%s", event_name);
    }
}

static uint64_t test_sink_begin_interval(ixland_instrumentation_origin_t origin,
                                         const char *interval_name,
                                         const ixland_instrumentation_attribute_t *attrs,
                                         uint32_t attr_count)
{
    // S0: Diagnostic - track that begin_interval is actually being called
    begin_interval_calls_count++;
    any_interval_received = true;

    if (!interval_name)
        return 0;

    if (strcmp(interval_name, "guest.do_exit_group.entry") == 0) {
        exit_event_received = true;
        last_exit_code = -1;
        for (uint32_t i = 0; i < attr_count; i++) {
            if (attrs[i].key && strcmp(attrs[i].key, "status") == 0) {
                last_exit_code = atoi(attrs[i].value);
                break;
            }
        }
        probe_task_exit_observed(last_exit_code);
        invoke_completion_callback(true, last_exit_code);
    } else if (strcmp(interval_name, "guest.do_exit.entry") == 0) {
        exit_event_received = true;
        last_exit_code = -1;
        for (uint32_t i = 0; i < attr_count; i++) {
            if (attrs[i].key && strcmp(attrs[i].key, "status") == 0) {
                last_exit_code = atoi(attrs[i].value);
                break;
            }
        }
        probe_task_exit_observed(last_exit_code);
        invoke_completion_callback(true, last_exit_code);
    } else if (strcmp(interval_name, "guest.first_fault.exit") == 0) {
        probe_capture_boundary(PROBE_BOUNDARY_FAULT);
    } else if (strcmp(interval_name, "task.proof.loader.elf_header") == 0) {
        // M1: Main ELF header checkpoint - this interval fires regardless of record_event budget
        // Check if role attribute is "main" (indicates elf_exec processing main binary)
        // D2.1: Interpreter ELF header checkpoint - same interval name, role="interp"
        for (uint32_t i = 0; i < attr_count; i++) {
            if (attrs[i].key && strcmp(attrs[i].key, "role") == 0 && attrs[i].value) {
                if (strcmp(attrs[i].value, "main") == 0) {
                    main_elf_header_accepted = true;
                    snprintf(last_loader_event, sizeof(last_loader_event),
                             "task.proof.loader.elf_header:role=main");
                    break;
                } else if (strcmp(attrs[i].value, "interp") == 0) {
                    interp_header_loaded = true;
                    snprintf(last_loader_event, sizeof(last_loader_event),
                             "task.proof.loader.elf_header:role=interp");
                    break;
                }
            }
        }
    } else if (strcmp(interval_name, "task.proof.loader.biases") == 0) {
        // Non-budgeted stable loader checkpoint - fires unconditionally
        // Contains interp_path attribute with resolved interpreter path
        for (uint32_t i = 0; i < attr_count; i++) {
            if (attrs[i].key && strcmp(attrs[i].key, "interp_path") == 0 && attrs[i].value) {
                // Only count real interpreter paths, not "none"
                if (strncmp(attrs[i].value, "none", 4) != 0) {
                    interp_path_resolved = true;
                    snprintf(resolved_interp_path, sizeof(resolved_interp_path), "%s",
                             attrs[i].value);
                    snprintf(last_loader_event, sizeof(last_loader_event),
                             "task.proof.loader.biases:interp_path_resolved");
                    // Invoke callback for loader boundary - interp path resolved via stable
                    // interval
                    invoke_completion_callback(false, -1);
                }
                break;
            }
        }
    } else if (strcmp(interval_name, "task.proof.exec.load_entry.reached") == 0) {
        main_image_loaded = true;
        snprintf(last_loader_event, sizeof(last_loader_event), "%s", interval_name);
    } else if (strcmp(interval_name, "task.proof.do_execve.entry") == 0) {
        // X0: do_execve entry checkpoint
        do_execve_entered = true;
        snprintf(last_loader_event, sizeof(last_loader_event), "task.proof.do_execve.entry");
    } else if (strcmp(interval_name, "task.proof.do_execve.before_format_exec") == 0) {
        // X1: before format_exec - proves transition into format_exec
        format_exec_entered = true;
        snprintf(last_loader_event, sizeof(last_loader_event),
                 "task.proof.do_execve.before_format_exec");
    } else if (strcmp(interval_name, "task.proof.do_execve.before_elf_exec") == 0) {
        // X2: before elf_exec - proves transition from format_exec to elf_exec
        before_elf_exec_entered = true;
        snprintf(last_loader_event, sizeof(last_loader_event),
                 "task.proof.do_execve.before_elf_exec");
    } else if (strcmp(interval_name, "task.proof.elf_exec.after_return_to_caller") == 0) {
        // X3: elf_exec returned to caller - proves elf_exec was entered
        elf_exec_entered = true;
        snprintf(last_loader_event, sizeof(last_loader_event),
                 "task.proof.elf_exec.after_return_to_caller");
    }

    return 0;
}

static void test_sink_end_interval(uint64_t interval_id,
                                   const ixland_instrumentation_attribute_t *attrs,
                                   uint32_t attr_count)
{
}

// Test sink is always active in test context - instrumentation only works when a sink is registered
static bool test_sink_is_active(void)
{
    return true;
}

// Public API
void guest_execution_trace_sink_init(void)
{
    ixland_instrumentation_register_sink(&test_sink);
    sink_registered = true;
}

bool guest_execution_trace_sink_exit_observed(void)
{
    return exit_event_received;
}

int guest_execution_trace_sink_get_exit_code(void)
{
    return last_exit_code;
}

void guest_execution_trace_sink_set_completion_callback(
    guest_execution_trace_sink_callback_t callback)
{
    os_unfair_lock_lock(&sink_state_lock);
    if (completion_callback != NULL) {
        Block_release(completion_callback);
        completion_callback = NULL;
    }

    if (callback != NULL) {
        completion_callback = Block_copy(callback);
    }
    os_unfair_lock_unlock(&sink_state_lock);
}

void guest_execution_trace_sink_reset(void)
{
    os_unfair_lock_lock(&sink_state_lock);
    exit_event_received = false;
    last_exit_code = -1;
    completion_callback = NULL;
    interp_path_resolved = false;
    elf_exec_reached = false;
    main_elf_header_accepted = false;
    interp_header_loaded = false;
    interp_mappings_exist = false;
    main_image_loaded = false;
    auxv_initialized = false;
    resolved_interp_path[0] = '\0';
    last_loader_event[0] = '\0';
    interp_open_attempted = false;
    interp_open_succeeded = false;
    interp_open_errno = 0;
    do_execve_entered = false;
    format_exec_entered = false;
    before_elf_exec_entered = false;
    elf_exec_entered = false;
    begin_interval_calls_count = 0;
    any_interval_received = false;
    os_unfair_lock_unlock(&sink_state_lock);
}

// Milestone B: Dynamic ELF observation API
bool guest_execution_trace_sink_interp_path_resolved(void)
{
    return interp_path_resolved;
}

// DIAGNOSTIC: elf_exec was reached
bool guest_execution_trace_sink_elf_exec_reached(void)
{
    return elf_exec_reached;
}

// M1: Main ELF header accepted (emitted after read_header succeeds for main binary)
bool guest_execution_trace_sink_main_elf_header_accepted(void)
{
    return main_elf_header_accepted;
}

bool guest_execution_trace_sink_interp_header_loaded(void)
{
    return interp_header_loaded;
}

bool guest_execution_trace_sink_interp_mappings_exist(void)
{
    return interp_mappings_exist;
}

bool guest_execution_trace_sink_main_image_loaded(void)
{
    return main_image_loaded;
}

bool guest_execution_trace_sink_auxv_initialized(void)
{
    return auxv_initialized;
}

const char *guest_execution_trace_sink_get_interp_path(void)
{
    return resolved_interp_path;
}

const char *guest_execution_trace_sink_get_last_loader_event(void)
{
    return last_loader_event;
}

// D2.0: Interp open state accessors
bool guest_execution_trace_sink_interp_open_attempted(void)
{
    return interp_open_attempted;
}

bool guest_execution_trace_sink_interp_open_succeeded(void)
{
    return interp_open_succeeded;
}

int guest_execution_trace_sink_interp_open_errno(void)
{
    return interp_open_errno;
}

// Pre-elf_exec diagnostic ladder accessors
bool guest_execution_trace_sink_do_execve_entered(void)
{
    return do_execve_entered;
}

bool guest_execution_trace_sink_format_exec_entered(void)
{
    return format_exec_entered;
}

bool guest_execution_trace_sink_before_elf_exec_entered(void)
{
    return before_elf_exec_entered;
}

bool guest_execution_trace_sink_elf_exec_entered(void)
{
    return elf_exec_entered;
}


// S0: Diagnostic accessors - prove sink is receiving callbacks
uint64_t guest_execution_trace_sink_begin_interval_calls_count(void)
{
    return begin_interval_calls_count;
}

bool guest_execution_trace_sink_any_interval_received(void)
{
    return any_interval_received;
}
