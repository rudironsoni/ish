Please also reference the following rules as needed. The list below is provided in TOON format, and `@` stands for the project root directory.

rules[5]{path}:
  @.agents/memories/10-case-contract.md
  @.agents/memories/20-instrumentation-lifecycle.md
  @.agents/memories/30-runtime-reduction.md
  @.agents/memories/40-tracing-logging.md
  @.agents/memories/50-simulator-constraint.md

# Additional Conventions Beyond the Built-in Functions

As this project's AI coding tool, you must follow the additional conventions below, in addition to the built-in functions.

# 00-non-negotiables

These are repository-wide non-negotiable rules.

## Mandatory rules

1. No stub may count as pass.
2. No dynamic case discovery.
3. No hidden runtime dispatcher.
4. No direct edits to generated outputs.
5. No phase skipping.
6. No success-on-fallback behavior.
7. No case without explicit build wiring.
8. No vague completion claim without evidence review.
9. No later-phase progression while earlier gate cases are not `REAL PASS`.
10. No broad debugging while a smaller failing boundary can be reduced first.
11. No app bootstrap ownership outside app-owned `ISHInstrumentation`.
12. No direct investigation logging in product code.
13. No guest-runtime reintroduction before Task Zero is `REAL PASS`.
14. No broad observability work once runtime/kernel is the active blocker.

## Required status vocabulary

Use only:
- `REAL PASS`
- `REAL FAIL`
- `STUB`
- `BLOCKED`
- `INVALID`

## Repository posture

- Prefer smaller units.
- Prefer explicit contracts.
- Prefer deterministic fixtures.
- Prefer repo-local truth.
- Prefer exact failing edges over broad narratives.

## Instrumentation ownership

The canonical instrumentation system is `ISHInstrumentation`.

### Ownership model

- `main.m` MUST own minimal instrumentation bootstrap only.
- `AppDelegate` MUST own instrumentation activation.
- Lower layers MUST emit semantic events only.
- Lower layers MUST NOT own bootstrap, backend selection, recovery, persistence, or export policy.

### Product code isolation

- `kernel/*`, `app/*`, `emu/*`, and `tcti/*` MUST emit semantic instrumentation events only.
- Product code MUST NOT use direct `printk`, `NSLog`, `os_log`, or ad hoc logging for investigation.
- Product code MUST NOT own instrumentation bootstrap policy.
- Product code MUST NOT use constructor markers, proof files, startup path probes, or recovery files for investigation.

### Allowed producer path

Product code
→ semantic instrumentation API
→ app-owned instrumentation bridge
→ sink fanout
→ Apple logging / signposts / OpenTelemetry / MetricKit

## Control-plane MCPs

The control plane MUST treat only these as first-class MCPs:
- `XcodeBuildMCP`
- `GitHub`
