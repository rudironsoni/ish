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

## Instrumentation ownership model

**CRITICAL:** Product code MUST emit semantic instrumentation events only. The app-owned `ISHInstrumentation` framework owns lifecycle and sinks.

### Architecture

```
Product code (kernel/*, app/*, emu/*, tcti/*)
    ↓
Emit semantic events via C bridge API (ish_instrumentation_*)
    ↓
ISHInstrumentation framework (app-owned)
    ↓
Route to configured sinks:
    - Apple local logging / os_log
    - Signposts
    - OpenTelemetry Swift spans/export
    - MetricKit subscriber integration
```

### Forbidden patterns

- Product code MUST NOT own instrumentation bootstrap policy
- Product code MUST NOT own backend selection
- Product code MUST NOT own recovery or persistence logic
- NO direct printk/ISH_LOG/NSLog/os_log for investigation in product code
- NO backend-specific formatting in product code
- NO mixed observability (product code + direct logging)
- NO constructor markers for instrumentation bootstrap
- NO startup proof files for trace initialization
- NO path probes for ring recovery
- NO trace bootstrap experiments in product code

### Required producer path

Product code emits semantic events through the C bridge:

```c
// C bridge functions (lower layers use these)
ish_instrumentation_record_event(const char* name);
ish_instrumentation_record_event_with_attrs(const char* name, const char* attrs);
ish_instrumentation_begin_interval(const char* name, const char* attrs);
ish_instrumentation_end_interval(const char* name, const char* attrs);
```

The C bridge routes to the Objective-C façade:

```objc
// Objective-C façade (app-owned, AppDelegate controls)
[ISHInstrumentation recordEvent:@"event_name"];
[ISHInstrumentation recordEvent:@"event_name" attributes:@{@"key": @"value"}];
[ISHInstrumentation beginInterval:@"interval_name" attributes:@{@"key": @"value"}];
[ISHInstrumentation endInterval:@"interval_name" attributes:@{@"key": @"value"}];
```

### Lifecycle ownership

- `main.m` owns minimal bootstrap only (`ish_instrumentation_bootstrap()`)
- `AppDelegate` owns activation (`[ISHInstrumentation activate]`)
- Lower layers emit semantic events only
- Lower layers MUST NOT own instrumentation policy

### Violation cleanup required

Any investigation-specific direct logging added to product code MUST be:
1. Replaced with semantic instrumentation event emissions via C bridge
2. Removed from product code entirely

Backend-specific output (os_log, NSLog, OpenTelemetry) MUST live ONLY in the app-owned `ISHInstrumentation` sinks.

## App shell stabilization (Task Zero)

**Task Zero** is a first-class control-plane state representing:
- Guest startup disabled
- App shell stabilized
- Terminal UI reachable without guest execution
- Runtime reintroduction blocked until shell is stable

### Task Zero gate requirements

Before any guest runtime reintroduction:
1. App MUST bootstrap instrumentation (`ish_instrumentation_bootstrap`)
2. AppDelegate MUST activate instrumentation (`[ISHInstrumentation activate]`)
3. App shell MUST be stable (no crashes in app code)
4. Terminal UI MUST be reachable
5. Instrumentation MUST be recording events

### Runtime reintroduction discipline

Linux/emulator reintroduction happens one exact boundary at a time:
- Each boundary MUST have a defined case
- Each case MUST report:
  - Last known good point
  - First known bad point
  - Exact failing edge
- NO broad "kernel issue" narratives allowed
- Runtime reductions MUST be precise

## Observability freeze rule

Once app-shell stabilization and observability migration are complete:
- When the active blocker becomes runtime/kernel
- Broad observability work MUST freeze
- Only narrow semantic event additions required for proof MAY continue

The control plane MUST:
1. Detect when the blocker shifts to runtime/kernel
2. Freeze broad instrumentation changes
3. Require exact boundary reduction for runtime issues
4. Reject vague "add more logging" requests once freeze is active
