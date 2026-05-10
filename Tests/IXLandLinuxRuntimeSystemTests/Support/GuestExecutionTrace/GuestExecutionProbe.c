#include "GuestExecutionProbe.h"

#include <string.h>

// Single static instance for test observation
// This is thread-local to avoid conflicts between tests
static __thread guest_execution_probe_result_t probe_result;
static __thread bool probe_initialized = false;

void probe_reset(void)
{
    memset(&probe_result, 0, sizeof(probe_result));
    probe_result.exit_code = -1;
    probe_result.boundary_reason = PROBE_BOUNDARY_NONE;
    probe_initialized = true;
}

guest_execution_probe_result_t *probe_get_result(void)
{
    if (!probe_initialized) {
        probe_reset();
    }
    return &probe_result;
}

void probe_begin_fixture(const char *name)
{
    if (!probe_initialized) {
        probe_reset();
    }
    probe_result.fixture_name = name;
}

void probe_capture_pc_before(uint64_t pc)
{
    if (!probe_initialized)
        return;
    probe_result.pc_before = pc;
}

void probe_capture_pc_after(uint64_t pc)
{
    if (!probe_initialized)
        return;
    probe_result.pc_after = pc;
}

void probe_capture_boundary(probe_boundary_reason_t reason)
{
    if (!probe_initialized)
        return;
    probe_result.boundary_reason = reason;

    switch (reason) {
    case PROBE_BOUNDARY_SYSCALL:
        probe_result.reached_syscall_boundary = true;
        break;
    case PROBE_BOUNDARY_FAULT:
        probe_result.reached_fault_boundary = true;
        break;
    default:
        break;
    }
}

void probe_task_exit_observed(int exit_code)
{
    if (!probe_initialized)
        return;
    probe_result.task_exit_observed = true;
    probe_result.exit_code = exit_code;
    probe_result.reached_syscall_boundary = true;
    probe_result.boundary_reason = PROBE_BOUNDARY_SYSCALL;
    probe_result.completed = true;
}

void probe_set_error(const char *msg)
{
    if (!probe_initialized)
        return;
    probe_result.error_message = msg;
}

void probe_set_completed(bool completed)
{
    if (!probe_initialized)
        return;
    probe_result.completed = completed;
    probe_result.returned_to_harness = completed;
}
