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
| Fetch | green | present | n/a | n/a | present | synthetic | `TCTI/Pipeline/Fetch/TCTIPipelineFetchTests.m`, `IXLandLinuxRuntimeSystemTests/Guest/**`, `IXLandLinuxRuntimePerfTests/Perf/TCTIHotPath/TCTIHotPathPerfTests.m` |
| Decode | green | present | n/a | n/a | present | synthetic | `TCTI/Pipeline/Decode/TCTIPipelineDecodeTests.m`, `TCTI/Pipeline/Lowering/TCTIPipelineLoweringTests.m`, `IXLandLinuxRuntimeSystemTests/Guest/**`, `IXLandLinuxRuntimePerfTests/Perf/TCTIHotPath/TCTIHotPathPerfTests.m` |
| Lowering | green | present | present | n/a | present | synthetic | `TCTI/Pipeline/Lowering/TCTIPipelineLoweringTests.m`, `IXLandLinuxRuntimeSystemTests/Guest/**`, `IXLandLinuxRuntimePerfTests/Perf/TCTIHotPath/TCTIHotPathPerfTests.m` |
| DispatchPreservation | green | present | present | present | present | n/a | `TCTI/Pipeline/DispatchPreservation/TCTIPipelineDispatchPreservationTests.m`, `TCTI/ISAFamilies/BaseScalar/TCTIControlAndFlagsSemanticTests.m`, `TCTI/ISAFamilies/Memory/TCTIScalarLoadStoreSemanticTests.m` |
| ExitWriteback | green | present | present | present | present | n/a | `TCTI/Pipeline/ExitWriteback/TCTIPipelineExitWritebackTests.m`, `TCTI/ISAFamilies/Memory/TCTIPairAndFrameSemanticTests.m`, `TCTI/ISAFamilies/Memory/TCTIScalarLoadStoreSemanticTests.m` |
| BlockCacheAndGeneration | green | present | present | present | present | synthetic | `TCTI/Pipeline/BlockCacheAndGeneration/TCTIPipelineBlockCacheGenerationTests.m`, `IXLandLinuxRuntimeSystemTests/Guest/**`, `IXLandLinuxRuntimePerfTests/Perf/TCTIHotPath/TCTIHotPathPerfTests.m` |
| AtomicAndExclusiveEngine | green | present | present | present | present | missing | `TCTI/Pipeline/AtomicAndExclusiveEngine/TCTIPipelineAtomicAndExclusiveEngineTests.m`, `TCTI/ISAFamilies/Memory/TCTIAtomicAndExclusiveSemanticTests.m` |

## ISA Families

| Family | Mnemonics | Status | Decode | Lowering | Semantic | Live Guest | Perf | Owning files |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| BaseScalar/ControlAndFlags | `B`, `B.cond`, `BC.cond`, `BL`, `BLR`, `BR`, `RET`, `CBZ`, `CBNZ`, `TBZ`, `TBNZ`, `CCMN`, `CCMP`, `CSEL`, `CSET`, `CSETM`, `CSINC`, `CSINV`, `CSNEG`, `CMP`, `CMN`, `TST`, `RMIF`, `CFINV` | green | present | present | present | present | n/a | `TCTI/ISAFamilies/BaseScalar/TCTIControlAndFlagsSemanticTests.m`, `TCTI/Pipeline/Lowering/TCTIPipelineLoweringTests.m`, `IXLandLinuxRuntimeSystemTests/Guest/Execution/GuestBusyboxRuntimeExecutionTests.m` |
| BaseScalar/IntegerALU | `ADD`, `ADDS`, `SUB`, `SUBS`, `ADC`, `ADCS`, `SBC`, `SBCS`, `NEG`, `NEGS`, `NGC`, `NGCS`, `ABS`, `MADD`, `MSUB`, `MUL`, `MNEG`, `SDIV`, `UDIV`, `SMADDL`, `SMSUBL`, `SMULL`, `SMULH`, `UMADDL`, `UMSUBL`, `UMULL`, `UMULH` | green | present | present | present | present | n/a | `TCTI/ISAFamilies/BaseScalar/TCTIIntegerALUSemanticTests.m`, `TCTI/ISAFamilies/BaseScalar/TCTIControlAndFlagsSemanticTests.m`, `TCTI/Pipeline/Lowering/TCTIPipelineLoweringTests.m` |
| BaseScalar/MoveImmediateAndAddress | `MOV`, `MOVK`, `MOVN`, `MOVZ`, `MVN`, `ADR`, `ADRP` | green | present | present | present | present | n/a | `TCTI/ISAFamilies/BaseScalar/TCTIMoveImmediateAndAddressSemanticTests.m`, `TCTI/Pipeline/Decode/TCTIPipelineDecodeTests.m`, `TCTI/Pipeline/Lowering/TCTIPipelineLoweringTests.m`, `IXLandLinuxRuntimeSystemTests/Guest/Execution/GuestBusyboxRuntimeExecutionTests.m` |
| BaseScalar/LogicalBitfieldShift | `AND`, `ANDS`, `ORR`, `ORN`, `EOR`, `EON`, `BIC`, `BICS`, `BFM`, `BFI`, `BFC`, `BFXIL`, `SBFM`, `SBFIZ`, `SBFX`, `UBFM`, `UBFIZ`, `UBFX`, `EXTR`, `LSL`, `LSLV`, `LSR`, `LSRV`, `ASR`, `ASRV`, `ROR`, `RORV`, `RBIT`, `REV*`, `CLS`, `CLZ`, `CTZ`, `SXTB`, `SXTH`, `SXTW`, `UXTB`, `UXTH` | green | present | present | present | present | n/a | `TCTI/ISAFamilies/BaseScalar/TCTILogicalBitfieldShiftSemanticTests.m`, `TCTI/Pipeline/Lowering/TCTIPipelineLoweringTests.m`, `IXLandLinuxRuntimeSystemTests/Guest/Execution/GuestBusyboxRuntimeExecutionTests.m` |
| Memory/ScalarLoadsStores | `LDR*`, `STR*`, `LDUR*`, `STUR*`, `LDTR*`, `STTR*`, `LDAPUR*`, `STLUR*`, `PRFM`, `PRFUM` | green | present | present | present | present | n/a | `TCTI/ISAFamilies/Memory/TCTIScalarLoadStoreSemanticTests.m`, `TCTI/Pipeline/Lowering/TCTIPipelineLoweringTests.m`, `IXLandLinuxRuntimeSystemTests/Guest/Execution/GuestBusyboxRuntimeExecutionTests.m` |
| Memory/PairAndFrame | `LDP`, `STP`, `LDNP`, `STNP`, `LDPSW` | green | present | present | present | present | n/a | `TCTI/ISAFamilies/Memory/TCTIPairAndFrameSemanticTests.m`, `TCTI/Pipeline/Lowering/TCTIPipelineLoweringTests.m`, `IXLandLinuxRuntimeSystemTests/Guest/Execution/GuestBusyboxRuntimeExecutionTests.m` |
| Memory/OrderedExclusiveAtomic | `LDAR*`, `STLR*`, `LDAPR*`, `LDAXR*`, `STLXR*`, `LDXR*`, `STXR*`, `LDXP`, `STXP`, `LDAXP`, `STLXP`, `CAS*`, `SWP*`, `LDADD*`, `LDCLR*`, `LDEOR*`, `LDSET*`, `LDSMAX*`, `LDSMIN*`, `LDUMAX*`, `LDUMIN*`, `STADD*`, `STCLR*`, `STEOR*`, `STSET*`, `STSMAX*`, `STSMIN*`, `STUMAX*`, `STUMIN*` | red | present | present | present | indirect | n/a | `TCTI/ISAFamilies/Memory/TCTIAtomicAndExclusiveSemanticTests.m`, `TCTI/Pipeline/AtomicAndExclusiveEngine/TCTIPipelineAtomicAndExclusiveEngineTests.m`, `TCTI/Reality/TCTIFamilyGapInventoryTests.m`, `IXLandLinuxRuntimeSystemTests/Guest/Execution/GuestBusyboxRuntimeExecutionTests.m` |
| Memory/SpecialAndTagging | `LD64B`, `ST64B*`, `LDG`, `LDGM`, `STG`, `STGM`, `STZG`, `STZGM`, `ST2G`, `STZ2G`, `LDRAA`, `LDRAB` | unsupported | present | present | present | n/a | n/a | `TCTI/ISAFamilies/Memory/TCTISpecialAndTaggingSemanticTests.m`, `TCTI/Pipeline/Decode/TCTIPipelineDecodeTests.m`, `TCTI/Pipeline/Lowering/TCTIPipelineLoweringTests.m` |
| System/ExceptionsBarriersHints | `SVC`, `HVC`, `SMC`, `BRK`, `HLT`, `ERET`, `ERETAA`, `ERETAB`, `DMB`, `DSB`, `ISB`, `CSDB`, `CLREX`, `SB`, `SSBB`, `PSSBB`, `PSB`, `TSB`, `NOP`, `YIELD`, `WFE`, `WFET`, `WFI`, `WFIT`, `SEV`, `SEVL`, `HINT`, `DRPS`, `ESB`, `DGH`, `BTI` | green | present | present | present | present | n/a | `TCTI/ISAFamilies/System/TCTIExceptionsBarriersHintsSemanticTests.m`, `TCTI/Pipeline/Lowering/TCTIPipelineLoweringTests.m`, `IXLandLinuxRuntimeSystemTests/Guest/Execution/GuestBusyboxRuntimeExecutionTests.m` |
| System/SysregCacheTLB | `MRS`, `MSR`, `MRRS`, `MSRR`, `SYS`, `SYSL`, `SYSP`, `DC`, `IC`, `TLBI`, `TLBIP`, `CHKFEAT`, `SETF8`, `SETF16`, `XAFLAG` | green | present | present | present | present | n/a | `TCTI/ISAFamilies/System/TCTISysregCacheTLBSemanticTests.m`, `TCTI/Pipeline/Lowering/TCTIPipelineLoweringTests.m`, `IXLandLinuxRuntimeSystemTests/Guest/ProcessABI/GuestProcessABITests.m` |
| SIMDFP/ScalarFP | `FMOV`, `FADD`, `FSUB`, `FMUL`, `FDIV`, `FMADD`, `FMSUB`, `FNMADD`, `FNMSUB`, `FNEG`, `FABS`, `FCMP`, `FCMPE`, `FCCMP`, `FCCMPE`, `FCSEL`, `FCVT*`, `FRINT*`, `FSQRT`, `FRECPE`, `FRECPS`, `FRECPX`, `FRSQRTE`, `FRSQRTS`, `FSCALE`, `FJCVTZS` | green | present | present | present | n/a | n/a | `TCTI/ISAFamilies/SIMDFP/TCTIScalarFPSemanticTests.m`, `TCTI/Pipeline/Lowering/TCTIPipelineLoweringTests.m` |
| SIMDFP/VectorIntegerLogical | `DUP`, `INS`, `MOVI`, `MVNI`, `AND`, `ORR`, `ORN`, `EOR`, `BIC`, `BIF`, `BIT`, `BSL`, `CNT`, `CMEQ`, `CMGE`, `CMGT`, `CMHI`, `CMHS`, `CMLE`, `CMLT`, `CMTST`, `ADD*`, `SUB*`, `MUL`, `MLA`, `MLS`, `EXT`, `TBL`, `TBX`, `ZIP*`, `UZP*`, `TRN*`, `XTN*` | red | present | present | present | indirect | n/a | `TCTI/ISAFamilies/SIMDFP/TCTIVectorSemanticTests.m`, `TCTI/Pipeline/Decode/TCTIPipelineDecodeTests.m`, `TCTI/Pipeline/Lowering/TCTIPipelineLoweringTests.m`, `TCTI/Reality/TCTIFamilyGapInventoryTests.m`, `IXLandLinuxRuntimeSystemTests/Guest/Execution/GuestBusyboxRuntimeExecutionTests.m` |
| SIMDFP/VectorMemory | `LD1*`, `LD2*`, `LD3*`, `LD4*`, `ST1*`, `ST2*`, `ST3*`, `ST4*`, SIMD `LDR`, `LDUR`, `LDP`, `STR`, `STUR`, `STP` | green | present | present | present | n/a | n/a | `TCTI/ISAFamilies/SIMDFP/TCTIVectorMemorySemanticTests.m`, `TCTI/Pipeline/Lowering/TCTIPipelineLoweringTests.m` |
| SIMDFP/CryptoAndDotProduct | `AES*`, `SHA*`, `PMUL`, `PMULL*`, `SDOT`, `UDOT`, `USDOT`, `SUDOT`, `FDOT`, `BFDOT`, `SM3*`, `SM4*`, `BCAX`, `XAR` | unsupported | present | present | present | n/a | n/a | `TCTI/ISAFamilies/SIMDFP/TCTICryptoAndDotProductSemanticTests.m`, `TCTI/Pipeline/Decode/TCTIPipelineDecodeTests.m`, `TCTI/Pipeline/Lowering/TCTIPipelineLoweringTests.m` |
| Arm64e/PAuthAndAuthenticatedControl | `PAC*`, `AUT*`, `XPAC*`, `BRAA*`, `BRAB*`, `BLRAA*`, `BLRAB*`, `RETAA`, `RETAB`, `RETAASPPC`, `RETABSPPC`, `LDRAA`, `LDRAB`, `BTI` | unsupported | present | present | present | n/a | n/a | `TCTI/ISAFamilies/Arm64e/TCTIPAuthSemanticTests.m`, `TCTI/Pipeline/Decode/TCTIPipelineDecodeTests.m`, `TCTI/Pipeline/Lowering/TCTIPipelineLoweringTests.m` |
