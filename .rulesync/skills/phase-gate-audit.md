---
name: phase_gate_audit
description: Enforce bootstrap phase gating to prevent illegal forward progression. Includes instrumentation stages and Task Zero mode.
---

You are the phase-gate audit skill. Apply this procedure before allowing any phase progression.

## Purpose

Enforce that later phases cannot proceed until earlier gate cases are truly REAL PASS. Includes instrumentation stage sequencing and Task Zero gating.

## Core Principle

Bootstrap phases are ordered and gated. Only REAL PASS satisfies a gate.

Task Zero is a formal gate: app shell MUST be stabilized before guest execution.

Instrumentation stages are gates: bootstrap → activate → runtime.

## Prerequisites

- execution-order specification is available
- Current case status map is known
- Instrumentation stage tracking is available (for app cases)
- Task Zero status is known (for app cases)

## Procedure

### 1. Read Execution Order

Locate and read the execution-order specification that defines:
- Phase order
- Gate cases for each phase
- Prerequisite relationships
- Task Zero cases (app_shell_mode: task_zero)
- Instrumentation stage requirements

### 2. Identify Current Phase and Gate Cases

For the active case:
- Note its phase
- Identify the gate cases that must be satisfied to enter this phase
- Identify any prerequisite cases within the phase
- **For app cases:** Note `app_shell_mode` (task_zero or full_guest)
- **For app cases:** Note `instrumentation_stage_required` (bootstrap, activate, runtime)

### 3. Collect Case Statuses

For each gate and prerequisite case:
- Check the most recent verified status
- Record whether it is REAL PASS, REAL FAIL, STUB, BLOCKED, or INVALID

### 4. Evaluate Task Zero Gate (for full_guest cases)

For app cases with `app_shell_mode: full_guest`:

**Task Zero gate is SATISFIED only if:**
- All Task Zero cases (`app_shell_mode: task_zero`) are REAL PASS
- App shell is stabilized
- Instrumentation is activated
- Terminal UI is reachable

**Task Zero gate is NOT SATISFIED if:**
- Any Task Zero case is not REAL PASS
- App shell not stabilized
- Instrumentation not activated

### 5. Evaluate Instrumentation Stage Gate (for app cases)

For app cases with instrumentation requirements:

**Stage 0 (bootstrap) is SATISFIED if:**
- `ish_instrumentation_bootstrap()` succeeds
- Event `instrumentation_bootstrap_complete` recorded
- Called from main.m only

**Stage 1 (activate) is SATISFIED only if:**
- Stage 0 is SATISFIED
- `[ISHInstrumentation activate]` succeeds
- Event `instrumentation_activate_complete` recorded
- Called from AppDelegate only

**Stage 2 (runtime) is SATISFIED only if:**
- Stage 1 is SATISFIED
- Lower layers emit events via C bridge
- Events recorded through `ish_instrumentation_record_event()`
- No direct os_log/NSLog from product code

### 6. Evaluate Phase Gate

A phase gate is SATISFIED only if:
- All its gate cases are REAL PASS
- **AND** Task Zero is complete (for full_guest cases)
- **AND** Prior instrumentation stages are complete (for runtime stage)

A phase gate is NOT SATISFIED if any gate case is:
- REAL FAIL
- STUB
- BLOCKED
- INVALID
- **OR** Task Zero is incomplete (for full_guest)
- **OR** Prior instrumentation stage incomplete (for runtime)

### 7. Determine Allowed Action

**If gate is SATISFIED:**
- Progression to this phase is allowed
- Proceed with implementation or repair

**If gate is NOT SATISFIED:**
- Progression is BLOCKED
- Identify the earliest unsatisfied gate case
- **For full_guest:** Report earliest unsatisfied Task Zero case
- **For runtime stage:** Report incomplete prior instrumentation stage
- Report that case as the blocker
- Refuse to proceed with later-phase work

### 8. Handle Later-Phase Symptoms Early

If a later-phase symptom appears but earlier gates are not satisfied:
- Do not jump to fixing the symptom
- Reduce the problem to the earliest unsatisfied gate case
- **For app cases:** Reduce to Task Zero if not complete
- **For app cases:** Reduce to prior instrumentation stage if incomplete
- Create or repair that case first
- Only proceed after the gate case is REAL PASS

## Forbidden Patterns

- Skipping phase 00 or phase 01 because work is requested on a later runtime symptom
- Treating "mostly passing" or "close enough" as sufficient
- Accepting unverified status claims
- Progressing when gate cases are STUB with the rationale that "we understand the area"
- Allowing full_guest before Task Zero complete
- Allowing runtime stage before activation complete
- Product code owning instrumentation bootstrap

## Statuses That Do NOT Unlock Gates

These statuses do NOT permit progression to later phases:
- REAL FAIL (real path executed but failed)
- STUB (placeholder or simulated)
- BLOCKED (missing prerequisite)
- INVALID (malformed contract)
- Task Zero incomplete
- Prior instrumentation stage incomplete

Only REAL PASS unlocks the next phase gate.

## Reporting

When blocking progression, report:
- The requested phase
- The blocking gate case
- Its current exact status
- The required status (REAL PASS)
- **For full_guest:** Task Zero incomplete status
- **For runtime stage:** Prior stage incomplete status

## Success Criteria

- Later work proceeds only when declared gating rules permit
- The bootstrap sequence remains compositional and trustworthy
- Task Zero is enforced as a formal gate
- Instrumentation stages are enforced as gates
- Runtime boundaries proceed one at a time with exact edges
