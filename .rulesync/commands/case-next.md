# case-next

Deterministically select the single lawful next case for implementation work.

## Purpose

This command is the ONLY lawful way to select an active case. It reads the machine-readable phase inventory and status ledger, applies the active-case algorithm, and returns exactly one case that is eligible for work.

## Behavior

1. Read `tests/cases/execution-order.yaml` to get:
   - Canonical phase order (00 through 11)
   - Complete gate inventory for each phase
   - Gating rules and prerequisites

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

5. Create/update `tests/cases/active.yaml`:
   - Set case_id, phase, status_before
   - Set retry_budget_remaining (default: 3)
   - Set allowed_patch_scope
   - Set required_subagents and required_skills
   - Set stop_conditions

6. Return next-lawful-action output

## Output Format

```yaml
next_lawful_action:
  active_phase: "00-trace-harness"
  active_case: "TRACE-002"
  current_status: "STUB"
  next_action: "SCAFFOLD"  # SCAFFOLD | IMPLEMENT | REPAIR | VERIFY | PROMOTE | BLOCKED
  reason: "Case directory does not exist; must scaffold first"
  
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
  
  required_skills:
    - "active-case-lifecycle"
    - "phase-gate-audit"
    - "anti-slop-review"
  
  allowed_patch_scope:
    level: "case-only"
    paths:
      - "tests/cases/00-trace-harness/TRACE-002/**"
  
  stop_conditions:
    - "Case produces artifacts matching expected.yaml"
    - "Verifier confirms REAL PASS"
    - "Anti-slop review passes"
  
  retry_budget:
    remaining: 3
    max: 3
  
  active_lock:
    created: true
    path: "tests/cases/active.yaml"
```

## Fail-Closed Conditions

This command MUST refuse and exit with error if:

- `tests/cases/execution-order.yaml` is missing or malformed
- `tests/cases/status.yaml` is missing or malformed
- Phase inventory and status ledger have conflicting case IDs
- No unsatisfied gate cases exist (all 108 cases are REAL PASS)
- Multiple cases appear equally eligible (algorithm ambiguity)
- Required fields are missing from status entries

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
- Creates/updates active.yaml
- Does NOT create scaffolding
- Does NOT run the case

## Required Subagents

- orchestrator
- phase-gate

## Required Skills

- active-case-lifecycle
- phase-gate-audit
