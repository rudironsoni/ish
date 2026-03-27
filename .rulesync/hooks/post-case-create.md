# post-case-create

Run this immediately after creating a new case.

## Checklist

1. Confirm folder name matches case ID.
2. Confirm `case.yaml`, `expected.yaml`, and `authority.yaml` exist.
3. Confirm `fixtures/manifest.yaml` exists when needed.
4. Confirm harness identity is explicit.
5. Confirm Meson test identity is explicit.
6. Confirm Meson test identity matches the case contract.
7. Confirm no placeholder success claims exist.
8. Run `the case-audit`.

## Output

A structural validity report for the new case.
