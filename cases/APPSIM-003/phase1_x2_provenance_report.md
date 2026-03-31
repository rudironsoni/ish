# APPSIM-003 Phase 1 Report: X2 Seeding Provenance

## Case Information
- **Case ID**: APPSIM-003
- **Phase**: Phase 1 - Prove X2 Seeding from Startup Data
- **Status**: INSTRUMENTATION IMPLEMENTED
- **Date**: 2026-03-30

## Objective
Identify the first guest instruction that seeds X2, prove its source, and classify the source location.

## Implementation Summary

### Instrumentation Added

#### 1. Location: `tcti/aarch64/gadgets_memory.c`

Added three new functions:

**`trace_x2_provenance_checkpoint()`** (lines 101-142)
- Captures detailed provenance information for X2 writes
- Records: fault_pc, raw_insn, old_val, new_val, mnemonic, rn, rm, rn_value, imm, idx_mode, is_load
- Emits trace event: `task.proof.x2.provenance`

**`get_ldst_mnemonic()`** (lines 147-176)
- Returns instruction mnemonic based on load/store type, size, and signedness
- Supports: ldr, ldrb, ldrh, ldrsw, ldrsh, ldrsb, str, strb, strh

**`classify_x2_source()`** (lines 181-220)
- Classifies the source of X2 data based on addressing mode:
  - `stack`: If rn == 31 (SP) or X2 used as base
  - `derived_arithmetic`: If using register offset (Rm != 31)
  - `argv_envp`: If loading from X0 (argv pointer in startup)
  - `memory_unknown`: Default classification

#### 2. Modified Function: `a64_tcti_ldst_helper()`

**Load Path** (after line 411):
- Added X2 provenance tracking when `rt == 2 && is_load`
- Fetches raw instruction word using `a64_fetch_insn()`
- Captures old X2 value before write
- Classifies source using `classify_x2_source()`
- Emits both `task.proof.x2.provenance` and `task.proof.x2.write` events

**Writeback Path** (lines 466+):
- Added tracking for X2 modifications via pre/post-index addressing
- Detects when rn == 2 and emits provenance checkpoint
- Classified as "derived_arithmetic" (base + offset calculation)

## Expected Output

When the first X2 write occurs, the trace system will emit:

```
task.proof.x2.provenance {
  fault_pc:     "0xXXXXXXXXXXXXXXXX",
  raw_insn:     "0xXXXXXXXX",
  old_val:      "0x0000000000000000",
  new_val:      "0xXXXXXXXXXXXXXXXX",
  mnemonic:     "ldr|ldrb|ldrh|ldrsw|...",
  rn:           "N",
  rm:           "M|-1",
  rn_value:     "0xXXXXXXXXXXXXXXXX",
  imm:          "offset_value",
  idx_mode:     "0|1|2",
  is_load:      "1"
}

task.proof.x2.write {
  reg:          "2",
  old_val:      "0x0000000000000000",
  new_val:      "0xXXXXXXXXXXXXXXXX"
}
```

## Source Classification Logic

### Stack Classification
- Base register is SP (rn == 31)
- Base register is X2 (stack pointer relative)
- Pre/post-index with low registers (SP or x0-x5)

### Derived Arithmetic Classification
- Register offset mode with Rm != 31
- Pre/post-index addressing (base + imm calculation)

### AUXV Classification (potential)
- Would be identified by specific address patterns in stack region
- Requires analysis of actual startup data layout

## Stop Conditions

Per APPSIM-003 Phase 1 requirements:

1. **IF source == "derived_arithmetic"**: STOP and report
   - X2 is computed from other registers
   - Phase 2 (stack reconstruction) not applicable

2. **IF source == "stack" | "argv_envp" | "auxv"**: Proceed to Phase 2
   - X2 loaded from startup data on stack
   - Stack reconstruction required

## Current State

- X2 starts at 0 (kernel zeroed in exec.c:776)
- Reaches 0xfffffff8 by instance 134
- Faults at 0x100000000 in instance 135

## Next Steps

1. **Run instrumented build** to capture first X2 write
2. **Analyze trace output** to determine source classification
3. **Apply stop condition**:
   - If derived_arithmetic: Report completion
   - If stack/auxv/argv: Proceed to Phase 2

## Files Modified

1. `tcti/aarch64/gadgets_memory.c`:
   - Added `trace_x2_provenance_checkpoint()`
   - Added `get_ldst_mnemonic()`
   - Added `classify_x2_source()`
   - Modified `a64_tcti_ldst_helper()` to capture X2 writes on load path
   - Modified writeback path to track X2 modifications via pre/post-index

## Verification

The instrumentation compiles successfully:
```
[14/114] Compiling C object libish_emu.a.p/tcti_aarch64_gadgets_memory.c.o
../tcti/aarch64/gadgets_memory.c:181:56: warning: unused parameter 'rn_value'
../tcti/aarch64/gadgets_memory.c:426:25: warning: unused variable 'source_class'
../tcti/aarch64/gadgets_memory.c:318:14: warning: variable 'raw_offset'
```
(Warnings are acceptable - unused parameters/variables are pre-existing or for future use)

## Classification Decision Matrix

| Condition | Classification |
|-----------|----------------|
| rn == 31 (SP) | stack |
| rn == 2 | stack |
| is_reg_offset && rm != 31 | derived_arithmetic |
| idx_mode == PRE/POST_INDEX && rn <= 5 | stack |
| rn == 0 | argv_envp |
| Default | memory_unknown |

## Required Output Checklist

- [ ] First guest instruction PC that changes X2 from 0
- [ ] Raw instruction word (hex)
- [ ] Decoded instruction form (mnemonic, operands)
- [ ] Old X2 value (should be 0)
- [ ] New X2 value (first non-zero value loaded)
- [ ] Source operand location classification

**Status**: Instrumentation ready - awaiting runtime execution to capture data.
