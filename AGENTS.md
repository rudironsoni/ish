# iSH Autonomous Harness Control Plane

## 1. Mission

The harness exists only to move iSH toward real Linux execution on iOS through TCTI (Trace-Compile-Translate-Inline) and JIT-less emulation.

App simulator testing is part of that proof system. The harness validates:
- Real iOS simulator app launch
- App boot progression through deterministic milestones
- Guest startup achievement
- Process-entry invariants
- Structured crash classification
- Bounded self-healing retries
- Lawful promotion of app cases into machine-readable truth

## 2. Source-of-Truth Precedence

When sources disagree, the agent MUST fail closed and repair the harness.

**Priority order (highest to lowest):**
1. Synchronized Rulesync control-plane source (`.rulesync/`)
2. `tests/cases/execution-order.yaml`
3. `tests/cases/status.yaml`
4. `tests/cases/active.yaml`
5. Active case contract files (`case.yaml`, `expected.yaml`, `authority.yaml`)
6. Meson wiring (`meson.build`)
7. Code and runtime artifacts
8. Narrative reports

## 3. Mandatory Command Pipeline

All non-trivial work SHALL pass through the command pipeline:

```
harness-doctor → case-next → case-preflight → case-work → case-run → case-verify → case-promote
```

**Single lawful entrypoint:** `phase-drive`

If `phase-drive` exists and is lawful, it is the top-level autonomous entrypoint that orchestrates the full pipeline.

**Sequence enforcement:**
- `harness-doctor` MUST pass before any other command
- `case-next` MUST select the lawful active case before work begins
- `case-verify` MUST pass before `case-promote`
- Only `case-promote` may mutate status

## 4. App Testing is First-Class Lawful Work

App simulator launch, boot progression, guest startup, crash reproduction, and crash classification are lawful `tests/cases` work.

**SHALL NOT be handled as:**
- Ad hoc debugging outside the case system
- Manual simulator runs
- Prose-based reporting without structured artifacts

The repository supports two app-related case families:

### Family A: Early Simulator Harness Capability (Phase 02b)
Purpose: Make app testing lawful NOW, before later product phases complete.
Cases: APPSIM-001 through APPSIM-006
Proves: XcodeBuildMCP availability, simulator targeting, build/install/launch, log harvesting, crash normalization, bounded reset/retry.

### Family B: Early App Runtime-Entry (Phase 02c)
Purpose: Reduce current crash class to earliest failing milestone.
Cases: APP-001 through APP-005
Proves: First ELF exec, second exec/login entry, process-entry contract, login ELF boundaries.

### Family C: Later App Stability/Product (Phase 10)
Purpose: Validate late-stage product behavior.
Cases: APP-006 through APP-010
Proves: Guest loop entry, login-ready, shell-ready, relaunch stability, suspend/resume.

## 5. Milestone-First Crash Reduction

When the app crashes on simulator launch, the agent MUST:

1. Reduce the failure to the earliest failing app boot milestone
2. Produce structured artifacts identifying:
   - Highest completed milestone
   - First failing milestone
   - Crash signature hash
3. NOT jump directly to MMU, TCTI, ELF, ABI, or broad runtime blame
4. NOT speculate until a lawful lower-layer case or milestone artifact proves that boundary

**Boot milestones (in order):**
1. app_launched
2. boot_setup_started
3. first_elf_exec_entered
4. first_elf_exec_returned
5. second_execve_started
6. bin_login_elf_header_parsed
7. bin_login_program_headers_read
8. guest_loop_entered
9. login_ready
10. shell_ready

## 6. Required Pre-Change Header

Before editing ANY file, emit:

```
=== PRE-CHANGE COMMITMENT ===
Active Phase: <phase-id>
Active Case: <case-id>
Current Status: <STUB|REAL FAIL|BLOCKED|INVALID>
Lawful Selection Reason: <why this is earliest unsatisfied gate>
Session Type: <harness|app|guest>
Session Goal: <specific objective>
Command Pipeline: <harness-doctor → case-next → ...>
Allowed Patch Scope: <specific files>
Forbidden Scope: <files that must not be touched>
Required Subagents: <list>
Required Skills: <list>
Retry Budget: <remaining/max>
Stop Conditions: <specific criteria>
==============================
```

## 7. Required Post-Run Header

After completing work on a case, emit:

```
=== POST-RUN REPORT ===
Active Phase: <phase-id>
Active Case: <case-id>
Final Status: <REAL PASS|REAL FAIL|STUB|BLOCKED|INVALID>
App Launch Result: <success|failure|crash>
Boot Milestone Reached: <milestone name or null>
Crash Signature Result: <hash or null>
Real Artifacts Produced: <list>
Verifier Result: <PASS|FAIL>
Anti-Slop Result: <PASS|FAIL>
Review-Skeptic Result: <PASS|FAIL>
Status Ledger Changed: <true|false>
Active Lock Changed: <true|false>
Next Lawful Case: <case-id or null>
Session Continues: <true|false>
Exact Stop Reason: <reason or null>
========================
```

## 8. App-Specific Refusal Behavior

The agent MUST refuse:

- App debugging without a lawful app case (APPSIM-* or APP-*)
- Boot-log claims without structured artifacts
- Crash classification without normalized crash signature
- Status promotion without verifier-backed app artifacts
- Broad subsystem blame before first failing milestone identified
- Simulator runs outside the command pipeline
- Silent retries or retries beyond bounded policy

## 9. Harness-Only Scope Boundary

Harness-only tasks SHALL NOT touch product code:
- Tests under `tests/cases/` are harness
- `app/`, `src/`, `emu/`, `kernel/` are product
- When a failing case proves a product bug, the harness repairs the case; product code repairs are separate authorization

## 10. App-Harness Architecture

**Two-layer model:**

**Layer 1: Execution/Orchestration (XcodeBuildMCP)**
- Drives simulator boot, app build/install/launch
- Collects structured artifacts
- Uses: `session_show_defaults`, `build_run_sim`, `launch_app_logs_sim`, `list_sims`, `boot_sim`, `erase_sims`

**Layer 2: Validation (Meson-wired)**
- Validates Layer 1 artifacts
- Enforces schema/contract correctness
- Remains deterministic and repo-local

**XcodeBuildMCP is the canonical simulator control surface.**

If XcodeBuildMCP definitively lacks a required operation:
- Add bounded explicit fallback
- Fallback MUST be repo-local, documented, case-scoped
- Fallback MUST be classified as control-plane debt
- Fallback MUST NOT become the primary path

## 11. Status Ownership Boundary

`tests/cases/status.yaml` MAY only be mutated through `case-promote`.

**ILLEGAL mutations:**
- Direct edits by `case-work`, `case-run`, or ad hoc changes
- Implementation code promoting its own status
- Narrative reports claiming status without evidence

## 12. Revised Phase Architecture

**Total: 14 phases, 122 gate cases**

| Phase | Name | Gate Cases |
|-------|------|------------|
| 00 | Trace Harness | 6 (TRACE-001..006) |
| 01 | Decode | 11 (DEC-001..011) |
| 02 | Generator | 8 (GEN-001..008) |
| 02b | iOS Simulator Harness | 6 (APPSIM-001..006) |
| 02c | iOS App Runtime-Entry | 5 (APP-001..005) |
| 03 | Semantic Exec | 11 (EXEC-001..011) |
| 04 | MMU/ABI | 11 (MMU-001..006, ABI-001..005) |
| 05 | ELF Loader | 9 (ELF-001..009) |
| 06 | Syscalls | 12 (SYS-001..012) |
| 07 | Threads/Signals | 9 (THR-001..006, SIG-001..003) |
| 08 | musl | 8 (MUSL-001..008) |
| 09 | glibc | 10 (GLIBC-001..010) |
| 10 | Tooling/Stability/App | 12 (TOOL-001..004, STAB-001..003, APP-006..010) |
| 11 | Distro Matrix | 4 (DISTRO-001..004) |

**Legacy cases (excluded from gates):**
- IOS-001, IOS-002, IOS-003: deprecated, superseded by APPSIM and APP families

## 13. Non-Negotiable Rules

1. No stub may count as pass
2. No dynamic case discovery
3. No hidden runtime dispatcher
4. No direct edits to generated outputs
5. No phase skipping
6. No success-on-fallback behavior
7. No case without explicit Meson wiring
8. No vague completion claim without verifier and anti-slop review
9. No later-phase progression while earlier gate cases are not `REAL PASS`
10. No broad runtime debugging when a smaller failing case can be authored or repaired first

## Required Status Vocabulary

Use **exactly** these terms:
- `REAL PASS`: Case executes and produces valid evidence matching expected
- `REAL FAIL`: Case executes but produces output that does not match expected
- `STUB`: Case directory exists but has no real implementation
- `BLOCKED`: Case cannot execute because prerequisite case is not REAL PASS
- `INVALID`: Case contract is malformed or expectations are inconsistent

**Do not invent softer synonyms.**

## Repository Posture

- Prefer smaller units
- Prefer explicit contracts
- Prefer deterministic fixtures
- Prefer repo-local truth
- Prefer real execution paths over simulated proof

## Quick Start for Coding Agents

**Single command entrypoint:**
```
phase-drive
```

This will:
1. Run harness-doctor
2. Select the lawful active case (currently APPSIM-001 in phase 02b)
3. Execute through the full pipeline automatically
4. Continue to next case until phase complete or stop condition
5. Report progress at phase boundaries only

**What it does automatically:**
- Detects app cases (APPSIM-*, APP-*) and dispatches through XcodeBuildMCP
- Performs bounded retry with reset on first crash
- Stops on repeated crash signature
- Stops on gate-case REAL FAIL
- Captures milestones and crash signatures
- Updates machine-readable state after every transition

**Stop conditions (automatic)::**
- Phase complete (all gates REAL PASS)
- Gate-case REAL FAIL
- BLOCKED/INVALID case encountered
- harness-doctor failure
- Resources unavailable

**Current target:**
- Phase: 02b-iOS-Simulator-Harness
- Case: APPSIM-001-xcodebuildmcp-availability
- Goal: Prove XcodeBuildMCP is available and iSH app target is discoverable
