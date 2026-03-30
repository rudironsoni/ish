# 10-case-contract

Every case MUST declare an explicit execution contract.

## Required case fields

Each case contract MUST define:
- `case_id`
- `phase`
- `kind`
- `gate`
- `meson_target` or explicit execution target
- `expected_artifacts`
- `verifier_expectations`
- `app_shell_mode`
- `instrumentation_stage_required`

## App shell modes

Allowed values:
- `task_zero`
- `full_guest`

### `task_zero`
`task_zero` means:
- guest startup is disabled
- app shell MUST boot
- terminal UI MUST be reachable
- app MUST survive smoke interval
- Linux/emulator startup MUST NOT occur

### `full_guest`
`full_guest` means:
- guest startup is enabled
- runtime boundary reduction MUST be exact
- failure reports MUST include last known good point, first known bad point, and exact failing edge

## Instrumentation stage required

Allowed values:
- `bootstrap`
- `activate`
- `runtime`

A case MUST NOT require a later instrumentation stage than has actually been reached.

## Required execution truth

A case MUST always answer:
- what is enabled
- what is disabled
- what is under test
- what exact status is justified
