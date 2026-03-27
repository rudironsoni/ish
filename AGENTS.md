# AGENTS.md — Mandatory Control Bootloader

## 0. Non-Negotiable Precondition

**ALL non-trivial work MUST pass through the mandatory command pipeline.**

The agent CANNOT lawfully work outside this path. Any deviation is INVALID.

---

## 1. Mission

Make iSH on iOS fully run Linux with blazing fast speed through TCTI and JIT-less emulation.

The harness exists ONLY to drive iSH toward this goal. Do not optimize the harness for its own sake. Do not add ornamental scaffolding. The harness is a means, not an end.

---

## 2. Source-of-Truth Precedence

When any documents disagree, use this exact order:

1. **Synchronized Rulesync control-plane source** (`.rulesync/commands/*.md`)
2. **`tests/cases/execution-order.yaml`** (canonical phase inventory)
3. **`tests/cases/status.yaml`** (evidence-bearing status ledger)
4. **`tests/cases/active.yaml`** (single active-case lock)
5. **Active case contract files** (`case.yaml`, `expected.yaml`, `authority.yaml`)
6. **Meson wiring** (`meson.build` files)
7. **Code and runtime artifacts**

**If AGENTS.md conflicts with any of the above, STOP immediately and repair the harness.**

---

## 3. Mandatory Command Pipeline

Every non-trivial task MUST execute through this EXACT sequence:

### 3.1 Startup Stage
```
harness-doctor
```
**Purpose:** Validate control-plane integrity before any work begins.
**Fail-closed:** If `harness-doctor` fails, NO other commands may run.
**Required subagents:** `orchestrator`, `phase-gate`

### 3.2 Case Selection Stage
```
case-next
```
**Purpose:** Deterministically select the single lawful next case.
**Output:** Creates/updates `tests/cases/active.yaml` with active lock.
**Required subagents:** `orchestrator`, `phase-gate`

### 3.3 Preflight Stage
```
case-preflight
```
**Purpose:** Verify active case is ready and block illegal operations.
**Fail-closed:** If preflight fails, NO implementation work may begin.
**Required subagents:** `orchestrator`, `phase-gate`, `case-author`

### 3.4 Implementation Stage
```
case-work
```
**Purpose:** Execute implementation for ONE active case only.
**Constraints:** Stays within `allowed_patch_scope`, never writes final status.
**Required subagents:** `harness-author` + specialist by case type

### 3.5 Execution Stage
```
case-run
```
**Purpose:** Run active case through explicit Meson entrypoint.
**Output:** Produces artifacts and deterministic evidence.
**Required subagents:** `orchestrator`

### 3.6 Verification Stage
```
case-verify
```
**Purpose:** Produce evidence-backed classification.
**Output:** Exact status, mismatch summary, evidence result.
**Required subagents:** `verifier`, `anti-slop`, `review-skeptic`

### 3.7 Promotion Stage
```
case-promote
```
**Purpose:** Update status ledger and close/advance active lock.
**Constraints:** ONLY runs after successful `case-verify`.
**Required subagents:** `orchestrator`, `phase-gate`

---

## 4. Active-Case Algorithm

To select the lawful next case:

1. Read `tests/cases/execution-order.yaml` for canonical phase order
2. Read `tests/cases/status.yaml` for current case statuses
3. Find the EARLIEST phase whose gate cases are NOT all `REAL PASS`
4. Within that phase, select the EARLIEST gate case that is not `REAL PASS`
5. If case substrate is missing, scaffold first (delegate to `case-substrate`)
6. If case contract is invalid, repair first (delegate to `case-author`)
7. If prerequisite is not `REAL PASS`, move backward to that prerequisite
8. ONLY ONE implementation-active case may exist at any time

The active case is the ONLY case eligible for implementation work.

---

## 5. Exact Status Vocabulary

Use ONLY these five terms. No synonyms. No softening.

- **`REAL PASS`** — Case executes and produces valid evidence matching `expected.yaml` exactly. Use ONLY after verifier confirms match.
- **`REAL FAIL`** — Case executes but produces output that does not match `expected.yaml`. Use ONLY when execution completes but comparison fails.
- **`STUB`** — Case directory exists but has no real implementation (scaffolded only, placeholder harness, no-op checks). Use when substrate exists but implementation is missing.
- **`BLOCKED`** — Case cannot execute because prerequisite case is not `REAL PASS`. Use when dependencies are unsatisfied.
- **`INVALID`** — Case contract is malformed, expectations are inconsistent, or authority chain is broken. Use when `case.yaml` or `expected.yaml` is corrupt or contradictory.

**Do NOT invent softer synonyms** like "passing", "done", "ready", "fixed", "working", "green", or "clean".

---

## 6. Mandatory Subagent Invocation Matrix

The agent does NOT choose whether to use subagents. The agent chooses which mandatory set applies.

### 6.1 By Stage

| Stage | Required Subagents |
|-------|-------------------|
| Startup | `orchestrator`, `phase-gate`, `harness-doctor` |
| Case Selection | `orchestrator`, `phase-gate` |
| Preflight | `orchestrator`, `phase-gate`, `case-author` |
| Contract Repair | `case-author`, `truth` agents, `fixture-author`, `meson-wire` |
| Implementation | `harness-author` + specialists |
| Verification | `verifier`, `anti-slop`, `review-skeptic` |
| Promotion | `orchestrator`, `phase-gate` |

### 6.2 By Case Type (Implementation Stage)

| Case Type | Required Specialists |
|-----------|---------------------|
| TRACE-* | `trace-observer` |
| DEC-* | `decoder-truth` |
| GEN-* | `generator-truth` |
| EXEC-* | `isa-truth` |
| MMU-*, ABI-* | `abi-truth` |
| ELF-* | `elf-truth` |
| SYS-* | `abi-truth` |
| THR-*, SIG-* | `abi-truth` |

---

## 7. Mandatory Skills Mapping

Each stage MUST invoke its required skills:

| Skill | Used By | Purpose |
|-------|---------|---------|
| `active-case-lifecycle` | `case-next`, `case-promote` | Active case lock management |
| `phase-gate-audit` | `harness-doctor`, `case-next`, `case-verify` | Phase progression validation |
| `truth-first-case-authoring` | Contract stage commands | Authority-driven case creation |
| `anti-slop-review` | `case-verify` | Detect fake success and drift |
| `meson-wire-check` | `case-preflight`, `harness-doctor` | Meson integration validation |
| `harness-health-audit` | `harness-doctor` | Cross-check control artifacts |

---

## 8. Retry Budgets and Forced Termination

### 8.1 Bounded Repair Loops

- **Maximum 3 bounded repair loops** per active case
- If the same verifier mismatch repeats after 3 attempts, STOP and classify
- If substrate or contract remains malformed after 3 repair attempts, classify `INVALID`
- If real prerequisite is missing, classify `BLOCKED`
- **NO infinite repair loops permitted**

### 8.2 Retry Budget Tracking

`tests/cases/active.yaml` tracks:
- `retry_budget_remaining`: decrements each repair cycle
- `repair_attempts`: increments each attempt
- `last_error`: last mismatch for pattern detection

When `retry_budget_remaining` reaches 0, the case MUST be classified and the active lock released.

---

## 9. Patch-Scope Enforcement

Every active case has explicit `allowed_patch_scope` in `active.yaml`.

The harness FAILS CLOSED if:
- Diff exceeds `allowed_patch_scope`
- Changes touch files outside active case directory
- Cross-case modifications detected
- Global refactor attempted during single-case work

### 9.1 Scope Categories

| Scope Level | Allowed Modifications |
|-------------|----------------------|
| `case-only` | Only files in `tests/cases/<phase>/<case>/` |
| `harness-local` | Case files + harness-specific files |
| `subsystem` | Case files + relevant subsystem (e.g., trace/, decode/) |
| `global` | Global changes (requires special authorization) |

---

## 10. Fail-Closed Refusal Rules

The agent MUST refuse and report ILLEGAL if asked to:

- **Skip phases** — Never work on phase N+1 when phase N has unsatisfied gates
- **Multi-case repairs** — Only one case implementation-active at any time
- **Broad runtime debugging** — If a smaller case can isolate the failure, author it first
- **Status promotion without verification** — Never promote based on implementation code alone
- **Work outside allowed patch scope** — Stay within `active.yaml` boundaries
- **Bypass harness-doctor** — Must pass health check before any work
- **Fake success on fallback** — No exit 0 from placeholder is evidence
- **Direct artifact edits** — Modify source, never generated outputs

---

## 11. Required Pre-Change Header

Before editing ANY file, emit this EXACT header:

```
=== PRE-CHANGE COMMITMENT ===
Active Phase: <phase-id>
Active Case: <case-id>
Current Status: <STUB|REAL FAIL|BLOCKED|INVALID>
Lawful Selection Reason: <why this is the earliest unsatisfied gate case>
Allowed Patch Scope: <specific files that may be modified>
Retry Budget Remaining: <N>
Stop Condition: <what constitutes completion for this edit>
Required Subagents: <list of mandatory subagents for this stage>
Required Skills: <list of mandatory skills for this stage>
==============================
```

**NO EXCEPTIONS. NO EDITS BEFORE THE HEADER.**

---

## 12. Required Post-Run Header

After completing work on a case, emit this EXACT header:

```
=== POST-RUN REPORT ===
Active Phase: <phase-id>
Active Case: <case-id>
Final Status: <REAL PASS|REAL FAIL|STUB|BLOCKED|INVALID>
Real Artifacts Produced: <list of actual output files>
Verifier Result: <PASS|FAIL|NOT_RUN>
Anti-Slop Result: <PASS|FAIL|NOT_RUN>
Review Skeptic Result: <PASS|FAIL|NOT_RUN>
Phase Gate Result: <PASS|FAIL|NOT_RUN>
Phase Progression Unlocked: <YES|NO>
Retry Budget Remaining: <N>
===============================
```

**NO STATUS CLAIMS WITHOUT VERIFIER AND ANTI-SLOP REVIEW.**

---

## 13. Next-Lawful-Action Output

`case-next` and related commands MUST return machine-readable next-lawful-action:

```yaml
next_lawful_action:
  active_phase: "00-trace-harness"
  active_case: "TRACE-002"
  current_status: "STUB"
  next_action: "IMPLEMENT"  # IMPLEMENT | REPAIR | SCAFFOLD | VERIFY | BLOCKED
  required_commands:
    - "case-preflight"
    - "case-work"
    - "case-run"
    - "case-verify"
    - "case-promote"
  required_subagents:
    - "orchestrator"
    - "harness-author"
    - "trace-observer"
  required_skills:
    - "active-case-lifecycle"
    - "anti-slop-review"
  allowed_patch_scope: "case-only"
  stop_conditions:
    - "Case produces artifacts matching expected.yaml"
    - "Verifier confirms REAL PASS"
    - "Anti-slop review passes"
```

This removes ALL ambiguity about what the agent is allowed to do next.

---

## 14. Conflict Resolution Policy

If AGENTS.md, synchronized instructions, and case files disagree:

1. **STOP** all product work immediately
2. **Identify** the specific conflict
3. **Repair** the harness (documentation, NOT code)
4. **Resume** product work only after conflict is resolved

**Never "choose whichever doc sounds easiest".**

---

## 15. Current Known Limitations

- Not all 108 gate cases have scaffolded directories — substrate may need creation on first encounter
- The bootstrap sequence (6 cases in phases 00-05) must be completed before autonomous progression is fully reliable

Do not assume the roadmap is complete when encountering missing substrate.

---

## 16. Commit and Push Policy

When the user says "commit" or asks you to commit changes:

**Required sequence:**
1. Stage the changes (`git add`)
2. Commit with descriptive message (`git commit`)
3. Push to remote (`git push`)

**Never:**
- Commit without pushing
- Leave commits local when the user asked for a commit
- Wait for a separate "push" command

---

## Appendix: Non-Negotiable Rules

### A. Mandatory Rules

1. No stub may count as pass.
2. No dynamic case discovery.
3. No hidden runtime dispatcher.
4. No direct edits to generated outputs.
5. No phase skipping.
6. No success-on-fallback behavior.
7. No case without explicit Meson wiring.
8. No vague completion claim without verifier and anti-slop review.
9. No later-phase progression while earlier gate cases are not `REAL PASS`.
10. No broad runtime debugging when a smaller failing case can be authored first.
11. No work outside the mandatory command pipeline.
12. No subagent invocation choice — mandatory sets only.
13. No status promotion without `case-verify`.
14. No edits without pre-change header.
15. No status claims without post-run header.

### B. Repository Posture

- Prefer smaller units.
- Prefer explicit contracts.
- Prefer deterministic fixtures.
- Prefer repo-local truth.
- Prefer real execution paths over simulated proof.
- Prefer narrower lawful paths over broader flexibility.

---

## 17. Critical Status Ledger Ownership Boundary

**`tests/cases/status.yaml` may ONLY be mutated by `case-promote`.**

This is the key ownership boundary that keeps implementation from laundering itself into success.

### 17.1 Forbidden Mutations

The following are **ILLEGAL** and MUST be refused:

- `case-work` modifying status.yaml — **FORBIDDEN**
- `case-run` modifying status.yaml — **FORBIDDEN**
- Implementation code modifying status.yaml — **FORBIDDEN**
- Ad hoc edits to status.yaml — **FORBIDDEN**
- Direct status changes without verification — **FORBIDDEN**

### 17.2 Lawful Mutation Path

Status changes MUST follow this exact path:

1. `case-work` implements the case (does NOT touch status.yaml)
2. `case-run` executes and produces artifacts (does NOT touch status.yaml)
3. `case-verify` produces evidence-backed classification (does NOT touch status.yaml)
4. `case-promote` updates status.yaml based on verify results (ONLY lawful mutator)

### 17.3 Enforcement

- `case-work` and `case-run` MUST refuse if asked to modify status.yaml
- `case-promote` MUST validate verification passed before mutating
- Any direct edit to status.yaml MUST fail harness-doctor
- Status promotion without `case-verify` is ILLEGAL

---

**Version:** 2.0
**Last Updated:** 2026-03-27
**Control System Status:** STRICT MODE
