# 71-instrumentation-lifecycle

Instrumentation lifecycle is app-owned and strictly sequenced.

## Required instrumentation framework

The control plane MUST recognize this canonical framework:

### Framework name

`ISHInstrumentation`

### Objective-C façade

```objc
// Bootstrap Stage 0 - called from main.m
+ (void)bootstrap;

// Activation Stage 1 - called from AppDelegate
+ (void)activate;
+ (BOOL)isActive;

// Event recording
+ (void)recordEvent:(NSString*)name;
+ (void)recordEvent:(NSString*)name attributes:(NSDictionary*)attrs;

// Interval tracking
+ (void)beginInterval:(NSString*)name attributes:(NSDictionary*)attrs;
+ (void)endInterval:(NSString*)name attributes:(NSDictionary*)attrs;
```

### C bridge

```c
// Stage 0 bootstrap
void ish_instrumentation_bootstrap(void);

// Stage 1 activation
void ish_instrumentation_activate(void);
bool ish_instrumentation_is_active(void);

// Event recording
void ish_instrumentation_record_event(const char* name);
void ish_instrumentation_record_event_with_attrs(const char* name, const char* attrs);

// Interval tracking
void ish_instrumentation_begin_interval(const char* name, const char* attrs);
void ish_instrumentation_end_interval(const char* name, const char* attrs);
```

## Lifecycle stages

### Stage 0: Bootstrap

- **Owner:** `main.m`
- **Action:** Minimal initialization only
- **Scope:** Prepare instrumentation without activating sinks
- **Entry:** Application launch
- **Exit:** Returns before AppDelegate initialization

### Stage 1: Activation

- **Owner:** `AppDelegate`
- **Action:** Full activation with sink configuration
- **Scope:** Enable all configured sinks (os_log, signposts, OpenTelemetry, MetricKit)
- **Entry:** AppDelegate initialization
- **Exit:** Instrumentation fully active and recording

### Stage 2: Runtime Reintroduction

- **Owner:** Guest runtime (kernel/emu/tcti)
- **Action:** Emit semantic events via C bridge
- **Scope:** One exact boundary at a time
- **Requirement:** Instrumentation MUST be active before runtime events

## Ownership model

### App owns

- Instrumentation lifecycle (bootstrap → activate)
- Backend selection and configuration
- Recovery and persistence policy
- Sink routing (os_log, signposts, OpenTelemetry, MetricKit)

### Lower layers (kernel/emu/tcti) MUST NOT own

- Instrumentation bootstrap policy
- Backend selection
- Recovery or persistence logic
- Sink configuration

### Lower layers MAY only

- Emit semantic events via C bridge
- Query active status (`ish_instrumentation_is_active`)

## Forbidden patterns

- Product code calling `ish_instrumentation_bootstrap()` (app-only)
- Product code configuring sinks
- Product code handling instrumentation recovery
- Constructor markers for instrumentation initialization
- Static initialization dependencies on instrumentation
- Direct NSLog/printf in product code for investigation

## Required case fields

App cases MUST declare:

```yaml
instrumentation:
  required_stage: "bootstrap|activate|runtime"  # Minimum stage required
  expected_events:
    - "app_launched"
    - "instrumentation_bootstrapped"
    - "instrumentation_activated"
  forbidden_patterns:
    - "direct_nslog_in_product_code"
    - "product_code_bootstrap_call"
```

## Verification requirements

For instrumentation-dependent cases:
1. Verify instrumentation reached required stage
2. Verify expected events were recorded
3. Verify no forbidden patterns present
4. Verify events recorded through C bridge, not direct backend calls
