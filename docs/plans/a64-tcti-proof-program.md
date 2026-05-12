# Arm64 TCTI Family Test Architecture

## Summary
Refactor the arm64 proof surface around **instruction families and TCTI pipeline stages**, not around ad hoc repros or harness-specific scenarios.

The repository will keep the current runtime-owned layering:
- `IXLandLinuxRuntimeContractTests` for TCTI fetch/decode/lowering/semantic contracts
- `IXLandLinuxRuntimeSystemTests` for real guest-runtime and ABI/system proofs
- `IXLandLinuxRuntimePerfTests` for hot-path cost and scaling contracts
- `IXLandTerminal*` only for UI behavior, never for runtime semantics

This aligns with the upstream `IXLandSystem` split: Linux-facing proof stays separate from host/app proof, and compile/build smoke does not pretend to prove runtime behavior.

## Test Architecture
### 1. TCTI contract suites become two explicit axes
Keep all arm64/TCTI proof under `Tests/IXLandLinuxRuntimeContractTests/TCTI`, but split it cleanly into:

- `Pipeline`
  - `Fetch`
  - `Decode`
  - `Lowering`
  - `DispatchPreservation`
  - `ExitWriteback`
  - `BlockCacheAndGeneration`
  - `AtomicAndExclusiveEngine`
- `ISA Families`
  - `BaseScalar`
  - `Memory`
  - `Atomics`
  - `System`
  - `SIMDFP`
  - `Arm64e`

The rule is:
- pipeline suites prove TCTI stage correctness independent of workload
- family suites prove architectural semantics of instruction groups through the actual runtime execution path in `cpu.c` and related runtime files
- no duplicated mini-runtime, no helper execution shim, no fake harness truth

### 2. Family grouping is canonical and fixed
The long arm64 list is normalized into these family buckets:

- `BaseScalar/ControlAndFlags`
  - `B`, `B.cond`, `BC.cond`, `BL`, `BLR`, `BR`, `RET`, `CBZ`, `CBNZ`, `TBZ`, `TBNZ`, `CCMN`, `CCMP`, `CSEL`, `CSET`, `CSETM`, `CSINC`, `CSINV`, `CSNEG`, `CMP`, `CMN`, `TST`, `RMIF`, `CFINV`
- `BaseScalar/IntegerALU`
  - `ADD`, `ADDS`, `SUB`, `SUBS`, `ADC`, `ADCS`, `SBC`, `SBCS`, `NEG`, `NEGS`, `NGC`, `NGCS`, `ABS`, `MADD`, `MSUB`, `MUL`, `MNEG`, `SDIV`, `UDIV`, `SMADDL`, `SMSUBL`, `SMULL`, `SMULH`, `UMADDL`, `UMSUBL`, `UMULL`, `UMULH`
- `BaseScalar/MoveImmediateAndAddress`
  - `MOV`, `MOVK`, `MOVN`, `MOVZ`, `MVN`, `ADR`, `ADRP`
- `BaseScalar/LogicalBitfieldShift`
  - `AND`, `ANDS`, `ORR`, `ORN`, `EOR`, `EON`, `BIC`, `BICS`, `BFM`, `BFI`, `BFC`, `BFXIL`, `SBFM`, `SBFIZ`, `SBFX`, `UBFM`, `UBFIZ`, `UBFX`, `EXTR`, `LSL`, `LSLV`, `LSR`, `LSRV`, `ASR`, `ASRV`, `ROR`, `RORV`, `RBIT`, `REV*`, `CLS`, `CLZ`, `CTZ`, `SXTB`, `SXTH`, `SXTW`, `UXTB`, `UXTH`
- `Memory/ScalarLoadsStores`
  - `LDR*`, `STR*`, `LDUR*`, `STUR*`, `LDTR*`, `STTR*`, `LDAPUR*`, `STLUR*`, `PRFM`, `PRFUM`
- `Memory/PairAndFrame`
  - `LDP`, `STP`, `LDNP`, `STNP`, `LDPSW`
- `Memory/OrderedExclusiveAtomic`
  - `LDAR*`, `STLR*`, `LDAPR*`, `LDAXR*`, `STLXR*`, `LDXR*`, `STXR*`, `LDXP`, `STXP`, `LDAXP`, `STLXP`, all `CAS*`, `SWP*`, `LDADD*`, `LDCLR*`, `LDEOR*`, `LDSET*`, `LDSMAX*`, `LDSMIN*`, `LDUMAX*`, `LDUMIN*`, `STADD*`, `STCLR*`, `STEOR*`, `STSET*`, `STSMAX*`, `STSMIN*`, `STUMAX*`, `STUMIN*`
- `Memory/SpecialAndTagging`
  - `LD64B`, `ST64B*`, `LDG`, `LDGM`, `STG`, `STGM`, `STZG`, `STZGM`, `ST2G`, `STZ2G`, `LDRAA`, `LDRAB`
- `System/ExceptionsBarriersHints`
  - `SVC`, `HVC`, `SMC`, `BRK`, `HLT`, `ERET`, `ERETAA`, `ERETAB`, `DMB`, `DSB`, `ISB`, `CSDB`, `CLREX`, `SB`, `SSBB`, `PSSBB`, `PSB`, `TSB`, `NOP`, `YIELD`, `WFE`, `WFET`, `WFI`, `WFIT`, `SEV`, `SEVL`, `HINT`, `DRPS`, `ESB`, `DGH`, `BTI`
- `System/SysregCacheTLB`
  - `MRS`, `MSR`, `MRRS`, `MSRR`, `SYS`, `SYSL`, `SYSP`, `DC`, `IC`, `TLBI`, `TLBIP`, `CHKFEAT`, `SETF8`, `SETF16`, `XAFLAG`
- `SIMDFP/ScalarFP`
  - `FMOV`, `FADD`, `FSUB`, `FMUL`, `FDIV`, `FMADD`, `FMSUB`, `FNMADD`, `FNMSUB`, `FNEG`, `FABS`, `FCMP`, `FCMPE`, `FCCMP`, `FCCMPE`, `FCSEL`, `FCVT*`, `FRINT*`, `FSQRT`, `FRECPE`, `FRECPS`, `FRECPX`, `FRSQRTE`, `FRSQRTS`, `FSCALE`, `FJCVTZS`
- `SIMDFP/VectorIntegerLogical`
  - `DUP`, `INS`, `MOVI`, `MVNI`, `AND`, `ORR`, `ORN`, `EOR`, `BIC`, `BIF`, `BIT`, `BSL`, `CNT`, `CMEQ`, `CMGE`, `CMGT`, `CMHI`, `CMHS`, `CMLE`, `CMLT`, `CMTST`, `ADD*`, `SUB*`, `MUL`, `MLA`, `MLS`, `EXT`, `TBL`, `TBX`, `ZIP*`, `UZP*`, `TRN*`, `XTN*`
- `SIMDFP/VectorMemory`
  - `LD1*`, `LD2*`, `LD3*`, `LD4*`, `ST1*`, `ST2*`, `ST3*`, `ST4*`, SIMD `LDR`, `LDUR`, `LDP`, `STR`, `STUR`, `STP`
- `SIMDFP/CryptoAndDotProduct`
  - `AES*`, `SHA*`, `PMUL`, `PMULL*`, `SDOT`, `UDOT`, `USDOT`, `SUDOT`, `FDOT`, `BFDOT`, `SM3*`, `SM4*`, `BCAX`, `XAR`
- `Arm64e/PAuthAndAuthenticatedControl`
  - all `PAC*`, `AUT*`, `XPAC*`, `BRAA*`, `BRAB*`, `BLRAA*`, `BLRAB*`, `RETAA`, `RETAB`, `RETAASPPC`, `RETABSPPC`, `LDRAA`, `LDRAB`, `BTI`

### 3. Every family gets the same proof ladder
For each family, add tests in this order:

- `Decode contract`
  - instruction word classification
  - alias normalization
  - width/sign/variant selection
  - unsupported encoding rejection
- `Lowering contract`
  - correct gadget selection
  - correct operand carrier/writeback shape
  - no stale register or flag publication
  - no fallback interpreter path
- `Semantic execution contract`
  - execute minimal programs through the actual runtime entrypoints in `cpu.c`
  - prove register, flags, memory, PC, and exception state
  - cover overlap hazards, writeback hazards, width hazards, and same-block chaining hazards
- `Live guest contract`
  - add runtime-system tests only when a real guest path hits the family
  - map guest failures back to family buckets instead of inventing synthetic UI symptoms
- `Perf contract`
  - add hot-path perf cases only for families that are on live hot paths or known regressors

## Concrete refactor and additions
### 1. Replace monolithic semantic accretion with family-owned files
Refactor `Tests/IXLandLinuxRuntimeContractTests/TCTI/Semantic` into files owned by family, not by historical bug source. Target structure:

- `BaseScalar/TCTIControlAndFlagsSemanticTests.m`
- `BaseScalar/TCTIIntegerALUSemanticTests.m`
- `BaseScalar/TCTIMoveImmediateAndAddressSemanticTests.m`
- `BaseScalar/TCTILogicalBitfieldShiftSemanticTests.m`
- `Memory/TCTIScalarLoadStoreSemanticTests.m`
- `Memory/TCTIPairAndFrameSemanticTests.m`
- `Memory/TCTIAtomicAndExclusiveSemanticTests.m`
- `System/TCTISystemAndExceptionSemanticTests.m`
- `SIMDFP/TCTIScalarFPSemanticTests.m`
- `SIMDFP/TCTIVectorSemanticTests.m`
- `Arm64e/TCTIPAuthSemanticTests.m`

Existing musl/busybox-driven cases move into the owning family file. `tcti_semantic_scenarios.c` remains only as shared fixture/scenario assembly, not as a surrogate runtime.

### 2. Add a canonical family coverage ledger
Add one source-of-truth ledger under the TCTI contract tree, for example `Tests/IXLandLinuxRuntimeContractTests/TCTI/TCTIInstructionFamilyMatrix.md`.

Each family entry records:
- instruction names covered by the family
- `status`: `green`, `red`, `unsupported`, `deferred`
- decode coverage present or missing
- lowering coverage present or missing
- semantic coverage present or missing
- live guest evidence present or missing
- perf coverage present or missing
- current owning test files

Rules:
- `unsupported` must be explicit and justified, not implicit by omission
- `deferred` is allowed only for families not yet reached and not yet claimed
- arm64e instructions must have an explicit support policy
- no mnemonic disappears from the matrix

### 3. Use real runtime entrypoints only
All semantic execution tests must enter through the actual runtime execution path:
- real `cpu.c` compile/execute entrypoints
- real TCTI lowering and gadget emission
- real writeback and exit paths

Forbidden:
- helper runtimes
- shadow interpreters
- duplicate execution engines
- harness-side semantic execution

### 4. System tests stay runtime-owned and guest-visible
`IXLandLinuxRuntimeSystemTests` should prove:
- guest ABI and process bringup
- loader behavior
- syscall/runtime behavior
- real guest instruction-family failures when they appear in musl/busybox or shell flows

They should not duplicate family semantics exhaustively. Their job is to expose:
- which live workload fails
- which guest PC/family is implicated
- whether the failure is semantic, dispatch, cache, fault, or performance related

### 5. Perf tests become family-aware, not synthetic
`IXLandLinuxRuntimePerfTests` should be grouped into:
- `TCTIHotPath/FetchDecodeLowering`
- `TCTIHotPath/GadgetExecution`
- `TCTIHotPath/BlockCacheAndGeneration`
- `TCTIHotPath/AtomicsAndExclusives`
- `GuestRuntimeCore`
- `TerminalIO`

Perf acceptance is contract-shaped:
- no repeated same-PC recompilation inside stable code generation
- bounded block-cache invalidation
- no per-block tracing path regressions in normal debug tracing
- lowering and execution cost scale with block/instruction count, not accidental churn

## Test plan and rollout order
### Phase 1. Stabilize the structure
- Freeze current targets: `IXLandLinuxRuntimeContractTests`, `IXLandLinuxRuntimeSystemTests`, `IXLandLinuxRuntimePerfTests`
- Refactor semantic files into family-owned suites
- Add the coverage ledger and map every mnemonic in your list into one family
- Remove or demote any leftover tests whose names encode historical repros more than architectural ownership

### Phase 2. Fill base scalar and memory first
Implement full decode/lowering/semantic coverage for:
- `ControlAndFlags`
- `IntegerALU`
- `MoveImmediateAndAddress`
- `LogicalBitfieldShift`
- `ScalarLoadsStores`
- `PairAndFrame`
- `AtomicAndExclusive`

This is the first mandatory green tranche because it covers the bulk of musl/busybox startup and the current live regressions.

### Phase 3. Fill system and hot SIMD/FP
Implement:
- `SystemAndException`
- `SysregCacheTLB`
- `ScalarFP`
- `VectorMemory`
- the SIMD/vector integer families already exercised by guest workloads

Only after that, expand to crypto, dot-product, and the less common vector families.

### Phase 4. Arm64e support matrix
Add explicit tests for:
- `BTI`
- authenticated branch/return
- `PAC*`, `AUT*`, `XPAC*`
- authenticated loads

Each one must be marked as one of:
- fully supported in TCTI
- traps or rejects intentionally
- deferred and not yet claimed

### Phase 5. Kernel-style runtime proof
For Linux-owner runtime behavior outside pure ISA semantics, add KUnit-shaped C tests inside runtime-owned test targets for:
- memory-management contracts
- syscall-facing credential/task/signal behavior
- VFS/fdtable invariants
- PTY/runtime contracts

These are Linux-runtime tests, not app tests, and they are separate from TCTI family semantics.

## Acceptance criteria
- Every mnemonic in the provided list appears in the canonical family matrix.
- No arm64 instruction is “implicitly covered”; ownership is explicit by family.
- Base scalar and memory families have decode, lowering, and semantic execution coverage through the real runtime path.
- Real guest regressions are tied to family buckets in `IXLandLinuxRuntimeSystemTests`, not UI tests.
- Perf tests cover TCTI cache, generation, lowering, and gadget-execution hot paths without relying on fake harness loops.
- `IXLandTerminal` remains out of runtime semantic proof.
- No helper execution engine, no interpreter path, no duplicated runtime in tests.

## Assumptions and defaults
- The existing target split is kept; no new app-surface target is introduced for runtime semantics.
- `IXLandLinuxRuntimeContractTests` is the owner for exhaustive TCTI family proof.
- `IXLandLinuxRuntimeSystemTests` is the owner for live guest/runtime proof.
- `IXLandLinuxRuntimePerfTests` is the owner for hot-path performance proof.
- Upstream `IXLandSystem` remains the architectural reference for test layering: Linux-facing proof separate from host-facing proof, compile smoke separate from runtime proof.
- TCTI remains the only valid guest AArch64 execution engine.
