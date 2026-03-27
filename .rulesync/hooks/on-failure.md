# on-failure

Run this when a case run does not achieve `REAL PASS`.

## Checklist

1. Classify exactly:
   - `REAL FAIL`
   - `STUB`
   - `BLOCKED`
   - `INVALID`
2. Identify the smallest failing unit.
3. Determine whether the problem is:
   - truth-chain issue
   - case-contract issue
   - Meson wiring issue
   - harness implementation issue
   - product-code issue
4. Decide whether a bounded repair loop is allowed.
5. Do not widen scope casually.
6. Do not pivot to a different case to avoid the failure.

## Output

A failure classification and next action.
