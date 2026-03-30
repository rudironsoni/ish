# ish-drive

Autonomously continue lawful case work until an explicit stop condition is reached.

## Purpose

This is the ONLY lawful autonomous loop.

## MUST do

1. Run `ish-doctor`.
2. Determine active case and phase state.
3. Select or continue the active case.
4. Dispatch through `ish-case`.
5. Stop only on a lawful terminal condition.

## MUST stop when

- a gate case becomes `REAL FAIL`
- a case is `BLOCKED`
- a case is `INVALID`
- control-plane integrity fails
- budget is exhausted
- user explicitly stops

## MUST NOT stop when

- a case was just selected
- preparation just succeeded
- execution just produced artifacts
- verification just completed
- promotion just completed and another lawful case exists

## Required subagent
- `ish-conductor`

## Required skills
- `ish-control-plane-audit`
- `ish-case-lifecycle`

## Output

Report:
- active case
- current phase
- current stage
- stop reason or next action
