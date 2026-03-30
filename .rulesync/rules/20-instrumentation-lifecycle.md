# 20-instrumentation-lifecycle

This rule defines the canonical instrumentation lifecycle.

## Canonical framework

The framework is `ISHInstrumentation`.

### Objective-C façade
- `+bootstrap`
- `+activate`
- `+isActive`
- `+recordEvent:`
- `+recordEvent:attributes:`
- `+beginInterval:attributes:`
- `+endInterval:attributes:`

### C bridge
- `ish_instrumentation_bootstrap`
- `ish_instrumentation_activate`
- `ish_instrumentation_is_active`
- `ish_instrumentation_record_event`
- `ish_instrumentation_begin_interval`
- `ish_instrumentation_end_interval`

## Lifecycle stages

### Stage: bootstrap
- minimal
- app-owned
- startup-safe
- no heavy backend attach

### Stage: activate
- app-owned
- safe lifecycle boundary only
- sink fanout becomes active

### Stage: runtime
- lower layers emit semantic events only
- no runtime-owned policy

## Observability freeze

Once the active blocker becomes runtime/kernel:
- broad observability work MUST stop
- instrumentation architecture work MUST freeze
- only narrow semantic event additions needed to prove an exact failing edge MAY continue
