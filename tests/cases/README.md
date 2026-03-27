# iSH Case System

Strict test cases for AArch64 Linux userspace emulation on iOS AArch64.

## Architecture

```
tests/cases/
├── schema.yaml              # Case structure definition
├── execution-order.yaml     # Phase gates and bootstrap sequence
├── meson.build              # EXPLICIT test declarations only
├── harness/                 # Harness binaries
│   ├── meson.build         # Build harness executables
│   ├── case_schema.py      # CI/developer validation
│   ├── decode_golden.c     # Decode validation harness
│   ├── generator_golden.c  # Generator validation harness
│   ├── semantic_micro.c    # Execution validation harness
│   ├── abi_fixture.c       # ABI validation harness
│   └── runtime_trace.c     # Runtime trace harness
└── [phase]/[case]/
    ├── case.yaml           # REQUIRED: Case definition
    ├── expected.yaml       # REQUIRED for decode/generator
    └── authority.yaml      # REQUIRED for decode/generator
```

## Execution

```bash
# Run single case
meson test case:DEC-001

# Run all bootstrap cases
meson test --suite bootstrap

# Validate case schemas
python3 tests/cases/harness/case_schema.py --validate-all
```

## File Requirements

| Case Type | Required Files |
|-----------|---------------|
| Trace harness | `case.yaml` |
| Decode golden | `case.yaml`, `expected.yaml`, `authority.yaml` |
| Generator golden | `case.yaml`, `expected.yaml`, `authority.yaml` |
| Semantic micro | `case.yaml`, `expected.yaml` (SHOULD) |
| ABI fixture | `case.yaml`, `expected.yaml` (SHOULD) |

## Bootstrap Sequence

1. TRACE-001 - Trace subsystem validation
2. DEC-001 - Decode validation
3. GEN-001 - Generator validation
4. EXEC-001 - Semantic execution validation
5. ABI-001 - ABI validation
6. ELF-001 - Loader validation

## Source of Truth

- **Tier 0:** Arm ARM (https://developer.arm.com/documentation/ddi0602/latest)
- **Tier 1:** llvm-mc (https://llvm.org/docs/CommandGuide/llvm-mc.html)
- **Tier 2:** llvm-objdump (https://llvm.org/docs/CommandGuide/llvm-objdump.html)
- **Tier 3:** GNU objdump (https://sourceware.org/binutils/docs/binutils/objdump.html)

## Rules

1. **No dynamic discovery** - Tests explicitly declared in meson.build
2. **Immutable case IDs** - Once introduced, never rename
3. **Clean artifacts** - Harness recreates artifact directory each run
4. **No ad hoc patches** - Patch only after proving first failing layer
5. **Authority chain** - Never invent expectations from runtime behavior
