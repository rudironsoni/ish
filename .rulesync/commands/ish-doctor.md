# ish-doctor

Validate control-plane integrity before any work begins.

## Purpose

This is the FIRST REQUIRED command for non-trivial work.

## MUST verify

1. `.rulesync` structure is complete and internally consistent.
2. Active case files are present and coherent.
3. Required build and execution wiring exists.
4. App-case control-plane fields are valid.
5. `ISHInstrumentation` ownership assumptions are encoded correctly.
6. Task Zero and runtime reduction concepts are available in the control plane.
7. Only `XcodeBuildMCP` and `GitHub` are treated as control-plane MCPs.

## MUST fail closed if

- control-plane files are missing
- case inventory is inconsistent
- active case state is malformed
- stale old-trace assumptions are present
- instrumentation ownership is ambiguous
- Task Zero or runtime reduction support is missing

## Required subagent
- `ish-conductor`

## Required skill
- `ish-control-plane-audit`

## Output

Report:
- status
- exact failure reasons if any
- next lawful command
