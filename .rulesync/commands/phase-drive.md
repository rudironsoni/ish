# phase-drive

The only lawful entrypoint for autonomous case work. Implements the self-continuing execution loop.

## Purpose

This command implements the mandatory outer-loop that processes cases automatically until a stop condition is met. It is the ONLY way to perform non-trivial autonomous work.

## Behavior

The `phase-drive` command implements this exact sequence:

```
phase-drive:
  1. harness-doctor
     └─ fail → stop with reason: control_plane_failure
  
  2. case-next
     └─ select active case → create/update active.yaml
     └─ no unsatisfied cases → stop with reason: mission_complete
  
  3. case-preflight
     └─ fail → stop with reason: preflight_blocked
  
  4. case-work
     └─ implement/repair active case
  
  5. case-run
     └─ execute and produce artifacts
  
  6. case-verify
     └─ produce evidence-backed classification
  
  7. case-promote
     └─ update status.yaml and active.yaml
  
  8. Check continuation:
     ├─ if REAL PASS and budget remains → GOTO step 2
     ├─ if REAL FAIL and retry budget remains → GOTO step 4
     └─ if stop condition met → stop with reason
```

## Session Budget Enforcement

Before each iteration, check `session_budget` in `active.yaml`:

- `max_cases_per_session`: Maximum cases to process (default: 3)
- `max_phase_progression`: Maximum phases to complete (default: 1)
- `session_case_count`: Incremented after each successful promotion

If budget exhausted, stop with `stop_reason: session_budget_exhausted`.

## Output Format

```yaml
phase_drive_result:
  session:
    session_id: "2026-03-27T00:00:00Z"
    started_at: "2026-03-27T00:00:00Z"
    cases_processed: 2
    budget_remaining: 1
  
  execution_log:
    - case_id: "TRACE-001"
      status: "REAL PASS"
      promotion_at: "2026-03-27T00:10:00Z"
    - case_id: "TRACE-002"
      status: "REAL PASS"
      promotion_at: "2026-03-27T00:20:00Z"
  
  continuation:
    next_case_required: true
    next_lawful_case: "TRACE-003"
    next_lawful_action: "IMPLEMENT"
    reason: "Budget remaining, more cases to process"
  
  stop_condition:
    stop_requested: false
    budget_exhausted: false
    all_complete: false
    explicit_reason: null
```

## Stop Conditions

The agent stops ONLY if:

1. **Control plane failure** - `harness-doctor` fails
2. **Mission complete** - All 108 cases are REAL PASS
3. **Blocked dependency** - Next case is BLOCKED
4. **Invalid contract** - Next case is INVALID
5. **Budget exhausted** - Session case budget reached
6. **Explicit user stop** - User requests stop

## Required Subagents

- `orchestrator` - overall coordination
- `phase-gate` - phase progression validation

## Required Skills

- `active-case-lifecycle`
- `phase-gate-audit`
- `harness-health-audit`

## Constraints

- This is the ONLY lawful entrypoint for autonomous work
- MUST check budget before each iteration
- MUST emit checkpoint report after each case
- MUST stop cleanly with explicit reason
- MUST NOT stop after promotion without checking continuation
