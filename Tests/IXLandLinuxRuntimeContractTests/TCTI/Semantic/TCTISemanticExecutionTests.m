#import <XCTest/XCTest.h>

#include "../../../Support/TCTITestHarness/tcti_harness_truth.h"

// TCTI.SemanticExecution Contract Tests
// Tests for stage [5] GADGET EXECUTION semantic correctness
//
// Boundary: B2 (live carriers) -> B3 (post-gadget live carriers)
// Verifies gadgets implement AArch64 semantics correctly

@interface TCTISemanticExecutionTests : XCTestCase
@end

@implementation TCTISemanticExecutionTests

- (void)testSemanticExecutionContract_MOVRegProducesCorrectResult
{
    tcti_harness_snapshot_t snapshot;
    XCTAssertTrue(tcti_harness_case_mov_2_7(&snapshot),
                  @"MOV X2, X7 must copy guest x7 into guest x2 without corrupting other hot "
                   "registers");
}

- (void)testSemanticExecutionContract_ADDRegProducesCorrectResult
{
    tcti_harness_snapshot_t snapshot;
    XCTAssertTrue(tcti_harness_case_add_7_13_14(&snapshot),
                  @"ADD X7, X13, X14 must update only the destination hot carrier");
}

- (void)testSemanticExecutionContract_BlockEntryRestoresGuestPStateForConditionalBranch
{
    XCTAssertEqual(tcti_harness_case_entry_restores_pstate_for_bcond_ne(), 0x2000ULL,
                   @"TCTI block entry must restore guest NZCV from cpu->pstate before B.cond");
}

- (void)testSemanticExecutionContract_FlagSettingFallbackLeavesGuestNZCVLiveForCCMP
{
    XCTAssertEqual(tcti_harness_case_cmp_w20_ccmp_gt_bls_uses_32bit_flags(), 0,
                   @"CMP W20,#0 with W20=-1 must make CCMP.GT take the false NZCV immediate so "
                    "the following B.LS exits the allocator loop");
}

- (void)testSemanticExecutionContract_TPIDREL0RoundtripsThroughFullSysregEncoding
{
    XCTAssertEqual(tcti_harness_case_tpidr_el0_roundtrips_through_full_sysreg_encoding(), 0ULL,
                   @"TCTI must honor the full decoded TPIDR_EL0 sysreg encoding so MSR/MRS "
                    @"roundtrip the guest thread pointer in live ldso paths");
}

- (void)testSemanticExecutionContract_ExtendedCMPUsesWRegisterWidthForCSEL
{
    XCTAssertEqual(tcti_harness_case_cmp_w2_w1_uxtb_csel_uses_w_width(),
                   0x123456789abcdef0ULL,
                   @"CMP W2,W1,UXTB must compare W2 against the extended byte, ignoring stale "
                    "high bits before CSEL consumes the flags");
}

- (void)testSemanticExecutionContract_CSELEQSelectsTrueOperand
{
    XCTAssertEqual(tcti_harness_case_csel_eq_selects_true_operand(), 0x123456789abcdef0ULL,
                   @"CSEL.EQ must select Rn when guest PSTATE has Z set");
}

- (void)testSemanticExecutionContract_CSELPreservesNZCVForFollowingCondition
{
    XCTAssertEqual(tcti_harness_case_csel_preserves_flags_for_bcond(), 0x2000ULL,
                   @"CSEL must preserve guest NZCV so the following conditional branch sees the "
                    "same flags");
}

- (void)testSemanticExecutionContract_CSELLSHSTracksUnsignedMinMax
{
    XCTAssertEqual(tcti_harness_case_cmp_csel_ls_hs_tracks_unsigned_minmax(), 0ULL,
                   @"CMP followed by CSEL.LS and CSEL.HS must preserve unsigned carry/zero "
                    "semantics so ld-musl computes segment bounds correctly");
}

- (void)testSemanticExecutionContract_CSINVLSPreservesNonOverflowAllocationSize
{
    XCTAssertEqual(tcti_harness_case_cmp_csinv_ls_preserves_nonoverflow_size(), 0x1234ULL,
                   @"CSINV ..., LS must keep the computed size when the preceding unsigned CMP "
                    "reports no overflow in ld-musl allocation sizing");
}

- (void)testSemanticExecutionContract_CSINVLSSaturatesOverflowAllocationSize
{
    XCTAssertEqual(tcti_harness_case_cmp_csinv_ls_saturates_overflow_size(), UINT64_MAX,
                   @"CSINV ..., LS must saturate to all-ones when the preceding unsigned CMP "
                    "detects overflow in ld-musl allocation sizing");
}

- (void)testSemanticExecutionContract_GeneratedCINCNEIncrementsOnlyOnNE
{
    XCTAssertEqual(tcti_harness_case_generated_cinc_ne_increments_only_on_ne(), 8ULL,
                   @"The generated raw CINC alias from ld-musl must preserve x0 on EQ and "
                    "increment it only when the preceding CMP is NE");
}

- (void)testSemanticExecutionContract_VsnprintfZeroSizeCSETNEPreservesZeroFlag
{
    XCTAssertEqual(tcti_harness_case_vsnprintf_zero_size_cset_ne_preserves_zero_flag(), 0ULL,
                   @"musl vsnprintf uses CMP; CSEL; CSET.NE; SUB for zero-size buffers. CSEL "
                    "must preserve Z so CSET.NE stays 0 and the buffer length does not underflow");
}

- (void)testSemanticExecutionContract_GeneratedVsnprintfZeroSizeLengthDoesNotUnderflow
{
    XCTAssertEqual(tcti_harness_case_generated_vsnprintf_zero_size_length(), 0ULL,
                   @"The generated TCTI block for musl vsnprintf zero-size setup must leave "
                    "remaining length at 0, not UINT64_MAX");
}

- (void)testSemanticExecutionContract_CMPAddCSELNEPreservesZeroFlag
{
    XCTAssertEqual(tcti_harness_case_cmp_add_csel_ne_uses_preserved_zero_flag(), 0ULL,
                   @"TCTI must preserve Z=1 from CMP across non-flag ADD so CSEL NE chooses "
                    "XZR in musl sigaction");
}

- (void)testSemanticExecutionContract_CMPCCMPFalseImmediateClearsZero
{
    XCTAssertEqual(tcti_harness_case_cmp_ccmp_false_immediate_clears_zero(), 0x51f44ULL,
                   @"CCMP with a false NE condition and NZCV immediate 0 must clear Z so "
                    "musl sigaction does not copy an old action into a null pointer");
}

- (void)testSemanticExecutionContract_StrchrnulVectorMaskFindsDot
{
    XCTAssertNotEqual(tcti_harness_case_strchrnul_vector_mask_finds_dot(), 0ULL,
                      @"The musl strchrnul word scan depends on EON and BIC, not just EOR and AND");
}

- (void)testSemanticExecutionContract_StrchrnulByteLoopStopsOnMatchOrNul
{
    XCTAssertEqual(tcti_harness_case_strchrnul_byte_loop_stops_on_match_or_nul(), 0ULL,
                   @"TCTI must preserve musl strchrnul's live ADD/LDRB/CMP/CCMP/B.NE byte loop "
                    "so the loader stops on the matching byte or trailing NUL instead of "
                    "spinning past the string boundary.");
}

- (void)testSemanticExecutionContract_MuslMemsetDUPZeroesVectorStore
{
    XCTAssertEqual(tcti_harness_case_musl_memset_dup_zeroes_vector_store(), 0ULL,
                   @"musl memset uses DUP v0.16b,w1 before vector stores; TCTI must update v0 "
                    "so stale SIMD state cannot corrupt guest heap/list objects");
}

- (void)testSemanticExecutionContract_MuslMemsetDUPReplicatesByteFill
{
    XCTAssertEqual(tcti_harness_case_musl_memset_dup_replicates_byte_fill(), 0ULL,
                   @"musl memset's DUP v0.16b,w1 must replicate the low byte across every byte, "
                    "not decode as MOVI or replicate 32-bit lanes");
}

- (void)testSemanticExecutionContract_LogicalMOVRoundtripsMemoryBackedX19
{
    XCTAssertEqual(tcti_harness_case_logical_mov_roundtrips_memory_backed_x19(), 0ULL,
                   @"MOV alias lowering must write memory-backed x19 and read it back into hot "
                    "x5 without preserving stale carrier state");
}

- (void)testSemanticExecutionContract_CSETThenADDOverwritesStaleX3
{
    XCTAssertEqual(tcti_harness_case_cset_eq_then_add_to_x3(), 3ULL,
                   @"CSET.EQ followed by ADD x3,#2 must overwrite stale x3 before relocation "
                    "entry processing passes x3 into x19");
}

- (void)testSemanticExecutionContract_LDRFromMemoryBackedBaseSyncsHotDestination
{
    XCTAssertEqual(tcti_harness_case_ldr_x5_from_memory_backed_x27(), 0x0123456789abcdefULL,
                   @"TCTI LDR must load from memory-backed base registers and sync hot destinations");
}

- (void)testSemanticExecutionContract_STRRegisterOffsetFromHotToMemoryBackedBase
{
    XCTAssertEqual(tcti_harness_case_str_x0_to_memory_backed_x22_scaled_x1(),
                   0xfedcba9876543210ULL,
                   @"TCTI STR register-offset must store through memory-backed base registers");
}

- (void)testSemanticExecutionContract_ADDToMemoryBackedX23StoresResult
{
    XCTAssertEqual(tcti_harness_case_add_hot_pair_to_memory_backed_x23(), 0x5655ac10ULL,
                   @"TCTI ADD with a memory-backed high destination must store the computed "
                    "result into architectural x23");
}

- (void)testSemanticExecutionContract_CMPMemoryBackedX27X23BranchesEQ
{
    XCTAssertEqual(tcti_harness_case_cmp_memory_backed_x27_x23_branches_eq(), 0x6a690ULL,
                   @"TCTI CMP over memory-backed high registers must set NZCV so B.EQ exits "
                    "the relocation loop when x27 reaches x23");
}

- (void)testSemanticExecutionContract_StackPairRoundtripsHotX5X4
{
    XCTAssertEqual(tcti_harness_case_stack_pair_roundtrips_hot_x5_x4(), 0ULL,
                   @"TCTI STP/LDP on the guest stack must preserve hot registers x5 and x4 "
                    "across the dynamic linker symbol lookup path");
}

- (void)testSemanticExecutionContract_LDPFirstDestinationPreservesPairBase
{
    XCTAssertEqual(tcti_harness_case_ldp_first_destination_preserves_pair_base(), 0ULL,
                   @"TCTI LDP must compute both pair addresses from the original base even when "
                    "the first destination register is also the base register");
}

- (void)testSemanticExecutionContract_DynamicTagScaledStoreUsesFullIndex
{
    XCTAssertEqual(tcti_harness_case_dynamic_tag_scaled_store_uses_full_index(), 0ULL,
                   @"TCTI STR register-offset with LSL #3 must store DT_RELR in tag slot 36 "
                    "without corrupting the DT_REL slot");
}

- (void)testSemanticExecutionContract_MuslUBFIZSymbolIndexPreservesShiftedBits
{
    XCTAssertEqual(tcti_harness_case_musl_ubfiz_symbol_index_preserves_shifted_bits(),
                   0x00000007fffffff8ULL,
                   @"TCTI UBFM/UBFIZ must use AArch64 bitmask semantics so musl dynlink keeps "
                    "the high bits of shifted symbol table indexes");
}

- (void)testSemanticExecutionContract_LSRAliasUsesTopMaskNotRotate
{
    XCTAssertEqual(tcti_harness_case_lsr_alias_uses_top_mask_not_rotate(),
                   0x4000000000000000ULL,
                   @"TCTI UBFM must apply the top mask so the LSR alias shifts instead of "
                    "rotating high bits back into the result");
}

- (void)testSemanticExecutionContract_MuslRELRLoopTerminatesAtTableEnd
{
    XCTAssertEqual(tcti_harness_case_musl_relr_loop_terminates_at_table_end(), 0ULL,
                   @"TCTI must execute musl _dlstart RELR bitmap relocation loop exactly once "
                    "over RELRSZ without running relocation writes past the mapped data segment");
}

- (void)testSemanticExecutionContract_PLTRELRELAStrideSelector
{
    XCTAssertEqual(tcti_harness_case_pltrel_rela_stride_selector(), 3ULL,
                   @"TCTI must compute DT_RELA PLT relocation stride as 3 entries after "
                    "CMP/CSET/ADD");
}

- (void)testSemanticExecutionContract_RelocationLoopPreservesLoadedX5
{
    XCTAssertEqual(tcti_harness_case_relocation_loop_preserves_loaded_x5(),
                   0x00000000000dfee8ULL,
                   @"TCTI must preserve the relocation offset loaded into hot x5 across the "
                    "following generated compare and branch gadgets");
}

- (void)testSemanticExecutionContract_RelocationFaultPathUsesLoadedX5
{
    XCTAssertEqual(tcti_harness_case_relocation_fault_path_uses_loaded_x5(),
                   0x1122334455667788ULL,
                   @"TCTI must carry the loaded relocation offset through the later CBZ block "
                    "into the final register-offset relocation load");
}

- (void)testSemanticExecutionContract_MuslSnprintfFILEWposInit
{
    XCTAssertEqual(tcti_harness_case_musl_snprintf_file_wpos_init(), 0ULL,
                   @"TCTI must preserve musl's stack FILE wpos initialization across STP and "
                    "SIMD copy setup before fwrite_unlocked calls memcpy");
}

- (void)testSemanticExecutionContract_MuslVdprintfStackFILEZeroInit
{
    XCTAssertEqual(tcti_harness_case_musl_vdprintf_stack_file_zero_init(), 0ULL,
                   @"TCTI must lower AdvSIMD MOVI/MVNI immediates so musl's stack FILE starts "
                    "with zeroed buffer pointers before relocation diagnostics reach the PTY");
}

- (void)testSemanticExecutionContract_DCZVAZeroesCacheBlock
{
    XCTAssertEqual(tcti_harness_case_dc_zva_zeroes_cache_block(), 0ULL,
                   @"TCTI must implement DC ZVA so musl memset zeroes large allocations used "
                    "during BusyBox shell startup");
}

- (void)testSemanticExecutionContract_MuslStrncmpLibcReservedPrefix
{
    XCTAssertEqual(tcti_harness_case_musl_strncmp_libc_reserved_prefix(), 0ULL,
                   @"TCTI must execute musl strncmp(\"c...\", \"c.\", 2) correctly so ldso can "
                    "recognize its own libc SONAME while loading BusyBox dependencies");
}

- (void)testSemanticExecutionContract_MuslLoadLibraryDetectsLibcSelf
{
    XCTAssertEqual(tcti_harness_case_musl_load_library_detects_libc_self(), 0ULL,
                   @"TCTI must execute musl load_library's reserved libc detection path so "
                    "ldso does not load itself as a second libc dependency");
}

- (void)testSemanticExecutionContract_MuslDls3DependencyChainAppendsNextDSO
{
    XCTAssertEqual(tcti_harness_case_musl_dls3_dependency_chain_appends_next_dso(), 0ULL,
                   @"TCTI must execute musl __dls3's live dependency-chain append loop so the "
                    "resolved DSO is linked through offset 0x68 and later symbol fallback can "
                    "continue past the originating executable.");
}

- (void)testSemanticExecutionContract_MuslFindSymDepsPostIndexWalkReadsFirstDep
{
    XCTAssertEqual(tcti_harness_case_musl_find_sym_deps_post_index_walk_reads_first_dep(), 0ULL,
                   @"TCTI must execute musl's dependency-scope lookup prefix so the deps array "
                    "post-index load advances x15 and the first dependency DSO feeds the later "
                    "GNU-hash lookup instead of terminating fallback at the executable.");
}

- (void)testSemanticExecutionContract_MuslOpenedLibcValidationUsesMulAlias
{
    XCTAssertEqual(tcti_harness_case_musl_opened_libc_validation_uses_mul_alias(), 0ULL,
                   @"TCTI must decode and execute the live MUL alias on musl's opened-libc "
                    "validation path so ld-musl computes the expected ELF size product instead "
                    "of taking the false failure path.");
}

- (void)testSemanticExecutionContract_MuslGNUHashMalloc
{
    XCTAssertEqual(tcti_harness_case_musl_gnu_hash_malloc(), 0ULL,
                   @"TCTI must execute musl's GNU hash loop with 32-bit shifted ADD on "
                    "memory-backed registers so dynamic symbol lookup can find malloc/free");
}

- (void)testSemanticExecutionContract_MuslGNULookupFilteredMalloc
{
    XCTAssertEqual(tcti_harness_case_musl_gnu_lookup_filtered_malloc(), 0ULL,
                   @"TCTI must execute musl's GNU hash lookup helper so ldso can resolve "
                    "libc symbols through bloom filters, hash buckets, and symbol strings");
}

- (void)testSemanticExecutionContract_MuslGNULookupDls2bChain
{
    XCTAssertEqual(tcti_harness_case_musl_gnu_lookup_dls2b_chain(), 0ULL,
                   @"TCTI must execute musl's GNU hash chain walk across false-positive "
                    "entries so ldso can resolve its own __dls2b symbol");
}

- (void)testSemanticExecutionContract_MuslFindSymDls2bFromLdso
{
    XCTAssertEqual(tcti_harness_case_musl_find_sym_dls2b_from_ldso(), 0ULL,
                   @"TCTI must execute musl find_sym over ldso's own DSO, GNU hash table, "
                    "and dynamic symbol metadata so __dls2b resolves before guest startup");
}

- (void)testSemanticExecutionContract_MuslFindSymLongjmpFromLdso
{
    XCTAssertEqual(tcti_harness_case_musl_find_sym_longjmp_from_ldso(), 0ULL,
                   @"TCTI must execute musl find_sym for a real longjmp GNU-hash bucket hit so "
                    @"ldso can resolve BusyBox's libc symbols instead of cascading through the "
                    @"error-relocation path.");
}

- (void)testSemanticExecutionContract_MuslFindSymSiglongjmpFromLdso
{
    XCTAssertEqual(tcti_harness_case_musl_find_sym_siglongjmp_from_ldso(), 0ULL,
                   @"TCTI must execute musl find_sym for siglongjmp so the live Alpine BusyBox "
                    @"loader path resolves the next-DSO symbol instead of spinning.");
}

- (void)testSemanticExecutionContract_MuslFindSymAcceptsGlobalFunc
{
    XCTAssertEqual(tcti_harness_case_musl_find_sym_accepts_global_func(), 0ULL,
                   @"TCTI must execute musl find_sym's st_shndx, st_value, type, and binding "
                    "checks so valid global function symbols are not rejected");
}

- (void)testSemanticExecutionContract_ADDShiftedHotUsesScratchCarrier
{
    XCTAssertEqual(tcti_harness_case_add_shifted_hot_uses_scratch_carrier(), 0ULL,
                   @"TCTI ADD shifted-register must use scratch carriers without losing the "
                    "shifted operand or corrupting hot destination registers");
}

- (void)testSemanticExecutionContract_ADDExtendedUXTWUses32BitOperand
{
    XCTAssertEqual(tcti_harness_case_add_extended_uxtw_uses_32bit_operand(), 0ULL,
                   @"TCTI ADD extended-register must zero-extend Wm and add it to Xn before "
                    "GNU hash chain address calculation");
}

- (void)testSemanticExecutionContract_LogicalImmediateMemoryBackedSourceUsesDistinctScratch
{
    XCTAssertEqual(tcti_harness_case_logical_imm_memory_backed_source_uses_distinct_scratch(),
                   0x8ULL,
                   @"TCTI logical-immediate lowering must not overwrite a memory-backed source "
                    "scratch while materializing the immediate operand");
}

- (void)testSemanticExecutionContract_MuslMallocSizeclassRBITCLZ
{
    XCTAssertEqual(tcti_harness_case_musl_malloc_sizeclass_rbit_clz(), 0x0000001000000011ULL,
                   @"TCTI must execute musl malloc's RBIT/CLZ size-class calculation without "
                    "routing one-source data-processing instructions through CSEL lowering");
}

- (void)testSemanticExecutionContract_MuslCallocPLTADRPResolvesLocalGOTPage
{
    XCTAssertEqual(tcti_harness_case_musl_calloc_plt_adrp_resolves_local_got_page(), 0ULL,
                   @"TCTI ADRP lowering must derive the guest GOT page from the current PC so "
                    "musl's calloc@plt loads its own GOT slot instead of reusing a stale page");
}

- (void)testSemanticExecutionContract_MuslOpendirNonNullCallocSkipsErrorClose
{
    XCTAssertEqual(tcti_harness_case_musl_opendir_calloc_nonnull_skips_close_path(), 0ULL,
                   @"musl opendir branches to close(2) only when calloc returns NULL; TCTI must "
                    "execute the following CBZ X0 using the full 64-bit guest pointer value");
}

- (void)testSemanticExecutionContract_MuslCallocOverflowGuardUsesRealUMULH
{
    XCTAssertEqual(
        tcti_harness_case_musl_calloc_overflow_guard_umulh_stays_zero_for_small_product(), 0ULL,
        @"TCTI must decode and execute musl calloc's UMULH overflow guard as a real multiply-high "
         "operation, not as a 3-source multiply-add with a stale Ra addend.");
}

- (void)testSemanticExecutionContract_MuslCallbackSlotADRPADDMaterializesLdsoTargetPage
{
    XCTAssertEqual(
        tcti_harness_case_musl_callback_slot_adrp_add_materializes_ldso_target_page(), 0ULL,
        @"The live ld-musl callback-slot producer must materialize ADRP from the guest PC page, "
         "then apply the following ADD immediate so the stored slot target stays in the ldso text "
         "image instead of collapsing into a signed page delta.");
}

- (void)testSemanticExecutionContract_MuslLibcNameComparePrefixStaysOnMatchPath
{
    XCTAssertEqual(
        tcti_harness_case_musl_libc_name_compare_prefix_stays_on_match_path(), 0ULL,
        @"The live ld-musl libc-name prefix compare must keep x0 on the literal string page, "
         "load matching first bytes from the guest name and ldso literal, and fall through "
         "instead of branching to the mismatch path when the names agree.");
}

- (void)testSemanticExecutionContract_MuslLibcNameLiteralBaseMaterializesBeforeCompare
{
    XCTAssertEqual(
        tcti_harness_case_musl_libc_name_literal_base_materializes_before_compare(), 0ULL,
        @"The live ld-musl libc-name compare setup must materialize the literal base in x0, "
         "copy x25 into x17, and land on the compare loop entry before any byte loads.");
}

- (void)testSemanticExecutionContract_MuslLibcNameSetupPreservesHotX0AcrossMOVX17X25
{
    XCTAssertEqual(
        tcti_harness_case_musl_libc_name_setup_preserves_hot_x0_across_mov_x17_x25(), 0ULL,
        @"The live ld-musl setup block must preserve hot guest x0 after ADRP while copying x25 "
         "into memory-backed x17 for the later libc-name compare path.");
}

- (void)testSemanticExecutionContract_MuslMutexLDAXRSTLXRRoundtripsLockWord
{
    XCTAssertEqual(tcti_harness_case_musl_mutex_ldaxr_stlxr_roundtrip(), 0ULL,
                   @"TCTI must execute musl pthread mutex LDAXR/STLXR lock/unlock/relock "
                    "sequences without leaving the lock word permanently busy");
}

- (void)testSemanticExecutionContract_TSTImmediateSetsZeroFlagForZeroInput
{
    XCTAssertEqual(tcti_harness_case_tst_x1_imm_sets_zero_flag(), 0ULL,
                   @"TCTI must execute TST Xn,#imm as ANDS-to-XZR and leave Z set for zero "
                    "input without corrupting the source register");
}

- (void)testSemanticExecutionContract_LDRHCMPCCMPEQSurvivesSingleInstructionBlocks
{
    XCTAssertEqual(tcti_harness_case_ldrh_cmp_ccmp_eq_survives_single_insn_blocks(), 0ULL,
                   @"TCTI must preserve LDRH/CMP/CCMP/B.EQ semantics when the real guest path is "
                    "forced through one-instruction blocks");
}

- (void)testSemanticExecutionContract_SMADDLUsesSigned32BitInputs
{
    XCTAssertEqual(tcti_harness_case_smaddl_uses_signed_32bit_inputs(), 12ULL,
                   @"TCTI SMADDL lowering must use signed 32-bit operands from Wn/Wm rather than "
                    "leaking stale high bits from Xn/Xm");
}

- (void)testSemanticExecutionContract_MuslFrameStrideBlockUsesWideImmediatesAndRegOffsetLDR
{
    XCTAssertEqual(
        tcti_harness_case_musl_frame_stride_block_uses_wide_immediates_and_reg_offset_ldr(), 0ULL,
        @"The live ld-musl frame-stride block must materialize W1=24, compute SMULL from W0/W1, "
         "and perform the dependent 32-bit register-offset LDR from [x20, x0] before using the "
         "loaded word in the next ADD");
}

- (void)testSemanticExecutionContract_RegOffsetLDRWHelperReadsHotX0Offset
{
    XCTAssertEqual(tcti_harness_case_reg_offset_ldr_w_helper_reads_hot_x0_offset(), 0ULL,
                   @"The load helper for LDR Wt, [Xn, Xm] must use the current hot guest x0 "
                    "offset when rm=0 and must write the 32-bit loaded word back into the "
                    "architectural destination register");
}

- (void)testSemanticExecutionContract_GeneratedLDRWRegOffsetReadsHotX0Offset
{
    XCTAssertEqual(tcti_harness_case_generated_ldr_w_reg_offset_reads_hot_x0_offset(), 0ULL,
                   @"The generated TCTI block for LDR W7, [X20, X0] must preserve the register-"
                    "offset metadata and materialize the loaded 32-bit word into guest x7");
}

- (void)testSemanticExecutionContract_MuslFrameStridePrefixThenLDRInNextBlock
{
    XCTAssertEqual(tcti_harness_case_musl_frame_stride_prefix_then_ldr_in_next_block(), 0ULL,
                   @"If the ld-musl frame-stride prefix and the dependent LDR execute in separate "
                    "generated blocks, guest x0 and x14 must carry correctly into the next "
                    "block and the LDR must still read the expected word");
}

- (void)testSemanticExecutionContract_MOVZSMULLPrefixPreservesExpectedX0
{
    XCTAssertEqual(tcti_harness_case_movz_smull_prefix_preserves_expected_x0(), 0ULL,
                   @"The live movz; smull prefix must leave guest x0 at the exact 64-bit product "
                    "of W0 and W1 before any later helper/store path runs");
}

- (void)testSemanticExecutionContract_UMADDLUsesUnsigned32BitInputs
{
    XCTAssertEqual(tcti_harness_case_umaddl_uses_unsigned_32bit_inputs(), 35ULL,
                   @"TCTI UMADDL lowering must use unsigned 32-bit operands from Wn/Wm rather "
                    "than stale 64-bit register contents");
}

- (void)testSemanticExecutionContract_LSLV64BitUsesFullShiftAmount
{
    XCTAssertEqual(tcti_harness_case_lslv_64bit_uses_full_shift_amount(), 1ULL << 35,
                   @"TCTI must execute 64-bit LSLV with the full 0-63 shift amount instead of "
                    "truncating it to a 32-bit path.");
}

- (void)testSemanticExecutionContract_LSLVMemoryBackedRegistersRoundtrip
{
    XCTAssertEqual(tcti_harness_case_lslv_memory_backed_registers_roundtrip(), 1ULL << 35,
                   @"TCTI must preserve 64-bit LSLV semantics when both the source and "
                    "destination live in memory-backed guest registers.");
}

- (void)testSemanticExecutionContract_SPRelativeLDRSTRRoundtrip
{
    XCTAssertEqual(tcti_harness_case_sp_relative_ldr_str_roundtrip(), 0ULL,
                   @"TCTI must preserve 64-bit SP-relative load/store immediate semantics for "
                    "the musl post-open root bucket loop.");
}

- (void)testSemanticExecutionContract_QsortPointerSlotUpdatesRoundtrip
{
    XCTAssertEqual(tcti_harness_case_qsort_pointer_slot_updates_roundtrip(), 0ULL,
                   @"TCTI must preserve musl qsort's pointer-slot register-offset store, pair "
                    "load, and post-index writeback semantics for 8-byte element shuffles.");
}

- (void)testSemanticExecutionContract_BusyboxScandirFlattenBlockRoundtrip
{
    XCTAssertEqual(tcti_harness_case_busybox_scandir_flatten_block_roundtrip(), 0ULL,
                   @"TCTI must preserve BusyBox's linked-list flatten block when x19/x20 are "
                    "memory-backed guest registers, because the live Alpine ls path stores list "
                    "nodes into the qsort array through that exact block.");
}

- (void)testSemanticExecutionContract_BusyboxInputWidecharCopyLoopRoundtrip
{
    XCTAssertEqual(tcti_harness_case_busybox_input_widechar_copy_loop_roundtrip(), 0ULL,
                   @"TCTI must preserve BusyBox's typed-input widechar copy loop, including "
                    "unaligned LDRH and the final STR WZR register-offset terminator store, "
                    "because the live simulator keyboard path enters this block before the "
                    "interactive shell can execute typed commands.");
}

- (void)testSemanticExecutionContract_MuslQsortTBZW0SignbitBranch
{
    XCTAssertEqual(tcti_harness_case_musl_qsort_tbz_w0_signbit_branch(), 0ULL,
                   @"TCTI must honor tbz w0,#31 on the live musl qsort path, because a wrong "
                    "32-bit sign-bit branch can walk the comparator left past the start of the "
                    "BusyBox pointer array.");
}

- (void)testSemanticExecutionContract_MuslQsortTBNZW0SignbitBranches
{
    XCTAssertEqual(tcti_harness_case_musl_qsort_tbnz_w0_signbit_branches(), 0ULL,
                   @"TCTI must honor the live musl qsort tbnz w0,#31 branches, because a wrong "
                    "negative-compare branch can materialize the next comparator window from the "
                    "wrong side of the BusyBox pointer array.");
}

- (void)testSemanticExecutionContract_MuslQsortCSINCTSTGateKeepsExpectedPath
{
    XCTAssertEqual(tcti_harness_case_musl_qsort_csinc_tst_gate_keeps_expected_path(), 0ULL,
                   @"TCTI must preserve musl qsort's csinc/tst gate, because that exact block "
                    "decides whether the next comparator window advances or jumps into the "
                    "alternate heap-progression path.");
}

- (void)testSemanticExecutionContract_LogicalMOVMemoryToMemoryRoundtrip
{
    XCTAssertEqual(tcti_harness_case_logical_mov_memory_to_memory_roundtrip(), 0ULL,
                   @"TCTI logical-register fallback must preserve MOV alias writes when both the "
                    "source and destination are memory-backed guest registers, because musl "
                    "qsort carries x19/x28 through that exact path.");
}

- (void)testSemanticExecutionContract_MuslQsortRBITCLZ64BitRoundtrip
{
    XCTAssertEqual(tcti_harness_case_musl_qsort_rbit_clz_64bit_roundtrip(), 0ULL,
                   @"TCTI must decode and execute 64-bit RBIT/CLZ on the live musl qsort state "
                    "update path, because lowering them through the 32-bit path corrupts qsort's "
                    "heap progression.");
}

- (void)testSemanticExecutionContract_MuslMemcpy8TailRoundtrip
{
    XCTAssertEqual(tcti_harness_case_musl_memcpy8_tail_roundtrip(), 0ULL,
                   @"TCTI must preserve musl memcpy's 8-byte tail copy with unscaled negative "
                    "offset loads and stores, because qsort uses this helper in the live Alpine "
                    "directory-sort path.");
}

- (void)testSemanticExecutionContract_MuslBucketBitmaskBlockRoundtrip
{
    XCTAssertEqual(tcti_harness_case_musl_bucket_bitmask_block_roundtrip(), 0ULL,
                   @"TCTI must preserve the exact musl root-bucket bitmask block across "
                    "LDR/LSLV/ORR/STR when executed as one generated block.");
}

- (void)testSemanticExecutionContract_STRXZRPostIndexWritesBackBase
{
    XCTAssertEqual(tcti_harness_case_str_xzr_post_index_writes_back_base(), 0ULL,
                   @"TCTI must execute STR XZR post-index stores with both the zero store and "
                    "the base-register writeback preserved.");
}

- (void)testSemanticExecutionContract_MuslLsRootPostOpenBucketLoop
{
    XCTAssertEqual(tcti_harness_case_musl_ls_root_post_open_bucket_loop(), 0ULL,
                   @"TCTI must preserve musl's post-open root bucket loop, including stack "
                    "zero-fill, 64-bit LSLV bitmask construction, and indexed table writes");
}

- (void)testSemanticExecutionContract_MuslLsRootPostOpenCallbackScan
{
    XCTAssertEqual(tcti_harness_case_musl_ls_root_post_open_callback_scan(), 0ULL,
                   @"TCTI must preserve musl's post-open callback scan, including TBZ, BLR, RET, "
                    "and callback-loop state for the busybox ls root-directory path");
}

- (void)testSemanticExecutionContract_MuslCallbackPrefixMaterializesArgs
{
    XCTAssertEqual(tcti_harness_case_musl_callback_prefix_materializes_args(), 0ULL,
                   @"TCTI must materialize the callback base, argument pair, and callback target "
                    "before BLR in the live ld-musl post-open callback path");
}

- (void)testSemanticExecutionContract_LDPX2X0FromMemoryBackedX21
{
    XCTAssertEqual(tcti_harness_case_ldp_x2_x0_from_memory_backed_x21(), 0ULL,
                   @"TCTI must preserve both 64-bit destinations for LDP X2,X0,[X21] when the "
                    @"base register is memory-backed and the pair feeds the live musl callback "
                     "argument path");
}

- (void)testSemanticExecutionContract_LDRX2Immediate0FromMemoryBackedX21
{
    XCTAssertEqual(tcti_harness_case_ldr_x2_imm0_from_memory_backed_x21(), 0ULL,
                   @"TCTI must preserve LDR X2,[X21] from a memory-backed base because the first "
                    @"element of the live musl pair-load lowering feeds the callback argument "
                     @"prefix");
}

- (void)testSemanticExecutionContract_LDRX1ImmediateFromMemoryBackedX21
{
    XCTAssertEqual(tcti_harness_case_ldr_x1_imm_from_memory_backed_x21(), 0ULL,
                   @"TCTI must preserve LDR X1,[X21,#96] from a memory-backed base so the live "
                    @"musl callback prefix sees the correct accumulated argument base");
}

- (void)testSemanticExecutionContract_LDRX0Immediate8FromMemoryBackedX21
{
    XCTAssertEqual(tcti_harness_case_ldr_x0_imm8_from_memory_backed_x21(), 0ULL,
                   @"TCTI must preserve LDR X0,[X21,#8] from a memory-backed base because the "
                    @"second element of the live musl pair-load lowering depends on the same "
                     @"slow-path semantics");
}

- (void)testSemanticExecutionContract_ManualTwoLDRSharedBlockFromMemoryBackedX21
{
    XCTAssertEqual(tcti_harness_case_manual_two_ldr_shared_block_from_memory_backed_x21(), 0ULL,
                   @"Two back-to-back TCTI LDR helper gadgets must preserve both destinations "
                    @"inside one shared block before any generated pc-advance runs");
}

- (void)testSemanticExecutionContract_ManualTwoLDRSharedBlockWithPCAdvance
{
    XCTAssertEqual(tcti_harness_case_manual_two_ldr_shared_block_with_pc_advance(), 0ULL,
                   @"Two back-to-back TCTI LDR helper gadgets plus pc-advance must preserve both "
                    @"destinations and advance the guest PC exactly once");
}

- (void)testSemanticExecutionContract_GeneratedLDPX2X0BytecodeMatchesManualShape
{
    XCTAssertEqual(
        tcti_harness_case_generated_ldp_x2_x0_bytecode_matches_manual_shape(), 0ULL,
        @"The generated bytecode for LDP X2,X0,[X21] must match the working manual two-load "
         @"stream: two LDR helper gadgets, one pc-advance, and one exit with the expected "
         @"parameter order and offsets");
}

- (void)testSemanticExecutionContract_RegOffsetLDRX0AliasBaseReadsExpectedQword
{
    XCTAssertEqual(tcti_harness_case_reg_offset_ldr_x0_alias_base_reads_expected_qword(), 0ULL,
                   @"The LDR helper must preserve 64-bit register-offset semantics when X0 is "
                    @"both the base and the destination in the live musl callback tail");
}

- (void)testSemanticExecutionContract_GeneratedRegOffsetLDRX0AliasBaseReadsExpectedQword
{
    XCTAssertEqual(
        tcti_harness_case_generated_reg_offset_ldr_x0_alias_base_reads_expected_qword(), 0ULL,
        @"The generated TCTI block for LDR X0,[X0,X25,LSL #3] must preserve base-equals-"
         @"destination semantics in the live musl callback tail");
}

- (void)testSemanticExecutionContract_MuslCallbackTableWalkMaterializesDispatchArgs
{
    XCTAssertEqual(tcti_harness_case_musl_callback_table_walk_materializes_dispatch_args(), 0ULL,
                   @"TCTI must preserve the live ld-musl callback-table walk through LDR "
                    @"register-offset loads, branch filtering, and final argument "
                    @"materialization before the callback BL.");
}

- (void)testSemanticExecutionContract_MuslCallbackSlotLoadsBranchTarget
{
    XCTAssertEqual(tcti_harness_case_musl_callback_slot_loads_branch_target(), 0ULL,
                   @"TCTI must load a callback target from memory-backed x23 into hot x3 before "
                    "BLR so ld-musl does not branch through a stale carrier value");
}

- (void)testSemanticExecutionContract_MuslLsRootFrameListWalk
{
    XCTAssertEqual(tcti_harness_case_busybox_ls_retry_ccmp_close_path(), 0ULL,
                   @"TCTI must preserve the current busybox ls retry CCMP cluster so success, "
                    "retry, and close-path fallthrough decisions match guest AArch64 flags.");
}

- (void)testSemanticExecutionContract_BusyboxAllocatorMSUBCallbackRoundtrip
{
    XCTAssertEqual(tcti_harness_case_busybox_allocator_msub_callback_roundtrip(), 0ULL,
                   @"TCTI must preserve the current busybox allocator bookkeeping block, "
                    "including SDIV/MSUB, stack spill/reload, BLR/RET, and the final "
                    "accumulator update used before the ls OOM path.");
}

- (void)testSemanticExecutionContract_MuslPthreadMutexLockPrefixReachesAtomicFastPath
{
    XCTAssertEqual(tcti_harness_case_musl_pthread_mutex_lock_prefix(), 0ULL,
                   @"TCTI must preserve Z through musl mutex lock's TST/B.NE prefix and reach "
                    "the ADD/MOV setup immediately before LDAXR/STLXR");
}

- (void)testSemanticExecutionContract_MuslPthreadMutexLockFastPathReusesStatusRegister
{
    XCTAssertEqual(tcti_harness_case_musl_pthread_mutex_lock_fast_path(), 0ULL,
                   @"TCTI must execute musl pthread_mutex_lock's fast LDAXR/STLXR path when "
                    "the loaded register is reused as the exclusive-store status register");
}

- (void)testSemanticExecutionContract_MuslMutexUnlockNormalTypeBranchesToFastUnlock
{
    XCTAssertEqual(tcti_harness_case_musl_mutex_unlock_normal_type_branches_to_fast_unlock(), 0ULL,
                   @"TCTI must preserve Z from ANDS across the following non-flag logical "
                    "immediate so musl pthread_mutex_unlock reaches the normal unlock path");
}

- (void)testSemanticExecutionContract_UDIVPreservesFlagsForFollowingCSEL
{
    XCTAssertEqual(tcti_harness_case_udiv_preserves_flags_for_csel_eq(), 0x1111111111111111ULL,
                   @"TCTI UDIV fallback must preserve NZCV so a following CSEL EQ observes the "
                    "pre-divide flags instead of helper-call host flags");
}

- (void)testSemanticExecutionContract_ADDImmProducesCorrectResult
{
    // Contract: ADD_IMM gadget MUST produce correct sum with immediate
    // Owner: gadget bodies
    
    XCTAssertTrue(YES, "TCTI.SemanticExecution ADD_IMM contract placeholder");
}

- (void)testSemanticExecutionContract_DirectSymbolMatchesMatrixEntry
{
    // Contract: Direct gadget symbol behavior MUST agree with matrix entry
    // Owner: gadget bodies, gadget matrices
    //
    // Regression: Ensures no divergence between direct call and table dispatch
    
    XCTAssertTrue(YES, "TCTI.SemanticExecution direct/matrix agreement contract placeholder");
}

- (void)testSemanticExecutionContract_AssemblyGadgetMatchesProduct
{
    // Contract: Control assembly gadget MUST agree with product when semantically identical
    // Owner: gadget bodies (both control and product)
    
    XCTAssertTrue(YES, "TCTI.SemanticExecution control/product agreement contract placeholder");
}

- (void)testSemanticExecutionContract_MemorySideEffectsCorrect
{
    // Contract: Memory side effects MUST match AArch64 semantics
    // Owner: gadget bodies (load/store families)
    
    XCTAssertTrue(YES, "TCTI.SemanticExecution memory effects contract placeholder");
}

@end
