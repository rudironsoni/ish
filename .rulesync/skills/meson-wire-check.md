---
name: meson_wire_check
description: Verify explicit Meson registration for case harnesses and tests without dynamic discovery.
---

You are the Meson wire check skill. Apply this procedure to verify build system registration.

## Purpose

Ensure every case is explicitly registered in the build system with consistent identity.

## Core Principle

All cases must be wired explicitly in Meson. No dynamic discovery. No hidden dispatchers.

## Prerequisites

- Case contract exists with declared Meson identity
- Build system files are available

## Verification Checklist

### 1. Explicit Test Registration

Verify:
- Each case has an explicit `test(...)` entry in the build system
- The test name matches the case contract
- The test invokes the correct harness binary
- The test passes correct arguments:
  - `--case-yaml` with correct path
  - `--artifact-dir` with deterministic path

### 2. Harness Target Registration

Verify:
- Each harness has an explicit executable target
- The harness target name matches the case contract
- The harness is linked with required libraries
- The harness includes correct dependencies

### 3. Identity Consistency

Verify these match:
- Folder name prefix
- case.yaml id field
- Harness name in case.yaml
- Meson test name
- Artifact directory naming (derived from case ID)

If any mismatch, flag as **identity-drift**.

### 4. No Dynamic Discovery

Check for and reject:
- Glob patterns finding test files dynamically
- Find commands discovering cases at build time
- Generated test lists from directory scanning
- Hidden dispatcher executables that route to actual tests

### 5. No Hidden Dispatchers

Check for:
- Python scripts that dynamically dispatch to harnesses
- Shell scripts used as public workflow instead of explicit Meson tests
- Indirection layers between Meson and the actual harness binary

If found, flag as **hidden-dispatcher pattern**.

### 6. Case on Disk But Not Wired

Check:
- Cases existing in tests/cases/ but not explicitly wired
- Orphaned case folders with no Meson registration

If found, flag as **unwired-case**.

### 7. Test Points at Wrong Path

Check:
- Meson test entry points at different case than its name suggests
- Test arguments reference wrong case.yaml path

If found, flag as **miswired-test**.

## Forbidden Patterns

- Dynamic test discovery using glob or find
- Python runtime dispatcher replacing explicit registration
- Shell-script public workflows instead of explicit Meson tests
- Silent case aliases or name mismatches
- Harness binary called through indirection layers

## Response to Issues

When issues are found:

1. **Document** the exact mismatch or violation
2. **Classify** case as INVALID if identity is inconsistent
3. **Require** explicit Meson wiring correction
4. **Report** to meson-wire agent for fix

## Critical Violations

These require immediate correction:
- Dynamic discovery mechanisms
- Hidden runtime dispatchers
- Identity mismatches between contract and build system

## Success Criteria

- Every case is explicitly registered one by one
- Case identity is consistent across all locations
- Another agent can find the exact case entrypoint from Meson files without guessing
- No dynamic discovery or hidden dispatchers exist
