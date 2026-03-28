---
trigger: /phase-drive
turbo: true
---
# Workflow: /phase-drive

# phase-drive

The only lawful entrypoint for autonomous case work. Implements the self-continuing execution loop.

## Purpose

This command implements the mandatory outer-loop that processes cases automatically until a stop condition is met. It is the ONLY way to perform non-trivial autonomous work.

## Critical Rule: Selection is NOT Terminal

**The loop must NOT stop when selecting a new active case.**

After promotion, the loop must:
1. Select the next lawful case
2. IMMEDIATELY dispatch into its required stage
3. Continue until an explicit stop condition is met

Selection, scaffolding, and preflight success are NOT terminal states.

## Autonomous Execution Mode

**Default scope: current-phase only**

The loop runs continuously within the current phase until:
- All gate cases in the phase are REAL PASS
- A gate case is classified as REAL FAIL (terminal stop)
- A case is classified as BLOCKED (terminal stop)
- A case is classified as INVALID (terminal stop)
- harness-doctor fails (terminal stop)
- Resources unavailable (terminal stop)
- Budget exhausted (terminal stop)

**Retry policy (hardcoded):**
- One bounded retry with reset if case policy allows
- Repeated same normalized crash signature → classify REAL FAIL
- No further retries on repeated signature

## App Case Detection and Dispatch

**App case patterns:** `APPSIM-*`, `APP-*`

For app cases, `phase-drive` MUST dispatch through mandatory subagents and skills:

### Startup Stage (before first iteration)
- `harness-doctor` - verify control plane
- `orchestrator` - overall coordination
- `phase-gate` - phase validation
- `ios-app-driver` - XcodeBuildMCP capability audit (app cases only)

### Selection Stage (case-next)
- `orchestrator` - case selection
- `phase-gate` - phase boundary validation
- Skip legacy cases (IOS-001, IOS-002, IOS-003: excluded from selection)

### Preflight Stage (case-preflight)
For app cases:
- `simulator-launch-audit` skill
- `app-case-lifecycle` skill
- `meson-wire-check` skill
- Validation only (no state mutation)
- Stateless: verify defaults resolvable, simulator targetable

### Implementation/Work Stage (case-work, case-run)
For app cases:
- `ios-app-driver` subagent - XcodeBuildMCP operations
- `self-healing-runner` subagent - bounded retry discipline
- `boot-milestone-auditor` subagent - milestone extraction
- `crash-classifier` subagent - signature normalization

**Primary operation:** `build_run_sim` (for smoke tests)
**Relaunch operation:** `launch_app_logs_sim` (when build known-good)

### Verification Stage (case-verify)
For app cases:
- `verifier` - evidence comparison
- `anti-slop` - fake success detection
- `review-skeptic` - adversarial review
- `simulator-artifact-verifier` subagent - artifact validation
- `exec-entry-truth` subagent - process entry validation (where relevant)

### Promotion Stage (case-promote)
- `orchestrator` - coordination
- `phase-gate` - phase progression validation
- `active-case-lifecycle` skill

## Behavior

The `phase-drive` command implements a self-continuing dispatch loop:

```
phase-drive:
  Initialize session_state in active.yaml
  
  LOOP until terminal_stop:
    1. Check continuation invariants from active.yaml:
       - can_continue: must be true
       - terminal_stop: must be false
       - budget_remaining: must be > 0
       - stop_reason: must be null
    
    2. If any invariant violated → EXIT with explicit stop_reason
    
    3. Read current_stage and next_action from active.yaml
    
    4. DISPATCH based on next_action:
       
       IF next_action = "SELECT" or case_just_promoted:
         - Run case-next to select active case
         - Skip legacy/deprecated cases (exclude_from_selection: true)
         - Detect app cases (APPSIM-*, APP-*) for specialized dispatch
         - Update active.yaml with new case
         - Set next_action based on case state:
           ├─ STUB with no substrate → next_action = SCAFFOLD
           ├─ STUB with substrate → next_action = IMPLEMENT
           ├─ REAL FAIL → next_action = REPAIR
           ├─ BLOCKED → EXIT with stop_reason = "BLOCKED"
           └─ INVALID → EXIT with stop_reason = "INVALID"
         
         - If entering new phase → EMIT phase boundary report
       
       IF next_action = "SCAFFOLD":
         - Scaffold case directory and contract files
         - Set next_action = IMPLEMENT
         - DO NOT EXIT - continue immediately
       
       IF next_action = "IMPLEMENT" or "REPAIR":
         - Run case-preflight
         - For app cases: invoke mandatory app preflight subagents/skills
         - If preflight fails → EXIT with stop_reason
         - Run case-work
         - Set next_action = RUN
         - DO NOT EXIT - continue immediately
       
       IF next_action = "RUN":
         - Run case-run
         - For app cases: invoke ios-app-driver, self-healing-runner
         - On crash: harvest artifacts, normalize signature
         - If retry policy allows and first crash: reset, retry once
         - If same signature repeats: classify REAL FAIL, NO retry
         - If run fails → Set next_action = REPAIR, continue
         - Set next_action = VERIFY
         - DO NOT EXIT - continue immediately
       
       IF next_action = "VERIFY":
         - Run case-verify
         - For app cases: invoke boot-milestone-auditor, crash-classifier
         - Produce evidence-backed classification
         - Set next_action = PROMOTE
         - DO NOT EXIT - continue immediately
       
       IF next_action = "PROMOTE":
         - Run case-promote
         - Update status.yaml (ONLY case-promote may mutate status)
         - Increment session_case_count
         - Update execution_log
         - If gate case and status = REAL FAIL → terminal_stop
         - If all gate cases in current phase = REAL PASS → terminal_stop (phase complete)
         - Set next_action = SELECT (marks case_just_promoted)
         - DO NOT EXIT - continue immediately to next case
    
    5. After each transition, update continuation invariants
    
    6. Loop back to step 1
```

## Continuation Invariants

Before each iteration, `phase-drive` MUST verify:

```yaml
continuation_invariants:
  can_continue: true       # Must be true to continue
  terminal_stop: false     # Must be false to continue
  budget_remaining: 4      # Must be > 0
  stop_reason: null        # Must be null
  auto_continue: true      # Must be true
  next_action: "IMPLEMENT" # Must be actionable (not "STOP")
```

**Invariant:** If `can_continue = true` and `terminal_stop = false`, the session MAY NOT end.

## Terminal States (When Loop MUST Exit)

The loop ONLY exits when ONE of these is true:

1. **Control plane failure** - `harness-doctor` fails
2. **Phase complete** - All gate cases in current phase are REAL PASS
3. **Gate-case REAL FAIL** - Active gate case classified as REAL FAIL
4. **Blocked dependency** - Next case is BLOCKED
5. **Invalid contract** - Next case is INVALID
6. **Retry exhausted** - Active case retry budget = 0
7. **Budget exhausted** - Session case budget reached
8. **Explicit user stop** - User requests stop

## Non-Terminal States (When Loop MUST Continue)

The loop MUST NOT exit when:

- A new case was just selected
- A case was just scaffolded
- Preflight just succeeded
- Work just completed
- Run just produced artifacts
- Verify just classified the case
- A case was just promoted (select next immediately)
- First crash occurred (retry allowed)

## Reporting Policy

**Human-facing reports (emitted only at):**
- Phase boundaries: "Phase 02b complete: 6/6 gate cases REAL PASS"
- Terminal stop: "Stopped: Gate case APPSIM-003 REAL FAIL"
- Invalid stop: "Stopped: Invalid control behavior detected"
- Gate-case REAL FAIL: Immediate report with crash signature and failing milestone

**Machine-readable state (updated after every transition):**
- `tests/cases/status.yaml`
- `tests/cases/active.yaml`

**Silent periods:**
- Individual case transitions (no per-step narrative)
- Scaffolding success
- Preflight success
- Successful promotion (unless phase boundary)

## Session State Machine

```yaml
session_state:
  current_stage: "IMPLEMENT"  # Where we are now
  next_action: "RUN"          # Where we're going next
  scope: "current-phase"      # Hardcoded default scope
  
  continuation_invariants:
    can_continue: true
    terminal_stop: false
    budget_remaining: 4
    stop_reason: null
  
  # These must be updated after every transition
  continued_last_transition: true  # Did we actually continue?
  last_transition: "PROMOTE → SELECT"
  
  # App-specific tracking
  app_session_state:
    current_app_stage: null
    retry_count: 0
    relaunch_count: 0
```

## Output Format

```yaml
phase_drive_result:
  session:
    session_id: "2026-03-27T00:00:00Z"
    started_at: "2026-03-27T00:00:00Z"
    cases_processed: 3
    budget_remaining: 3
    terminal_stop: false
    stop_reason: null
  
  phase_progress:
    phase_id: "02b-ios-simulator-harness"
    gate_cases_total: 6
    gate_cases_passed: 3
    gate_cases_failed: 0
    status: "IN_PROGRESS"
  
  execution_log:
    - case_id: "APPSIM-001"
      status: "REAL PASS"
      promotion_at: "2026-03-27T00:10:00Z"
      continued_to: "APPSIM-002"
    - case_id: "APPSIM-002"
      status: "REAL PASS"
      promotion_at: "2026-03-27T00:20:00Z"
      continued_to: "APPSIM-003"
    - case_id: "APPSIM-003"
      status: "REAL PASS"
      promotion_at: "2026-03-27T00:30:00Z"
      continued_to: "APPSIM-004"
  
  final_state:
    current_stage: "IMPLEMENT"
    active_case: "APPSIM-004"
    next_action: "RUN"
  
  continuation_invariants:
    can_continue: true
    terminal_stop: false
    budget_remaining: 3
    stop_reason: null
  
  # This exposes illegal stopping:
  invariants_violated: false  # If true, session is invalid
  invalid_stop_detected: false
  required_next_action: "RUN"  # What should happen next
```

## Illegal Stop Detection

If `phase-drive` exits while:
- `budget_remaining > 0`
- `auto_continue = true`
- `stop_reason` is null
- `terminal_stop = false`
- And a lawful next action exists

Then the session MUST be classified as **invalid control behavior**.

## Required Subagents

- `orchestrator` - overall coordination and dispatch
- `phase-gate` - phase progression validation
- `ios-app-driver` - XcodeBuildMCP operations (app cases)
- `boot-milestone-auditor` - milestone extraction (app cases)
- `crash-classifier` - crash signature normalization (app cases)
- `self-healing-runner` - bounded retry discipline (app cases)
- `simulator-artifact-verifier` - app artifact validation (app cases)
- `exec-entry-truth` - process entry validation (app cases, where relevant)

## Required Skills

- `active-case-lifecycle`
- `phase-gate-audit`
- `harness-health-audit`
- `continuation-invariant`
- `simulator-launch-audit` (app cases)
- `app-case-lifecycle` (app cases)
- `boot-log-milestone-audit` (app cases)
- `crash-signature-normalizer` (app cases)

## Constraints

- This is the ONLY lawful entrypoint for autonomous work
- MUST dispatch immediately after each transition
- MUST NOT stop at selection/scaffold boundaries
- MUST verify continuation invariants before each iteration
- MUST flag invalid stops in output
- MUST emit checkpoint report after each case promotion
- MUST NOT continue past gate-case REAL FAIL
- MUST scope autonomous execution to current-phase only by default
- MUST detect app cases and dispatch through mandatory subagents/skills
- MUST hardcode retry policy (one retry, same signature stops)

// turbo
