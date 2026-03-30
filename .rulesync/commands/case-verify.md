# case-verify

Produce evidence-backed classification for the active case. Run verifier, anti-slop, review-skeptic, and instrumentation-verifier.

## Purpose

Produce exact evidence-backed classification. This is the ONLY lawful way to determine case status.

## Required Pre-conditions

1. `harness-doctor` passed
2. `case-next` selected active case
3. `case-preflight` passed
4. `case-work` completed
5. `case-run` executed and produced artifacts

## Behavior

1. **Collect artifacts:**
   - Read `observed_artifacts` from artifact directory
   - Compare with `expected_artifacts` from expected.yaml
   - Generate mismatch report if differences

2. **Run verifier:**
   - Compare actual vs expected output
   - Determine match/mismatch
   - Produce exact classification

3. **Run anti-slop:**
   - Detect fake success patterns
   - Check for placeholder behavior
   - Verify no success-on-fallback
   - Verify instrumentation ownership
   - Verify Task Zero compliance

4. **Run review-skeptic:**
   - Challenge optimistic narratives
   - Verify evidence matches claims
   - Confirm no overclaiming

5. **Run instrumentation-verifier (for app cases):**
   - Verify instrumentation stage reached
   - Verify events recorded via C bridge
   - Verify app owns lifecycle
   - Verify no direct os_log/NSLog in product code
   - Verify Task Zero complete (if task_zero mode)

6. **Run boot-milestone-auditor (for app cases):**
   - Extract boot milestones
   - Verify milestone ordering
   - Verify Task Zero milestones (if task_zero mode)
   - Verify runtime boundaries (if full_guest mode)

7. **Run crash-classifier (for app cases if crash):**
   - Normalize crash signature
   - Identify last completed milestone
   - Identify first failing milestone
   - Report exact failing edge

8. **Run phase-gate (if relevant):**
   - Check if phase can progress
   - Verify all gate cases in phase

9. **Produce classification:**
   - Determine exact status: REAL PASS | REAL FAIL | STUB | BLOCKED | INVALID
   - Generate mismatch summary
   - Produce evidence result
   - Report instrumentation verification (app cases)
   - Report Task Zero status (app cases)
   - Report runtime boundary status (app cases)

## Fail-Closed Conditions

This command MUST refuse and report ILLEGAL if:

- Case has not been run (`case-run` not executed)
- Artifacts are missing
- Verifier cannot determine status
- Implementation code tries to force status
- Anti-slop detects fake success
- **Instrumentation events not via C bridge (app cases)**
- **Task Zero not complete but full_guest case attempted (app cases)**
- **Instrumentation stage not reached (app cases)**
- **Product code owns instrumentation (app cases)**

## Output Format

```yaml
verification_result:
  case_id: "APPSIM-003"
  phase: "02b-ios-simulator-harness"
  final_status: "REAL PASS"  # REAL PASS | REAL FAIL | STUB | BLOCKED | INVALID
  
  artifacts_produced:
    - "sim_launch.json"
    - "boot_milestones.json"
    - "report.json"
    - "instrumentation_events.json"
  
  # App case specific
  app_shell_mode: "task_zero"  # or "full_guest"
  instrumentation_stage_reached: "activate"
  task_zero_complete: true
  
  instrumentation_verification:
    stage_reached: "activate"
    events_verified: true
    ownership_correct: true
    no_direct_nslog: true
    compliant: true
  
  runtime_boundary:
    boundary_id: "guest_init_first_syscall"
    last_known_good: "guest_init_entered"
    first_known_bad: null
    exact_failing_edge: null
    compliant: true
  
  verifier_result: "PASS"
  anti_slop_result: "PASS"
  review_skeptic_result: "PASS"
  instrumentation_verifier_result: "PASS"
  phase_gate_result: "PASS"
  
  mismatch_summary: null  # or detailed mismatch report
  evidence_result:
    artifacts_match_expected: true
    harness_executed: true
    no_placeholder_behavior: true
    instrumentation_compliant: true
    task_zero_complete: true
  
  recommended_next_action: "PROMOTE"  # PROMOTE | REPAIR | BLOCKED
  retry_budget_recommendation: "CONTINUE"  # CONTINUE | EXHAUSTED
```

## Required Sequence

This command runs AFTER:
- `harness-doctor`
- `case-next`
- `case-preflight`
- `case-work`
- `case-run`

This command runs BEFORE:
- `case-promote`

## Required Subagents

- `verifier` — for evidence comparison
- `anti-slop` — for fake success detection
- `review-skeptic` — for adversarial review
- `instrumentation-verifier` — for app cases
- `boot-milestone-auditor` — for app cases
- `crash-classifier` — for app cases (if crash)

## Required Skills

- `phase-gate-audit`
- `anti-slop-review`
- `instrumentation-stage-audit` — for app cases
- `runtime-boundary-reduction` — for app cases with guest execution

## Constraints

- Does NOT perform implementation work
- Does NOT modify files
- Does NOT mark status directly
- Returns exact classification only
- Verifies instrumentation for app cases
- Verifies Task Zero for app cases
- Verifies runtime boundaries for full_guest cases
- Must pass before promotion allowed
