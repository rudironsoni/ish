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
         - Update active.yaml with new case
         - Set next_action based on case state:
           ├─ STUB with no substrate → next_action = SCAFFOLD
           ├─ STUB with substrate → next_action = IMPLEMENT
           ├─ REAL FAIL → next_action = REPAIR
           └─ BLOCKED/INVALID → EXIT with stop_reason
       
       IF next_action = "SCAFFOLD":
         - Scaffold case directory and contract files
         - Set next_action = IMPLEMENT
         - DO NOT EXIT - continue immediately
       
       IF next_action = "IMPLEMENT" or "REPAIR":
         - Run case-preflight
         - If preflight fails → EXIT with stop_reason
         - Run case-work
         - Set next_action = RUN
         - DO NOT EXIT - continue immediately
       
       IF next_action = "RUN":
         - Run case-run
         - If run fails → Set next_action = REPAIR, continue
         - Set next_action = VERIFY
         - DO NOT EXIT - continue immediately
       
       IF next_action = "VERIFY":
         - Run case-verify
         - Produce evidence-backed classification
         - Set next_action = PROMOTE
         - DO NOT EXIT - continue immediately
       
       IF next_action = "PROMOTE":
         - Run case-promote
         - Update status.yaml
         - Increment session_case_count
         - Update execution_log
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

## Terminal States (When Loop MAY Exit)

The loop ONLY exits when ONE of these is true:

1. **Control plane failure** - `harness-doctor` fails
2. **Mission complete** - All 108 cases are REAL PASS
3. **Blocked dependency** - Next case is BLOCKED
4. **Invalid contract** - Next case is INVALID
5. **Retry exhausted** - Active case retry budget = 0
6. **Budget exhausted** - Session case budget reached
7. **Explicit user stop** - User requests stop
8. **Phase boundary** - Configured to stop at phase boundary

## Non-Terminal States (When Loop MUST Continue)

The loop MUST NOT exit when:

- A new case was just selected
- A case was just scaffolded
- Preflight just succeeded
- Work just completed
- Run just produced artifacts
- Verify just classified the case
- A case was just promoted (select next immediately)

## Session State Machine

```yaml
session_state:
  current_stage: "IMPLEMENT"  # Where we are now
  next_action: "RUN"          # Where we're going next
  
  continuation_invariants:
    can_continue: true
    terminal_stop: false
    budget_remaining: 4
    stop_reason: null
  
  # These must be updated after every transition
  continued_last_transition: true  # Did we actually continue?
  last_transition: "PROMOTE → SELECT"
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
  
  execution_log:
    - case_id: "TRACE-001"
      status: "REAL PASS"
      promotion_at: "2026-03-27T00:10:00Z"
      continued_to: "TRACE-002"
    - case_id: "TRACE-002"
      status: "REAL PASS"
      promotion_at: "2026-03-27T00:20:00Z"
      continued_to: "TRACE-003"
    - case_id: "TRACE-003"
      status: "REAL PASS"
      promotion_at: "2026-03-27T00:30:00Z"
      continued_to: "TRACE-004"
  
  final_state:
    current_stage: "IMPLEMENT"
    active_case: "TRACE-004"
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

## Required Skills

- `active-case-lifecycle`
- `phase-gate-audit`
- `harness-health-audit`
- `continuation-invariant`

## Constraints

- This is the ONLY lawful entrypoint for autonomous work
- MUST dispatch immediately after each transition
- MUST NOT stop at selection/scaffold boundaries
- MUST verify continuation invariants before each iteration
- MUST flag invalid stops in output
- MUST emit checkpoint report after each case promotion
