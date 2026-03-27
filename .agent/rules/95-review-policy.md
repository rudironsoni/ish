---
trigger: always_on
---
# 95-review-policy

Implementation is not self-certifying.

## Required reviews before a completion claim

Before any "done", "fixed", or `REAL PASS` claim:
- verifier must review
- anti-slop must review
- review-skeptic must review
- phase-gate must approve forward movement if phase progression is implicated

## Rule

The same implementation agent must not be the only authority approving its own work.

## Forbidden patterns

- implementation-only success claims
- bypassing skeptical review because the code "looks right"
- phase progression based on unverified implementation narrative
