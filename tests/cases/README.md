# iSH Case System

Strict test cases for AArch64 Linux userspace emulation on iOS AArch64.

This directory contains the canonical validation inventory for the iSH-A64 migration project. The case system enforces phase-gated progression where each phase must achieve `REAL PASS` on all gate cases before the next phase can begin.

## Architecture

```
tests/cases/
├── README.md                # This file - case system documentation
├── schema.yaml              # Case structure and metadata schema
├── execution-order.yaml     # Canonical phase/gate inventory (MUST READ FIRST)
├── meson.build              # EXPLICIT test declarations only
├── harness/                 # Harness implementations
│   ├── meson.build         # Build harness executables
│   ├── case_schema.py      # CI/developer validation
│   ├── decode_golden.c     # Decode validation harness
│   ├── generator_golden.c  # Generator validation harness
│   ├── semantic_micro.c    # Execution validation harness
│   ├── abi_fixture.c       # ABI validation harness
│   ├── runtime_trace.c     # Runtime trace harness
│   ├── ios_app_harness.c   # iOS app validation harness (NEW)
│   └── ...                 # Additional app harness utilities
└── [phase]/[case]/
    ├── case.yaml           # REQUIRED: Case definition (see schema.yaml)
    ├── expected.yaml       # REQUIRED for decode/generator cases
    ├── authority.yaml      # REQUIRED for decode/generator cases
    └── fixtures/           # REQUIRED when fixtures needed
        └── manifest.yaml   # Fixture specification
```

## What is a Gate Case?

A **gate case** is a case that MUST achieve `REAL PASS` status before the next phase can begin. Gate cases are listed in `execution-order.yaml` under each phase's `gate_cases` list.

Gate cases serve as:
- **Phase entry criteria**: All gate cases in phase N must pass before phase N+1 work begins
- **Quality checkpoints**: Each gate validates a specific capability before building upon it
- **Progression guards**: Enforcement prevents phase skipping and premature advancement

## Status Classification

Use **exactly** these status terms:

| Status | Meaning |
|--------|---------|
| **REAL PASS** | Case executes and produces valid evidence matching expected results |
| **REAL FAIL** | Case executes but output does not match expected results |
| **STUB** | Case directory exists but has scaffolded-only implementation |
| **BLOCKED** | Case cannot execute because prerequisite case is not REAL PASS |
| **INVALID** | Case contract is malformed or expectations are inconsistent |

### Status Rules

1. **STUB is NOT PASS**: A scaffolded case does NOT satisfy gate requirements
2. **Only REAL PASS counts**: Phase progression requires `REAL PASS`, not just any non-fail state
3. **BLOCKED requires repair**: Fix prerequisite first, then retry
4. **INVALID requires contract repair**: Fix the case definition before implementation

## Active Case Selection

The autonomous agent operates on **exactly one** active case at a time. The active case selection algorithm:

### Step 1: Read Canonical Inventory
```yaml
# Read execution-order.yaml to get:
# - Phase order (00 → 01 → 02 → 02b → 02c → 03 → ...)
# - Gate case list for each phase
# - Gating policy
# - Legacy/deprecated cases to exclude
```

### Step 2: Find Earliest Unsatisfied Gate
For each phase in order:
- Check if all gate cases have status `REAL PASS`
- First phase where NOT all gates pass → **target phase**
- Skip deprecated/legacy cases (exclude_from_selection: true)

### Step 3: Find Active Case
Within the target phase:
- Find the earliest gate case not `REAL PASS`
- That case becomes the **active case**

### Step 4: Validate Prerequisites
- Read the active case's `case.yaml`
- Check if all `prerequisites` are `REAL PASS`
- If prerequisites missing → mark **BLOCKED**, repair dependencies

### Step 5: Ensure Substrate Exists
- If active case directory missing → scaffold it
- If `case.yaml` missing → create from schema
- If contract invalid → repair before implementation

### Step 6: Execute or Repair
- Only work on the active case
- Run the case via Meson
- If `REAL FAIL` → debug and fix
- If `STUB` → implement
- Verify result

### Step 7: Loop
Return to Step 2 and repeat until all phases complete.

## Why Only One Implementation-Active Case?

**Enforced constraint**: Only one case may be in implementation/repair state at a time.

Rationale:
1. **Focused debugging**: Multiple failing cases create ambiguous causality
2. **Clear progression**: One case at a time provides unambiguous progress markers
3. **Prevent scope creep**: Multi-case work leads to unreviewed complexity
4. **Honest status**: Each case status reflects actual work completed

A case becomes "implementation-active" when:
- It is the active case (earliest unsatisfied gate)
- All prerequisites are satisfied
- Substrate exists and contract is valid
- Work is in progress to move it from `STUB`/`REAL FAIL` to `REAL PASS`

## App Harness Architecture

The repository now supports **autonomous iOS app testing** through two app-related case families:

### Family A: Early Simulator Harness Capability (Phase 02b)
**Purpose**: Make app testing lawful NOW, before later product phases complete.
**Cases**: APPSIM-001 through APPSIM-006
**Proves**: XcodeBuildMCP availability, simulator targeting, build/install/launch, log harvesting, crash normalization, bounded reset/retry.

### Family B: Early App Runtime-Entry (Phase 02c)
**Purpose**: Reduce current crash class to earliest failing milestone.
**Cases**: APP-001 through APP-005
**Proves**: First ELF exec, second exec/login entry, process-entry contract, login ELF boundaries.

### Family C: Later App Stability/Product (Phase 10)
**Purpose**: Validate late-stage product behavior.
**Cases**: APP-006 through APP-010
**Proves**: Guest loop entry, login-ready, shell-ready, relaunch stability, suspend/resume.

### Two-Layer App Harness Model

**Layer 1: Execution/Orchestration (XcodeBuildMCP)**
- Drives simulator boot, app build/install/launch
- Collects structured artifacts
- Primary operations: `build_run_sim`, `launch_app_logs_sim`, `list_sims`, `boot_sim`, `erase_sims`

**Layer 2: Validation (Meson-wired)**
- Validates Layer 1 artifacts
- Enforces schema/contract correctness
- Remains deterministic and repo-local

### XcodeBuildMCP Operation Mapping

| Stage | Primary Operation | Purpose |
|-------|-------------------|---------|
| harness-doctor | `session_show_defaults`, `list_sims` | Verify tool availability |
| case-preflight | `session_set_defaults` (validation only) | Verify defaults resolvable |
| case-work/run | `build_run_sim` | Build, install, launch, collect logs |
| retry/reset | `erase_sims`, `boot_sim` | Clean state retry |

### Milestone-First Crash Reduction

When the app crashes on simulator launch, the agent MUST:
1. Reduce the failure to the earliest failing app boot milestone
2. Produce structured artifacts identifying:
   - Highest completed milestone
   - First failing milestone
   - Crash signature hash
3. NOT jump directly to MMU, TCTI, ELF, ABI, or broad runtime blame

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

## Phase Gating Policy

**No phase skipping allowed**. The agent MUST:
- Complete all gate cases in phase N before phase N+1
- Refuse to work on phase N+1 cases when phase N gates are incomplete
- Treat phase N+1 cases as **BLOCKED** if phase N gates are not `REAL PASS`

### Phase Order (14 phases, 122 gate cases)

Phase order is determined by the **literal ordered sequence** in `execution-order.yaml`, NOT by lexical sort or numeric parsing of phase IDs. Phase IDs are opaque strings.

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

**Total**: 14 phases, 122 gate cases

**Legacy cases (excluded from gate counts):**
- IOS-001, IOS-002, IOS-003: deprecated, superseded by APPSIM and APP families

## Execution

```bash
# Run single case
meson test case:DEC-001

# Run all bootstrap cases (defined in meson.build)
meson test --suite bootstrap

# Run app harness capability cases
meson test --suite app-harness-capability

# Run app runtime-entry cases
meson test --suite app-runtime-entry

# Validate case schemas
python3 tests/cases/harness/case_schema.py --validate-all
```

## Scaffolding vs Completion

**Scaffolding** creates the case substrate:
- Directory structure
- `case.yaml` with metadata
- Contract definition
- **Status remains STUB**

**Completion** achieves `REAL PASS`:
- Implementation exists
- Test executes
- Output matches expected
- Evidence artifacts generated

**Scaffolding is NOT completion**. A scaffolded case remains `STUB` until implementation produces `REAL PASS`.

## Bootstrap Sequence

The initial bootstrap validates core infrastructure:

1. TRACE-001 - Trace subsystem validation
2. DEC-001 - Decode validation
3. GEN-001 - Generator validation
4. EXEC-001 - Semantic execution validation
5. ABI-001 - ABI validation
6. ELF-001 - Loader validation

After bootstrap, continue with remaining gate cases in each phase.

## Source of Truth

Expectation authorities (in priority order):

- **Tier 0**: Arm ARM (https://developer.arm.com/documentation/ddi0602/latest)
- **Tier 1**: llvm-mc (https://llvm.org/docs/CommandGuide/llvm-mc.html)
- **Tier 2**: llvm-objdump (https://llvm.org/docs/CommandGuide/llvm-objdump.html)
- **Tier 3**: GNU objdump (https://sourceware.org/binutils/docs/binutils/objdump.html)

## Rules

1. **No dynamic discovery** - Tests explicitly declared in meson.build
2. **Immutable case IDs** - Once introduced, never rename
3. **Clean artifacts** - Harness recreates artifact directory each run
4. **No ad hoc patches** - Patch only after proving first failing layer
5. **Authority chain** - Never invent expectations from runtime behavior
6. **Explicit Meson wiring** - Every case must have explicit Meson test declaration
7. **One active case** - Only one implementation-active case at a time
8. **No phase skipping** - Complete phase N before phase N+1
9. **Honest status** - Never claim STUB as REAL PASS
10. **Refuse illegal ops** - Agent must refuse phase skipping and multi-case loops
11. **Status ownership** - Only `case-promote` may mutate status.yaml
12. **Milestone-first debugging** - Reduce crashes to earliest failing milestone before subsystem blame
13. **XcodeBuildMCP canonical** - Use XcodeBuildMCP as primary simulator control surface

## File Requirements

| Case Type | Required Files |
|-----------|---------------|
| Trace harness | `case.yaml` |
| Decode golden | `case.yaml`, `expected.yaml`, `authority.yaml` |
| Generator golden | `case.yaml`, `expected.yaml`, `authority.yaml` |
| Semantic micro | `case.yaml`, `expected.yaml` (SHOULD) |
| ABI fixture | `case.yaml`, `expected.yaml` (SHOULD) |
| ELF fixture | `case.yaml`, `expected.yaml` (SHOULD) |
| Syscall fixture | `case.yaml`, `expected.yaml` (SHOULD) |
| Thread fixture | `case.yaml`, `expected.yaml` (SHOULD) |
| Signal fixture | `case.yaml`, `expected.yaml` (SHOULD) |
| musl fixture | `case.yaml` + fixtures |
| glibc fixture | `case.yaml` + fixtures |
| iOS fixture | `case.yaml`, `expected.yaml` (SHOULD) |
| iOS app harness | `case.yaml`, expected app artifacts |
| Distro fixture | `case.yaml` + fixtures |

## Autonomous Operation Checklist

Before starting work, verify:
- [ ] Read `execution-order.yaml` for phase/gate inventory
- [ ] Identified target phase (earliest unsatisfied)
- [ ] Identified active case (earliest gate not REAL PASS)
- [ ] Checked prerequisites are REAL PASS
- [ ] Verified case directory exists
- [ ] Validated case.yaml against schema
- [ ] Confirmed only working on active case
- [ ] No phase skipping intended
- [ ] For app cases: XcodeBuildMCP available and simulator targetable

## Troubleshooting

**"Cannot proceed to next phase"**
- Check `execution-order.yaml` for current phase gate status
- Verify all gate cases in current phase are `REAL PASS`
- Do not skip ahead

**"Case marked BLOCKED"**
- Check case.yaml prerequisites list
- Ensure prerequisite cases are `REAL PASS`
- Repair prerequisite first

**"Contract INVALID"**
- Validate case.yaml against `schema.yaml`
- Check required fields are present
- Verify field values match enum constraints

**"Expected file missing"**
- Check file requirements table above
- Create required files per case type
- Do not fake expected content

**"App case crashes without milestone artifacts"**
- Ensure boot_milestones.json is being generated
- Check crash_signature.json is produced on crash
- Verify simulator logs are being harvested
- Review milestone-first debugging policy

**"Status promotion refused"**
- Verify required app artifacts present
- Check milestone ordering consistency
- Ensure crash signature present if crash occurred
- Confirm structured artifacts match narrative
