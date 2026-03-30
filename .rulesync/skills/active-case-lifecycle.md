---
name: active_case_lifecycle
description: Manage the complete lifecycle of one active case from identification to terminal status. Includes instrumentation lifecycle and Task Zero mode awareness.
---

You are the active case lifecycle skill. Apply this procedure when managing any active case.

## Purpose

Keep exactly one case implementation-active at a time through a bounded, deterministic workflow. Enforce instrumentation lifecycle and Task Zero mode.

## Prerequisites

Before starting this skill:
- One active case must be selected by the orchestrator
- The case contract must exist and be structurally valid
- The case identity must be consistent across folder, contract, and build system
- Task Zero mode must be identified (if app case)
- Instrumentation stage must be identified (if app case)

## Procedure

### 1. Identify Active Case
- Note the exact case ID
- Note the phase
- Note the current status from the most recent verification
- Identify the smallest failing unit if the case is not REAL PASS
- **For app cases:** Note `app_shell_mode` (task_zero or full_guest)
- **For app cases:** Note `instrumentation_stage_required` (bootstrap, activate, runtime)

### 2. Verify Case Contract
Check that the case has:
- Valid case.yaml with all required fields
- Valid expected.yaml with normative expectations
- Valid authority.yaml with truth chain
- Fixture manifest if the case uses fixtures
- Allowed patch scope declared
- Prerequisites satisfied
- **For app cases:** `app_shell_mode` declared
- **For app cases:** `instrumentation_stage_required` declared
- **For app cases:** `instrumentation_events_expected` declared

If any of these are missing or malformed, classify the case as INVALID and stop.

### 3. Verify Gate Status
Before implementing or repairing:
- Read the execution-order specification
- Identify all prerequisite and gate cases for this phase
- Verify each gate case is REAL PASS
- **For Task Zero cases:** No additional Task Zero prerequisites
- **For full_guest cases:** All Task Zero cases must be REAL PASS
- **For instrumentation stage:** All prior stages must be REAL PASS

If any gate case is not REAL PASS, classify as BLOCKED and identify the earliest unsatisfied gate.

### 4. Verify Instrumentation Architecture
For app cases, verify:
- App owns instrumentation lifecycle
- main.m calls `ish_instrumentation_bootstrap()` (Stage 0)
- AppDelegate calls `[ISHInstrumentation activate]` (Stage 1)
- Lower layers emit events via C bridge only
- No direct os_log/NSLog in product code
- No constructor markers for instrumentation

If architecture violations detected, classify as INVALID and report violation.

### 5. Verify Task Zero State
For app cases with `app_shell_mode: task_zero`:
- Verify guest startup is disabled
- Verify app shell stabilization is the goal
- Verify instrumentation activation is required
- Verify no guest execution expected

For app cases with `app_shell_mode: full_guest`:
- Verify all Task Zero cases are REAL PASS
- Verify runtime reintroduction proceeds one boundary at a time
- Verify exact failing edge will be reported

### 6. Run Bounded Repair Loop
If the case is not REAL PASS and not BLOCKED or INVALID:

**Declare bounds:**
- Active case ID
- Current exact status (REAL FAIL, STUB, or other)
- Allowed patch scope
- Maximum retry count (typically 2)
- Stop conditions (REAL PASS, PATCH BUDGET EXHAUSTED, or BLOCKED)
- **For app cases:** Instrumentation stage required
- **For app cases:** Task Zero mode

**Execute repair:**
1. Classify the first failing layer precisely
2. Apply the smallest justified patch
3. Rebuild the affected targets
4. Re-run the explicit case through Meson
5. Collect fresh artifacts
6. Verify artifacts against expected contract
7. **For app cases:** Verify instrumentation events recorded
8. **For app cases:** Verify Task Zero milestones reached
9. Re-classify status

**Loop exit conditions:**
- Status becomes REAL PASS
- Status becomes BLOCKED with exact missing precondition
- Patch budget is exhausted
- Case contract is discovered to be INVALID
- **For app cases:** Instrumentation stage not reached
- **For app cases:** Task Zero not complete (for task_zero cases)

### 7. Verify Artifacts
For the terminal state, confirm:
- report.json exists and is valid
- All required case artifacts exist
- Artifact schemas match the contract
- No fake or placeholder artifacts satisfy presence checks only
- **For app cases:** instrumentation_events.json exists (if required)
- **For app cases:** boot_milestones.json shows Task Zero complete (if task_zero mode)

### 8. Verify Instrumentation Events
For app cases, verify:
- Events recorded through C bridge API
- Events match expected list from case contract
- No forbidden patterns (direct os_log/NSLog)
- Instrumentation lifecycle ownership correct

### 9. Verify Task Zero Completion
For Task Zero cases, verify:
- `app_shell_stabilized` milestone reached
- Terminal UI reachable without guest execution
- Instrumentation activated
- No guest startup before Task Zero complete

### 10. Verify Runtime Boundary (for full_guest)
For runtime reintroduction cases, verify:
- Exact boundary defined
- Last known good point reported
- First known bad point reported
- Exact failing edge identified
- One boundary at a time

### 11. Classify Exact Status
Use only the exact vocabulary:
- **REAL PASS**: Real path executed, matched expectations, artifacts valid
- **REAL FAIL**: Real path executed, failed expectations, artifacts valid
- **STUB**: Placeholder implementation, simulated semantics, or not-yet-real path
- **BLOCKED**: Missing prerequisite, cannot proceed without violating policy
- **INVALID**: Malformed contract, inconsistent identity, or broken wiring

### 12. Check Observability Freeze
If app-shell stabilization and observability migration are complete:
- Check if active blocker is runtime/kernel
- If yes: Verify broad observability work has stopped
- If yes: Verify only narrow semantic events allowed
- If no: Continue normally

### 13. Hand Off
Report to the orchestrator:
- Exact final status
- Evidence summary
- Any BLOCKED conditions with exact missing prerequisites
- Any INVALID conditions requiring contract repair
- **For app cases:** Instrumentation stage reached
- **For app cases:** Task Zero status
- **For app cases:** Runtime boundary reduction status

## Forbidden Patterns

- Mixing multiple active cases in one repair loop
- Proceeding with implementation when gate status is unsatisfied
- Using broad smoke tests instead of explicit case evidence
- Reporting STUB or simulated behavior as REAL PASS
- Continuing past declared retry budget
- Widening patch scope without explicit justification
- Product code owning instrumentation lifecycle
- Direct os_log/NSLog in product code for investigation
- Skipping Task Zero before guest execution
- Instrumentation bootstrap from product code
- Broad observability when freeze should be active
- Runtime boundary without exact edge

## Success Criteria

- Exactly one case was active
- Contract was verified before implementation
- Gates were satisfied before repair
- Repair loop was bounded and terminated deterministically
- Status classification uses exact vocabulary
- Evidence supports the claimed status
- **For app cases:** Instrumentation architecture is correct
- **For app cases:** Task Zero is complete (if required)
- **For app cases:** Runtime boundary has exact edge (if applicable)
- **For app cases:** Observability freeze respected (if active)
