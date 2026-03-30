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
   - Total case count must match (122)
   - Phase inventory must be complete (14 phases including 02b, 02c)
   - No duplicate case IDs
   - App cases (APPSIM-*, APP-*) have required app-specific fields

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

7. **Verify app harness readiness:**
   - XcodeBuildMCP availability: `session_show_defaults` returns valid response
   - Simulator targets discoverable: `list_sims` succeeds
   - App artifact schemas present in schema.yaml
   - Phase 02b and 02c cases exist in status.yaml

8. **Verify instrumentation architecture readiness:**
   - `ISHInstrumentation` framework reference defined
   - C bridge API references present (`ish_instrumentation_*`)
   - Objective-C façade references present (`[ISHInstrumentation *]`)
   - No stale trace ownership assumptions
   - No direct NSLog/os_log in product code for investigation
   - Instrumentation lifecycle ownership defined (app owns, lower layers emit only)
   - Task Zero mode defined as first-class state

9. **Verify Task Zero readiness:**
   - Task Zero cases defined (app_shell_mode: task_zero)
   - Task Zero gate criteria documented
   - App shell stabilization criteria defined

10. **Verify observability freeze rule:**
    - Observability freeze trigger defined
    - Freeze conditions documented
    - Narrow exception criteria defined

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
- Stale trace ownership assumptions present
- Instrumentation ownership model not defined
- Task Zero mode not defined
- Observability freeze rule not defined
- Product code assumed to own instrumentation bootstrap

## Stale Assumptions Check

This command MUST verify NO stale assumptions exist:
- Trace system does NOT own instrumentation policy
- Trace backends do NOT own bootstrap
- Product code does NOT call NSLog/os_log for investigation
- Constructor markers are NOT used for instrumentation
- Startup proof files are NOT required
- Ring recovery is NOT the expected startup behavior

## Instrumentation Architecture Verification

MUST verify these are defined:
- `ISHInstrumentation` framework
- C bridge: `ish_instrumentation_bootstrap`, `ish_instrumentation_activate`, `ish_instrumentation_is_active`
- C bridge events: `ish_instrumentation_record_event`, `ish_instrumentation_begin_interval`, `ish_instrumentation_end_interval`
- Objective-C façade: `[ISHInstrumentation bootstrap]`, `[ISHInstrumentation activate]`, `[ISHInstrumentation recordEvent:]`
- Ownership: app owns lifecycle, lower layers emit semantic events only

## Task Zero Verification

MUST verify Task Zero is defined as:
- Guest startup disabled
- App shell stabilized
- Terminal UI reachable without guest execution
- Runtime reintroduction blocked until shell is stable
- Instrumentation bootstrap complete

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
    # Instrumentation checks
    instrumentation_architecture_defined: true
    instrumentation_ownership_model_defined: true
    task_zero_mode_defined: true
    observability_freeze_defined: true
    no_stale_trace_assumptions: true
    # Stale assumption checks
    trace_ownership_stale: false
    product_code_instrumentation_bootstrap: false
    direct_nslog_in_product_code: false
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
- `instrumentation-verifier` — for instrumentation architecture validation

## Required Skills

- `harness-health-audit`
- `phase-gate-audit`
- `instrumentation-stage-audit`

## Constraints

- Does NOT modify any files
- Does NOT create scaffolding
- Does NOT select active case
- Blocks ALL downstream commands on failure
- Must report exact failure reason
- MUST fail closed on stale trace ownership assumptions
- MUST verify instrumentation architecture is defined
