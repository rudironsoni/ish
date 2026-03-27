---
name: active_case_lifecycle
description: Manage the complete lifecycle of one active case from identification to terminal status.
---

You are the active case lifecycle skill. Apply this procedure when managing any active case.

## Purpose

Keep exactly one case implementation-active at a time through a bounded, deterministic workflow.

## Prerequisites

Before starting this skill:
- One active case must be selected by the orchestrator
- The case contract must exist and be structurally valid
- The case identity must be consistent across folder, contract, and build system

## Procedure

### 1. Identify Active Case
- Note the exact case ID
- Note the phase
- Note the current status from the most recent verification
- Identify the smallest failing unit if the case is not REAL PASS

### 2. Verify Case Contract
Check that the case has:
- Valid case.yaml with all required fields
- Valid expected.yaml with normative expectations
- Valid authority.yaml with truth chain
- Fixture manifest if the case uses fixtures
- Allowed patch scope declared
- Prerequisites satisfied

If any of these are missing or malformed, classify the case as INVALID and stop.

### 3. Verify Gate Status
Before implementing or repairing:
- Read the execution-order specification
- Identify all prerequisite and gate cases for this phase
- Verify each gate case is REAL PASS

If any gate case is not REAL PASS, classify as BLOCKED and identify the earliest unsatisfied gate.

### 4. Run Bounded Repair Loop
If the case is not REAL PASS and not BLOCKED or INVALID:

**Declare bounds:**
- Active case ID
- Current exact status (REAL FAIL, STUB, or other)
- Allowed patch scope
- Maximum retry count (typically 2)
- Stop conditions (REAL PASS, PATCH BUDGET EXHAUSTED, or BLOCKED)

**Execute repair:**
1. Classify the first failing layer precisely
2. Apply the smallest justified patch
3. Rebuild the affected targets
4. Re-run the explicit case through Meson
5. Collect fresh artifacts
6. Verify artifacts against expected contract
7. Re-classify status

**Loop exit conditions:**
- Status becomes REAL PASS
- Status becomes BLOCKED with exact missing precondition
- Patch budget is exhausted
- Case contract is discovered to be INVALID

### 5. Verify Artifacts
For the terminal state, confirm:
- report.json exists and is valid
- All required case artifacts exist
- Artifact schemas match the contract
- No fake or placeholder artifacts satisfy presence checks only

### 6. Classify Exact Status
Use only the exact vocabulary:
- **REAL PASS**: Real path executed, matched expectations, artifacts valid
- **REAL FAIL**: Real path executed, failed expectations, artifacts valid
- **STUB**: Placeholder implementation, simulated semantics, or not-yet-real path
- **BLOCKED**: Missing prerequisite, cannot proceed without violating policy
- **INVALID**: Malformed contract, inconsistent identity, or broken wiring

### 7. Hand Off
Report to the orchestrator:
- Exact final status
- Evidence summary
- Any BLOCKED conditions with exact missing prerequisites
- Any INVALID conditions requiring contract repair

## Forbidden Patterns

- Mixing multiple active cases in one repair loop
- Proceeding with implementation when gate status is unsatisfied
- Using broad smoke tests instead of explicit case evidence
- Reporting STUB or simulated behavior as REAL PASS
- Continuing past declared retry budget
- Widening patch scope without explicit justification

## Success Criteria

- Exactly one case was active
- Contract was verified before implementation
- Gates were satisfied before repair
- Repair loop was bounded and terminated deterministically
- Status classification uses exact vocabulary
- Evidence supports the claimed status
