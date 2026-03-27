# case-run

Run exactly one active case through its explicit Meson entrypoint and collect deterministic evidence.

## Purpose

Use this command to run the active case after the contract is valid.

## Required sequence

1. Run `the pre-run`
2. Confirm:
   - active case ID
   - exact Meson test identity
   - artifact directory
   - verifier expectations
3. Run the explicit Meson case target
4. Collect emitted artifacts
5. Invoke `verifier`
6. Invoke `anti-slop`
7. If phase movement is implicated, invoke `phase-gate`

## Required outputs

- explicit run result
- artifact set
- exact case status
- mismatch report if not `REAL PASS`

## Refuse if

- the case contract is `INVALID`
- Meson identity is inconsistent
- the harness still contains known fake-success fallback behavior
