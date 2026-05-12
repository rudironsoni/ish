#import <XCTest/XCTest.h>

#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>
#import <IXLandLinuxRuntime/emu/aarch64/memory.h>
#import <IXLandLinuxRuntime/emu/tlb.h>
#import <IXLandLinuxRuntime/kernel/memory.h>

#include "../../Support/ISAFamilies/BaseScalar/tcti_control_and_flags_semantic_scenarios.h"
#include "../../Support/ISAFamilies/BaseScalar/tcti_scalar_runtime_semantic_scenarios.h"
#include "../../Support/ISAFamilies/Memory/tcti_memory_atomic_runtime_semantic_scenarios.h"
#include "../../Support/ISAFamilies/Memory/tcti_memory_pair_semantic_scenarios.h"

@interface TCTIControlAndFlagsSemanticTests : XCTestCase
@end

static void tcti_init_cond_branch_cpu(struct cpu_state *cpu, uint64_t pc, uint64_t pstate)
{
    memset(cpu, 0, sizeof(*cpu));
    cpu->pc = pc;
    cpu->pstate = pstate;
}

#define TCTI_DECLARE_COND_BRANCH_SEMANTIC_TEST(_name, _insn, _pc, _pstate, _expectedPC)          \
- (void)testSemanticExecutionContract_##_name                                                      \
{                                                                                                  \
    struct cpu_state cpu;                                                                          \
    static const uint32_t insn = _insn;                                                            \
    tcti_init_cond_branch_cpu(&cpu, _pc, _pstate);                                                \
    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, _pc, &insn, 1), 0);                           \
    XCTAssertEqual(cpu.pc, (uint64_t)_expectedPC);                                                 \
}

@implementation TCTIControlAndFlagsSemanticTests
- (void)testSemanticExecutionContract_MOVRegProducesCorrectResult
{
    tcti_semantic_snapshot_t snapshot;
    XCTAssertTrue(tcti_semantic_case_mov_2_7(&snapshot),
                  @"MOV X2, X7 must copy guest x7 into guest x2 without corrupting other hot "
                   "registers");
}

- (void)testSemanticExecutionContract_ADDRegProducesCorrectResult
{
    tcti_semantic_snapshot_t snapshot;
    XCTAssertTrue(tcti_semantic_case_add_7_13_14(&snapshot),
                  @"ADD X7, X13, X14 must update only the destination hot carrier");
}

- (void)testSemanticExecutionContract_BlockEntryRestoresGuestPStateForConditionalBranch
{
    XCTAssertEqual(tcti_semantic_case_entry_restores_pstate_for_bcond_ne(), 0x2000ULL,
                   @"TCTI block entry must restore guest NZCV from cpu->pstate before B.cond");
}

TCTI_DECLARE_COND_BRANCH_SEMANTIC_TEST(BEQTakenOnZeroFlag, 0x54000000, 0x70000, 0x40000000ULL,
                                       0x70000)
TCTI_DECLARE_COND_BRANCH_SEMANTIC_TEST(BNETakenOnNonZeroFlag, 0x54000001, 0x70004, 0ULL,
                                       0x70004)
TCTI_DECLARE_COND_BRANCH_SEMANTIC_TEST(BHSTakenOnCarryFlag, 0x54000002, 0x70008, 0x20000000ULL,
                                       0x70008)
TCTI_DECLARE_COND_BRANCH_SEMANTIC_TEST(BLOTakenOnCarryClear, 0x54000003, 0x7000c, 0ULL, 0x7000c)
TCTI_DECLARE_COND_BRANCH_SEMANTIC_TEST(BMITakenOnNegativeFlag, 0x54000004, 0x70010, 0x80000000ULL,
                                       0x70010)
TCTI_DECLARE_COND_BRANCH_SEMANTIC_TEST(BPLTakenOnNegativeClear, 0x54000005, 0x70014, 0ULL,
                                       0x70014)
TCTI_DECLARE_COND_BRANCH_SEMANTIC_TEST(BVSTakenOnOverflowFlag, 0x54000006, 0x70018, 0x10000000ULL,
                                       0x70018)
TCTI_DECLARE_COND_BRANCH_SEMANTIC_TEST(BVCTakenOnOverflowClear, 0x54000007, 0x7001c, 0ULL,
                                       0x7001c)
TCTI_DECLARE_COND_BRANCH_SEMANTIC_TEST(BHITakenOnCarrySetAndZeroClear, 0x54000008, 0x70020,
                                       0x20000000ULL, 0x70020)
TCTI_DECLARE_COND_BRANCH_SEMANTIC_TEST(BLSTakenOnZeroSet, 0x54000009, 0x70024, 0x40000000ULL,
                                       0x70024)
TCTI_DECLARE_COND_BRANCH_SEMANTIC_TEST(BGETakenOnMatchingSignAndOverflow, 0x5400000a, 0x70028,
                                       0ULL, 0x70028)
TCTI_DECLARE_COND_BRANCH_SEMANTIC_TEST(BLTTakenOnMismatchedSignAndOverflow, 0x5400000b, 0x7002c,
                                       0x80000000ULL, 0x7002c)
TCTI_DECLARE_COND_BRANCH_SEMANTIC_TEST(BGTTakenOnZeroClearAndMatchingSignOverflow, 0x5400000c,
                                       0x70030, 0ULL, 0x70030)
TCTI_DECLARE_COND_BRANCH_SEMANTIC_TEST(BLETakenOnZeroSet, 0x5400000d, 0x70034, 0x40000000ULL,
                                       0x70034)

TCTI_DECLARE_COND_BRANCH_SEMANTIC_TEST(BCEQTakenOnZeroFlag, 0x54000010, 0x70038, 0x40000000ULL,
                                       0x70038)
TCTI_DECLARE_COND_BRANCH_SEMANTIC_TEST(BCNETakenOnNonZeroFlag, 0x54000011, 0x7003c, 0ULL,
                                       0x7003c)
TCTI_DECLARE_COND_BRANCH_SEMANTIC_TEST(BCSHSTakenOnCarryFlag, 0x54000012, 0x70040, 0x20000000ULL,
                                       0x70040)
TCTI_DECLARE_COND_BRANCH_SEMANTIC_TEST(BCCLOTakenOnCarryClear, 0x54000013, 0x70044, 0ULL,
                                       0x70044)
TCTI_DECLARE_COND_BRANCH_SEMANTIC_TEST(BCMITakenOnNegativeFlag, 0x54000014, 0x70048, 0x80000000ULL,
                                       0x70048)
TCTI_DECLARE_COND_BRANCH_SEMANTIC_TEST(BCPLTakenOnNegativeClear, 0x54000015, 0x7004c, 0ULL,
                                       0x7004c)
TCTI_DECLARE_COND_BRANCH_SEMANTIC_TEST(BCVSTakenOnOverflowFlag, 0x54000016, 0x70050, 0x10000000ULL,
                                       0x70050)
TCTI_DECLARE_COND_BRANCH_SEMANTIC_TEST(BCVCTakenOnOverflowClear, 0x54000017, 0x70054, 0ULL,
                                       0x70054)
TCTI_DECLARE_COND_BRANCH_SEMANTIC_TEST(BCHITakenOnCarrySetAndZeroClear, 0x54000018, 0x70058,
                                       0x20000000ULL, 0x70058)
TCTI_DECLARE_COND_BRANCH_SEMANTIC_TEST(BCLSTakenOnZeroSet, 0x54000019, 0x7005c, 0x40000000ULL,
                                       0x7005c)
TCTI_DECLARE_COND_BRANCH_SEMANTIC_TEST(BCGETakenOnMatchingSignAndOverflow, 0x5400001a, 0x70060,
                                       0ULL, 0x70060)
TCTI_DECLARE_COND_BRANCH_SEMANTIC_TEST(BCLTTakenOnMismatchedSignAndOverflow, 0x5400001b, 0x70064,
                                       0x80000000ULL, 0x70064)
TCTI_DECLARE_COND_BRANCH_SEMANTIC_TEST(BCGTTakenOnZeroClearAndMatchingSignOverflow, 0x5400001c,
                                       0x70068, 0ULL, 0x70068)
TCTI_DECLARE_COND_BRANCH_SEMANTIC_TEST(BCLETakenOnZeroSet, 0x5400001d, 0x7006c, 0x40000000ULL,
                                       0x7006c)

- (void)testSemanticExecutionContract_FlagSettingFallbackLeavesGuestNZCVLiveForCCMP
{
    XCTAssertEqual(tcti_semantic_case_cmp_w20_ccmp_gt_bls_uses_32bit_flags(), 0,
                   @"CMP W20,#0 with W20=-1 must make CCMP.GT take the false NZCV immediate so "
                    "the following B.LS exits the allocator loop");
}

- (void)testSemanticExecutionContract_ExtendedCMPUsesWRegisterWidthForCSEL
{
    XCTAssertEqual(tcti_semantic_case_cmp_w2_w1_uxtb_csel_uses_w_width(),
                   0x123456789abcdef0ULL,
                   @"CMP W2,W1,UXTB must compare W2 against the extended byte, ignoring stale "
                    "high bits before CSEL consumes the flags");
}

- (void)testSemanticExecutionContract_CSELEQSelectsTrueOperand
{
    XCTAssertEqual(tcti_semantic_case_csel_eq_selects_true_operand(), 0x123456789abcdef0ULL,
                   @"CSEL.EQ must select Rn when guest PSTATE has Z set");
}

- (void)testSemanticExecutionContract_CSELPreservesNZCVForFollowingCondition
{
    XCTAssertEqual(tcti_semantic_case_csel_preserves_flags_for_bcond(), 0x2000ULL,
                   @"CSEL must preserve guest NZCV so the following conditional branch sees the "
                    "same flags");
}

- (void)testSemanticExecutionContract_CSELLSHSTracksUnsignedMinMax
{
    XCTAssertEqual(tcti_semantic_case_cmp_csel_ls_hs_tracks_unsigned_minmax(), 0ULL,
                   @"CMP followed by CSEL.LS and CSEL.HS must preserve unsigned carry/zero "
                    "semantics so ld-musl computes segment bounds correctly");
}

- (void)testSemanticExecutionContract_CSINVLSPreservesNonOverflowAllocationSize
{
    XCTAssertEqual(tcti_semantic_case_cmp_csinv_ls_preserves_nonoverflow_size(), 0x1234ULL,
                   @"CSINV ..., LS must keep the computed size when the preceding unsigned CMP "
                    "reports no overflow in ld-musl allocation sizing");
}

- (void)testSemanticExecutionContract_CSINVLSSaturatesOverflowAllocationSize
{
    XCTAssertEqual(tcti_semantic_case_cmp_csinv_ls_saturates_overflow_size(), UINT64_MAX,
                   @"CSINV ..., LS must saturate to all-ones when the preceding unsigned CMP "
                    "detects overflow in ld-musl allocation sizing");
}

- (void)testSemanticExecutionContract_GeneratedCINCNEIncrementsOnlyOnNE
{
    XCTAssertEqual(tcti_semantic_case_generated_cinc_ne_increments_only_on_ne(), 8ULL,
                   @"The generated raw CINC alias from ld-musl must preserve x0 on EQ and "
                    "increment it only when the preceding CMP is NE");
}

- (void)testSemanticExecutionContract_GeneratedCSETMWNEZeroExtendsAllOnes
{
    XCTAssertEqual(tcti_semantic_case_generated_csetm_w_ne_zero_extends(), 0x00000000ffffffffULL,
                   @"TCTI must treat 32-bit CSETM as a CSINV-width operation so the alias "
                    "produces 32-bit all-ones and clears stale high bits.");
}

- (void)testSemanticExecutionContract_GeneratedCINVWNEZeroExtendsInvertedLow32
{
    XCTAssertEqual(tcti_semantic_case_generated_cinv_w_ne_zero_extends(), 0x00000000fffffff0ULL,
                   @"TCTI must execute 32-bit CINV modulo W width and zero-extend the result "
                    "instead of preserving stale high bits from the old X register.");
}

- (void)testSemanticExecutionContract_GeneratedCNEGWNEZeroExtendsNegatedLow32
{
    XCTAssertEqual(tcti_semantic_case_generated_cneg_w_ne_zero_extends(), 0x2ULL,
                   @"TCTI must execute 32-bit CNEG modulo W width and zero-extend the result "
                    "so alias users do not leak stale X-register high bits.");
}

- (void)testSemanticExecutionContract_VsnprintfZeroSizeCSETNEPreservesZeroFlag
{
    XCTAssertEqual(tcti_semantic_case_vsnprintf_zero_size_cset_ne_preserves_zero_flag(), 0ULL,
                   @"musl vsnprintf uses CMP; CSEL; CSET.NE; SUB for zero-size buffers. CSEL "
                    "must preserve Z so CSET.NE stays 0 and the buffer length does not underflow");
}

- (void)testSemanticExecutionContract_GeneratedVsnprintfZeroSizeLengthDoesNotUnderflow
{
    XCTAssertEqual(tcti_semantic_case_generated_vsnprintf_zero_size_length(), 0ULL,
                   @"The generated TCTI block for musl vsnprintf zero-size setup must leave "
                    "remaining length at 0, not UINT64_MAX");
}

- (void)testSemanticExecutionContract_CMPAddCSELNEPreservesZeroFlag
{
    XCTAssertEqual(tcti_semantic_case_cmp_add_csel_ne_uses_preserved_zero_flag(), 0ULL,
                   @"TCTI must preserve Z=1 from CMP across non-flag ADD so CSEL NE chooses "
                    "XZR in musl sigaction");
}

- (void)testSemanticExecutionContract_ADDFromSPImmediatePreservesZeroFlagAndResult
{
    enum {
        stackTop = 0x200000,
        cmpPC = 0x51f1c,
        addPC = 0x51f20,
    };

    static const uint32_t cmpInsn = 0xf100009f; // cmp x4, #0
    static const uint32_t addInsn = 0x910103e2; // add x2, sp, #0x40

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.sp = stackTop;
    cpu.x[4] = 0;

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, cmpPC, &cmpInsn, 1), 0);
    XCTAssertEqual(cpu.pstate & 0x60000000ULL, 0x60000000ULL,
                   @"CMP X4,#0 with X4=0 must leave the architectural Z and C flags live before "
                    "the following ADD block");

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, addPC, &addInsn, 1), 0);
    XCTAssertEqual(cpu.x[2], stackTop + 0x40ULL,
                   @"ADD X2, SP, #0x40 must materialize the architectural SP-relative address");
    XCTAssertEqual(cpu.pstate & 0x60000000ULL, 0x60000000ULL,
                   @"Non-flag ADD from SP must preserve the live CMP flags for the following "
                    "conditional user");
}

- (void)testSemanticExecutionContract_CSELNEAfterCMPAndSPAddChoosesFalseOperand
{
    enum {
        stackTop = 0x200000,
        textPC = 0x51f1c,
    };

    static const uint32_t insns[] = {
        0xf100009f, // cmp x4, #0
        0x910103e2, // add x2, sp, #0x40
        0x9a9f1042, // csel x2, x2, xzr, ne
        0xd65f03c0, // ret
    };

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.sp = stackTop;
    cpu.x[2] = 0x1111111111111111ULL;
    cpu.x[4] = 0;
    cpu.x[30] = 0;

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, insns, sizeof(insns) / sizeof(insns[0])),
                   0);
    XCTAssertEqual(cpu.x[2], 0ULL,
                   @"CSEL.NE must choose XZR after CMP X4,#0 sets Z=1, even when the true "
                    "operand was just materialized from SP in the same TCTI block");
}

- (void)testSemanticExecutionContract_CMPCCMPFalseImmediateClearsZero
{
    XCTAssertEqual(tcti_semantic_case_cmp_ccmp_false_immediate_clears_zero(), 0x51f44ULL,
                   @"CCMP with a false NE condition and NZCV immediate 0 must clear Z so "
                    "musl sigaction does not copy an old action into a null pointer");
}

- (void)testSemanticExecutionContract_CCMPFalseNEWithNZCV4PublishesZeroForStrchrnulLoop
{
    enum {
        textPC = 0x6e9e4,
    };

    static const uint32_t insns[] = {
        0x7100001f, // cmp w0, #0
        0x7a411004, // ccmp w0, w1, #4, ne
        0xd65f03c0, // ret
    };

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.x[0] = 0;
    cpu.x[1] = '.';
    cpu.x[30] = 0;

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, insns, sizeof(insns) / sizeof(insns[0])),
                   0);
    XCTAssertEqual(cpu.pstate & 0x40000000ULL, 0x40000000ULL,
                   @"CCMP ..., #4, NE must publish Z=1 on the false path so strchrnul stops on "
                    "the trailing NUL byte");
}

- (void)testSemanticExecutionContract_StrchrnulVectorMaskFindsDot
{
    XCTAssertNotEqual(tcti_semantic_case_strchrnul_vector_mask_finds_dot(), 0ULL,
                      @"The musl strchrnul word scan depends on EON and BIC, not just EOR and AND");
}

- (void)testSemanticExecutionContract_StrchrnulByteLoopStopsOnMatchOrNul
{
    XCTAssertEqual(tcti_semantic_case_strchrnul_byte_loop_stops_on_match_or_nul(), 0ULL,
                   @"TCTI must preserve musl strchrnul's live ADD/LDRB/CMP/CCMP/B.NE byte loop "
                    "so the loader stops on the matching byte or trailing NUL instead of "
                    "spinning past the string boundary.");
}

- (void)testSemanticExecutionContract_StrchrnulLoopFallsThroughToMoveOnNul
{
    enum {
        textBase = 0x6e9dc,
        movPC = 0x6e9f0,
        dataBase = 0x220000,
    };

    static const uint32_t loopInsns[] = {
        0x91000442, // add x2, x2, #1
        0x39400040, // ldrb w0, [x2]
        0x7100001f, // cmp w0, #0
        0x7a411004, // ccmp w0, w1, #4, ne
        0x54ffff81, // b.ne 0x6e9dc
    };

    struct mem mem;
    mem_init(&mem);
    XCTAssertEqual(pt_map_nothing(&mem, PAGE(dataBase), 1, P_READ | P_WRITE), 0);

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.pc = textBase;
    cpu.x[1] = '.';
    cpu.x[2] = dataBase;

    XCTAssertEqual(a64_guest_write8(&cpu, &tlb, dataBase + 1, 0), A64_MEM_OK);
    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textBase, loopInsns,
                                              sizeof(loopInsns) / sizeof(loopInsns[0])),
                   0);
    XCTAssertEqual(cpu.x[2], dataBase + 1ULL,
                   @"The byte loop must advance to the NUL byte before deciding whether to loop");
    XCTAssertEqual(cpu.pc, movPC,
                   @"After seeing NUL, CCMP/B.NE must fall through to the MOV block instead of "
                    "branching back to the loop head");

    mem_destroy(&mem);
}

- (void)testSemanticExecutionContract_StrchrnulLoopSecondEntryAfterNonMatchFallsThroughOnNul
{
    enum {
        textBase = 0x6e9dc,
        movPC = 0x6e9f0,
        dataBase = 0x220000,
    };

    static const uint32_t loopInsns[] = {
        0x91000442, // add x2, x2, #1
        0x39400040, // ldrb w0, [x2]
        0x7100001f, // cmp w0, #0
        0x7a411004, // ccmp w0, w1, #4, ne
        0x54ffff81, // b.ne 0x6e9dc
    };

    struct mem mem;
    mem_init(&mem);
    XCTAssertEqual(pt_map_nothing(&mem, PAGE(dataBase), 1, P_READ | P_WRITE), 0);

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.pc = textBase;
    cpu.x[1] = '.';
    cpu.x[2] = dataBase - 1;

    XCTAssertEqual(a64_guest_write8(&cpu, &tlb, dataBase + 0, 'Z'), A64_MEM_OK);
    XCTAssertEqual(a64_guest_write8(&cpu, &tlb, dataBase + 1, 0), A64_MEM_OK);

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textBase, loopInsns,
                                              sizeof(loopInsns) / sizeof(loopInsns[0])),
                   0);
    XCTAssertEqual(cpu.pc, textBase,
                   @"The first non-matching byte must branch back to the loop head");
    XCTAssertEqual(cpu.x[1], '.' ,
                   @"The loop must preserve the needle register across same-PC re-entry");
    XCTAssertEqual(cpu.x[2], dataBase,
                   @"The first iteration must advance to the first data byte");

    cpu.pc = textBase;
    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textBase, loopInsns,
                                              sizeof(loopInsns) / sizeof(loopInsns[0])),
                   0);
    XCTAssertEqual(cpu.x[2], dataBase + 1ULL,
                   @"The second iteration must advance to the trailing NUL byte");
    XCTAssertEqual(cpu.pc, movPC,
                   @"After a non-match followed by NUL, the second same-PC execution must "
                    "fall through to the MOV block");

    mem_destroy(&mem);
}

- (void)testSemanticExecutionContract_StrchrnulNulPathCompletesMoveAndRet
{
    enum {
        textBase = 0x6e9dc,
        movPC = 0x6e9f0,
        retPC = 0x6e9f4,
        dataBase = 0x220000,
    };

    static const uint32_t loopInsns[] = {
        0x91000442, // add x2, x2, #1
        0x39400040, // ldrb w0, [x2]
        0x7100001f, // cmp w0, #0
        0x7a411004, // ccmp w0, w1, #4, ne
        0x54ffff81, // b.ne 0x6e9dc
    };
    static const uint32_t movInsn = 0xaa0203e0; // mov x0, x2
    static const uint32_t retInsn = 0xd65f03c0; // ret

    struct mem mem;
    mem_init(&mem);
    XCTAssertEqual(pt_map_nothing(&mem, PAGE(dataBase), 1, P_READ | P_WRITE), 0);

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.pc = textBase;
    cpu.x[1] = '.';
    cpu.x[2] = dataBase;
    cpu.x[30] = 0;

    XCTAssertEqual(a64_guest_write8(&cpu, &tlb, dataBase + 1, 0), A64_MEM_OK);

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textBase, loopInsns,
                                              sizeof(loopInsns) / sizeof(loopInsns[0])),
                   0);
    XCTAssertEqual(cpu.pc, movPC);

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, movPC, &movInsn, 1), 0);
    if (cpu.pc == movPC)
        cpu.pc = retPC;
    XCTAssertEqual(cpu.pc, retPC);
    XCTAssertEqual(cpu.x[0], dataBase + 1ULL,
                   @"The follow-on MOV block must publish the NUL-match address into X0");

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, retPC, &retInsn, 1), 0);
    XCTAssertEqual(cpu.pc, 0ULL,
                   @"The follow-on RET block must return through X30 after the NUL exit path");

    mem_destroy(&mem);
}

- (void)testSemanticExecutionContract_MuslMemsetDUPZeroesVectorStore
{
    XCTAssertEqual(tcti_semantic_case_musl_memset_dup_zeroes_vector_store(), 0ULL,
                   @"musl memset uses DUP v0.16b,w1 before vector stores; TCTI must update v0 "
                    "so stale SIMD state cannot corrupt guest heap/list objects");
}

- (void)testSemanticExecutionContract_MuslMemsetDUPReplicatesByteFill
{
    XCTAssertEqual(tcti_semantic_case_musl_memset_dup_replicates_byte_fill(), 0ULL,
                   @"musl memset's DUP v0.16b,w1 must replicate the low byte across every byte, "
                    "not decode as MOVI or replicate 32-bit lanes");
}

- (void)testSemanticExecutionContract_BusyboxPromptFirstTurnLDURBAndCSELHIBlockRoundTrips
{
    XCTAssertEqual(tcti_semantic_case_busybox_prompt_first_turn_ldurb_csel_hi_block(), 0ULL,
                   @"The first busybox shell prompt block must survive LDURB, CMP, CSEL.HI, and "
                    "RET in one TCTI turn without crashing or corrupting guest state");
}

- (void)testSemanticExecutionContract_BusyboxPromptLoopCCMPBLSExitsTakenPath
{
    XCTAssertEqual(tcti_semantic_case_busybox_prompt_loop_ccmp_bls_exits_taken_path(), 0ULL,
                   @"The busybox prompt loop block must preserve MVN/LDRB/SUB/ADD/CMP/CCMP/B.LS "
                    "semantics so the taken path exits the hot retry loop instead of spinning");
}

- (void)testSemanticExecutionContract_CSETThenADDOverwritesStaleX3
{
    XCTAssertEqual(tcti_semantic_case_cset_eq_then_add_to_x3(), 3ULL,
                   @"CSET.EQ followed by ADD x3,#2 must overwrite stale x3 before relocation "
                    "entry processing passes x3 into x19");
}

- (void)testSemanticExecutionContract_CMPMemoryBackedX27X23BranchesEQ
{
    XCTAssertEqual(tcti_semantic_case_cmp_memory_backed_x27_x23_branches_eq(), 0x6a690ULL,
                   @"TCTI CMP over memory-backed high registers must set NZCV so B.EQ exits "
                    "the relocation loop when x27 reaches x23");
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

- (void)testSemanticExecutionContract_MuslLsRootFrameListWalk
{
    XCTAssertEqual(tcti_semantic_case_busybox_ls_retry_ccmp_close_path(), 0ULL,
                   @"TCTI must preserve the current busybox ls retry CCMP cluster so success, "
                    "retry, and close-path fallthrough decisions match guest AArch64 flags.");
}

- (void)testSemanticExecutionContract_MuslLsRootPostOpenCallbackScan
{
    XCTAssertEqual(tcti_semantic_case_musl_ls_root_post_open_callback_scan(), 0ULL,
                   @"TCTI must preserve musl's post-open callback scan, including TBZ, BLR, RET, "
                    "and callback-loop state for the busybox ls root-directory path");
}

- (void)testSemanticExecutionContract_MuslPthreadMutexLockPrefixReachesAtomicFastPath
{
    XCTAssertEqual(tcti_semantic_case_musl_pthread_mutex_lock_prefix(), 0ULL,
                   @"TCTI must preserve Z through musl mutex lock's TST/B.NE prefix and reach "
                    "the ADD/MOV setup immediately before LDAXR/STLXR");
}

- (void)testSemanticExecutionContract_MuslMutexUnlockNormalTypeBranchesToFastUnlock
{
    XCTAssertEqual(tcti_semantic_case_musl_mutex_unlock_normal_type_branches_to_fast_unlock(), 0ULL,
                   @"TCTI must preserve Z from ANDS across the following non-flag logical "
                    "immediate so musl pthread_mutex_unlock reaches the normal unlock path");
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

- (void)testSemanticExecutionContract_MuslOpendirNonNullCallocSkipsErrorClose
{
    XCTAssertEqual(tcti_semantic_case_musl_opendir_calloc_nonnull_skips_close_path(), 0ULL,
                   @"musl opendir branches to close(2) only when calloc returns NULL; TCTI must "
                    "execute the following CBZ X0 using the full 64-bit guest pointer value");
}

- (void)testSemanticExecutionContract_MuslStrncmpLibcReservedPrefix
{
    XCTAssertEqual(tcti_semantic_case_musl_strncmp_libc_reserved_prefix(), 0ULL,
                   @"TCTI must execute musl strncmp(\"c...\", \"c.\", 2) correctly so ldso can "
                    "recognize its own libc SONAME while loading BusyBox dependencies");
}

- (void)testSemanticExecutionContract_MuslLibcNameComparePrefixStaysOnMatchPath
{
    XCTAssertEqual(
        tcti_semantic_case_musl_libc_name_compare_prefix_stays_on_match_path(), 0ULL,
        @"The live ld-musl libc-name prefix compare must keep x0 on the literal string page, "
         "load matching first bytes from the guest name and ldso literal, and fall through "
         "instead of branching to the mismatch path when the names agree.");
}

- (void)testSemanticExecutionContract_MuslLoadLibraryDetectsLibcSelf
{
    XCTAssertEqual(tcti_semantic_case_musl_load_library_detects_libc_self(), 0ULL,
                   @"TCTI must execute musl load_library's reserved libc detection path so "
                    "ldso does not load itself as a second libc dependency");
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

@end
