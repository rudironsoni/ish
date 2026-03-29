---
root: true
targets: ["*"]
description: "Repository-wide non-negotiable rules for agent-first harness engineering"
globs: ["**/*"]
---

# 00-non-negotiables

These are repository-wide non-negotiable rules.

## Mandatory rules

1. No stub may count as pass.
2. No dynamic case discovery.
3. No hidden runtime dispatcher.
4. No direct edits to generated outputs.
5. No phase skipping.
6. No success-on-fallback behavior.
7. No case without explicit Meson wiring.
8. No vague completion claim without verifier and anti-slop review.
9. No later-phase progression while earlier gate cases are not `REAL PASS`.
10. No broad runtime debugging when a smaller failing case can be authored or repaired first.

## Required status vocabulary

Use only:
- `REAL PASS`
- `REAL FAIL`
- `STUB`
- `BLOCKED`
- `INVALID`

Do not invent softer synonyms.

## Repository posture

- Prefer smaller units.
- Prefer explicit contracts.
- Prefer deterministic fixtures.
- Prefer repo-local truth.
- Prefer real execution paths over simulated proof.

## Trace system exclusivity for investigation

**CRITICAL:** Investigation instrumentation MUST use ONLY the trace system.

### Product code isolation

- kernel/*, app/*, emu/*, tcti/* MUST emit semantic trace events only
- NO direct printk/ISH_LOG/NSLog/os_log for investigation
- NO backend-specific formatting in product code
- NO mixed observability (trace + direct logging)

### Allowed trace producer path

```
Product code (kernel/app/emu/tcti)
    ↓
Emit trace events via trace API
    ↓
Trace system (trace_events.def, trace.c, trace.h)
    ↓
Backend-specific rendering (trace_backends.c)
    ↓
Output (printk/os_log/NSLog/ring/dump/JSON/etc)
```

### Violation cleanup required

Any investigation-specific direct logging added to product code MUST be:
1. Replaced with trace event definitions
2. Replaced with trace event emissions
3. Removed from product code entirely

Backend-specific output (printk, os_log, NSLog) MUST live ONLY in trace backends.
