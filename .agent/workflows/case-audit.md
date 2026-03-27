---
trigger: /case-audit
turbo: true
---
# Workflow: /case-audit

# case-audit

Audit a case for deterministic contract integrity.

## Purpose

Use this command to validate that a case is structurally ready for implementation or verification.

## Audit checklist

Check all of the following:
- folder name and case ID match
- `case.yaml`, `expected.yaml`, and `authority.yaml` exist
- `fixtures/manifest.yaml` exists when needed
- harness name is explicit
- Meson test name is explicit
- Meson test name matches the case contract
- artifact directory naming is deterministic
- prerequisites are explicit
- allowed patch scope is explicit
- trace defaults are explicit for trace-dependent cases
- no normative truth lives only inside harness code

## Outputs

- PASS or FAIL audit result
- exact inconsistency list
- `INVALID` recommendation if the contract is malformed

## Refuse if

- the case path does not resolve
- the case files are so incomplete that the audit cannot begin

// turbo
