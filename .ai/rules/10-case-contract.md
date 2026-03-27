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
- `README.md`
- `notes.md`

## Required case fields

At minimum, the case contract MUST define:
- exact case ID
- exact phase
- exact harness name
- exact Meson test identity
- prerequisites
- success criteria
- required artifacts
- allowed patch scope
- trace defaults when tracing matters
- informational-only flag if applicable

## Consistency requirements

The following MUST agree:
- folder prefix
- `case.yaml:id`
- harness identity
- Meson test name
- deterministic artifact directory naming

If these drift, the case is `INVALID`.

## Forbidden patterns

- normative expectations only inside harness code
- placeholder truth in place of explicit fields
- artifact presence checks with no schema or semantic comparison
- implicit fixtures with no manifest or provenance
