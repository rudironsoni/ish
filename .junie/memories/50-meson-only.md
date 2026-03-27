# 50-meson-only

Case entrypoints must be explicit and discoverable from Meson.

## Required behavior

- Register each case explicitly in Meson.
- Register each harness target explicitly in Meson.
- Keep case identity aligned between case files and Meson entries.

## Forbidden patterns

- dynamic case discovery
- hidden runtime dispatchers
- Python runtime dispatch used to replace explicit case registration
- shell-script public workflows used in place of explicit Meson test entries
- untracked case aliases

## Outcome

Another agent must be able to locate the exact case entrypoint from Meson files without guessing.
