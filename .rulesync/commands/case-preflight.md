# case-preflight

Verify the active case is ready for implementation work and block illegal operations before they occur.

## Purpose

This command is the fail-closed gate that prevents illegal work from starting. It runs after `case-next` selects the active case and before any file modifications occur.

## Behavior

1. Read the active case ID from `case-next` output

2. Verify case contract exists and is valid:
   - Check `tests/cases/<phase>/<case_id>/case.yaml` exists
   - Check `tests/cases/<phase>/<case_id>/expected.yaml` exists
   - Check `tests/cases/<phase>/<case_id>/authority.yaml` exists
   - Validate schema compliance

3. Verify phase progression is lawful:
   - Confirm all earlier phases have all gate cases at `REAL PASS`
   - Confirm current phase is the earliest unsatisfied phase
   - Confirm no phase skipping would occur

4. Verify only one implementation-active case:
   - Confirm no other case directories are being modified
   - Confirm patch scope is limited to active case files
   - Confirm no broad cross-case changes

5. Verify explicit Meson identity exists:
   - Check `meson.build` has explicit test definition
   - Verify test name matches case_id
   - Confirm no dynamic discovery

6. Verify no illegal conditions:
   - Not attempting to skip phases
   - Not attempting multi-case repairs
   - Not modifying generated artifacts directly
   - Not working on BLOCKED case without addressing blocker

7. **Verify Task Zero compliance (for app cases):**
   - If `app_shell_mode: full_guest`, verify all Task Zero cases are `REAL PASS`
   - If `app_shell_mode: task_zero`, verify no guest startup expected
   - Verify Task Zero gate not skipped

8. **Verify instrumentation stage (for app cases):**
   - Check `instrumentation_stage_required` field
   - Verify all prior stages are `REAL PASS`
   - Verify stage sequencing (bootstrap → activate → runtime)
   - Verify no stage skipping

9. **Verify no stale trace assumptions:**
   - Verify trace system does NOT own instrumentation
   - Verify product code does NOT own instrumentation bootstrap
   - Verify no direct NSLog/os_log expected in product code
   - Verify no constructor markers expected
   - Verify no startup proof files expected

## Output Format

```yaml
preflight_result:
  case_id: "TRACE-001"
  phase: "00-trace-harness"
  status: "STUB"
  
  # App case specific
  app_shell_mode: "task_zero"  # or "full_guest"
  instrumentation_stage_required: "bootstrap"  # or "activate" or "runtime"
  
  checks_passed:
    contract_exists: true
    contract_valid: false
    phase_progression_lawful: true
    single_active_case: true
    meson_identity_explicit: false
    no_illegal_conditions: true
    task_zero_compliant: true  # for app cases
    instrumentation_stage_valid: true  # for app cases
    no_stale_trace_assumptions: true
  
  blockers:
    - "Contract invalid: case.yaml missing required field 'harness_kind'"
    - "Meson identity missing: no explicit test registration for TRACE-001"
    - "Task Zero not complete: cannot proceed with full_guest case"  # if applicable
    - "Instrumentation stage not reached: bootstrap incomplete"  # if applicable
  
  ready_for_work: false
  required_first:
    - "Delegate to case-substrate to scaffold case directory"
    - "Delegate to meson-wire to add explicit Meson test registration"
    - "Complete Task Zero cases first"  # if applicable
    - "Complete prior instrumentation stage"  # if applicable
```

## Fail-Closed Conditions

This command MUST refuse and report ILLEGAL if:

- Phase progression would skip unsatisfied earlier phases
- Multiple cases are being touched simultaneously
- Case contract is INVALID and not being repaired
- Explicit Meson identity is missing
- Patch scope extends beyond active case boundaries
- Working on BLOCKED case without addressing prerequisite
- **Harness-only task attempts to modify product code** (trace/, emu/, tcti/, loader/, abi/, syscall/)
- **Task Zero not complete but full_guest case attempted**
- **Prior instrumentation stage not complete but later stage attempted**
- **Stale trace assumptions present**

## Task Zero Enforcement

For app cases with `app_shell_mode: full_guest`:
- MUST verify all Task Zero cases are `REAL PASS`
- MUST fail closed if Task Zero incomplete
- MUST report earliest unsatisfied Task Zero case

## Instrumentation Stage Enforcement

For app cases with instrumentation requirements:
- Stage 0 (bootstrap) MUST be complete before Stage 1
- Stage 1 (activate) MUST be complete before Stage 2
- MUST fail closed if prior stage incomplete
- MUST report earliest unsatisfied stage

## Stale Assumption Detection

This command MUST detect and block:
- Trace system owning instrumentation policy
- Product code owning instrumentation bootstrap
- Direct NSLog/osLog in product code
- Constructor markers for instrumentation
- Startup proof files expected
- Ring recovery expected

## Harness-Only Scope Validation

When `allowed_patch_scope.level` is `harness-only`:

1. Check that NO product code paths are in the scope
2. Product code paths include:
   - `trace/**`
   - `emu/**`
   - `tcti/**`
   - `loader/**`
   - `abi/**`
   - `syscall/**`
   - Any decoder/generator/execution implementation

3. Verify ONLY control-plane files are targeted:
   - `AGENTS.md`
   - `.rulesync/**/*.md`
   - `tests/cases/*.yaml`
   - `tests/cases/**/*.md`
   - `.opencode/skill/**/*.md`

4. If product code paths detected, fail closed with:
   - `blocker: "Harness-only task attempted to modify product code: <path>"`
   - `ready_for_work: false`
   - `required_first: "Remove product code paths from patch scope"`

## Required Sequence

This command MUST be run:
- AFTER `case-next` returns the active case
- BEFORE any file modifications
- BEFORE any implementation work begins

## Constraints

- Does NOT modify any files
- Does NOT create scaffolding
- Does NOT run the case
- Blocks work with clear error message if checks fail
- Reports exactly what must be fixed first
- Enforces Task Zero as first-class gate
- Enforces instrumentation stage sequencing
- Detects stale trace assumptions
