# TCTI Instruction Family Matrix

This file is the source-of-truth ledger for arm64 TCTI family ownership in
`IXLandLinuxRuntimeContractTests`.

Rules:

- Every mnemonic listed in the arm64 family plan appears in one explicit family.
- `status` is one of `green`, `red`, `unsupported`, or `deferred`.
- Coverage columns record the current proof surface, not aspirational coverage.
- Semantic execution coverage means execution through the real runtime entrypoints
  in `cpu.c` and the real TCTI lowering/writeback path.
- Live guest evidence belongs in `IXLandLinuxRuntimeSystemTests`; this matrix only
  records whether that evidence exists.
- Some Phase 1 files are still temporary convergence buckets for multiple
  subfamilies. Those buckets are called out explicitly instead of implying
  broader coverage than the current tree proves.

## Pipeline

| Axis | Status | Decode | Lowering | Semantic | Live Guest | Perf | Owning files |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Fetch | green | present | n/a | n/a | present | present | `TCTI/Pipeline/Fetch/TCTIPipelineFetchTests.m`, `IXLandLinuxRuntimeSystemTests/Guest/**`, `IXLandLinuxRuntimePerfTests/Perf/TCTIHotPath/TCTIHotPathPerfTests.m` |
| Lowering | green | present | present | n/a | indirect | present | `TCTI/Pipeline/Lowering/TCTIPipelineLoweringTests.m`, `IXLandLinuxRuntimePerfTests/Perf/TCTIHotPath/TCTIHotPathPerfTests.m` |
| DispatchPreservation | green | present | present | present | present | deferred | `TCTI/Pipeline/DispatchPreservation/TCTIPipelineDispatchPreservationTests.m`, `TCTI/ISAFamilies/BaseScalar/TCTIControlAndFlagsSemanticTests.m`, `TCTI/ISAFamilies/Memory/TCTIScalarLoadStoreSemanticTests.m` |
| ExitWriteback | green | present | present | present | present | deferred | `TCTI/Pipeline/ExitWriteback/TCTIPipelineExitWritebackTests.m`, `TCTI/ISAFamilies/Memory/TCTIPairAndFrameSemanticTests.m`, `TCTI/ISAFamilies/Memory/TCTIScalarLoadStoreSemanticTests.m` |
| BlockCacheAndGeneration | deferred | missing | missing | missing | present | partial | `IXLandLinuxRuntimeSystemTests/Guest/**`, `IXLandLinuxRuntimePerfTests/Perf/TCTIHotPath/TCTIHotPathPerfTests.m` |
| AtomicAndExclusiveEngine | green | present | present | present | present | deferred | `TCTI/Pipeline/AtomicAndExclusiveEngine/TCTIPipelineAtomicAndExclusiveEngineTests.m`, `TCTI/ISAFamilies/Memory/TCTIAtomicAndExclusiveSemanticTests.m` |

## ISA Families

| Family | Mnemonics | Status | Decode | Lowering | Semantic | Live Guest | Perf | Owning files |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| BaseScalar/ControlAndFlags | `B`, `B.cond`, `BC.cond`, `BL`, `BLR`, `BR`, `RET`, `CBZ`, `CBNZ`, `TBZ`, `TBNZ`, `CCMN`, `CCMP`, `CSEL`, `CSET`, `CSETM`, `CSINC`, `CSINV`, `CSNEG`, `CMP`, `CMN`, `TST`, `RMIF`, `CFINV` | green | partial | partial | present | present | deferred | `TCTI/ISAFamilies/BaseScalar/TCTIControlAndFlagsSemanticTests.m`, `TCTI/Pipeline/Lowering/TCTIPipelineLoweringTests.m`, `IXLandLinuxRuntimeSystemTests/Guest/Execution/GuestBusyboxRuntimeExecutionTests.m` |
| BaseScalar/IntegerALU | `ADD`, `ADDS`, `SUB`, `SUBS`, `ADC`, `ADCS`, `SBC`, `SBCS`, `NEG`, `NEGS`, `NGC`, `NGCS`, `ABS`, `MADD`, `MSUB`, `MUL`, `MNEG`, `SDIV`, `UDIV`, `SMADDL`, `SMSUBL`, `SMULL`, `SMULH`, `UMADDL`, `UMSUBL`, `UMULL`, `UMULH` | green | partial | partial | present | present | deferred | `TCTI/ISAFamilies/BaseScalar/TCTIIntegerALUSemanticTests.m`, `TCTI/ISAFamilies/BaseScalar/TCTIControlAndFlagsSemanticTests.m` |
| BaseScalar/MoveImmediateAndAddress | `MOV`, `MOVK`, `MOVN`, `MOVZ`, `MVN`, `ADR`, `ADRP` | green | partial | partial | present | present | deferred | `TCTI/ISAFamilies/BaseScalar/TCTIMoveImmediateAndAddressSemanticTests.m`, `TCTI/ISAFamilies/BaseScalar/TCTIControlAndFlagsSemanticTests.m` |
| BaseScalar/LogicalBitfieldShift | `AND`, `ANDS`, `ORR`, `ORN`, `EOR`, `EON`, `BIC`, `BICS`, `BFM`, `BFI`, `BFC`, `BFXIL`, `SBFM`, `SBFIZ`, `SBFX`, `UBFM`, `UBFIZ`, `UBFX`, `EXTR`, `LSL`, `LSLV`, `LSR`, `LSRV`, `ASR`, `ASRV`, `ROR`, `RORV`, `RBIT`, `REV*`, `CLS`, `CLZ`, `CTZ`, `SXTB`, `SXTH`, `SXTW`, `UXTB`, `UXTH` | green | partial | partial | present | present | deferred | `TCTI/ISAFamilies/BaseScalar/TCTILogicalBitfieldShiftSemanticTests.m`, `TCTI/ISAFamilies/BaseScalar/TCTIControlAndFlagsSemanticTests.m` |
| Memory/ScalarLoadsStores | `LDR*`, `STR*`, `LDUR*`, `STUR*`, `LDTR*`, `STTR*`, `LDAPUR*`, `STLUR*`, `PRFM`, `PRFUM` | green | partial | partial | present | present | deferred | `TCTI/ISAFamilies/Memory/TCTIScalarLoadStoreSemanticTests.m` |
| Memory/PairAndFrame | `LDP`, `STP`, `LDNP`, `STNP`, `LDPSW` | green | partial | partial | present | present | deferred | `TCTI/ISAFamilies/Memory/TCTIPairAndFrameSemanticTests.m` |
| Memory/OrderedExclusiveAtomic | `LDAR*`, `STLR*`, `LDAPR*`, `LDAXR*`, `STLXR*`, `LDXR*`, `STXR*`, `LDXP`, `STXP`, `LDAXP`, `STLXP`, `CAS*`, `SWP*`, `LDADD*`, `LDCLR*`, `LDEOR*`, `LDSET*`, `LDSMAX*`, `LDSMIN*`, `LDUMAX*`, `LDUMIN*`, `STADD*`, `STCLR*`, `STEOR*`, `STSET*`, `STSMAX*`, `STSMIN*`, `STUMAX*`, `STUMIN*` | red | partial | partial | present | present | deferred | `TCTI/ISAFamilies/Memory/TCTIAtomicAndExclusiveSemanticTests.m`, `TCTI/Pipeline/AtomicAndExclusiveEngine/TCTIPipelineAtomicAndExclusiveEngineTests.m` |
| Memory/SpecialAndTagging | `LD64B`, `ST64B*`, `LDG`, `LDGM`, `STG`, `STGM`, `STZG`, `STZGM`, `ST2G`, `STZ2G`, `LDRAA`, `LDRAB` | unsupported | partial | partial | present | missing | deferred | `TCTI/ISAFamilies/Memory/TCTISpecialAndTaggingSemanticTests.m` |
| System/ExceptionsBarriersHints | `SVC`, `HVC`, `SMC`, `BRK`, `HLT`, `ERET`, `ERETAA`, `ERETAB`, `DMB`, `DSB`, `ISB`, `CSDB`, `CLREX`, `SB`, `SSBB`, `PSSBB`, `PSB`, `TSB`, `NOP`, `YIELD`, `WFE`, `WFET`, `WFI`, `WFIT`, `SEV`, `SEVL`, `HINT`, `DRPS`, `ESB`, `DGH`, `BTI` | green | partial | partial | present | partial | deferred | `TCTI/ISAFamilies/System/TCTIExceptionsBarriersHintsSemanticTests.m`, `TCTI/Pipeline/Lowering/TCTIPipelineLoweringTests.m` |
| System/SysregCacheTLB | `MRS`, `MSR`, `MRRS`, `MSRR`, `SYS`, `SYSL`, `SYSP`, `DC`, `IC`, `TLBI`, `TLBIP`, `CHKFEAT`, `SETF8`, `SETF16`, `XAFLAG` | green | partial | partial | present | partial | deferred | `TCTI/ISAFamilies/System/TCTISysregCacheTLBSemanticTests.m`, `TCTI/Pipeline/Lowering/TCTIPipelineLoweringTests.m` |
| SIMDFP/ScalarFP | `FMOV`, `FADD`, `FSUB`, `FMUL`, `FDIV`, `FMADD`, `FMSUB`, `FNMADD`, `FNMSUB`, `FNEG`, `FABS`, `FCMP`, `FCMPE`, `FCCMP`, `FCCMPE`, `FCSEL`, `FCVT*`, `FRINT*`, `FSQRT`, `FRECPE`, `FRECPS`, `FRECPX`, `FRSQRTE`, `FRSQRTS`, `FSCALE`, `FJCVTZS` | green | partial | partial | present | missing | deferred | `TCTI/ISAFamilies/SIMDFP/TCTIScalarFPSemanticTests.m`, `TCTI/Pipeline/Lowering/TCTIPipelineLoweringTests.m` |
| SIMDFP/VectorIntegerLogical | `DUP`, `INS`, `MOVI`, `MVNI`, `AND`, `ORR`, `ORN`, `EOR`, `BIC`, `BIF`, `BIT`, `BSL`, `CNT`, `CMEQ`, `CMGE`, `CMGT`, `CMHI`, `CMHS`, `CMLE`, `CMLT`, `CMTST`, `ADD*`, `SUB*`, `MUL`, `MLA`, `MLS`, `EXT`, `TBL`, `TBX`, `ZIP*`, `UZP*`, `TRN*`, `XTN*` | red | partial | partial | present | present | deferred | `TCTI/ISAFamilies/SIMDFP/TCTIVectorSemanticTests.m`, `TCTI/ISAFamilies/BaseScalar/TCTIControlAndFlagsSemanticTests.m` |
| SIMDFP/VectorMemory | `LD1*`, `LD2*`, `LD3*`, `LD4*`, `ST1*`, `ST2*`, `ST3*`, `ST4*`, SIMD `LDR`, `LDUR`, `LDP`, `STR`, `STUR`, `STP` | green | partial | partial | present | missing | deferred | `TCTI/ISAFamilies/SIMDFP/TCTIVectorMemorySemanticTests.m` |
| SIMDFP/CryptoAndDotProduct | `AES*`, `SHA*`, `PMUL`, `PMULL*`, `SDOT`, `UDOT`, `USDOT`, `SUDOT`, `FDOT`, `BFDOT`, `SM3*`, `SM4*`, `BCAX`, `XAR` | unsupported | partial | partial | present | missing | deferred | `TCTI/ISAFamilies/SIMDFP/TCTICryptoAndDotProductSemanticTests.m` |
| Arm64e/PAuthAndAuthenticatedControl | `PAC*`, `AUT*`, `XPAC*`, `BRAA*`, `BRAB*`, `BLRAA*`, `BLRAB*`, `RETAA`, `RETAB`, `RETAASPPC`, `RETABSPPC`, `LDRAA`, `LDRAB`, `BTI` | unsupported | partial | partial | present | missing | deferred | `TCTI/ISAFamilies/Arm64e/TCTIPAuthSemanticTests.m` |
