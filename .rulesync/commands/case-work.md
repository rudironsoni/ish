# case-work

Execute implementation for ONE active case only. Stay within allowed patch scope, never write final status.

## Purpose

Own the implementation loop for the active case. Invoke required specialist subagents and skills for that case type.

## Required Pre-conditions

1. `harness-doctor` passed
2. `case-next` selected active case
3. `case-preflight` passed (no blockers)
4. Valid active lock in `tests/cases/active.yaml`
5. Retry budget > 0

## Behavior

1. **Read active case from lock:**
   - Get `case_id`, `phase`, `allowed_patch_scope`
   - Get `required_subagents` for this case type
   - Get `required_skills` for this stage

2. **Validate patch scope:**
   - Confirm all modifications within `allowed_patch_scope`
   - Refuse if scope would be exceeded
   - Fail closed on cross-case modifications

3. **Invoke required specialists by case type:**
   - TRACE-*: `trace-observer`
   - DEC-*: `decoder-truth`
   - GEN-*: `generator-truth`
   - EXEC-*: `isa-truth`
   - MMU-*, ABI-*: `abi-truth`
   - ELF-*: `elf-truth`
   - SYS-*: `abi-truth`
   - THR-*, SIG-*: `abi-truth`

4. **Perform implementation:**
   - Implement case harness
   - Implement verification logic
   - Do NOT modify status ledger
   - Do NOT mark case as REAL PASS

5. **Track retry budget:**
   - Decrement retry budget on repair attempt
   - Log last error for pattern detection
   - Stop if retry budget reaches 0

## Fail-Closed Conditions

This command MUST refuse and report ILLEGAL if:

- No valid active lock exists
- `case-preflight` did not pass
- Patch scope would be exceeded
- Multiple cases being modified
- Status ledger being modified directly
- Required subagents not available
- Retry budget is 0
- **Harness-only task attempts to modify product code**

## Harness-Only Enforcement

When `allowed_patch_scope.level` is `harness-only`:

1. Verify ALL modifications are within control-plane files only
2. Refuse to modify product code:
   - trace/*, emu/*, tcti/*, loader/*, abi/*, syscall/*
   - decoder/generator/execution implementations
3. Only allow:
   - AGENTS.md
   - .rulesync/**/*.md
   - tests/cases/*.yaml and **/*.md
   - .opencode/skill/**/*.md
4. If product code modification detected, abort with:
   - `implementation_result: FAILED`
   - `failure_reason: "Harness-only task scope violation"`

## Output Format

```yaml
case_work_result:
  case_id: "TRACE-002"
  phase: "00-trace-harness"
  status: "COMPLETE"  # COMPLETE | IN_PROGRESS | FAILED
  work_type: "IMPLEMENT"  # IMPLEMENT | REPAIR | SCAFFOLD
  patch_scope_compliant: true
  files_modified:
    - "tests/cases/00-trace-harness/TRACE-002/case.yaml"
    - "tests/cases/00-trace-harness/TRACE-002/expected.yaml"
  retry_budget_remaining: 2
  repair_attempts: 1
  next_command: "case-run"
  ready_for_execution: true
```

## Required Sequence

This command runs AFTER:
- `harness-doctor` (must pass)
- `case-next` (must select active case)
- `case-preflight` (must pass)

This command runs BEFORE:
- `case-run`
- `case-verify`
- `case-promote`

## Required Subagents

- `harness-author` — for harness implementation
- Specialist by case type (see behavior section)

## Required Skills

- `active-case-lifecycle`
- Specialist skills by case type

## Constraints

- ONLY modifies files within `allowed_patch_scope`
- NEVER modifies status ledger directly
- NEVER marks case as REAL PASS
- NEVER works on multiple cases
- Tracks retry budget
- Fails closed on scope violation
