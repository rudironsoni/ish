---
trigger: always_on
---
# 80-patch-budget

Every repair loop must be bounded.

## Before implementation or repair, declare

- active case ID
- current case status
- smallest failing unit
- allowed patch scope
- maximum retry count
- stop conditions

## Required behavior

- Stay inside the declared patch scope unless the case contract itself is invalid and must be repaired first.
- Re-run the explicit Meson target after each bounded repair loop.
- Re-run verifier and anti-slop before claiming progress.

## Forbidden patterns

- scope creep across multiple active cases
- widening patch scope without explicit justification
- continuing indefinitely without retry or stop discipline
