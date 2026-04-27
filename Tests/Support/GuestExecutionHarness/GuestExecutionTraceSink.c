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

// --- New proof event storage (protected by sink_state_lock) ---
static bool ldrh_6d1c0_seen = false;
static uint64_t ldrh_6d1c0_addr = 0;
static uint16_t ldrh_6d1c0_val = 0;
static int ldrh_6d1c0_mem_ret = 0;
static uint64_t ldrh_6d1c0_host_ptr = 0;

static bool wb_6d1c0_seen = false;
static uint64_t wb_6d1c0_x0_after = 0;
static uint64_t wb_6d1c0_value = 0;
static unsigned long long wb_6d1c0_rt = 0;
static unsigned long long wb_6d1c0_size = 0;
static int wb_6d1c0_is_64bit = 0;

// Simple fixed-size table for x0 mutation observations
#define X0_MUTATION_TABLE_SIZE 32
struct x0_mutation_entry {
    uint64_t pc;
    uint64_t new_x0;
    uint64_t old_x0;
    uint64_t value;
    unsigned long long size;
    int is_64bit;
    int used;
};
static struct x0_mutation_entry x0_mutations[X0_MUTATION_TABLE_SIZE];

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

    // Preserve all prior milestone and diagnostic event handling, then merge in the
    // structured proof parsing implemented earlier. Maintain lock protection for
    // shared mutable sink state.

    // Capture guest.do_exit_group.entry event (Milestones A/B)
    if (strcmp(event_name, "guest.do_exit_group.entry") == 0) {
        exit_event_received = true;
        probe_task_exit_observed(last_exit_code);
        return;
    }
    // Capture guest.do_exit.entry
    if (strcmp(event_name, "guest.do_exit.entry") == 0) {
        exit_event_received = true;
        probe_task_exit_observed(last_exit_code);
        return;
    }

    // Milestone B: Interp path resolved (only if it's a REAL path, not "none")
    if (strstr(event_name, "loader.interpreter_path=path:") != NULL) {
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
        return;
    }

    // DIAGNOSTIC: format_exec was called
    if (strstr(event_name, "loader.format_exec.called") != NULL) {
        snprintf(last_loader_event, sizeof(last_loader_event), "%s", event_name);
        return;
    }

    // DIAGNOSTIC: elf_exec was reached
    if (strstr(event_name, "loader.elf_exec.reached") != NULL) {
        elf_exec_reached = true;
        snprintf(last_loader_event, sizeof(last_loader_event), "%s", event_name);
        return;
    }

    // M1: Main ELF header accepted (emitted after read_header succeeds for main binary)
    if (strstr(event_name, "loader.main_elf.header") != NULL) {
        main_elf_header_accepted = true;
        snprintf(last_loader_event, sizeof(last_loader_event), "%s", event_name);
        return;
    }

    // Milestone B: Interp pt_load mapping
    if (strstr(event_name, "loader.interp.pt_load.map") != NULL) {
        interp_mappings_exist = true;
        snprintf(last_loader_event, sizeof(last_loader_event), "%s", event_name);
        return;
    }

    // Milestone B: Auxv initialized with AT_BASE
    if (strstr(event_name, "loader.auxv.at_base.write") != NULL) {
        auxv_initialized = true;
        snprintf(last_loader_event, sizeof(last_loader_event), "%s", event_name);
        return;
    }

    // D2.0: Interp open result (emitted at exec.c after generic_open attempt)
    if (strstr(event_name, "loader.interp.open.result") != NULL) {
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
        return;
    }

    // Milestone B: Interp header loaded (emitted after read_header succeeds)
    if (strstr(event_name, "loader.interp_elf.header") != NULL) {
        interp_header_loaded = true;
        snprintf(last_loader_event, sizeof(last_loader_event), "%s", event_name);
        return;
    }

    // Milestone B: Interp bias compute (emitted during interpreter mapping phase)
    if (strstr(event_name, "loader.interp.bias.compute") != NULL) {
        interp_header_loaded = true;
        snprintf(last_loader_event, sizeof(last_loader_event), "%s", event_name);
        return;
    }

    // Milestone B: Main image loaded
    if (strstr(event_name, "task.proof.exec.load_entry.reached") != NULL) {
        main_image_loaded = true;
        snprintf(last_loader_event, sizeof(last_loader_event), "%s", event_name);
        return;
    }

    // PROOF events: parse structured proof lines (robust parsing, keep prior semantics intact)
    if (strstr(event_name, "task.proof.6d1c0.ldrh_result=") != NULL) {
        const char *p = strstr(event_name, "addr:");
        if (p) {
            unsigned long long tmp_addr = 0ULL;
            unsigned long long tmp_host_ptr = 0ULL;
            unsigned int read_val = 0;
            int mem_ret = 0;
            int sscanf_ret = sscanf(p, "addr:0x%llx,read_val:0x%04x,mem_ret:%d,host_ptr:0x%llx",
                                    &tmp_addr, &read_val, &mem_ret, &tmp_host_ptr);
            if (sscanf_ret >= 4) {
                os_unfair_lock_lock(&sink_state_lock);
                ldrh_6d1c0_seen = true;
                ldrh_6d1c0_addr = (uint64_t)tmp_addr;
                ldrh_6d1c0_val = (uint16_t)read_val;
                ldrh_6d1c0_mem_ret = mem_ret;
                ldrh_6d1c0_host_ptr = (uint64_t)tmp_host_ptr;
                os_unfair_lock_unlock(&sink_state_lock);
            }
        }
        return;
    }

    if (strstr(event_name, "task.proof.6d1c0.writeback=") != NULL) {
        const char *p = strstr(event_name, "x0_after_write:");
        if (p) {
            unsigned long long x0_after = 0ULL, value = 0ULL, rt_tmp = 0ULL, size_tmp = 0ULL;
            int is64 = 0;
            int sscanf_ret2 = sscanf(p, "x0_after_write:0x%llx,value:0x%llx,rt:%llu,size:%llu,is_64bit:%d",
                                     &x0_after, &value, &rt_tmp, &size_tmp, &is64);
            if (sscanf_ret2 >= 5) {
                os_unfair_lock_lock(&sink_state_lock);
                wb_6d1c0_seen = true;
                wb_6d1c0_x0_after = (uint64_t)x0_after;
                wb_6d1c0_value = (uint64_t)value;
                wb_6d1c0_rt = rt_tmp;
                wb_6d1c0_size = size_tmp;
                wb_6d1c0_is_64bit = is64;
                os_unfair_lock_unlock(&sink_state_lock);
            }
        }
        return;
    }

    if (strstr(event_name, "task.proof.x0.mutation=") != NULL) {
        const char *p = strstr(event_name, "pc:");
        if (p) {
            unsigned long long pc = 0ULL, new_x0 = 0ULL, old_x0 = 0ULL, value = 0ULL, size_tmp = 0ULL;
            int is64 = 0;
            int sscanf_ret3 = sscanf(p, "pc:0x%llx,new_x0:0x%llx,old_x0:0x%llx,value:0x%llx,size:%llu,is_64bit:%d",
                                    &pc, &new_x0, &old_x0, &value, &size_tmp, &is64);
            if (sscanf_ret3 >= 6) {
                os_unfair_lock_lock(&sink_state_lock);
                int inserted = 0;
                for (int i = 0; i < X0_MUTATION_TABLE_SIZE; i++) {
                    if (!x0_mutations[i].used) {
                        x0_mutations[i].pc = (uint64_t)pc;
                        x0_mutations[i].new_x0 = (uint64_t)new_x0;
                        x0_mutations[i].old_x0 = (uint64_t)old_x0;
                        x0_mutations[i].value = (uint64_t)value;
                        x0_mutations[i].size = size_tmp;
                        x0_mutations[i].is_64bit = is64;
                        x0_mutations[i].used = 1;
                        inserted = 1;
                        break;
                    }
                }
                if (!inserted) {
                    x0_mutations[0].pc = (uint64_t)pc;
                    x0_mutations[0].new_x0 = (uint64_t)new_x0;
                    x0_mutations[0].old_x0 = (uint64_t)old_x0;
                    x0_mutations[0].value = (uint64_t)value;
                    x0_mutations[0].size = size_tmp;
                    x0_mutations[0].is_64bit = is64;
                    x0_mutations[0].used = 1;
                }
                os_unfair_lock_unlock(&sink_state_lock);
            }
        }
        return;
    }

    // If we reach here, the event wasn't matched above - keep last_loader_event for diagnostics
    snprintf(last_loader_event, sizeof(last_loader_event), "%s", event_name);
}

static uint64_t test_sink_begin_interval(ixland_instrumentation_origin_t origin,
                                         const char *interval_name,
                                         const ixland_instrumentation_attribute_t *attrs,
                                         uint32_t attr_count)
{
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
    } else if (strcmp(interval_name, "task.proof.loader.elf_header") == 0) {
        for (uint32_t i = 0; i < attr_count; i++) {
            if (attrs[i].key && strcmp(attrs[i].key, "role") == 0 && attrs[i].value) {
                if (strcmp(attrs[i].value, "main") == 0) {
                    main_elf_header_accepted = true;
                    snprintf(last_loader_event, sizeof(last_loader_event), "task.proof.loader.elf_header:role=main");
                    break;
                } else if (strcmp(attrs[i].value, "interp") == 0) {
                    interp_header_loaded = true;
                    snprintf(last_loader_event, sizeof(last_loader_event), "task.proof.loader.elf_header:role=interp");
                    break;
                }
            }
        }
    } else if (strcmp(interval_name, "task.proof.loader.biases") == 0) {
        for (uint32_t i = 0; i < attr_count; i++) {
            if (attrs[i].key && strcmp(attrs[i].key, "interp_path") == 0 && attrs[i].value) {
                if (strncmp(attrs[i].value, "none", 4) != 0) {
                    interp_path_resolved = true;
                    snprintf(resolved_interp_path, sizeof(resolved_interp_path), "%s", attrs[i].value);
                    snprintf(last_loader_event, sizeof(last_loader_event), "task.proof.loader.biases:interp_path_resolved");
                    invoke_completion_callback(false, -1);
                }
                break;
            }
        }
    } else if (strcmp(interval_name, "task.proof.exec.load_entry.reached") == 0) {
        main_image_loaded = true;
        snprintf(last_loader_event, sizeof(last_loader_event), "%s", interval_name);
    } else if (strcmp(interval_name, "task.proof.do_execve.entry") == 0) {
        do_execve_entered = true;
        snprintf(last_loader_event, sizeof(last_loader_event), "task.proof.do_execve.entry");
    } else if (strcmp(interval_name, "task.proof.do_execve.before_format_exec") == 0) {
        format_exec_entered = true;
        snprintf(last_loader_event, sizeof(last_loader_event), "task.proof.do_execve.before_format_exec");
    } else if (strcmp(interval_name, "task.proof.do_execve.before_elf_exec") == 0) {
        before_elf_exec_entered = true;
        snprintf(last_loader_event, sizeof(last_loader_event), "task.proof.do_execve.before_elf_exec");
    } else if (strcmp(interval_name, "task.proof.elf_exec.after_return_to_caller") == 0) {
        elf_exec_entered = true;
        snprintf(last_loader_event, sizeof(last_loader_event), "task.proof.elf_exec.after_return_to_caller");
    }

    return 0;
}

static void test_sink_end_interval(uint64_t interval_id,
                                   const ixland_instrumentation_attribute_t *attrs,
                                   uint32_t attr_count)
{
}

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

// Accessors omitted here are identical to header declarations and focus on proof queries
// Provide minimal set required by tests
bool guest_execution_trace_sink_interp_path_resolved(void) { return interp_path_resolved; }
bool guest_execution_trace_sink_elf_exec_reached(void) { return elf_exec_reached; }
bool guest_execution_trace_sink_main_elf_header_accepted(void) { return main_elf_header_accepted; }
bool guest_execution_trace_sink_interp_header_loaded(void) { return interp_header_loaded; }
bool guest_execution_trace_sink_interp_mappings_exist(void) { return interp_mappings_exist; }
bool guest_execution_trace_sink_main_image_loaded(void) { return main_image_loaded; }
bool guest_execution_trace_sink_auxv_initialized(void) { return auxv_initialized; }
const char *guest_execution_trace_sink_get_interp_path(void) { return resolved_interp_path; }
const char *guest_execution_trace_sink_get_last_loader_event(void) { return last_loader_event; }
bool guest_execution_trace_sink_interp_open_attempted(void) { return interp_open_attempted; }
bool guest_execution_trace_sink_interp_open_succeeded(void) { return interp_open_succeeded; }
int guest_execution_trace_sink_interp_open_errno(void) { return interp_open_errno; }
bool guest_execution_trace_sink_do_execve_entered(void) { return do_execve_entered; }
bool guest_execution_trace_sink_format_exec_entered(void) { return format_exec_entered; }
bool guest_execution_trace_sink_before_elf_exec_entered(void) { return before_elf_exec_entered; }
bool guest_execution_trace_sink_elf_exec_entered(void) { return elf_exec_entered; }
uint64_t guest_execution_trace_sink_begin_interval_calls_count(void) { return begin_interval_calls_count; }
bool guest_execution_trace_sink_any_interval_received(void) { return any_interval_received; }
bool guest_execution_trace_sink_has_ldrh_6d1c0(void) { bool v; os_unfair_lock_lock(&sink_state_lock); v = ldrh_6d1c0_seen; os_unfair_lock_unlock(&sink_state_lock); return v; }
void guest_execution_trace_sink_get_ldrh_6d1c0(uint64_t *addr, uint16_t *read_val, int *mem_ret, uint64_t *host_ptr) { os_unfair_lock_lock(&sink_state_lock); if (addr) *addr = ldrh_6d1c0_addr; if (read_val) *read_val = ldrh_6d1c0_val; if (mem_ret) *mem_ret = ldrh_6d1c0_mem_ret; if (host_ptr) *host_ptr = ldrh_6d1c0_host_ptr; os_unfair_lock_unlock(&sink_state_lock); }
bool guest_execution_trace_sink_has_6d1c0_writeback(void) { bool v; os_unfair_lock_lock(&sink_state_lock); v = wb_6d1c0_seen; os_unfair_lock_unlock(&sink_state_lock); return v; }
void guest_execution_trace_sink_get_6d1c0_writeback(uint64_t *x0_after_write, uint64_t *value, unsigned long *rt, unsigned long *size, int *is_64bit) { os_unfair_lock_lock(&sink_state_lock); if (x0_after_write) *x0_after_write = wb_6d1c0_x0_after; if (value) *value = wb_6d1c0_value; if (rt) *rt = (unsigned long)wb_6d1c0_rt; if (size) *size = (unsigned long)wb_6d1c0_size; if (is_64bit) *is_64bit = wb_6d1c0_is_64bit; os_unfair_lock_unlock(&sink_state_lock); }
bool guest_execution_trace_sink_any_x0_mutation(void) { bool any=false; os_unfair_lock_lock(&sink_state_lock); for (int i=0;i<X0_MUTATION_TABLE_SIZE;i++){ if (x0_mutations[i].used) { any=true; break; } } os_unfair_lock_unlock(&sink_state_lock); return any; }
bool guest_execution_trace_sink_has_x0_mutation_at(uint64_t pc) { bool found=false; os_unfair_lock_lock(&sink_state_lock); for (int i=0;i<X0_MUTATION_TABLE_SIZE;i++){ if (x0_mutations[i].used && x0_mutations[i].pc == pc) { found=true; break; } } os_unfair_lock_unlock(&sink_state_lock); return found; }
bool guest_execution_trace_sink_get_x0_mutation_at(uint64_t pc, uint64_t *new_x0, uint64_t *old_x0, uint64_t *value, unsigned long *size, int *is_64bit) { bool found=false; os_unfair_lock_lock(&sink_state_lock); for (int i=0;i<X0_MUTATION_TABLE_SIZE;i++){ if (x0_mutations[i].used && x0_mutations[i].pc == pc) { if (new_x0) *new_x0 = x0_mutations[i].new_x0; if (old_x0) *old_x0 = x0_mutations[i].old_x0; if (value) *value = x0_mutations[i].value; if (size) *size = (unsigned long)x0_mutations[i].size; if (is_64bit) *is_64bit = x0_mutations[i].is_64bit; found=true; break; } } os_unfair_lock_unlock(&sink_state_lock); return found; }

/* End of test-only trace sink */
