#import <XCTest/XCTest.h>

#include "../../Support/ISAFamilies/Memory/tcti_memory_atomic_runtime_semantic_scenarios.h"

// This suite is the Phase 1 family-owned landing zone for live guest memory,
// pair/frame, and atomic/exclusive contracts until the remaining subfamily
// extractions land.
@interface TCTIMemoryAndAtomicRuntimeSemanticTests : XCTestCase
@end

@implementation TCTIMemoryAndAtomicRuntimeSemanticTests
- (void)testSemanticExecutionContract_MuslFrameStrideBlockUsesWideImmediatesAndRegOffsetLDR
{
    XCTAssertEqual(
        tcti_semantic_case_musl_frame_stride_block_uses_wide_immediates_and_reg_offset_ldr(), 0ULL,
        @"The live ld-musl frame-stride block must materialize W1=24, compute SMULL from W0/W1, "
         "and perform the dependent 32-bit register-offset LDR from [x20, x0] before using the "
         "loaded word in the next ADD");
}

- (void)testSemanticExecutionContract_RegOffsetLDRWHelperReadsHotX0Offset
{
    XCTAssertEqual(tcti_semantic_case_reg_offset_ldr_w_helper_reads_hot_x0_offset(), 0ULL,
                   @"The load helper for LDR Wt, [Xn, Xm] must use the current hot guest x0 "
                    "offset when rm=0 and must write the 32-bit loaded word back into the "
                    "architectural destination register");
}

- (void)testSemanticExecutionContract_GeneratedLDRWRegOffsetReadsHotX0Offset
{
    XCTAssertEqual(tcti_semantic_case_generated_ldr_w_reg_offset_reads_hot_x0_offset(), 0ULL,
                   @"The generated TCTI block for LDR W7, [X20, X0] must preserve the register-"
                    "offset metadata and materialize the loaded 32-bit word into guest x7");
}

- (void)testSemanticExecutionContract_MuslFrameStridePrefixThenLDRInNextBlock
{
    XCTAssertEqual(tcti_semantic_case_musl_frame_stride_prefix_then_ldr_in_next_block(), 0ULL,
                   @"If the ld-musl frame-stride prefix and the dependent LDR execute in separate "
                    "generated blocks, guest x0 and x14 must carry correctly into the next "
                    "block and the LDR must still read the expected word");
}

- (void)testSemanticExecutionContract_MOVZSMULLPrefixPreservesExpectedX0
{
    XCTAssertEqual(tcti_semantic_case_movz_smull_prefix_preserves_expected_x0(), 0ULL,
                   @"The live movz; smull prefix must leave guest x0 at the exact 64-bit product "
                    "of W0 and W1 before any later helper/store path runs");
}

- (void)testSemanticExecutionContract_UMADDLUsesUnsigned32BitInputs
{
    XCTAssertEqual(tcti_semantic_case_umaddl_uses_unsigned_32bit_inputs(), 35ULL,
                   @"TCTI UMADDL lowering must use unsigned 32-bit operands from Wn/Wm rather "
                    "than stale 64-bit register contents");
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

- (void)testSemanticExecutionContract_SPRelativeLDRSTRRoundtrip
{
    XCTAssertEqual(tcti_semantic_case_sp_relative_ldr_str_roundtrip(), 0ULL,
                   @"TCTI must preserve 64-bit SP-relative load/store immediate semantics for "
                    "the musl post-open root bucket loop.");
}

- (void)testSemanticExecutionContract_QsortPointerSlotUpdatesRoundtrip
{
    XCTAssertEqual(tcti_semantic_case_qsort_pointer_slot_updates_roundtrip(), 0ULL,
                   @"TCTI must preserve musl qsort's pointer-slot register-offset store, pair "
                    "load, and post-index writeback semantics for 8-byte element shuffles.");
}

- (void)testSemanticExecutionContract_BusyboxScandirFlattenBlockRoundtrip
{
    XCTAssertEqual(tcti_semantic_case_busybox_scandir_flatten_block_roundtrip(), 0ULL,
                   @"TCTI must preserve BusyBox's linked-list flatten block when x19/x20 are "
                    "memory-backed guest registers, because the live Alpine ls path stores list "
                    "nodes into the qsort array through that exact block.");
}

- (void)testSemanticExecutionContract_BusyboxInputWidecharCopyLoopRoundtrip
{
    XCTAssertEqual(tcti_semantic_case_busybox_input_widechar_copy_loop_roundtrip(), 0ULL,
                   @"TCTI must preserve BusyBox's typed-input widechar copy loop, including "
                    "unaligned LDRH and the final STR WZR register-offset terminator store, "
                    "because the live simulator keyboard path enters this block before the "
                    "interactive shell can execute typed commands.");
}

- (void)testSemanticExecutionContract_MuslQsortTBZW0SignbitBranch
{
    XCTAssertEqual(tcti_semantic_case_musl_qsort_tbz_w0_signbit_branch(), 0ULL,
                   @"TCTI must honor tbz w0,#31 on the live musl qsort path, because a wrong "
                    "32-bit sign-bit branch can walk the comparator left past the start of the "
                    "BusyBox pointer array.");
}

- (void)testSemanticExecutionContract_MuslQsortTBNZW0SignbitBranches
{
    XCTAssertEqual(tcti_semantic_case_musl_qsort_tbnz_w0_signbit_branches(), 0ULL,
                   @"TCTI must honor the live musl qsort tbnz w0,#31 branches, because a wrong "
                    "negative-compare branch can materialize the next comparator window from the "
                    "wrong side of the BusyBox pointer array.");
}

- (void)testSemanticExecutionContract_MuslQsortCSINCTSTGateKeepsExpectedPath
{
    XCTAssertEqual(tcti_semantic_case_musl_qsort_csinc_tst_gate_keeps_expected_path(), 0ULL,
                   @"TCTI must preserve musl qsort's csinc/tst gate, because that exact block "
                    "decides whether the next comparator window advances or jumps into the "
                    "alternate heap-progression path.");
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

- (void)testSemanticExecutionContract_MuslQsortRestoreBlockRebuildsLiveFrame
{
    XCTAssertEqual(tcti_semantic_case_musl_qsort_restore_block_rebuilds_live_frame(), 0ULL,
                   @"TCTI must restore musl qsort's spilled frame through the live LDP/MOV "
                    "epilogue block, because that block reconstructs the comparator head, step "
                    "state, and pshift inputs before re-entering the smoothsort loop.");
}

- (void)testSemanticExecutionContract_MuslQsortReentryBlockPreservesHeadAndPshiftInputs
{
    XCTAssertEqual(tcti_semantic_case_musl_qsort_reentry_block_preserves_head_and_pshift_inputs(),
                   0ULL,
                   @"TCTI must preserve musl qsort's re-entry frame build, because the live "
                    "block saves the current head on the stack, zero-extends w4 into pshift, and "
                    "materializes the next smoothsort iteration inputs before branching.");
}

- (void)testSemanticExecutionContract_MuslMemcpy8TailRoundtrip
{
    XCTAssertEqual(tcti_semantic_case_musl_memcpy8_tail_roundtrip(), 0ULL,
                   @"TCTI must preserve musl memcpy's 8-byte tail copy with unscaled negative "
                    "offset loads and stores, because qsort uses this helper in the live Alpine "
                    "directory-sort path.");
}

- (void)testSemanticExecutionContract_MuslBucketBitmaskBlockRoundtrip
{
    XCTAssertEqual(tcti_semantic_case_musl_bucket_bitmask_block_roundtrip(), 0ULL,
                   @"TCTI must preserve the exact musl root-bucket bitmask block across "
                    "LDR/LSLV/ORR/STR when executed as one generated block.");
}

- (void)testSemanticExecutionContract_STRXZRPostIndexWritesBackBase
{
    XCTAssertEqual(tcti_semantic_case_str_xzr_post_index_writes_back_base(), 0ULL,
                   @"TCTI must execute STR XZR post-index stores with both the zero store and "
                    "the base-register writeback preserved.");
}

- (void)testSemanticExecutionContract_MuslLsRootPostOpenBucketLoop
{
    XCTAssertEqual(tcti_semantic_case_musl_ls_root_post_open_bucket_loop(), 0ULL,
                   @"TCTI must preserve musl's post-open root bucket loop, including stack "
                    "zero-fill, 64-bit LSLV bitmask construction, and indexed table writes");
}

- (void)testSemanticExecutionContract_MuslLsRootPostOpenCallbackScan
{
    XCTAssertEqual(tcti_semantic_case_musl_ls_root_post_open_callback_scan(), 0ULL,
                   @"TCTI must preserve musl's post-open callback scan, including TBZ, BLR, RET, "
                    "and callback-loop state for the busybox ls root-directory path");
}

- (void)testSemanticExecutionContract_MuslLsLongVectorTailAndDynamicTagScan
{
    XCTAssertEqual(tcti_semantic_case_musl_ls_long_vector_tail_and_dynamic_tag_scan(), 0ULL,
                   @"TCTI must preserve musl's live long-ls vector tail scan and dynamic-tag walk, "
                    "including post-index LDR writeback, NULL-sentinel termination, and the tag "
                    "6 value store that feeds the next ldso state build after 'total 0'.");
}

- (void)testSemanticExecutionContract_MuslCallbackPrefixMaterializesArgs
{
    XCTAssertEqual(tcti_semantic_case_musl_callback_prefix_materializes_args(), 0ULL,
                   @"TCTI must materialize the callback base, argument pair, and callback target "
                    "before BLR in the live ld-musl post-open callback path");
}

- (void)testSemanticExecutionContract_LDPX2X0FromMemoryBackedX21
{
    XCTAssertEqual(tcti_semantic_case_ldp_x2_x0_from_memory_backed_x21(), 0ULL,
                   @"TCTI must preserve both 64-bit destinations for LDP X2,X0,[X21] when the "
                    @"base register is memory-backed and the pair feeds the live musl callback "
                     "argument path");
}

- (void)testSemanticExecutionContract_LDRX2Immediate0FromMemoryBackedX21
{
    XCTAssertEqual(tcti_semantic_case_ldr_x2_imm0_from_memory_backed_x21(), 0ULL,
                   @"TCTI must preserve LDR X2,[X21] from a memory-backed base because the first "
                    @"element of the live musl pair-load lowering feeds the callback argument "
                     @"prefix");
}

- (void)testSemanticExecutionContract_LDRX1ImmediateFromMemoryBackedX21
{
    XCTAssertEqual(tcti_semantic_case_ldr_x1_imm_from_memory_backed_x21(), 0ULL,
                   @"TCTI must preserve LDR X1,[X21,#96] from a memory-backed base so the live "
                    @"musl callback prefix sees the correct accumulated argument base");
}

- (void)testSemanticExecutionContract_LDRX0Immediate8FromMemoryBackedX21
{
    XCTAssertEqual(tcti_semantic_case_ldr_x0_imm8_from_memory_backed_x21(), 0ULL,
                   @"TCTI must preserve LDR X0,[X21,#8] from a memory-backed base because the "
                    @"second element of the live musl pair-load lowering depends on the same "
                     @"slow-path semantics");
}

- (void)testSemanticExecutionContract_ManualTwoLDRSharedBlockFromMemoryBackedX21
{
    XCTAssertEqual(tcti_semantic_case_manual_two_ldr_shared_block_from_memory_backed_x21(), 0ULL,
                   @"Two back-to-back TCTI LDR helper gadgets must preserve both destinations "
                    @"inside one shared block before any generated pc-advance runs");
}

- (void)testSemanticExecutionContract_ManualTwoLDRSharedBlockWithPCAdvance
{
    XCTAssertEqual(tcti_semantic_case_manual_two_ldr_shared_block_with_pc_advance(), 0ULL,
                   @"Two back-to-back TCTI LDR helper gadgets plus pc-advance must preserve both "
                    @"destinations and advance the guest PC exactly once");
}

- (void)testSemanticExecutionContract_GeneratedLDPX2X0BytecodeMatchesManualShape
{
    XCTAssertEqual(
        tcti_semantic_case_generated_ldp_x2_x0_bytecode_matches_manual_shape(), 0ULL,
        @"The generated bytecode for LDP X2,X0,[X21] must match the working manual two-load "
         @"stream: two LDR helper gadgets, one pc-advance, and one exit with the expected "
         @"parameter order and offsets");
}

- (void)testSemanticExecutionContract_RegOffsetLDRX0AliasBaseReadsExpectedQword
{
    XCTAssertEqual(tcti_semantic_case_reg_offset_ldr_x0_alias_base_reads_expected_qword(), 0ULL,
                   @"The LDR helper must preserve 64-bit register-offset semantics when X0 is "
                    @"both the base and the destination in the live musl callback tail");
}

- (void)testSemanticExecutionContract_GeneratedRegOffsetLDRX0AliasBaseReadsExpectedQword
{
    XCTAssertEqual(
        tcti_semantic_case_generated_reg_offset_ldr_x0_alias_base_reads_expected_qword(), 0ULL,
        @"The generated TCTI block for LDR X0,[X0,X25,LSL #3] must preserve base-equals-"
         @"destination semantics in the live musl callback tail");
}

- (void)testSemanticExecutionContract_MuslCallbackTableWalkMaterializesDispatchArgs
{
    XCTAssertEqual(tcti_semantic_case_musl_callback_table_walk_materializes_dispatch_args(), 0ULL,
                   @"TCTI must preserve the live ld-musl callback-table walk through LDR "
                    @"register-offset loads, branch filtering, and final argument "
                    @"materialization before the callback BL.");
}

- (void)testSemanticExecutionContract_MuslCallbackSlotLoadsBranchTarget
{
    XCTAssertEqual(tcti_semantic_case_musl_callback_slot_loads_branch_target(), 0ULL,
                   @"TCTI must load a callback target from memory-backed x23 into hot x3 before "
                    "BLR so ld-musl does not branch through a stale carrier value");
}

- (void)testSemanticExecutionContract_MuslLsRootFrameListWalk
{
    XCTAssertEqual(tcti_semantic_case_busybox_ls_retry_ccmp_close_path(), 0ULL,
                   @"TCTI must preserve the current busybox ls retry CCMP cluster so success, "
                    "retry, and close-path fallthrough decisions match guest AArch64 flags.");
}

- (void)testSemanticExecutionContract_BusyboxAllocatorMSUBCallbackRoundtrip
{
    XCTAssertEqual(tcti_semantic_case_busybox_allocator_msub_callback_roundtrip(), 0ULL,
                   @"TCTI must preserve the current busybox allocator bookkeeping block, "
                    "including SDIV/MSUB, stack spill/reload, BLR/RET, and the final "
                    "accumulator update used before the ls OOM path.");
}

- (void)testSemanticExecutionContract_MuslPthreadMutexLockPrefixReachesAtomicFastPath
{
    XCTAssertEqual(tcti_semantic_case_musl_pthread_mutex_lock_prefix(), 0ULL,
                   @"TCTI must preserve Z through musl mutex lock's TST/B.NE prefix and reach "
                    "the ADD/MOV setup immediately before LDAXR/STLXR");
}

- (void)testSemanticExecutionContract_MuslPthreadMutexLockFastPathReusesStatusRegister
{
    XCTAssertEqual(tcti_semantic_case_musl_pthread_mutex_lock_fast_path(), 0ULL,
                   @"TCTI must execute musl pthread_mutex_lock's fast LDAXR/STLXR path when "
                    "the loaded register is reused as the exclusive-store status register");
}

- (void)testSemanticExecutionContract_MuslMutexUnlockNormalTypeBranchesToFastUnlock
{
    XCTAssertEqual(tcti_semantic_case_musl_mutex_unlock_normal_type_branches_to_fast_unlock(), 0ULL,
                   @"TCTI must preserve Z from ANDS across the following non-flag logical "
                    "immediate so musl pthread_mutex_unlock reaches the normal unlock path");
}

- (void)testSemanticExecutionContract_UDIVPreservesFlagsForFollowingCSEL
{
    XCTAssertEqual(tcti_semantic_case_udiv_preserves_flags_for_csel_eq(), 0x1111111111111111ULL,
                   @"TCTI UDIV fallback must preserve NZCV so a following CSEL EQ observes the "
                    "pre-divide flags instead of helper-call host flags");
}

@end
