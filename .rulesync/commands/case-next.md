# case-next

Deterministically select the single lawful next case for implementation work.

## Purpose

This command is the ONLY lawful way to select an active case. It reads the machine-readable phase inventory and status ledger, applies the active-case algorithm, and returns exactly one case that is eligible for work.

## Behavior

1. Read `tests/cases/execution-order.yaml` to get:
   - Canonical phase order (00 through 11)
   - Complete gate inventory for each phase
   - Gating rules and prerequisites

2. Read `tests/cases/status.yaml` to get:
   - Current status of every gate case
   - Summary statistics
   - Earliest unsatisfied phase/case hints

3. Apply the active-case algorithm:
   - Find the earliest phase whose gate cases are NOT all `REAL PASS`
   - Within that phase, find the earliest gate case that is not `REAL PASS`
   - That case is the ACTIVE CASE

4. Verify the active case:
   - If status is STUB: ready for implementation
   - If status is REAL FAIL: ready for repair
   - If status is BLOCKED: move to blocking prerequisite
   - If status is INVALID: requires contract repair first

5. Return exactly one lawful next case

## Output Format

```yaml
active_case:
  case_id: "TRACE-001"
  phase: "00-trace-harness"
  status: "STUB"
  artifact_dir: "tests/cases/00-trace-harness/TRACE-001"
  selection_reason: "Earliest unsatisfied gate in earliest incomplete phase"
  prerequisites_satisfied: true
  substrate_exists: false
  contract_valid: false
```

## Fail-Closed Conditions

This command MUST refuse and exit with error if:

- `tests/cases/execution-order.yaml` is missing or malformed
- `tests/cases/status.yaml` is missing or malformed
- Phase inventory and status ledger have conflicting case IDs
- No unsatisfied gate cases exist (all 108 cases are REAL PASS)
- Multiple cases appear equally eligible (algorithm ambiguity)
- Required fields are missing from status entries

## Required Sequence

This command MUST be run before:
- Any file modifications
- Any implementation work
- Any repair work

## Constraints

- Returns exactly one case
- Never returns multiple cases
- Never returns a case from a later phase when earlier phases have unsatisfied gates
- Never returns a BLOCKED case without identifying the blocking prerequisite
- Does NOT modify any files
- Does NOT create scaffolding
- Does NOT run the case
