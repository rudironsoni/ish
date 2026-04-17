#ifndef GUEST_EXECUTION_PROBE_H
#define GUEST_EXECUTION_PROBE_H

#include <stdbool.h>
#include <stdint.h>

// Test-only probe for external boundary observation
// This is NOT product code - it exists only to make test harness oracle valid
// Called from product runtime to report boundaries to test harness

// External probe function called by runtime at do_exit_group entry
void probe_task_exit_observed(int exit_code);

typedef enum {
    PROBE_BOUNDARY_NONE = 0,
    PROBE_BOUNDARY_SYSCALL,
    PROBE_BOUNDARY_FAULT,
    PROBE_BOUNDARY_BLOCK_COMPLETE,
    PROBE_BOUNDARY_ITERATION_LIMIT,
    PROBE_BOUNDARY_ERROR
} probe_boundary_reason_t;

typedef struct {
    const char *fixture_name;
    bool load_ok;
    uint64_t pc_before;
    uint64_t pc_after;
    uint64_t sp_before;
    uint64_t sp_after;
    uint64_t x0_after;
    uint64_t x8_after;
    bool block_compiled;
    bool block_executed;
    probe_boundary_reason_t boundary_reason;
    bool reached_syscall_boundary;
    bool reached_fault_boundary;
    bool task_exit_observed;
    int exit_code;
    bool returned_to_harness;
    bool completed;
    const char *error_message;
    // Harness classification ladder H0-H4
    bool harness_entered;
    int mount_root_return_value;
    bool mount_root_called;
    int become_first_process_return_value;
    bool become_first_process_called;
    bool do_execve_reached;
    int do_execve_return_value;
    bool do_execve_called;
} guest_execution_probe_result_t;

// Probe API - called from harness
void probe_reset(void);
guest_execution_probe_result_t *probe_get_result(void);

// Probe API - called from runtime (test-only hooks)
void probe_begin_fixture(const char *name);
void probe_capture_pc_before(uint64_t pc);
void probe_capture_pc_after(uint64_t pc);
void probe_capture_boundary(probe_boundary_reason_t reason);
void probe_task_exit_observed(int exit_code);
void probe_set_error(const char *msg);
void probe_set_completed(bool completed);

#endif // GUEST_EXECUTION_PROBE_H
