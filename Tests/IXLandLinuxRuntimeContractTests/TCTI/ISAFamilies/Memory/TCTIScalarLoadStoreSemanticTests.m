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

static uint64_t tcti_scalar_mask_for_size(int size)
{
    switch (size) {
    case A64_SIZE_B:
        return 0xffULL;
    case A64_SIZE_H:
        return 0xffffULL;
    case A64_SIZE_W:
        return 0xffffffffULL;
    case A64_SIZE_X:
    default:
        return UINT64_MAX;
    }
}

static int tcti_scalar_guest_write_width(struct cpu_state *cpu, struct tlb *tlb, uint64_t addr, int size,
                                         uint64_t value)
{
    switch (size) {
    case A64_SIZE_B:
        return a64_guest_write8(cpu, tlb, addr, (uint8_t)value);
    case A64_SIZE_H:
        return a64_guest_write16(cpu, tlb, addr, (uint16_t)value);
    case A64_SIZE_W:
        return a64_guest_write32(cpu, tlb, addr, (uint32_t)value);
    case A64_SIZE_X:
    default:
        return a64_guest_write64(cpu, tlb, addr, value);
    }
}

static int tcti_scalar_guest_read_width(struct cpu_state *cpu, struct tlb *tlb, uint64_t addr, int size,
                                        uint64_t *value)
{
    switch (size) {
    case A64_SIZE_B: {
        uint8_t tmp = 0;
        int rc = a64_guest_read8(cpu, tlb, addr, &tmp);
        *value = tmp;
        return rc;
    }
    case A64_SIZE_H: {
        uint16_t tmp = 0;
        int rc = a64_guest_read16(cpu, tlb, addr, &tmp);
        *value = tmp;
        return rc;
    }
    case A64_SIZE_W: {
        uint32_t tmp = 0;
        int rc = a64_guest_read32(cpu, tlb, addr, &tmp);
        *value = tmp;
        return rc;
    }
    case A64_SIZE_X:
    default: {
        uint64_t tmp = 0;
        int rc = a64_guest_read64(cpu, tlb, addr, &tmp);
        *value = tmp;
        return rc;
    }
    }
}

#define TCTI_DECLARE_SCALAR_LOAD_SEMANTIC_TEST(_name, _insn, _pc, _guestAddr, _baseReg, _baseValue, _dstReg, \
                                               _size, _isSigned, _memValue, _expected)                        \
- (void)testSemanticExecutionContract_##_name                                                                   \
{                                                                                                               \
    struct mem mem;                                                                                             \
    struct tlb tlb = {};                                                                                        \
    struct cpu_state cpu;                                                                                       \
    mem_init(&mem);                                                                                             \
    XCTAssertEqual(pt_map_nothing(&mem, PAGE(_guestAddr), 1, P_READ | P_WRITE), 0);                           \
    tlb_refresh(&tlb, &mem.mmu);                                                                                \
    memset(&cpu, 0, sizeof(cpu));                                                                               \
    cpu.mmu = &mem.mmu;                                                                                         \
    cpu.tlb = &tlb;                                                                                             \
    cpu.pc = _pc;                                                                                               \
    cpu.x[_baseReg] = _baseValue;                                                                               \
    XCTAssertEqual(tcti_scalar_guest_write_width(&cpu, &tlb, _guestAddr, _size, _memValue), A64_MEM_OK);      \
    static const uint32_t insn = _insn;                                                                         \
    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, _pc, &insn, 1), 0,                                         \
                   @"%s must execute through the real TCTI scalar load/store path", #_name);                   \
    XCTAssertEqual(cpu.x[_dstReg], (uint64_t)(_expected));                                                     \
    mem_destroy(&mem);                                                                                          \
}

#define TCTI_DECLARE_SCALAR_STORE_SEMANTIC_TEST(_name, _insn, _pc, _guestAddr, _baseReg, _baseValue, _srcReg, \
                                                _size, _srcValue)                                                \
- (void)testSemanticExecutionContract_##_name                                                                    \
{                                                                                                                \
    struct mem mem;                                                                                              \
    struct tlb tlb = {};                                                                                         \
    struct cpu_state cpu;                                                                                        \
    mem_init(&mem);                                                                                              \
    XCTAssertEqual(pt_map_nothing(&mem, PAGE(_guestAddr), 1, P_READ | P_WRITE), 0);                            \
    tlb_refresh(&tlb, &mem.mmu);                                                                                 \
    memset(&cpu, 0, sizeof(cpu));                                                                                \
    cpu.mmu = &mem.mmu;                                                                                          \
    cpu.tlb = &tlb;                                                                                              \
    cpu.pc = _pc;                                                                                                \
    cpu.x[_baseReg] = _baseValue;                                                                                \
    cpu.x[_srcReg] = (uint64_t)(_srcValue);                                                                      \
    static const uint32_t insn = _insn;                                                                          \
    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, _pc, &insn, 1), 0,                                          \
                   @"%s must execute through the real TCTI scalar load/store path", #_name);                    \
    uint64_t stored = 0;                                                                                         \
    XCTAssertEqual(tcti_scalar_guest_read_width(&cpu, &tlb, _guestAddr, _size, &stored), A64_MEM_OK);          \
    XCTAssertEqual(stored, ((uint64_t)(_srcValue) & tcti_scalar_mask_for_size(_size)));                         \
    mem_destroy(&mem);                                                                                           \
}

#define TCTI_DECLARE_PREFETCH_SEMANTIC_TEST(_name, _insn, _pc, _guestAddr, _baseReg)                           \
- (void)testSemanticExecutionContract_##_name                                                                   \
{                                                                                                               \
    struct mem mem;                                                                                             \
    struct tlb tlb = {};                                                                                        \
    struct cpu_state cpu;                                                                                       \
    mem_init(&mem);                                                                                             \
    XCTAssertEqual(pt_map_nothing(&mem, PAGE(_guestAddr), 1, P_READ | P_WRITE), 0);                           \
    tlb_refresh(&tlb, &mem.mmu);                                                                                \
    memset(&cpu, 0, sizeof(cpu));                                                                               \
    cpu.mmu = &mem.mmu;                                                                                         \
    cpu.tlb = &tlb;                                                                                             \
    cpu.pc = _pc;                                                                                               \
    cpu.x[_baseReg] = _guestAddr;                                                                               \
    cpu.x[30] = 0x1122334455667788ULL;                                                                          \
    static const uint32_t insn = _insn;                                                                         \
    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, _pc, &insn, 1), 0,                                         \
                   @"%s must execute through the real TCTI prefetch path", #_name);                             \
    XCTAssertEqual(cpu.x[30], 0x1122334455667788ULL);                                                           \
    mem_destroy(&mem);                                                                                          \
}

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

TCTI_DECLARE_SCALAR_LOAD_SEMANTIC_TEST(ScalarLoadStore_LDRBReadsZeroExtendedByte, 0x39400020, 0x100000,
                                       0x240000, 1, 0x240000, 0, A64_SIZE_B, NO, 0x5a, 0x5aULL)
TCTI_DECLARE_SCALAR_LOAD_SEMANTIC_TEST(ScalarLoadStore_LDRSWReadsSignExtendedWord, 0xb9800062, 0x100004,
                                       0x240040, 3, 0x240040, 2, A64_SIZE_W, YES, 0x89abcdefU,
                                       0xffffffff89abcdefULL)
TCTI_DECLARE_SCALAR_STORE_SEMANTIC_TEST(ScalarLoadStore_STRBStoresLowByte, 0x390000a4, 0x100008,
                                        0x240080, 5, 0x240080, 4, A64_SIZE_B, 0x12345678U)
TCTI_DECLARE_SCALAR_STORE_SEMANTIC_TEST(ScalarLoadStore_STRHStoresLowHalfword, 0x790000e6, 0x10000c,
                                        0x2400c0, 7, 0x2400c0, 6, A64_SIZE_H, 0x12345678U)
TCTI_DECLARE_SCALAR_LOAD_SEMANTIC_TEST(ScalarLoadStore_LDURHReadsUnscaledHalfword, 0x785ff128, 0x100010,
                                       0x240100, 9, 0x240101, 8, A64_SIZE_H, NO, 0x7abcU, 0x7abcULL)
TCTI_DECLARE_SCALAR_LOAD_SEMANTIC_TEST(ScalarLoadStore_LDURSBReadsSignExtendedByte, 0x389ff16a, 0x100014,
                                       0x240140, 11, 0x240141, 10, A64_SIZE_B, YES, 0x80U,
                                       0xffffffffffffff80ULL)
TCTI_DECLARE_SCALAR_LOAD_SEMANTIC_TEST(ScalarLoadStore_LDURSHReadsSignExtendedHalfword, 0x789ff1ac, 0x100018,
                                       0x240180, 13, 0x240181, 12, A64_SIZE_H, YES, 0x8001U,
                                       0xffffffffffff8001ULL)
TCTI_DECLARE_SCALAR_LOAD_SEMANTIC_TEST(ScalarLoadStore_LDURSWReadsSignExtendedWord, 0xb89fc1ee, 0x10001c,
                                       0x2401c0, 15, 0x2401c4, 14, A64_SIZE_W, YES, 0x80000011U,
                                       0xffffffff80000011ULL)
TCTI_DECLARE_SCALAR_STORE_SEMANTIC_TEST(ScalarLoadStore_STURBStoresUnscaledByte, 0x381ff230, 0x100020,
                                        0x240200, 17, 0x240201, 16, A64_SIZE_B, 0xa5U)
TCTI_DECLARE_SCALAR_STORE_SEMANTIC_TEST(ScalarLoadStore_STURHStoresUnscaledHalfword, 0x781fe272, 0x100024,
                                        0x240240, 19, 0x240242, 18, A64_SIZE_H, 0xbeefU)
TCTI_DECLARE_SCALAR_LOAD_SEMANTIC_TEST(ScalarLoadStore_LDTRReadsUnprivilegedDoubleword, 0xf8400ab4, 0x100028,
                                       0x240280, 21, 0x240280, 20, A64_SIZE_X, NO,
                                       0x1122334455667788ULL, 0x1122334455667788ULL)
TCTI_DECLARE_SCALAR_LOAD_SEMANTIC_TEST(ScalarLoadStore_LDTRBReadsUnprivilegedByte, 0x38400af6, 0x10002c,
                                       0x2402c0, 23, 0x2402c0, 22, A64_SIZE_B, NO, 0x44U, 0x44ULL)
TCTI_DECLARE_SCALAR_LOAD_SEMANTIC_TEST(ScalarLoadStore_LDTRHReadsUnprivilegedHalfword, 0x78400b38, 0x100030,
                                       0x240300, 25, 0x240300, 24, A64_SIZE_H, NO, 0x3344U, 0x3344ULL)
TCTI_DECLARE_SCALAR_LOAD_SEMANTIC_TEST(ScalarLoadStore_LDTRSBReadsUnprivilegedSignedByte, 0x38800b7a, 0x100034,
                                       0x240340, 27, 0x240340, 26, A64_SIZE_B, YES, 0x81U,
                                       0xffffffffffffff81ULL)
TCTI_DECLARE_SCALAR_LOAD_SEMANTIC_TEST(ScalarLoadStore_LDTRSHReadsUnprivilegedSignedHalfword, 0x78800bbc, 0x100038,
                                       0x240380, 29, 0x240380, 28, A64_SIZE_H, YES, 0x8002U,
                                       0xffffffffffff8002ULL)
TCTI_DECLARE_SCALAR_LOAD_SEMANTIC_TEST(ScalarLoadStore_LDTRSWReadsUnprivilegedSignedWord, 0xb8800820, 0x10003c,
                                       0x2403c0, 1, 0x2403c0, 0, A64_SIZE_W, YES, 0x80000033U,
                                       0xffffffff80000033ULL)
TCTI_DECLARE_SCALAR_STORE_SEMANTIC_TEST(ScalarLoadStore_STTRStoresUnprivilegedDoubleword, 0xf8000862, 0x100040,
                                        0x240400, 3, 0x240400, 2, A64_SIZE_X, 0x8877665544332211ULL)
TCTI_DECLARE_SCALAR_STORE_SEMANTIC_TEST(ScalarLoadStore_STTRBStoresUnprivilegedByte, 0x380008a4, 0x100044,
                                        0x240440, 5, 0x240440, 4, A64_SIZE_B, 0xfeU)
TCTI_DECLARE_SCALAR_STORE_SEMANTIC_TEST(ScalarLoadStore_STTRHStoresUnprivilegedHalfword, 0x780008e6, 0x100048,
                                        0x240480, 7, 0x240480, 6, A64_SIZE_H, 0xcafeU)
TCTI_DECLARE_SCALAR_LOAD_SEMANTIC_TEST(ScalarLoadStore_LDAPURReadsOrderedDoubleword, 0xd9400128, 0x10004c,
                                       0x2404c0, 9, 0x2404c0, 8, A64_SIZE_X, NO,
                                       0x0f1e2d3c4b5a6978ULL, 0x0f1e2d3c4b5a6978ULL)
TCTI_DECLARE_SCALAR_LOAD_SEMANTIC_TEST(ScalarLoadStore_LDAPURBReadsOrderedByte, 0x1940016a, 0x100050,
                                       0x240500, 11, 0x240500, 10, A64_SIZE_B, NO, 0x73U, 0x73ULL)
TCTI_DECLARE_SCALAR_LOAD_SEMANTIC_TEST(ScalarLoadStore_LDAPURHReadsOrderedHalfword, 0x594001ac, 0x100054,
                                       0x240540, 13, 0x240540, 12, A64_SIZE_H, NO, 0x6b7cU, 0x6b7cULL)
TCTI_DECLARE_SCALAR_LOAD_SEMANTIC_TEST(ScalarLoadStore_LDAPURSBReadsOrderedSignedByte, 0x198001ee, 0x100058,
                                       0x240580, 15, 0x240580, 14, A64_SIZE_B, YES, 0x90U,
                                       0xffffffffffffff90ULL)
TCTI_DECLARE_SCALAR_LOAD_SEMANTIC_TEST(ScalarLoadStore_LDAPURSHReadsOrderedSignedHalfword, 0x59800230, 0x10005c,
                                       0x2405c0, 17, 0x2405c0, 16, A64_SIZE_H, YES, 0x9001U,
                                       0xffffffffffff9001ULL)
TCTI_DECLARE_SCALAR_LOAD_SEMANTIC_TEST(ScalarLoadStore_LDAPURSWReadsOrderedSignedWord, 0x99800272, 0x100060,
                                       0x240600, 19, 0x240600, 18, A64_SIZE_W, YES, 0x8ffff001U,
                                       0xffffffff8ffff001ULL)
TCTI_DECLARE_SCALAR_STORE_SEMANTIC_TEST(ScalarLoadStore_STLURStoresOrderedDoubleword, 0xd90002b4, 0x100064,
                                        0x240640, 21, 0x240640, 20, A64_SIZE_X, 0x0102030405060708ULL)
TCTI_DECLARE_SCALAR_STORE_SEMANTIC_TEST(ScalarLoadStore_STLURBStoresOrderedByte, 0x190002f6, 0x100068,
                                        0x240680, 23, 0x240680, 22, A64_SIZE_B, 0x5dU)
TCTI_DECLARE_SCALAR_STORE_SEMANTIC_TEST(ScalarLoadStore_STLURHStoresOrderedHalfword, 0x59000338, 0x10006c,
                                        0x2406c0, 25, 0x2406c0, 24, A64_SIZE_H, 0x1357U)
TCTI_DECLARE_PREFETCH_SEMANTIC_TEST(ScalarLoadStore_PRFMSucceedsWithoutArchitecturalClobber, 0xf9800000,
                                    0x100070, 0x240700, 0)
TCTI_DECLARE_PREFETCH_SEMANTIC_TEST(ScalarLoadStore_PRFUMSucceedsWithoutArchitecturalClobber, 0xf8800020,
                                    0x100074, 0x240740, 1)

@end
