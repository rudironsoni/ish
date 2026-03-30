# 70-instrumentation-artifacts

Instrumentation artifacts are evidence, not decoration.

## Required instrumentation-contract fields

If a case depends on instrumentation evidence, the contract MUST define:
- Required instrumentation artifacts
- Artifact paths
- Artifact schema or decoding expectations
- Required event classes
- Required instrumentation stage (bootstrap, activate, runtime)
- Failure behavior when instrumentation initialization fails

## Requirements

- Fake or placeholder instrumentation files cannot satisfy an instrumentation requirement.
- An instrumentation artifact is only useful if it can be interpreted in the context of the case.
- Required instrumentation fields MUST appear in `case.yaml` and `expected.yaml` as appropriate.
- Instrumentation events MUST be emitted through the C bridge (`ish_instrumentation_*`), not direct backend calls.

## Forbidden patterns

- Passing after writing a stub instrumentation file because initialization failed
- Requiring instrumentation evidence but not defining what events or fields matter
- Presence-only instrumentation checks with no semantic use
- Product code owning instrumentation bootstrap or policy
- Direct os_log/NSLog calls in product code (MUST use C bridge)
- Constructor markers or startup proof files for instrumentation

## Instrumentation stages

Cases MUST specify which instrumentation stage is required:

### Stage 0: Bootstrap
- `ish_instrumentation_bootstrap()` has been called
- Minimal initialization complete
- Owned by `main.m`

### Stage 1: Activation
- `[ISHInstrumentation activate]` has been called
- All sinks configured and active
- Owned by `AppDelegate`

### Stage 2: Runtime
- Guest runtime emitting semantic events
- Events flow through C bridge to active sinks
- Lower layers emit only, do not own policy

## Event categories

Instrumentation events MUST be categorized:

- `lifecycle` - Instrumentation bootstrap, activation, teardown
- `app` - App-level events (launch, background, foreground)
- `guest` - Guest runtime events (init, exec, syscall)
- `kernel` - Kernel events (traps, faults, interrupts)
- `performance` - Performance markers (intervals, metrics)

## Ownership verification

For every instrumentation artifact, verify:
1. Events were emitted through C bridge API
2. App owns the instrumentation lifecycle
3. Lower layers do not own bootstrap or policy
4. No direct backend calls from product code
