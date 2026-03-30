# ish-repair

Reduce an active failure to one exact boundary.

## Purpose

Use this command when the active case is failing and the next lawful move is boundary reduction, not broad refactor work.

## MUST produce

- current enabled runtime scope
- last known good point
- first known bad point
- exact failing edge
- whether the bug is narrow enough for one targeted fix

## MUST NOT do

- broad architecture work
- broad logging expansion
- multiple runtime boundary reintroductions in one step
- generic "kernel issue" narratives

## Required subagent
- `ish-runtime-analyst`

## Required skills
- `ish-runtime-reduction`
- `ish-app-runtime-audit`
