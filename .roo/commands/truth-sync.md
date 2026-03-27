# truth-sync

Refresh or reconcile truth-chain files when case expectations are missing, stale, or inconsistent.

## Purpose

Use this command when `authority.yaml` or `expected.yaml` need to be aligned with current declared case scope.

## Required sequence

1. Identify the active case
2. Identify which truth domains apply:
   - ISA
   - decode
   - generator
   - ABI
   - ELF
3. Invoke the relevant truth agents
4. Update `authority.yaml`
5. Update `expected.yaml` through `case-author`
6. Re-run `case-audit command`

## Outputs

- synchronized truth chain
- reduced ambiguity in normative expectations

## Refuse if

- the case scope is undefined
- the requested truth update would hide unstable implementation details as normative without explicit declaration
