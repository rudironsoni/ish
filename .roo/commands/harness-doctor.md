# harness-doctor

Validate control-plane integrity before any work begins. Fail-closed if any inconsistencies detected.

## Purpose

This is the FIRST REQUIRED COMMAND for all non-trivial work. No other commands may run until harness-doctor passes.

## Behavior

1. **Verify control-plane files exist and are valid:**
   - `tests/cases/execution-order.yaml` (canonical phase inventory)
   - `tests/cases/status.yaml` (evidence-bearing status ledger)
   - `tests/cases/active.yaml` (active case lock)
   - `AGENTS.md` (control bootloader)

2. **Cross-check consistency:**
   - Execution-order.yaml and status.yaml must have matching case IDs
   - Total case count must match (108)
   - Phase inventory must be complete (12 phases)
   - No duplicate case IDs

3. **Verify synchronized command files:**
   - All required commands must have synchronized definitions
   - No placeholder commands (only scaffolded content)
   - No hardcoded control-plane paths in source bodies

4. **Validate active.yaml integrity:**
   - Schema version matches expected
   - All required fields present
   - No active case conflicts
   - Retry budget within bounds

5. **Check mandatory subagent definitions:**
   - Subagent invocation matrix defined
   - Skills mapping defined
   - Stage-to-subagent mapping complete

6. **Verify fail-closed enforcement:**
   - Patch-scope enforcement rules present
   - Retry budget limits defined
   - Refusal rules documented

## Fail-Closed Conditions

This command MUST refuse and exit with error if:

- Control-plane files are missing or malformed
- Execution-order.yaml and status.yaml disagree on case inventory
- Case counts don't match (expected: 108)
- Required commands are missing from `.rulesync/commands/`
- Required subagents not defined
- Required skills not mapped
- AGENTS.md contradicts machine-readable state
- active.yaml is malformed or inconsistent
- Broken placeholders exist
- Hardcoded control-plane path regressions exist
- Meson-only policy contradictions detected

## Output Format

```yaml
harness_health:
  status: "PASS"  # PASS | FAIL
  timestamp: "2026-03-27T00:00:00Z"
  checks:
    control_plane_files: true
    execution_order_valid: true
    status_ledger_valid: true
    active_lock_valid: true
    case_inventory_consistent: true
    required_commands_present: true
    required_subagents_defined: true
    required_skills_mapped: true
    patch_scope_enforcement: true
    retry_budgets_defined: true
  failures: []
  next_command: "case-next"
```

## Required Sequence

This command MUST be run FIRST before:
- `case-next`
- `case-preflight`
- `case-work`
- `case-run`
- `case-verify`
- `case-promote`

## Required Subagents

- `orchestrator` — for overall coordination
- `phase-gate` — for phase validation

## Required Skills

- `harness-health-audit`
- `phase-gate-audit`

## Constraints

- Does NOT modify any files
- Does NOT create scaffolding
- Does NOT select active case
- Blocks ALL downstream commands on failure
- Must report exact failure reason
