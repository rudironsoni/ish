# case-run

Run exactly one active case through its explicit Meson entrypoint and collect deterministic evidence. Supports instrumentation lifecycle and Task Zero mode.

## Purpose

Use this command to run the active case after the contract is valid.

## Required sequence

1. Run `pre-run hook`
2. Confirm:
   - active case ID
   - exact Meson test identity
   - artifact directory
   - verifier expectations
   - **for app cases:** app_shell_mode
   - **for app cases:** instrumentation_stage_required
3. Run the explicit Meson case target
4. Collect emitted artifacts
5. Invoke `verifier`
6. Invoke `anti-slop`
7. **For app cases:** invoke `instrumentation-verifier`
8. **For app cases:** invoke `boot-milestone-auditor`
9. **For app cases:** invoke `crash-classifier`
10. If phase movement is implicated, invoke `phase-gate`

## Required outputs

- explicit run result
- artifact set
- exact case status
- mismatch report if not `REAL PASS`
- **for app cases:** instrumentation events recorded
- **for app cases:** Task Zero milestones reached
- **for app cases:** runtime boundary results (if applicable)

## App Case Specific Behavior

For app cases (APPSIM-*, APP-*):

### Task Zero mode
- Run with guest startup disabled
- Verify app shell stabilizes
- Verify terminal UI reachable
- Verify instrumentation activated
- Record Task Zero milestones

### Full guest mode
- Run with guest startup enabled
- Enable one runtime boundary at a time
- Record last known good point
- Record first known bad point
- Record exact failing edge
- Report runtime boundary results

### Instrumentation verification
- Verify instrumentation stage reached
- Verify events recorded via C bridge
- Verify no direct os_log/NSLog in product code
- Verify app owns lifecycle
- Record instrumentation events artifact

## Refuse if

- the case contract is `INVALID`
- Meson identity is inconsistent
- the harness still contains known fake-success fallback behavior
- **Task Zero not complete but full_guest case attempted**
- **Prior instrumentation stage not complete**
- **Instrumentation events not via C bridge**

## Output Format

```yaml
case_run_result:
  case_id: "APPSIM-003"
  run_success: true
  artifacts_produced:
    - "sim_launch.json"
    - "boot_milestones.json"
    - "report.json"
    - "instrumentation_events.json"
  
  # App case specific
  app_shell_mode: "task_zero"
  instrumentation_stage_reached: "activate"
  task_zero_complete: true
  
  runtime_boundary:
    boundary_id: "guest_init_first_syscall"
    last_known_good: "guest_init_entered"
    first_known_bad: null
    exact_failing_edge: null
  
  instrumentation_events:
    - event: "instrumentation_bootstrap_complete"
      timestamp: "2026-03-28T12:00:01Z"
      source: "main.m"
    - event: "instrumentation_activate_complete"
      timestamp: "2026-03-28T12:00:02Z"
      source: "AppDelegate"
    - event: "app_shell_stabilized"
      timestamp: "2026-03-28T12:00:03Z"
      task_zero: true
  
  milestones_reached:
    - "app_launched"
    - "instrumentation_bootstrap_complete"
    - "instrumentation_activate_complete"
    - "app_shell_stabilized"
  
  next_command: "case-verify"
```

## Required Subagents

- `verifier` — evidence comparison
- `anti-slop` — fake success detection
- `instrumentation-verifier` — for app cases
- `boot-milestone-auditor` — for app cases
- `crash-classifier` — for app cases (if crash)

## Required Skills

- `active-case-lifecycle`
- `instrumentation-stage-audit` — for app cases
- `runtime-boundary-reduction` — for app cases with guest execution

## Constraints

- Runs exactly one case
- Collects all artifacts
- Reports exact status
- Verifies instrumentation for app cases
- Verifies Task Zero for app cases
- Reports runtime boundaries for full_guest cases
