# case-new

Create a deterministic new case before implementation begins.

## Purpose

Use this command when a task is not already covered by an existing valid case.

## Required sequence

1. Run `the pre-case-create`
2. Determine:
   - case ID
   - phase
   - slug
   - harness kind
   - prerequisites
3. Invoke `case-substrate`
4. Invoke the relevant truth agents
5. Invoke `fixture-author` if fixtures are needed
6. Invoke `case-author`
7. Invoke `meson-wire`
8. Run `the post-case-create`
9. Run `the case-audit`

## Required outputs

- deterministic case folder
- `case.yaml`
- `expected.yaml`
- `authority.yaml`
- `fixtures/manifest.yaml` if applicable
- explicit Meson registration

## Refuse if

- the requested work belongs to an existing valid case
- the phase is ambiguous and unresolved
- the resulting case would violate `the 50-meson-only`
