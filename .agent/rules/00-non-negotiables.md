---
trigger: always_on
---
# 00-non-negotiables

These are repository-wide non-negotiable rules.

## Mandatory rules

1. No stub may count as pass.
2. No dynamic case discovery.
3. No hidden runtime dispatcher.
4. No direct edits to generated outputs.
5. No phase skipping.
6. No success-on-fallback behavior.
7. No case without explicit Meson wiring.
8. No vague completion claim without verifier and anti-slop review.
9. No later-phase progression while earlier gate cases are not `REAL PASS`.
10. No broad runtime debugging when a smaller failing case can be authored or repaired first.

## Required status vocabulary

Use only:
- `REAL PASS`
- `REAL FAIL`
- `STUB`
- `BLOCKED`
- `INVALID`

Do not invent softer synonyms.

## Repository posture

- Prefer smaller units.
- Prefer explicit contracts.
- Prefer deterministic fixtures.
- Prefer repo-local truth.
- Prefer real execution paths over simulated proof.
