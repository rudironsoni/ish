# case-promote

Update status ledger and close/advance active lock after successful verification.

## Purpose

This command owns the promotion of case status. It updates status.yaml with evidence-backed classification, clears or advances the active lock, and completes the case lifecycle. This is the ONLY command that may modify status.yaml.

## Behavior

1. **Read verify_result from case-verify**
   - Must have successful verification
   - Classification must be evidence-backed
   - All required verifiers must have passed
   - **For app cases:** instrumentation-verifier must have passed
   - **For app cases:** Task Zero verification must have passed (if task_zero mode)

2. **Validate promotion is lawful**
   - Verify case-verify was run
   - Confirm verifier result is PASS
   - Check anti_slop result is PASS
   - Validate review_skeptic result is PASS
   - **For app cases:** Check instrumentation_verifier result is PASS
   - **For app cases:** Check Task Zero verification passed (if task_zero mode)

3. **Update status.yaml**
   - Set current_status to classification from verify_result
   - Update observed_artifacts list
   - Set verifier_result, anti_slop_result, review_skeptic_result
   - **For app cases:** Set instrumentation_stage_reached
   - **For app cases:** Set task_zero_complete (if task_zero mode)
   - **For app cases:** Set runtime_boundary_status (if full_guest mode)
   - Update last_run_command and last_run_at
   - Calculate evidence_hash if applicable
   - Clear block_reason if promoted to REAL_PASS

4. **Update active.yaml**
   - Clear or advance active case lock
   - If clearing: set case_id to null
   - If advancing: set to next lawful case
   - Update command_history
   - Reset retry_budget for new case
   - **Update instrumentation stage tracking**
   - **Update Task Zero tracking**

5. **Produce promotion report**
   - Summary of status change
   - Phase progression unlocked (if applicable)
   - **Instrumentation stage unlocked (if app case)**
   - **Task Zero complete notification (if applicable)**
   - Next lawful case (if any)

## Output Format

```yaml
promote_result:
  case_id: "APPSIM-003"
  phase: "02b-ios-simulator-harness"
  
  promotion:
    previous_status: "STUB"
    new_status: "REAL PASS"
    promotion_lawful: true
    verification_passed: true
  
  status_updates:
    status_yaml:
      updated: true
      fields_changed:
        - "current_status: STUB -> REAL PASS"
        - "verifier_result: null -> PASS"
        - "anti_slop_result: null -> PASS"
        - "review_skeptic_result: null -> PASS"
        - "instrumentation_verifier_result: null -> PASS"
        - "observed_artifacts: [] -> [sim_launch.json, boot_milestones.json, report.json, instrumentation_events.json]"
        - "instrumentation_stage_reached: null -> activate"
        - "task_zero_complete: null -> true"
    
  active_lock:
    previous:
      case_id: "APPSIM-003"
      status: "STUB"
    current:
      case_id: "APPSIM-004"  # Advanced to next case, not cleared
      status: "STUB"
    advanced: true
  
  session_state:
    case_count: 3
    budget_remaining: 1
    auto_continue: true
    
    # App session tracking
    app_session_state:
      current_app_stage: "task_zero_complete"
      instrumentation_stage: "activate"
      task_zero_complete: true
      runtime_boundary_current: null
  
  continuation:
    next_case_required: true
    next_lawful_case: "APPSIM-004"
    next_lawful_action: "IMPLEMENT"
    reason: "Budget remaining, more cases in phase"
  
  phase_progression:
    phase_unlocked: false
    unlocked_phase: null
  
  # App-specific progression
  instrumentation_progression:
    stage_unlocked: true
    stage: "activate"
    next_stage: "runtime"
  
  task_zero:
    complete: true
    notification: "Task Zero complete: app shell stabilized, instrumentation activated"
  
  summary:
    total_real_pass: 3
    total_stub: 105
    earliest_unsatisfied_case: "APPSIM-004"
    phase_completion: "3/6 gates complete"
    task_zero_completion: "Task Zero gate satisfied"
```

**NOTE:** This output MUST NOT use terminal language like "complete" or "done". 
A promotion is an iteration checkpoint, not a session end. The `continuation` block signals whether more work is required.

## Lawful Promotion Conditions

Promotion is ONLY lawful when:
- `case-verify` completed successfully
- `verifier_result` is PASS
- `anti_slop_result` is PASS
- `review_skeptic_result` is PASS
- **For app cases:** `instrumentation_verifier_result` is PASS
- **For app cases:** Task Zero verification passed (if task_zero mode)

## Refusal Conditions

This command MUST refuse if:
- `case-verify` was not run
- Verification failed
- Any verifier returned FAIL
- Attempting to promote from implementation code alone
- **For app cases:** instrumentation verification failed
- **For app cases:** Task Zero incomplete (for task_zero cases)

## Valid Status Transitions

| From | To | Allowed |
|------|-----|---------|
| STUB | REAL_PASS | Yes (after verify) |
| STUB | REAL_FAIL | Yes (after verify) |
| REAL_FAIL | REAL_PASS | Yes (after repair + verify) |
| BLOCKED | STUB | Yes (blocker cleared) |
| INVALID | STUB | Yes (after repair) |

## Required Sequence

This command MUST be run:
- AFTER `case-verify` completes with PASS
- As the final stage of case lifecycle

## Constraints

- ONLY updates status.yaml and active.yaml
- ONLY runs after successful case-verify
- ONLY promotes evidence-backed status
- MUST clear or advance active lock
- Updates instrumentation stage tracking
- Updates Task Zero tracking
- Notifies on Task Zero completion
