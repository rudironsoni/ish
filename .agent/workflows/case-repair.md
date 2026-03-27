---
trigger: /case-repair
turbo: true
---
# Workflow: /case-repair

# case-repair

Repair one failing active case within a declared patch budget.

## Purpose

Use this command when the active case is not `REAL PASS` and the next action is a bounded repair loop.

## Required sequence

1. Run `the on-failure`
2. Classify the current status:
   - `REAL FAIL`
   - `STUB`
   - `BLOCKED`
   - `INVALID`
3. Reduce to the smallest failing unit
4. Declare:
   - active case
   - allowed patch scope
   - retry budget
   - stop conditions
5. Invoke the necessary specialist agent
6. Re-run `the case-run`
7. Re-run verifier and anti-slop

## Required outputs

- bounded repair plan
- updated evidence
- updated exact status

## Refuse if

- multiple active cases are being mixed
- the case contract is invalid and needs contract repair before implementation
- the repair would illegally widen into a different phase without gate approval

// turbo
