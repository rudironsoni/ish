# 10-case-contract

A valid case is a deterministic contract.

## Required files

Every non-trivial case MUST include:
- `case.yaml`
- `expected.yaml`
- `authority.yaml`

Every fixture-based case MUST also include:
- `fixtures/manifest.yaml`

Optional files:
- `README`
- `notes`

## Required case fields

At minimum, the case contract MUST define:
- Exact case ID
- Exact phase
- Exact harness name
- Exact Meson test identity
- Prerequisites
- Success criteria
- Required artifacts
- Allowed patch scope
- Instrumentation requirements when instrumentation matters
- Informational-only flag if applicable

## App case additional fields

App cases (APPSIM-*, APP-*) MUST also define:
- `app_shell_mode`: `task_zero` or `full_guest`
- `instrumentation_stage_required`: `bootstrap`, `activate`, or `runtime`
- `instrumentation_events_expected`: List of expected event names
- `guest_startup`: `enabled` or `disabled`

### Task Zero mode

When `app_shell_mode: task_zero`:
- Guest execution is disabled
- App shell must stabilize
- Terminal UI must be reachable
- Instrumentation must be active
- No guest runtime events expected

### Full guest mode

When `app_shell_mode: full_guest`:
- Guest execution is enabled
- Runtime boundary reintroduction proceeds one boundary at a time
- Each boundary must report:
  - Last known good point
  - First known bad point
  - Exact failing edge

## Consistency requirements

The following MUST agree:
- Folder prefix
- `case.yaml:id`
- Harness identity
- Meson test name
- Deterministic artifact directory naming

If these drift, the case is `INVALID`.

## Forbidden patterns

- Normative expectations only inside harness code
- Placeholder truth in place of explicit fields
- Artifact presence checks with no schema or semantic comparison
- Implicit fixtures with no manifest or provenance
- Product code owning instrumentation policy
- Constructor markers for instrumentation bootstrap
- Startup proof files for trace initialization
