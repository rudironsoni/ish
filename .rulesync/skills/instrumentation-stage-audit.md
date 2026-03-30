---
name: instrumentation_stage_audit
description: Audit instrumentation lifecycle stages and enforce app-owned instrumentation architecture
---

You are the instrumentation stage audit skill. Apply this procedure to verify instrumentation lifecycle compliance.

## Purpose

Ensure instrumentation follows the app-owned architecture with strict stage sequencing.

## Architecture Overview

### Framework
- `ISHInstrumentation` - App-owned instrumentation framework

### Stage 0: Bootstrap
- **Owner:** main.m
- **API:** `ish_instrumentation_bootstrap()` (C) / `[ISHInstrumentation bootstrap]` (ObjC)
- **Purpose:** Minimal initialization
- **Events:** instrumentation_bootstrap_started, instrumentation_bootstrap_complete

### Stage 1: Activation
- **Owner:** AppDelegate
- **API:** `ish_instrumentation_activate()` (C) / `[ISHInstrumentation activate]` (ObjC)
- **Purpose:** Full activation with sinks
- **Events:** instrumentation_activate_started, instrumentation_activate_complete
- **Sinks:** os_log, signposts, OpenTelemetry, MetricKit

### Stage 2: Runtime
- **Owner:** kernel/emu/tcti (emit only)
- **API:** `ish_instrumentation_record_event()` (C)
- **Purpose:** Guest runtime event emission
- **Events:** guest_init, syscall_enter, syscall_exit, etc.

## Audit Procedure

### 1. Verify Stage 0 (Bootstrap)

**Required:**
- `ish_instrumentation_bootstrap()` called exactly once
- Called from main.m only
- Event recorded: instrumentation_bootstrap_complete
- C bridge initialized

**Forbidden:**
- Called from product code (kernel/emu/tcti)
- Called multiple times
- Constructor initialization
- Static initialization

### 2. Verify Stage 1 (Activation)

**Required:**
- `[ISHInstrumentation activate]` called exactly once
- Called from AppDelegate only
- Event recorded: instrumentation_activate_complete
- All sinks configured
- Returns YES/success

**Forbidden:**
- Called from product code
- Called before bootstrap
- Called multiple times
- Partial activation

### 3. Verify Stage 2 (Runtime)

**Required:**
- Lower layers emit via C bridge only
- Events recorded through `ish_instrumentation_record_event()`
- Events have semantic meaning
- Events route to configured sinks

**Forbidden:**
- Direct os_log/NSLog in product code
- Direct printf in product code
- Backend-specific formatting in product code
- Product code configuring sinks

### 4. Verify Ownership

**App MUST own:**
- Bootstrap policy
- Activation policy
- Backend selection
- Recovery logic
- Persistence logic

**Lower layers MUST NOT own:**
- Bootstrap
- Activation
- Backend configuration
- Recovery
- Persistence

**Lower layers MAY only:**
- Emit semantic events via C bridge
- Query active status

### 5. Verify Event Flow

**Correct flow:**
```
Product code
    ↓
ish_instrumentation_record_event()
    ↓
C bridge
    ↓
ISHInstrumentation (app-owned)
    ↓
Sinks (os_log, signposts, OpenTelemetry, MetricKit)
```

**Incorrect flow (forbidden):**
```
Product code
    ↓
os_log/NSLog (direct - forbidden)
```

## Audit Checklist

For each app case:

- [ ] Instrumentation stage required is declared
- [ ] Expected events are declared
- [ ] Stage 0 complete before Stage 1 (if Stage 1 required)
- [ ] Stage 1 complete before Stage 2 (if Stage 2 required)
- [ ] App owns lifecycle (main.m bootstrap, AppDelegate activation)
- [ ] Lower layers emit via C bridge only
- [ ] No direct os_log/NSLog in product code
- [ ] No constructor markers
- [ ] No startup proof files
- [ ] Events recorded match expected list

## Forbidden Patterns

- Product code calling `ish_instrumentation_bootstrap()`
- Product code calling `[ISHInstrumentation activate]`
- Product code configuring sinks
- Direct os_log/NSLog in product code for investigation
- Constructor initialization of instrumentation
- Static initialization dependencies
- Startup proof files
- Ring recovery mechanisms
- Trace bootstrap markers

## Reporting

Report violations:
```yaml
instrumentation_audit_result:
  case_id: "APP-003"
  stage_required: "activate"
  stage_reached: "bootstrap"
  violations:
    - type: "product_code_bootstrap"
      description: "ish_instrumentation_bootstrap called from kernel/"
      severity: "CRITICAL"
    - type: "direct_nslog"
      description: "Direct NSLog found in emu/"
      severity: "CRITICAL"
  compliant: false
  blocked: true
  required_action: "Remove product code instrumentation calls"
```

## Success Criteria

- App owns instrumentation lifecycle
- Lower layers emit semantic events only
- No direct backend calls from product code
- Stage sequencing correct
- Events recorded as expected
- No forbidden patterns present
