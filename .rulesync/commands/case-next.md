# case-next

Deterministically select the single lawful next case for implementation work.

## Purpose

This command is the ONLY lawful way to select an active case. It reads the machine-readable phase inventory and status ledger, applies the active-case algorithm, and returns exactly one case that is eligible for work.

## Behavior

1. Read `tests/cases/execution-order.yaml` to get:
   - Canonical phase order (00, 01, 02, 02b, 02c, 03 through 11)
   - Complete gate inventory for each phase
   - Gating rules and prerequisites
   - Legacy/deprecated cases to exclude (exclude_from_selection: true)

2. Read `tests/cases/status.yaml` to get:
   - Current status of every gate case
   - Summary statistics
   - Earliest unsatisfied phase/case hints

3. Apply the active-case algorithm:
   - Find the earliest phase whose gate cases are NOT all `REAL PASS`
   - Within that phase, find the earliest gate case that is not `REAL PASS`
   - That case is the ACTIVE CASE

4. Verify the active case:
   - If status is STUB: ready for implementation
   - If status is REAL FAIL: ready for repair
   - If status is BLOCKED: move to blocking prerequisite
   - If status is INVALID: requires contract repair first

5. Detect Task Zero cases:
   - Check `app_shell_mode` field
   - If `task_zero`: Task Zero mode (guest disabled, app shell stabilization)
   - If `full_guest`: Full guest mode (runtime reintroduction)
   - Task Zero cases MUST be satisfied before full_guest cases

6. Detect instrumentation stage requirements:
   - Check `instrumentation_stage_required` field
   - `bootstrap`: Requires `ish_instrumentation_bootstrap()` to succeed
   - `activate`: Requires `[ISHInstrumentation activate]` to succeed
   - `runtime`: Requires runtime event emission via C bridge

7. Create/update `tests/cases/active.yaml`:
   - Set case_id, phase, status_before
   - Set retry_budget_remaining (default: 3)
   - Set allowed_patch_scope
   - Set required_subagents and required_skills
   - Set stop_conditions
   - Set app_shell_mode if app case
   - Set instrumentation_stage_required if app case

8. Return next-lawful-action output

## Output Format

```yaml
next_lawful_action:
  active_phase: "00-trace-harness"
  active_case: "TRACE-002"
  current_status: "STUB"
  next_action: "SCAFFOLD"  # SCAFFOLD | IMPLEMENT | REPAIR | VERIFY | PROMOTE | BLOCKED
  reason: "Case directory does not exist; must scaffold first"
  
  # App case specific
  app_shell_mode: "task_zero"  # or "full_guest"
  instrumentation_stage_required: "bootstrap"  # or "activate" or "runtime"
  guest_startup: "disabled"  # or "enabled"
  
  required_commands:
    immediate: "case-preflight"
    sequence:
      - "case-preflight"
      - "case-work"
      - "case-run"
      - "case-verify"
      - "case-promote"
  
  required_subagents:
    - "orchestrator"
    - "harness-author"
    - "trace-observer"
    - "instrumentation-verifier"  # for app cases with instrumentation
  
  required_skills:
    - "active-case-lifecycle"
    - "phase-gate-audit"
    - "anti-slop-review"
    - "instrumentation-stage-audit"  # for app cases
  
  allowed_patch_scope:
    level: "case-only"
    paths:
      - "tests/cases/00-trace-harness/TRACE-002/**"
  
  stop_conditions:
    - "Case produces artifacts matching expected.yaml"
    - "Verifier confirms REAL PASS"
    - "Anti-slop review passes"
    - "Instrumentation stage reached (for app cases)"
  
  retry_budget:
    remaining: 3
    max: 3
  
  active_lock:
    created: true
    path: "tests/cases/active.yaml"
```

## Task Zero Detection

For app cases, detect Task Zero mode:

```yaml
task_zero_detection:
  if_app_shell_mode_is: "task_zero"
  then:
    guest_startup: "disabled"
    goal: "Stabilize app shell without guest execution"
    required_stage: "instrumentation_activate"
    blocked_until: "AppDelegate activates instrumentation"
  if_app_shell_mode_is: "full_guest"
  then:
    guest_startup: "enabled"
    goal: "Reintroduce guest runtime one boundary at a time"
    requires: "Task Zero cases are REAL PASS"
```

## Instrumentation Stage Detection

For app cases, detect required instrumentation stage:

```yaml
instrumentation_stage_detection:
  stage_bootstrap:
    required: "ish_instrumentation_bootstrap() succeeds"
    owner: "main.m"
    events_expected: ["instrumentation_bootstrapped"]
  
  stage_activate:
    required: "[ISHInstrumentation activate] succeeds"
    owner: "AppDelegate"
    events_expected: ["instrumentation_activated"]
    requires: "Stage bootstrap complete"
  
  stage_runtime:
    required: "Runtime emits events via C bridge"
    owner: "kernel/emu/tcti"
    events_expected: ["guest_init", "syscall_enter", "syscall_exit"]
    requires: "Stage activate complete"
```

## Fail-Closed Conditions

This command MUST refuse and exit with error if:

- `tests/cases/execution-order.yaml` is missing or malformed
- `tests/cases/status.yaml` is missing or malformed
- Phase inventory and status ledger have conflicting case IDs
- No unsatisfied gate cases exist (all 108 cases are REAL PASS)
- Multiple cases appear equally eligible (algorithm ambiguity)
- Required fields are missing from status entries
- Task Zero cases not defined but app cases present
- Instrumentation stage requirements missing from app cases
- Task Zero not satisfied but full_guest cases requested

## Required Sequence

This command MUST be run before:
- `case-preflight`
- `case-work`
- `case-run`
- `case-verify`
- `case-promote`

This command runs AFTER:
- `harness-doctor` (must pass first)

## Constraints

- Returns exactly one case
- Never returns multiple cases
- Never returns a case from a later phase when earlier phases have unsatisfied gates
- Never returns a BLOCKED case without identifying the blocking prerequisite
- Never returns a full_guest case when Task Zero cases are not REAL PASS
- Creates/updates active.yaml
- Does NOT create scaffolding
- Does NOT run the case
- MUST detect Task Zero as first-class state
- MUST detect instrumentation stage requirements

## Required Subagents

- orchestrator
- phase-gate
- instrumentation-verifier (for app cases)

## Required Skills

- active-case-lifecycle
- phase-gate-audit
- instrumentation-stage-audit (for app cases)
