# phase-audit

Audit whether the current repository state permits forward movement in phase order.

## Purpose

Use this command before moving into a later phase or when a later-phase symptom appears.

## Required sequence

1. Read `tests/cases/execution-order.yaml`
2. Collect current statuses for prerequisite and gate cases
3. Identify the earliest unsatisfied gate
4. Invoke `phase-gate`

## Outputs

- allowed or blocked decision
- earliest unsatisfied case
- exact reason progression is denied if blocked

## Refuse if

- current case statuses are not known
- the phase inventory is malformed
