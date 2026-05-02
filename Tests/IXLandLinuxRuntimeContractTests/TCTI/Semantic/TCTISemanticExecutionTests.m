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

- (void)testSemanticExecutionContract_StrchrnulVectorMaskFindsDot
{
    XCTAssertNotEqual(tcti_harness_case_strchrnul_vector_mask_finds_dot(), 0ULL,
                      @"The musl strchrnul word scan depends on EON and BIC, not just EOR and AND");
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

- (void)testSemanticExecutionContract_MuslStrncmpLibcReservedPrefix
{
    XCTAssertEqual(tcti_harness_case_musl_strncmp_libc_reserved_prefix(), 0ULL,
                   @"TCTI must execute musl strncmp(\"c...\", \"c.\", 2) correctly so ldso can "
                    "recognize its own libc SONAME while loading BusyBox dependencies");
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
