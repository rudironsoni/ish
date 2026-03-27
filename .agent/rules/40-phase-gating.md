---
trigger: always_on
---
# 40-phase-gating

Bootstrap phases are ordered and gated.

## Source of truth

Phase order and gate cases live in:
- `tests/cases/execution-order.yaml`

## Rule

A later phase may proceed only when the earlier gate conditions are satisfied.

Only `REAL PASS` satisfies a gate case.

The following do not unlock later phases:
- `REAL FAIL`
- `STUB`
- `BLOCKED`
- `INVALID`

## Required behavior

- If a later-phase symptom appears first, reduce the problem to the earliest unsatisfied prerequisite case.
- If a prerequisite case does not exist, create it first.
- Do not use broad runtime smoke as justification for skipping missing earlier proof.

## Goal

Keep the bootstrap sequence honest and compositional.
