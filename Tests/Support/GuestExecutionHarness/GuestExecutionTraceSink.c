// GuestExecutionTraceSink.c
// Test-only trace sink for capturing guest exit events
// Owner: Tests/Support/GuestExecutionHarness
// Uses ixland_instrumentation_register_sink() to observe events externally

#include "GuestExecutionTraceSink.h"

#include "GuestExecutionProbe.h"

#include <IXLandInstrumentation/IXLandInstrumentation.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Static state for exit observation
// These MUST NOT be __thread - callback is set from XCTest thread but invoked from guest thread
// exit_event_received and last_exit_code are checked from both threads
static bool sink_registered = false;
static bool exit_event_received = false;
static int last_exit_code = -1;
static guest_execution_trace_sink_callback_t completion_callback = NULL;

// Milestone B: Dynamic ELF loader state
static bool interp_path_resolved = false;
static bool interp_header_loaded = false;
static bool interp_mappings_exist = false;
static bool main_image_loaded = false;
static bool auxv_initialized = false;
static char resolved_interp_path[256] = { 0 };
static char last_loader_event[256] = { 0 };

// Forward declaration
static void test_sink_record_event(ixland_instrumentation_origin_t origin, const char *event_name);
static uint64_t test_sink_begin_interval(ixland_instrumentation_origin_t origin,
                                         const char *interval_name,
                                         const ixland_instrumentation_attribute_t *attrs,
                                         uint32_t attr_count);
static void test_sink_end_interval(uint64_t interval_id,
                                   const ixland_instrumentation_attribute_t *attrs,
                                   uint32_t attr_count);

// Test sink implementation - captures events from runtime
static ixland_instrumentation_sink_t test_sink = { .bootstrap = NULL,
                                                   .activate = NULL,
                                                   .is_active = NULL,
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
                strncpy(resolved_interp_path, path_start, sizeof(resolved_interp_path) - 1);
                strncpy(last_loader_event, event_name, sizeof(last_loader_event) - 1);
            }
        }
    }
    // Milestone B: Interp pt_load mapping
    else if (strstr(event_name, "loader.interp.pt_load.map") != NULL) {
        interp_mappings_exist = true;
        strncpy(last_loader_event, event_name, sizeof(last_loader_event) - 1);
    }
    // Milestone B: Auxv initialized with AT_BASE
    else if (strstr(event_name, "loader.auxv.at_base.write") != NULL) {
        auxv_initialized = true;
        strncpy(last_loader_event, event_name, sizeof(last_loader_event) - 1);
    }
    // Milestone B: Interp header loaded
    else if (strstr(event_name, "loader.interp.bias.compute") != NULL) {
        interp_header_loaded = true;
        strncpy(last_loader_event, event_name, sizeof(last_loader_event) - 1);
    }
    // Milestone B: Main image loaded
    else if (strstr(event_name, "task.proof.exec.load_entry.reached") != NULL) {
        main_image_loaded = true;
        strncpy(last_loader_event, event_name, sizeof(last_loader_event) - 1);
    }
}

static uint64_t test_sink_begin_interval(ixland_instrumentation_origin_t origin,
                                         const char *interval_name,
                                         const ixland_instrumentation_attribute_t *attrs,
                                         uint32_t attr_count)
{
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
        if (completion_callback) {
            completion_callback(true, last_exit_code);
        }
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
        if (completion_callback) {
            completion_callback(true, last_exit_code);
        }
    } else if (strcmp(interval_name, "guest.first_fault.exit") == 0) {
        probe_capture_boundary(PROBE_BOUNDARY_FAULT);
    }

    return 0;
}

static void test_sink_end_interval(uint64_t interval_id,
                                   const ixland_instrumentation_attribute_t *attrs,
                                   uint32_t attr_count)
{
}

// Public API
void guest_execution_trace_sink_init(void)
{
    if (!sink_registered) {
        ixland_instrumentation_register_sink(&test_sink);
        sink_registered = true;
    }
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
    completion_callback = callback;
}

void guest_execution_trace_sink_reset(void)
{
    exit_event_received = false;
    last_exit_code = -1;
    completion_callback = NULL;
    interp_path_resolved = false;
    interp_header_loaded = false;
    interp_mappings_exist = false;
    main_image_loaded = false;
    auxv_initialized = false;
    resolved_interp_path[0] = '\0';
    last_loader_event[0] = '\0';
}

// Milestone B: Dynamic ELF observation API
bool guest_execution_trace_sink_interp_path_resolved(void)
{
    return interp_path_resolved;
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
