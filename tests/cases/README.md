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
│   └── runtime_trace.c     # Runtime trace harness
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
# - Phase order (00 → 01 → 02 → ...)
# - Gate case list for each phase
# - Gating policy
```

### Step 2: Find Earliest Unsatisfied Gate
For each phase in order:
- Check if all gate cases have status `REAL PASS`
- First phase where NOT all gates pass → **target phase**

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

## Phase Gating Policy

**No phase skipping allowed**. The agent MUST:
- Complete all gate cases in phase N before phase N+1
- Refuse to work on phase N+1 cases when phase N gates are incomplete
- Treat phase N+1 cases as **BLOCKED** if phase N gates are not `REAL PASS`

### Phase Order (12 phases, 103 gate cases)

| Phase | Name | Gate Cases |
|-------|------|------------|
| 00 | Trace Harness | 6 cases (TRACE-001 to TRACE-006) |
| 01 | Decode | 11 cases (DEC-001 to DEC-011) |
| 02 | Generator | 8 cases (GEN-001 to GEN-008) |
| 03 | Semantic Exec | 10 cases (EXEC-001 to EXEC-010) |
| 04 | MMU/ABI | 11 cases (MMU-001 to MMU-006, ABI-001 to ABI-005) |
| 05 | ELF Loader | 9 cases (ELF-001 to ELF-009) |
| 06 | Syscalls | 12 cases (SYS-001 to SYS-012) |
| 07 | Threads/Signals | 9 cases (THR-001 to THR-006, SIG-001 to SIG-003) |
| 08 | musl | 8 cases (MUSL-001 to MUSL-008) |
| 09 | glibc | 10 cases (GLIBC-001 to GLIBC-010) |
| 10 | Tooling/Stability/iOS | 10 cases (TOOL-001 to TOOL-004, STAB-001 to STAB-003, IOS-001 to IOS-003) |
| 11 | Distro Matrix | 4 cases (DISTRO-001 to DISTRO-004) |

**Total**: 12 phases, 103 gate cases

## Execution

```bash
# Run single case
meson test case:DEC-001

# Run all bootstrap cases (defined in meson.build)
meson test --suite bootstrap

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
