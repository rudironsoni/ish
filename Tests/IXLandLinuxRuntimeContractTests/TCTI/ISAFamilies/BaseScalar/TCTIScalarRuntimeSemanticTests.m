#import <XCTest/XCTest.h>

#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>
#import <IXLandLinuxRuntime/emu/aarch64/memory.h>
#import <IXLandLinuxRuntime/emu/tlb.h>
#import <IXLandLinuxRuntime/kernel/memory.h>

#include "../../Support/ISAFamilies/BaseScalar/tcti_scalar_runtime_semantic_scenarios.h"

// This suite is the Phase 1 family-owned landing zone for scalar live-runtime cases.
// The family matrix records the remaining subfamily split across IntegerALU,
// MoveImmediateAndAddress, LogicalBitfieldShift, and System.
@interface TCTIScalarRuntimeSemanticTests : XCTestCase
@end

@implementation TCTIScalarRuntimeSemanticTests
- (void)testSemanticExecutionContract_DynamicTagScaledStoreUsesFullIndex
{
    XCTAssertEqual(tcti_semantic_case_dynamic_tag_scaled_store_uses_full_index(), 0ULL,
                   @"TCTI STR register-offset with LSL #3 must store DT_RELR in tag slot 36 "
                    "without corrupting the DT_REL slot");
}

- (void)testSemanticExecutionContract_MuslUBFIZSymbolIndexPreservesShiftedBits
{
    XCTAssertEqual(tcti_semantic_case_musl_ubfiz_symbol_index_preserves_shifted_bits(),
                   0x00000007fffffff8ULL,
                   @"TCTI UBFM/UBFIZ must use AArch64 bitmask semantics so musl dynlink keeps "
                    "the high bits of shifted symbol table indexes");
}

- (void)testSemanticExecutionContract_LSRAliasUsesTopMaskNotRotate
{
    XCTAssertEqual(tcti_semantic_case_lsr_alias_uses_top_mask_not_rotate(),
                   0x4000000000000000ULL,
                   @"TCTI UBFM must apply the top mask so the LSR alias shifts instead of "
                    "rotating high bits back into the result");
}

- (void)testSemanticExecutionContract_ASRAliasSignExtendsExtractedField
{
    XCTAssertEqual(tcti_semantic_case_asr_alias_sign_extends_extracted_field(), 0x255dULL,
                   @"TCTI SBFM must sign-extend the extracted field so the ASR alias shifts "
                    "right instead of rotating high bits back into the result");
}

- (void)testSemanticExecutionContract_MuslSecsToTmSMULHAsrSubBlockKeepsDayCount
{
    XCTAssertEqual(tcti_semantic_case_musl_secs_to_tm_smulh_asr_sub_block(), 0x255dULL,
                   @"TCTI must preserve musl __secs_to_tm's live SMULH/ASR/SUB block so "
                    "localtime_r keeps the day count for normal 2026 timestamps instead of "
                    "taking the EINVAL/NULL return path");
}

- (void)testSemanticExecutionContract_MuslRELRLoopTerminatesAtTableEnd
{
    XCTAssertEqual(tcti_semantic_case_musl_relr_loop_terminates_at_table_end(), 0ULL,
                   @"TCTI must execute musl _dlstart RELR bitmap relocation loop exactly once "
                    "over RELRSZ without running relocation writes past the mapped data segment");
}

- (void)testSemanticExecutionContract_PLTRELRELAStrideSelector
{
    XCTAssertEqual(tcti_semantic_case_pltrel_rela_stride_selector(), 3ULL,
                   @"TCTI must compute DT_RELA PLT relocation stride as 3 entries after "
                    "CMP/CSET/ADD");
}

- (void)testSemanticExecutionContract_RelocationLoopPreservesLoadedX5
{
    XCTAssertEqual(tcti_semantic_case_relocation_loop_preserves_loaded_x5(),
                   0x00000000000dfee8ULL,
                   @"TCTI must preserve the relocation offset loaded into hot x5 across the "
                    "following generated compare and branch gadgets");
}

- (void)testSemanticExecutionContract_RelocationFaultPathUsesLoadedX5
{
    XCTAssertEqual(tcti_semantic_case_relocation_fault_path_uses_loaded_x5(),
                   0x1122334455667788ULL,
                   @"TCTI must carry the loaded relocation offset through the later CBZ block "
                    "into the final register-offset relocation load");
}

- (void)testSemanticExecutionContract_MuslSnprintfFILEWposInit
{
    XCTAssertEqual(tcti_semantic_case_musl_snprintf_file_wpos_init(), 0ULL,
                   @"TCTI must preserve musl's stack FILE wpos initialization across STP and "
                    "SIMD copy setup before fwrite_unlocked calls memcpy");
}

- (void)testSemanticExecutionContract_MuslVdprintfStackFILEZeroInit
{
    XCTAssertEqual(tcti_semantic_case_musl_vdprintf_stack_file_zero_init(), 0ULL,
                   @"TCTI must lower AdvSIMD MOVI/MVNI immediates so musl's stack FILE starts "
                    "with zeroed buffer pointers before relocation diagnostics reach the PTY");
}

- (void)testSemanticExecutionContract_DCZVAZeroesCacheBlock
{
    XCTAssertEqual(tcti_semantic_case_dc_zva_zeroes_cache_block(), 0ULL,
                   @"TCTI must implement DC ZVA so musl memset zeroes large allocations used "
                    "during BusyBox shell startup");
}

- (void)testSemanticExecutionContract_MuslStrncmpLibcReservedPrefix
{
    XCTAssertEqual(tcti_semantic_case_musl_strncmp_libc_reserved_prefix(), 0ULL,
                   @"TCTI must execute musl strncmp(\"c...\", \"c.\", 2) correctly so ldso can "
                    "recognize its own libc SONAME while loading BusyBox dependencies");
}

- (void)testSemanticExecutionContract_MuslLoadLibraryDetectsLibcSelf
{
    XCTAssertEqual(tcti_semantic_case_musl_load_library_detects_libc_self(), 0ULL,
                   @"TCTI must execute musl load_library's reserved libc detection path so "
                    "ldso does not load itself as a second libc dependency");
}

- (void)testSemanticExecutionContract_MuslDls3DependencyChainAppendsNextDSO
{
    XCTAssertEqual(tcti_semantic_case_musl_dls3_dependency_chain_appends_next_dso(), 0ULL,
                   @"TCTI must execute musl __dls3's live dependency-chain append loop so the "
                    "resolved DSO is linked through offset 0x68 and later symbol fallback can "
                    "continue past the originating executable.");
}

- (void)testSemanticExecutionContract_MuslFindSymDepsPostIndexWalkReadsFirstDep
{
    XCTAssertEqual(tcti_semantic_case_musl_find_sym_deps_post_index_walk_reads_first_dep(), 0ULL,
                   @"TCTI must execute musl's dependency-scope lookup prefix so the deps array "
                    "post-index load advances x15 and the first dependency DSO feeds the later "
                    "GNU-hash lookup instead of terminating fallback at the executable.");
}

- (void)testSemanticExecutionContract_MuslOpenedLibcValidationUsesMulAlias
{
    XCTAssertEqual(tcti_semantic_case_musl_opened_libc_validation_uses_mul_alias(), 0ULL,
                   @"TCTI must decode and execute the live MUL alias on musl's opened-libc "
                    "validation path so ld-musl computes the expected ELF size product instead "
                    "of taking the false failure path.");
}

- (void)testSemanticExecutionContract_MuslGNUHashMalloc
{
    XCTAssertEqual(tcti_semantic_case_musl_gnu_hash_malloc(), 0ULL,
                   @"TCTI must execute musl's GNU hash loop with 32-bit shifted ADD on "
                    "memory-backed registers so dynamic symbol lookup can find malloc/free");
}

- (void)testSemanticExecutionContract_MuslGNULookupFilteredMalloc
{
    XCTAssertEqual(tcti_semantic_case_musl_gnu_lookup_filtered_malloc(), 0ULL,
                   @"TCTI must execute musl's GNU hash lookup helper so ldso can resolve "
                    "libc symbols through bloom filters, hash buckets, and symbol strings");
}

- (void)testSemanticExecutionContract_MuslGNULookupDls2bChain
{
    XCTAssertEqual(tcti_semantic_case_musl_gnu_lookup_dls2b_chain(), 0ULL,
                   @"TCTI must execute musl's GNU hash chain walk across false-positive "
                    "entries so ldso can resolve its own __dls2b symbol");
}

- (void)testSemanticExecutionContract_MuslFindSymDls2bFromLdso
{
    XCTAssertEqual(tcti_semantic_case_musl_find_sym_dls2b_from_ldso(), 0ULL,
                   @"TCTI must execute musl find_sym over ldso's own DSO, GNU hash table, "
                    "and dynamic symbol metadata so __dls2b resolves before guest startup");
}

- (void)testSemanticExecutionContract_MuslFindSymLongjmpFromLdso
{
    XCTAssertEqual(tcti_semantic_case_musl_find_sym_longjmp_from_ldso(), 0ULL,
                   @"TCTI must execute musl find_sym for a real longjmp GNU-hash bucket hit so "
                    @"ldso can resolve BusyBox's libc symbols instead of cascading through the "
                    @"error-relocation path.");
}

- (void)testSemanticExecutionContract_MuslFindSymSiglongjmpFromLdso
{
    XCTAssertEqual(tcti_semantic_case_musl_find_sym_siglongjmp_from_ldso(), 0ULL,
                   @"TCTI must execute musl find_sym for siglongjmp so the live Alpine BusyBox "
                    @"loader path resolves the next-DSO symbol instead of spinning.");
}

- (void)testSemanticExecutionContract_MuslFindSymAcceptsGlobalFunc
{
    XCTAssertEqual(tcti_semantic_case_musl_find_sym_accepts_global_func(), 0ULL,
                   @"TCTI must execute musl find_sym's st_shndx, st_value, type, and binding "
                    "checks so valid global function symbols are not rejected");
}

- (void)testSemanticExecutionContract_ADDShiftedHotUsesScratchCarrier
{
    XCTAssertEqual(tcti_semantic_case_add_shifted_hot_uses_scratch_carrier(), 0ULL,
                   @"TCTI ADD shifted-register must use scratch carriers without losing the "
                    "shifted operand or corrupting hot destination registers");
}

- (void)testSemanticExecutionContract_ADDExtendedUXTWUses32BitOperand
{
    XCTAssertEqual(tcti_semantic_case_add_extended_uxtw_uses_32bit_operand(), 0ULL,
                   @"TCTI ADD extended-register must zero-extend Wm and add it to Xn before "
                    "GNU hash chain address calculation");
}

- (void)testSemanticExecutionContract_LogicalImmediateMemoryBackedSourceUsesDistinctScratch
{
    XCTAssertEqual(tcti_semantic_case_logical_imm_memory_backed_source_uses_distinct_scratch(),
                   0x8ULL,
                   @"TCTI logical-immediate lowering must not overwrite a memory-backed source "
                    "scratch while materializing the immediate operand");
}

- (void)testSemanticExecutionContract_MuslMallocSizeclassRBITCLZ
{
    XCTAssertEqual(tcti_semantic_case_musl_malloc_sizeclass_rbit_clz(), 0x0000001000000011ULL,
                   @"TCTI must execute musl malloc's RBIT/CLZ size-class calculation without "
                    "routing one-source data-processing instructions through CSEL lowering");
}

- (void)testSemanticExecutionContract_MuslCallocPLTADRPResolvesLocalGOTPage
{
    XCTAssertEqual(tcti_semantic_case_musl_calloc_plt_adrp_resolves_local_got_page(), 0ULL,
                   @"TCTI ADRP lowering must derive the guest GOT page from the current PC so "
                    "musl's calloc@plt loads its own GOT slot instead of reusing a stale page");
}

- (void)testSemanticExecutionContract_MuslOpendirNonNullCallocSkipsErrorClose
{
    XCTAssertEqual(tcti_semantic_case_musl_opendir_calloc_nonnull_skips_close_path(), 0ULL,
                   @"musl opendir branches to close(2) only when calloc returns NULL; TCTI must "
                    "execute the following CBZ X0 using the full 64-bit guest pointer value");
}

- (void)testSemanticExecutionContract_MuslCallocOverflowGuardUsesRealUMULH
{
    XCTAssertEqual(
        tcti_semantic_case_musl_calloc_overflow_guard_umulh_stays_zero_for_small_product(), 0ULL,
        @"TCTI must decode and execute musl calloc's UMULH overflow guard as a real multiply-high "
         "operation, not as a 3-source multiply-add with a stale Ra addend.");
}

- (void)testSemanticExecutionContract_MuslCallbackSlotADRPADDMaterializesLdsoTargetPage
{
    XCTAssertEqual(
        tcti_semantic_case_musl_callback_slot_adrp_add_materializes_ldso_target_page(), 0ULL,
        @"The live ld-musl callback-slot producer must materialize ADRP from the guest PC page, "
         "then apply the following ADD immediate so the stored slot target stays in the ldso text "
         "image instead of collapsing into a signed page delta.");
}

- (void)testSemanticExecutionContract_MuslLibcNameComparePrefixStaysOnMatchPath
{
    XCTAssertEqual(
        tcti_semantic_case_musl_libc_name_compare_prefix_stays_on_match_path(), 0ULL,
        @"The live ld-musl libc-name prefix compare must keep x0 on the literal string page, "
         "load matching first bytes from the guest name and ldso literal, and fall through "
         "instead of branching to the mismatch path when the names agree.");
}

- (void)testSemanticExecutionContract_MuslLibcNameLiteralBaseMaterializesBeforeCompare
{
    XCTAssertEqual(
        tcti_semantic_case_musl_libc_name_literal_base_materializes_before_compare(), 0ULL,
        @"The live ld-musl libc-name compare setup must materialize the literal base in x0, "
         "copy x25 into x17, and land on the compare loop entry before any byte loads.");
}

- (void)testSemanticExecutionContract_MuslLibcNameSetupPreservesHotX0AcrossMOVX17X25
{
    XCTAssertEqual(
        tcti_semantic_case_musl_libc_name_setup_preserves_hot_x0_across_mov_x17_x25(), 0ULL,
        @"The live ld-musl setup block must preserve hot guest x0 after ADRP while copying x25 "
         "into memory-backed x17 for the later libc-name compare path.");
}

- (void)testSemanticExecutionContract_MuslMutexLDAXRSTLXRRoundtripsLockWord
{
    XCTAssertEqual(tcti_semantic_case_musl_mutex_ldaxr_stlxr_roundtrip(), 0ULL,
                   @"TCTI must execute musl pthread mutex LDAXR/STLXR lock/unlock/relock "
                    "sequences without leaving the lock word permanently busy");
}

- (void)testSemanticExecutionContract_TSTImmediateSetsZeroFlagForZeroInput
{
    XCTAssertEqual(tcti_semantic_case_tst_x1_imm_sets_zero_flag(), 0ULL,
                   @"TCTI must execute TST Xn,#imm as ANDS-to-XZR and leave Z set for zero "
                    "input without corrupting the source register");
}

- (void)testSemanticExecutionContract_LDRHCMPCCMPEQSurvivesSingleInstructionBlocks
{
    XCTAssertEqual(tcti_semantic_case_ldrh_cmp_ccmp_eq_survives_single_insn_blocks(), 0ULL,
                   @"TCTI must preserve LDRH/CMP/CCMP/B.EQ semantics when the real guest path is "
                    "forced through one-instruction blocks");
}

- (void)testSemanticExecutionContract_SMADDLUsesSigned32BitInputs
{
    XCTAssertEqual(tcti_semantic_case_smaddl_uses_signed_32bit_inputs(), 12ULL,
                   @"TCTI SMADDL lowering must use signed 32-bit operands from Wn/Wm rather than "
                    "leaking stale high bits from Xn/Xm");
}
@end
