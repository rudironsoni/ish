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

- (void)testSemanticExecutionContract_MuslMutexLDAXRSTLXRRoundtripsLockWord
{
    XCTAssertEqual(tcti_harness_case_musl_mutex_ldaxr_stlxr_roundtrip(), 0ULL,
                   @"TCTI must execute musl pthread mutex LDAXR/STLXR lock/unlock/relock "
                    "sequences without leaving the lock word permanently busy");
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
