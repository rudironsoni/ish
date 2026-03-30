---
name: runtime_boundary_reduction
description: Enforce one-boundary-at-a-time runtime reintroduction with exact failing edge reporting
---

You are the runtime boundary reduction skill. Apply this procedure when reintroducing guest runtime.

## Purpose

Ensure runtime reintroduction proceeds one exact boundary at a time with precise failure reporting.

## Prerequisites

Before runtime reintroduction:
- Task Zero MUST be complete (app shell stabilized)
- Instrumentation MUST be activated (Stage 1 complete)
- Observability freeze MAY be active (if runtime is the blocker)

## Procedure

### 1. Identify Current Boundary

Determine which runtime boundary to test:
- Boundary is a single, well-defined transition point
- Examples: guest_init → first_syscall, syscall_enter → syscall_exit, elf_exec_enter → elf_exec_return

### 2. Enable Single Boundary

Configuration:
- Enable only the current boundary
- Keep all subsequent boundaries disabled
- Guest execution limited to current boundary scope

### 3. Execute Test

Run app with:
- Current boundary enabled
- Instrumentation recording events
- Crash detection enabled

### 4. Collect Results

Record:
- Did the boundary execute?
- Did it complete successfully?
- Did it crash?
- What was the last successful event?
- What was the first failing event?
- What is the exact failing edge?

### 5. Report Exact Edge

If boundary fails, report:
```yaml
runtime_boundary_result:
  boundary_id: "guest_init_first_syscall"
  status: "REAL FAIL"
  last_known_good: "guest_init_entered"
  first_known_bad: "first_syscall_invocation"
  exact_failing_edge: "transition from guest_init to first_syscall"
  crash_signature: "..."  # if applicable
  events_before_failure:
    - "guest_init_entered"
    - "guest_init_setup_complete"
  events_at_failure:
    - "first_syscall_invocation"
  error: "SIGSEGV at 0x..."
```

### 6. Handle Success

If boundary succeeds:
- Mark as REAL PASS
- Proceed to next boundary
- Repeat procedure

### 7. Handle Failure

If boundary fails:
- Mark as REAL FAIL
- Do NOT proceed to next boundary
- Repair this boundary first
- Require exact edge report

## Forbidden Patterns

- Multiple boundaries enabled simultaneously
- Vague "kernel issue" narratives
- Broad runtime debugging
- Skipping boundary verification
- Proceeding without exact edge report
- Broad observability when freeze should be active

## Reporting Requirements

Each boundary MUST report:
- Exact boundary name
- Last known good point (event/function/line)
- First known bad point (event/function/line)
- Exact failing edge (transition between good and bad)
- Crash signature (if applicable)
- Events before failure
- Error details

## Success Criteria

- One boundary tested at a time
- Exact failing edge reported on failure
- No vague narratives
- No broad debugging
- Clear good/bad separation
- Observability freeze respected (if active)
