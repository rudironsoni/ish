# case-verify

Produce evidence-backed classification for the active case. Run verifier, anti-slop, and review-skeptic.

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

4. **Run review-skeptic:**
   - Challenge optimistic narratives
   - Verify evidence matches claims
   - Confirm no overclaiming

5. **Run phase-gate (if relevant):**
   - Check if phase can progress
   - Verify all gate cases in phase

6. **Produce classification:**
   - Determine exact status: REAL PASS | REAL FAIL | STUB | BLOCKED | INVALID
   - Generate mismatch summary
   - Produce evidence result

## Fail-Closed Conditions

This command MUST refuse and report ILLEGAL if:

- Case has not been run (`case-run` not executed)
- Artifacts are missing
- Verifier cannot determine status
- Implementation code tries to force status
- Anti-slop detects fake success

## Output Format

```yaml
verification_result:
  case_id: "TRACE-002"
  phase: "00-trace-harness"
  final_status: "REAL PASS"  # REAL PASS | REAL FAIL | STUB | BLOCKED | INVALID
  artifacts_produced:
    - "trace.ring"
    - "trace.json"
    - "report.json"
  verifier_result: "PASS"
  anti_slop_result: "PASS"
  review_skeptic_result: "PASS"
  phase_gate_result: "PASS"
  mismatch_summary: null  # or detailed mismatch report
  evidence_result:
    artifacts_match_expected: true
    harness_executed: true
    no_placeholder_behavior: true
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

## Required Skills

- `phase-gate-audit`
- `anti-slop-review`

## Constraints

- Does NOT perform implementation work
- Does NOT modify files
- Does NOT mark status directly
- Returns exact classification only
- Must pass before promotion allowed
