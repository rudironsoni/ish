#import <XCTest/XCTest.h>

#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>
#import <IXLandLinuxRuntime/emu/aarch64/memory.h>
#import <IXLandLinuxRuntime/emu/tlb.h>
#import <IXLandLinuxRuntime/kernel/memory.h>

#include "../../Support/ISAFamilies/BaseScalar/tcti_scalar_runtime_semantic_scenarios.h"
#include "../../Support/ISAFamilies/Memory/tcti_memory_atomic_runtime_semantic_scenarios.h"
#include "../../Support/ISAFamilies/Memory/tcti_memory_pair_semantic_scenarios.h"

@interface TCTIScalarLoadStoreSemanticTests : XCTestCase
@end

@implementation TCTIScalarLoadStoreSemanticTests

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

- (void)testSemanticExecutionContract_SPRelativeLDRSTRRoundtrip
{
    XCTAssertEqual(tcti_semantic_case_sp_relative_ldr_str_roundtrip(), 0ULL,
                   @"TCTI must preserve 64-bit SP-relative load/store immediate semantics for "
                    "the musl post-open root bucket loop.");
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

- (void)testSemanticExecutionContract_MuslMemcpy8TailRoundtrip
{
    XCTAssertEqual(tcti_semantic_case_musl_memcpy8_tail_roundtrip(), 0ULL,
                   @"TCTI must preserve musl memcpy's 8-byte tail copy with unscaled negative "
                    "offset loads and stores, because qsort uses this helper in the live Alpine "
                    "directory-sort path.");
}

- (void)testSemanticExecutionContract_LDURBNegativeTwoReadsPrecedingByte
{
    enum {
        textPC = 0x94000,
        dataBase = 0x250000,
    };

    static const uint32_t insn = 0x385fe004; // ldurb w4, [x0, #-2]

    struct mem mem;
    mem_init(&mem);
    XCTAssertEqual(pt_map_nothing(&mem, PAGE(dataBase), 1, P_READ | P_WRITE), 0);

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.x[0] = dataBase + 2;

    XCTAssertEqual(a64_guest_write8(&cpu, &tlb, dataBase + 0, 0x5a), A64_MEM_OK);
    XCTAssertEqual(a64_guest_write8(&cpu, &tlb, dataBase + 1, 0xc3), A64_MEM_OK);
    XCTAssertEqual(a64_guest_write8(&cpu, &tlb, dataBase + 2, 0x99), A64_MEM_OK);

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                   @"LDURB with imm=-2 must read the preceding byte through the real TCTI "
                    "unscaled load path because BusyBox uname reaches this exact form before "
                    "printing the guest machine string");
    XCTAssertEqual(cpu.x[4], 0x5aULL,
                   @"LDURB w4, [x0, #-2] must publish the zero-extended byte from x0-2");

    mem_destroy(&mem);
}

- (void)testSemanticExecutionContract_STRXZRPostIndexWritesBackBase
{
    XCTAssertEqual(tcti_semantic_case_str_xzr_post_index_writes_back_base(), 0ULL,
                   @"TCTI must execute STR XZR post-index stores with both the zero store and "
                    "the base-register writeback preserved.");
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

- (void)testSemanticExecutionContract_DynamicTagScaledStoreUsesFullIndex
{
    XCTAssertEqual(tcti_semantic_case_dynamic_tag_scaled_store_uses_full_index(), 0ULL,
                   @"TCTI STR register-offset with LSL #3 must store DT_RELR in tag slot 36 "
                    "without corrupting the DT_REL slot");
}

- (void)testSemanticExecutionContract_MuslRELRLoopTerminatesAtTableEnd
{
    XCTAssertEqual(tcti_semantic_case_musl_relr_loop_terminates_at_table_end(), 0ULL,
                   @"TCTI must execute musl _dlstart RELR bitmap relocation loop exactly once "
                    "over RELRSZ without running relocation writes past the mapped data segment");
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

- (void)testSemanticExecutionContract_MuslGNUHashMalloc
{
    XCTAssertEqual(tcti_semantic_case_musl_gnu_hash_malloc(), 0ULL,
                   @"TCTI must execute musl's GNU hash loop with 32-bit shifted ADD on "
                    "memory-backed registers so dynamic symbol lookup can find malloc/free");
}

@end
