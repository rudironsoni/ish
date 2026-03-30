# 30-runtime-reduction

Runtime reduction MUST be exact.

## Required reporting format

A runtime reduction report MUST include:
- current enabled runtime scope
- last known good point
- first known bad point
- exact failing edge
- strongest exact root-cause site so far

## Required discipline

- Reintroduce one runtime boundary at a time.
- Do not batch multiple deep runtime restorations.
- Do not report broad "kernel issue" narratives.
- Do not widen instrumentation work when exact edge proof is still missing.

## Exact runtime truth

Every runtime investigation MUST state:
- what boundary is currently enabled
- what remains disabled
- what exact edge is under test
- whether the bug is narrow enough for one targeted fix
