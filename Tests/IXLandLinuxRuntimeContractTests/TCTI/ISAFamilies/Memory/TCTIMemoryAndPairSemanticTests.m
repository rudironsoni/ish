#import <XCTest/XCTest.h>

#include "../../Support/ISAFamilies/Memory/tcti_memory_pair_semantic_scenarios.h"

@interface TCTIMemoryAndPairSemanticTests : XCTestCase
@end

@implementation TCTIMemoryAndPairSemanticTests
- (void)testSemanticExecutionContract_LogicalMOVRoundtripsMemoryBackedX19
{
    XCTAssertEqual(tcti_semantic_case_logical_mov_roundtrips_memory_backed_x19(), 0ULL,
                   @"MOV alias lowering must write memory-backed x19 and read it back into hot "
                    "x5 without preserving stale carrier state");
}

- (void)testSemanticExecutionContract_CSETThenADDOverwritesStaleX3
{
    XCTAssertEqual(tcti_semantic_case_cset_eq_then_add_to_x3(), 3ULL,
                   @"CSET.EQ followed by ADD x3,#2 must overwrite stale x3 before relocation "
                    "entry processing passes x3 into x19");
}

- (void)testSemanticExecutionContract_LDRFromMemoryBackedBaseSyncsHotDestination
{
    XCTAssertEqual(tcti_semantic_case_ldr_x5_from_memory_backed_x27(), 0x0123456789abcdefULL,
                   @"TCTI LDR must load from memory-backed base registers and sync hot destinations");
}

- (void)testSemanticExecutionContract_LDRMemoryBackedDestinationAliasingBaseUpdatesX20
{
    XCTAssertEqual(tcti_semantic_case_ldr_x20_from_memory_backed_x20_alias_base(), 0ULL,
                   @"TCTI LDR with Rt == Rn on memory-backed x20 must compute the address from "
                    "the original x20 and then publish the loaded pointer back into x20");
}

- (void)testSemanticExecutionContract_LDRChainAfterMemoryBackedX20AliasBaseReadsLiveValue
{
    XCTAssertEqual(tcti_semantic_case_ldr_chain_after_memory_backed_x20_alias_base(), 0ULL,
                   @"TCTI must preserve the busybox canary-style LDR chain where x20 first loads "
                    "a pointer from [x20,#imm] and the next LDR dereferences that new x20");
}

- (void)testSemanticExecutionContract_STRRegisterOffsetFromHotToMemoryBackedBase
{
    XCTAssertEqual(tcti_semantic_case_str_x0_to_memory_backed_x22_scaled_x1(),
                   0xfedcba9876543210ULL,
                   @"TCTI STR register-offset must store through memory-backed base registers");
}

- (void)testSemanticExecutionContract_ADDToMemoryBackedX23StoresResult
{
    XCTAssertEqual(tcti_semantic_case_add_hot_pair_to_memory_backed_x23(), 0x5655ac10ULL,
                   @"TCTI ADD with a memory-backed high destination must store the computed "
                    "result into architectural x23");
}

- (void)testSemanticExecutionContract_CMPMemoryBackedX27X23BranchesEQ
{
    XCTAssertEqual(tcti_semantic_case_cmp_memory_backed_x27_x23_branches_eq(), 0x6a690ULL,
                   @"TCTI CMP over memory-backed high registers must set NZCV so B.EQ exits "
                    "the relocation loop when x27 reaches x23");
}

- (void)testSemanticExecutionContract_StackPairRoundtripsHotX5X4
{
    XCTAssertEqual(tcti_semantic_case_stack_pair_roundtrips_hot_x5_x4(), 0ULL,
                   @"TCTI STP/LDP on the guest stack must preserve hot registers x5 and x4 "
                    "across the dynamic linker symbol lookup path");
}

- (void)testSemanticExecutionContract_StackPairStoresMemoryBackedX20X21
{
    XCTAssertEqual(tcti_semantic_case_stack_pair_stores_memory_backed_x20_x21(), 0ULL,
                   @"TCTI STP from memory-backed x20/x21 to the guest stack must store both "
                    "values, preserve SP for offset mode, and advance PC past the prologue pair "
                    "store used in the busybox prompt path");
}

- (void)testSemanticExecutionContract_BusyboxStackCanaryEqualPathBranchesToRestore
{
    XCTAssertEqual(tcti_semantic_case_busybox_stack_canary_equal_path_branches_to_restore(), 0ULL,
                   @"TCTI must preserve the busybox stack-canary equal path through indirect "
                    "canary load plus SUBS/B.EQ so the compare branches to the restore block");
}

- (void)testSemanticExecutionContract_BusyboxStackRestoreBlockRestoresFrame
{
    XCTAssertEqual(tcti_semantic_case_busybox_stack_restore_block_restores_frame(), 0ULL,
                   @"TCTI must preserve the busybox restore block: memory-backed LDP pairs, "
                    "frame unwind, and RET after the canary compare succeeds");
}

- (void)testSemanticExecutionContract_BusyboxStackCanaryMismatchCallsFailPath
{
    XCTAssertEqual(tcti_semantic_case_busybox_stack_canary_mismatch_calls_fail_path(), 0ULL,
                   @"TCTI must take the busybox stack-canary mismatch path to the fail call "
                    "site when the saved stack slot differs from the live canary value");
}

- (void)testSemanticExecutionContract_LDPFirstDestinationPreservesPairBase
{
    XCTAssertEqual(tcti_semantic_case_ldp_first_destination_preserves_pair_base(), 0ULL,
                   @"TCTI LDP must compute both pair addresses from the original base even when "
                    "the first destination register is also the base register");
}

- (void)testSemanticExecutionContract_LDPPostIndexHotFirstDestinationPreservesPairBase
{
    XCTAssertEqual(tcti_semantic_case_ldp_post_index_hot_first_destination_preserves_pair_base(), 0ULL,
                   @"TCTI LDP post-index must write back from the original hot base register even "
                    "when the first destination register aliases that base.");
}

- (void)testSemanticExecutionContract_LDPPostIndexFirstDestinationPreservesPairBase
{
    XCTAssertEqual(tcti_semantic_case_ldp_post_index_first_destination_preserves_pair_base(), 0ULL,
                   @"TCTI LDP post-index must still compute both pair loads from the original "
                    "base even when the first destination register aliases that base.");
}

- (void)testSemanticExecutionContract_LDPSWPairSignExtendsLiveMuslOffsets
{
    XCTAssertEqual(tcti_semantic_case_ldpsw_pair_sign_extends_live_musl_offsets(), 0ULL,
                   @"TCTI LDPSW must sign-extend both 32-bit pair elements before musl adds "
                    "stack offsets into the live group/passwd result buffer.");
}

- (void)testSemanticExecutionContract_MuslGetgrgidMatchPathPublishesResultSlot
{
    XCTAssertEqual(tcti_semantic_case_musl_getgrgid_match_path_publishes_result_slot(), 0ULL,
                   @"TCTI must preserve musl's live getgrgid match path so the helper publishes "
                    "a real struct group pointer into the result slot instead of stale stack data.");
}

- (void)testSemanticExecutionContract_MuslGetgrgidReallocTailPublishesBufferBase
{
    XCTAssertEqual(tcti_semantic_case_musl_getgrgid_realloc_tail_publishes_buffer_base(), 0ULL,
                   @"TCTI must preserve musl's post-realloc getgrgid tail so the shared buffer "
                    "base and size slots stay valid before the match path builds gr_name.");
}
@end
