#import <XCTest/XCTest.h>

#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>
#import <IXLandLinuxRuntime/emu/aarch64/memory.h>
#import <IXLandLinuxRuntime/emu/tlb.h>
#import <IXLandLinuxRuntime/kernel/memory.h>

#include "../../Support/ISAFamilies/BaseScalar/tcti_control_and_flags_semantic_scenarios.h"

@interface TCTIControlAndFlagsSemanticTests : XCTestCase
@end

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

- (void)testSemanticExecutionContract_FlagSettingFallbackLeavesGuestNZCVLiveForCCMP
{
    XCTAssertEqual(tcti_semantic_case_cmp_w20_ccmp_gt_bls_uses_32bit_flags(), 0,
                   @"CMP W20,#0 with W20=-1 must make CCMP.GT take the false NZCV immediate so "
                    "the following B.LS exits the allocator loop");
}

- (void)testSemanticExecutionContract_TPIDREL0RoundtripsThroughFullSysregEncoding
{
    XCTAssertEqual(tcti_semantic_case_tpidr_el0_roundtrips_through_full_sysreg_encoding(), 0ULL,
                   @"TCTI must honor the full decoded TPIDR_EL0 sysreg encoding so MSR/MRS "
                    @"roundtrip the guest thread pointer in live ldso paths");
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

@end
