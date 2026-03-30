# case-work

Execute implementation for ONE active case only. Stay within allowed patch scope, never write final status. Supports instrumentation lifecycle and Task Zero mode.

## Purpose

Own the implementation loop for the active case. Invoke required specialist subagents and skills for that case type.

## Required Pre-conditions

1. `harness-doctor` passed
2. `case-next` selected active case
3. `case-preflight` passed (no blockers)
4. Valid active lock in `tests/cases/active.yaml`
5. Retry budget > 0
6. **For app cases:** Task Zero status verified
7. **For app cases:** Instrumentation stage verified

## Behavior

1. **Read active case from lock:**
   - Get `case_id`, `phase`, `allowed_patch_scope`
   - Get `required_subagents` for this case type
   - Get `required_skills` for this stage
   - **For app cases:** Get `app_shell_mode`
   - **For app cases:** Get `instrumentation_stage_required`

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
   - **APPSIM-*, APP-*: `ios-app-driver`, `boot-milestone-auditor`, `instrumentation-verifier`**

4. **Perform implementation:**
   - Implement case harness
   - Implement verification logic
   - Do NOT modify status ledger
   - Do NOT mark case as REAL PASS
   - **For app cases:** Implement instrumentation-aware harness
   - **For app cases:** Handle Task Zero mode if applicable

5. **Track retry budget:**
   - Decrement retry budget on repair attempt
   - Log last error for pattern detection
   - Stop if retry budget reaches 0

6. **Instrumentation compliance:**
   - Ensure app owns instrumentation lifecycle
   - Ensure lower layers emit via C bridge only
   - Ensure no direct os_log/NSLog in product code
   - Ensure no constructor markers

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
- **Task Zero not complete but full_guest case attempted**
- **Prior instrumentation stage not complete**
- **Product code modified to own instrumentation**

## Instrumentation Enforcement

For app cases, this command MUST:
- Verify instrumentation architecture is correct
- Verify app owns lifecycle (main.m bootstrap, AppDelegate activation)
- Verify lower layers emit via C bridge only
- Reject any product code changes that own instrumentation
- Reject direct os_log/NSLog in product code

## Task Zero Handling

For Task Zero cases (`app_shell_mode: task_zero`):
- Implement guest startup disabled
- Implement app shell stabilization
- Implement terminal UI reachability
- Implement instrumentation activation

## Runtime Boundary Handling

For runtime reintroduction (`app_shell_mode: full_guest`):
- Implement one boundary at a time
- Implement exact failing edge reporting
- Implement last known good / first known bad tracking

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
  
  # App case specific
  app_shell_mode: "task_zero"  # or "full_guest"
  instrumentation_stage_required: "bootstrap"
  instrumentation_architecture_compliant: true
  task_zero_handled: true
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
- `instrumentation-verifier` — for app cases with instrumentation

## Required Skills

- `active-case-lifecycle`
- Specialist skills by case type
- `instrumentation-stage-audit` — for app cases

## Constraints

- ONLY modifies files within `allowed_patch_scope`
- NEVER modifies status ledger directly
- NEVER marks case as REAL PASS
- NEVER works on multiple cases
- Tracks retry budget
- Fails closed on scope violation
- Enforces instrumentation ownership
- Handles Task Zero mode
- Handles runtime boundaries
