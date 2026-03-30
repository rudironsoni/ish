# ish-case

Work exactly one active case through one explicit stage.

## Subcommands

### `ish-case select`
Select the next lawful active case.

### `ish-case prepare`
Validate the selected case contract, tooling readiness, shell mode, and instrumentation stage.

### `ish-case implement`
Perform the minimum implementation work required for the current case stage.

### `ish-case execute`
Run the active case through its explicit execution target and collect artifacts.

### `ish-case verify`
Produce evidence-backed classification.

### `ish-case promote`
Promote a verified result into the control-plane state.

## App-case rules

For app cases, `ish-case` MUST respect:
- `task_zero` versus `full_guest`
- instrumentation lifecycle stage
- observability freeze if runtime/kernel is the active blocker

## Required subagents by need
- `ish-conductor`
- `ish-app-driver`
- `ish-runtime-analyst`
- `ish-evidence-reviewer`
- `ish-case-author`

## Required skills by need
- `ish-case-lifecycle`
- `ish-app-runtime-audit`
- `ish-instrumentation-lifecycle`
- `ish-runtime-reduction`
- `ish-build-contract`

## Output

Each subcommand MUST report:
- active case
- exact stage
- status
- next lawful action
