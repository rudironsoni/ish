#import <XCTest/XCTest.h>

#include "../../Support/ISAFamilies/BaseScalar/tcti_scalar_runtime_semantic_scenarios.h"
#include "../../Support/ISAFamilies/Memory/tcti_memory_atomic_runtime_semantic_scenarios.h"
#include "../../Support/ISAFamilies/Memory/tcti_memory_pair_semantic_scenarios.h"

@interface TCTILogicalBitfieldShiftSemanticTests : XCTestCase
@end

@implementation TCTILogicalBitfieldShiftSemanticTests

- (void)testSemanticExecutionContract_LogicalMOVRoundtripsMemoryBackedX19
{
    XCTAssertEqual(tcti_semantic_case_logical_mov_roundtrips_memory_backed_x19(), 0ULL,
                   @"MOV alias lowering must write memory-backed x19 and read it back into hot "
                    "x5 without preserving stale carrier state");
}

- (void)testSemanticExecutionContract_LSLV64BitUsesFullShiftAmount
{
    XCTAssertEqual(tcti_semantic_case_lslv_64bit_uses_full_shift_amount(), 1ULL << 35,
                   @"TCTI must execute 64-bit LSLV with the full 0-63 shift amount instead of "
                    "truncating it to a 32-bit path.");
}

- (void)testSemanticExecutionContract_LSLVMemoryBackedRegistersRoundtrip
{
    XCTAssertEqual(tcti_semantic_case_lslv_memory_backed_registers_roundtrip(), 1ULL << 35,
                   @"TCTI must preserve 64-bit LSLV semantics when both the source and "
                    "destination live in memory-backed guest registers.");
}

- (void)testSemanticExecutionContract_LogicalMOVMemoryToMemoryRoundtrip
{
    XCTAssertEqual(tcti_semantic_case_logical_mov_memory_to_memory_roundtrip(), 0ULL,
                   @"TCTI logical-register fallback must preserve MOV alias writes when both the "
                    "source and destination are memory-backed guest registers, because musl "
                    "qsort carries x19/x28 through that exact path.");
}

- (void)testSemanticExecutionContract_MuslQsortRBITCLZ64BitRoundtrip
{
    XCTAssertEqual(tcti_semantic_case_musl_qsort_rbit_clz_64bit_roundtrip(), 0ULL,
                   @"TCTI must decode and execute 64-bit RBIT/CLZ on the live musl qsort state "
                    "update path, because lowering them through the 32-bit path corrupts qsort's "
                    "heap progression.");
}

- (void)testSemanticExecutionContract_MuslQsortTrailingZeroBlock
{
    XCTAssertEqual(tcti_semantic_case_musl_qsort_trailing_zero_block(), 0ULL,
                   @"TCTI must preserve the live musl qsort trailing-zero block, because that "
                    "exact rbit/clz path computes the increment that later drives pshift and the "
                    "bad comparator window in the interactive BusyBox ls crash.");
}

- (void)testSemanticExecutionContract_MuslQsortExtractPrefixPreservesPshiftState
{
    XCTAssertEqual(tcti_semantic_case_musl_qsort_extract_prefix_preserves_pshift_state(), 0ULL,
                   @"TCTI must preserve the live musl qsort extract prefix, because EXTR "
                    "materializes the next pshift state before the smoothsort path spills head "
                    "and re-enters the comparator window that later crashes Alpine ls.");
}

- (void)testSemanticExecutionContract_MuslQsortShiftMergeBlockUsesLiveHotRegs
{
    XCTAssertEqual(tcti_semantic_case_musl_qsort_shift_merge_block_uses_live_hot_regs(), 0ULL,
                   @"TCTI C-helper fallbacks must read the current hot-register state, because "
                    "musl qsort's live shift/merge block consumes values produced earlier in the "
                    "same generated block before the next comparator call.");
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

- (void)testSemanticExecutionContract_MuslBucketBitmaskBlockRoundtrip
{
    XCTAssertEqual(tcti_semantic_case_musl_bucket_bitmask_block_roundtrip(), 0ULL,
                   @"TCTI must preserve the exact musl root-bucket bitmask block across "
                    "LDR/LSLV/ORR/STR when executed as one generated block.");
}

- (void)testSemanticExecutionContract_MuslLsRootPostOpenBucketLoop
{
    XCTAssertEqual(tcti_semantic_case_musl_ls_root_post_open_bucket_loop(), 0ULL,
                   @"TCTI must preserve musl's post-open root bucket loop, including stack "
                    "zero-fill, 64-bit LSLV bitmask construction, and indexed table writes");
}

@end
