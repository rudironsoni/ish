---
name: phase_gate_audit
description: Enforce bootstrap phase gating to prevent illegal forward progression.
---

You are the phase-gate audit skill. Apply this procedure before allowing any phase progression.

## Purpose

Enforce that later phases cannot proceed until earlier gate cases are truly REAL PASS.

## Core Principle

Bootstrap phases are ordered and gated. Only REAL PASS satisfies a gate.

## Prerequisites

- execution-order specification is available
- Current case status map is known

## Procedure

### 1. Read Execution Order

Locate and read the execution-order specification that defines:
- Phase order
- Gate cases for each phase
- Prerequisite relationships

### 2. Identify Current Phase and Gate Cases

For the active case:
- Note its phase
- Identify the gate cases that must be satisfied to enter this phase
- Identify any prerequisite cases within the phase

### 3. Collect Case Statuses

For each gate and prerequisite case:
- Check the most recent verified status
- Record whether it is REAL PASS, REAL FAIL, STUB, BLOCKED, or INVALID

### 4. Evaluate Gate Conditions

A phase gate is SATISFIED only if:
- All its gate cases are REAL PASS

A phase gate is NOT SATISFIED if any gate case is:
- REAL FAIL
- STUB
- BLOCKED
- INVALID

### 5. Determine Allowed Action

**If gate is SATISFIED:**
- Progression to this phase is allowed
- Proceed with implementation or repair

**If gate is NOT SATISFIED:**
- Progression is BLOCKED
- Identify the earliest unsatisfied gate case
- Report that case as the blocker
- Refuse to proceed with later-phase work

### 6. Handle Later-Phase Symptoms Early

If a later-phase symptom appears but earlier gates are not satisfied:
- Do not jump to fixing the symptom
- Reduce the problem to the earliest unsatisfied gate case
- Create or repair that case first
- Only proceed after the gate case is REAL PASS

## Forbidden Patterns

- Skipping phase 00 or phase 01 because work is requested on a later runtime symptom
- Treating "mostly passing" or "close enough" as sufficient
- Accepting unverified status claims
- Progressing when gate cases are STUB with the rationale that "we understand the area"

## Statuses That Do NOT Unlock Gates

These statuses do NOT permit progression to later phases:
- REAL FAIL (real path executed but failed)
- STUB (placeholder or simulated)
- BLOCKED (missing prerequisite)
- INVALID (malformed contract)

Only REAL PASS unlocks the next phase gate.

## Reporting

When blocking progression, report:
- The requested phase
- The blocking gate case
- Its current exact status
- The required status (REAL PASS)

## Success Criteria

- Later work proceeds only when declared gating rules permit
- The bootstrap sequence remains compositional and trustworthy
- No phase is illegally bypassed
