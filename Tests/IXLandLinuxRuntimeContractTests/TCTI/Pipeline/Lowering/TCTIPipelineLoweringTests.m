#import <XCTest/XCTest.h>

#import <IXLandLinuxRuntime/emu/aarch64/decode.h>
#import <IXLandLinuxRuntime/tcti/aarch64/gen.h>

typedef void (*tcti_gadget_t)(void);
extern tcti_gadget_t gadget_atomic_ldst;
extern tcti_gadget_t gadget_simd_tbl;
extern tcti_gadget_t gadget_simd_tbx;
extern tcti_gadget_t gadget_simd_xtn;
extern tcti_gadget_t gadget_simd_xtn2;
extern tcti_gadget_t gadget_simd_zip1;
extern tcti_gadget_t gadget_simd_zip2;
extern tcti_gadget_t gadget_simd_trn1;
extern tcti_gadget_t gadget_simd_trn2;
extern tcti_gadget_t gadget_simd_uzp1;
extern tcti_gadget_t gadget_simd_uzp2;
extern tcti_gadget_t gadget_simd_and;
extern tcti_gadget_t gadget_simd_orr;
extern tcti_gadget_t gadget_simd_eor;
extern tcti_gadget_t gadget_simd_add;
extern tcti_gadget_t gadget_simd_sub;
extern tcti_gadget_t gadget_simd_mul;
extern tcti_gadget_t gadget_simd_bic;
extern tcti_gadget_t gadget_simd_orn;
extern tcti_gadget_t gadget_simd_bsl;
extern tcti_gadget_t gadget_simd_bit;
extern tcti_gadget_t gadget_simd_bif;
extern tcti_gadget_t gadget_simd_ext;
extern tcti_gadget_t gadget_simd_cnt;
extern tcti_gadget_t gadget_simd_ins_gpr;
extern tcti_gadget_t gadget_simd_cmge;
extern tcti_gadget_t gadget_simd_cmhi;
extern tcti_gadget_t gadget_simd_cmhs;
extern tcti_gadget_t gadget_simd_cmle;
extern tcti_gadget_t gadget_simd_cmlt;
extern tcti_gadget_t gadget_simd_cmtst;
extern void gadget_br_impl(void);
extern void gadget_ccmp_fallback_impl(void);

#define TCTI_DECLARE_COND_BRANCH_LOWERING_TEST(_name, _insn, _pc, _cond)                          \
- (void)testLoweringContract_##_name                                                               \
{                                                                                                  \
    a64_instr_t decoded = [self decodeInstruction:_insn];                                          \
    XCTAssertEqual(decoded.cat, A64_BRANCH);                                                       \
    XCTAssertEqual(decoded.subtype, A64_BRANCH_COND);                                              \
    XCTAssertEqual(decoded.cond, _cond);                                                           \
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];                                              \
    a64_gen_state_t state;                                                                         \
    [self generateInstruction:_insn atPC:_pc state:&state gadgets:gadgets];                       \
    XCTAssertTrue(state.is_complete);                                                              \
    XCTAssertEqual(state.num_gadgets, (size_t)3);                                                  \
}

#define TCTI_DECLARE_ATOMIC_LOWERING_TEST(_name, _insn, _pc, _rd, _rn, _rm, _size, _isLoad)      \
- (void)testLoweringContract_##_name                                                               \
{                                                                                                  \
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];                                              \
    a64_gen_state_t state;                                                                         \
    [self generateInstruction:_insn atPC:_pc state:&state gadgets:gadgets];                       \
    XCTAssertEqual(gadgets[0], gadget_atomic_ldst);                                                \
    XCTAssertGreaterThanOrEqual(state.num_gadgets, (size_t)7);                                     \
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], (uint64_t)_pc);                               \
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], (uint64_t)_rd);                               \
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[3], (uint64_t)_rn);                               \
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[4], (uint64_t)_rm);                               \
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[5], (uint64_t)_size);                             \
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[6], (uint64_t)_isLoad);                           \
}

#define TCTI_DECLARE_PAIR_ATOMIC_LOWERING_TEST(_name, _insn, _pc, _rn, _size, _isLoad, _rt2)     \
- (void)testLoweringContract_##_name                                                               \
{                                                                                                  \
    a64_instr_t decoded = [self decodeInstruction:_insn];                                          \
    XCTAssertTrue(decoded.is_pair,                                                                  \
                  @"Pair-exclusive lowering must begin from a pair-classified decode");            \
    XCTAssertEqual(bits(_insn, 14, 10), _rt2);                                                     \
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];                                              \
    a64_gen_state_t state;                                                                         \
    [self generateInstruction:_insn atPC:_pc state:&state gadgets:gadgets];                       \
    XCTAssertEqual(gadgets[0], gadget_atomic_ldst);                                                \
    XCTAssertGreaterThanOrEqual(state.num_gadgets, (size_t)7);                                     \
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], (uint64_t)_pc);                               \
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[3], (uint64_t)_rn);                               \
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[5], (uint64_t)_size);                             \
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[6], (uint64_t)_isLoad);                           \
}

#define TCTI_DECLARE_SCALAR_LDST_LOWERING_TEST(_name, _insn, _pc, _rd, _rn, _size, _is64,        \
                                               _isSigned, _idxMode, _imm)                          \
- (void)testLoweringContract_##_name                                                               \
{                                                                                                  \
    a64_instr_t decoded = [self decodeInstruction:_insn];                                          \
    XCTAssertEqual(decoded.cat, A64_LD_ST);                                                        \
    XCTAssertEqual(decoded.subtype, A64_LDST_SINGLE);                                              \
    XCTAssertEqual(decoded.Rd, _rd);                                                               \
    XCTAssertEqual(decoded.Rn, _rn);                                                               \
    XCTAssertEqual(decoded.size, _size);                                                           \
    XCTAssertEqual(decoded.is_64bit, _is64);                                                       \
    XCTAssertEqual(decoded.is_signed, _isSigned);                                                  \
    XCTAssertEqual(decoded.idx_mode, _idxMode);                                                    \
    XCTAssertEqual(decoded.imm, _imm);                                                             \
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];                                              \
    a64_gen_state_t state;                                                                         \
    [self generateInstruction:_insn atPC:_pc state:&state gadgets:gadgets];                       \
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);                                            \
}

#define TCTI_DECLARE_PREFETCH_LOWERING_TEST(_name, _insn, _pc, _rn, _idxMode, _imm)               \
- (void)testLoweringContract_##_name                                                               \
{                                                                                                  \
    a64_instr_t decoded = [self decodeInstruction:_insn];                                          \
    XCTAssertEqual(decoded.cat, A64_LD_ST);                                                        \
    XCTAssertEqual(decoded.subtype, A64_LDST_SINGLE);                                              \
    XCTAssertEqual(decoded.Rn, _rn);                                                               \
    XCTAssertEqual(decoded.idx_mode, _idxMode);                                                    \
    XCTAssertEqual(decoded.imm, _imm);                                                             \
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];                                              \
    a64_gen_state_t state;                                                                         \
    [self generateInstruction:_insn atPC:_pc state:&state gadgets:gadgets];                       \
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);                                            \
}

#define TCTI_DECLARE_VECTOR_LOWERING_TEST(_name, _insn, _pc, _gadget, _rd, _rn, _rm, _vecBytes)  \
- (void)testLoweringContract_##_name                                                               \
{                                                                                                  \
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];                                              \
    a64_gen_state_t state;                                                                         \
    [self generateInstruction:_insn atPC:_pc state:&state gadgets:gadgets];                       \
    XCTAssertEqual(gadgets[0], _gadget);                                                           \
    XCTAssertGreaterThanOrEqual(state.num_gadgets, (size_t)5);                                     \
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], (uint64_t)_rd);                               \
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], (uint64_t)_rn);                               \
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[3], (uint64_t)_rm);                               \
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[4], (uint64_t)_vecBytes);                         \
}

#define TCTI_DECLARE_VECTOR_UNARY_LOWERING_TEST(_name, _insn, _pc, _gadget, _rd, _rn, _vecBytes) \
- (void)testLoweringContract_##_name                                                                \
{                                                                                                   \
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];                                               \
    a64_gen_state_t state;                                                                          \
    [self generateInstruction:_insn atPC:_pc state:&state gadgets:gadgets];                        \
    XCTAssertEqual(gadgets[0], _gadget);                                                            \
    XCTAssertGreaterThanOrEqual(state.num_gadgets, (size_t)4);                                      \
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], (uint64_t)_rd);                                \
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], (uint64_t)_rn);                                \
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[3], (uint64_t)_vecBytes);                          \
}

#define TCTI_DECLARE_REJECT_LOWERING_TEST(_name, _insn, _pc)                                       \
- (void)testLoweringContract_##_name                                                                \
{                                                                                                   \
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];                                               \
    a64_gen_state_t state;                                                                          \
    [self rejectInstruction:_insn atPC:_pc state:&state gadgets:gadgets];                          \
}

// TCTI.Pipeline.Lowering Contract Tests
// Tests for stage [3] LOWERING: semantic op -> gadget chain plan
//
// Boundary: B2 (normalized semantic op) -> B3 (gadget chain shape)

@interface TCTIPipelineLoweringTests : XCTestCase
@end

@implementation TCTIPipelineLoweringTests

- (a64_instr_t)decodeInstruction:(uint32_t)insn
{
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(insn, &decoded), 0);
    return decoded;
}

- (void)generateInstruction:(uint32_t)insn
                       atPC:(uint64_t)pc
                      state:(a64_gen_state_t *)state
                    gadgets:(tcti_gadget_t *)gadgets
{
    XCTAssertEqual(a64_gen_init(state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(state, pc);
    XCTAssertEqual(a64_gen_instruction(state, insn, pc), A64_GEN_OK);
    XCTAssertGreaterThan(state->num_gadgets, (size_t)0);
}

- (void)rejectInstruction:(uint32_t)insn
                     atPC:(uint64_t)pc
                    state:(a64_gen_state_t *)state
                  gadgets:(tcti_gadget_t *)gadgets
{
    XCTAssertEqual(a64_gen_init(state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(state, pc);
    XCTAssertNotEqual(a64_gen_instruction(state, insn, pc), A64_GEN_OK,
                      @"Unsupported decode-owned instructions must reject explicitly during "
                       "lowering instead of producing a partial gadget stream");
    XCTAssertEqual(state->num_gadgets, (size_t)0);
}

- (void)testLoweringContract_SUBExtendedSPSourceAndDestinationLowers
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    uint32_t subSpSpX0Uxtx = 0xcb2063ff;
    [self generateInstruction:subSpSpX0Uxtx atPC:0x6d2b8 state:&state gadgets:gadgets];
}

- (void)testLoweringContract_AArch64BarriersDecodeAndLower
{
    struct {
        uint32_t raw;
        uint8_t op;
    } barriers[] = {
        {0xd5033f9f, 4}, // DSB SY
        {0xd5033bbf, 5}, // DMB ISH
        {0xd5033fdf, 6}, // ISB SY
    };

    for (size_t i = 0; i < sizeof(barriers) / sizeof(barriers[0]); i++) {
        a64_instr_t decoded = [self decodeInstruction:barriers[i].raw];
        XCTAssertEqual(decoded.subtype, A64_SYSTEM_BARRIER);
        XCTAssertEqual(decoded.op, barriers[i].op);

        tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
        a64_gen_state_t state;
        [self generateInstruction:barriers[i].raw atPC:0x190e8 state:&state gadgets:gadgets];
    }
}

- (void)testLoweringContract_BRMemoryBackedX16Lowers
{
    uint32_t brX16 = 0xd61f0200;
    a64_instr_t decoded = [self decodeInstruction:brX16];
    XCTAssertEqual(decoded.cat, A64_BRANCH2);
    XCTAssertEqual(decoded.subtype, A64_BRANCH_REG);
    XCTAssertEqual(decoded.Rn, 16);
    XCTAssertNotEqual((uintptr_t)gadget_br_impl, (uintptr_t)0);

    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:brX16 atPC:0x69800 state:&state gadgets:gadgets];
    XCTAssertTrue(state.is_complete);
}

- (void)testLoweringContract_CBZMemoryBackedRegisterLowers
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    uint32_t cbzX21 = 0xb40004d5;
    [self generateInstruction:cbzX21 atPC:0x6c084 state:&state gadgets:gadgets];
    XCTAssertTrue(state.is_complete);
}

- (void)testLoweringContract_CompareBranchFamilyDecodesAndLowers
{
    struct {
        uint32_t raw;
        int op;
        int rd;
        BOOL is64Bit;
    } cases[] = {
        {0xb40004d5, 0, 21, YES}, // cbz x21, ...
        {0x34000380, 0, 0, NO},   // cbz w0, ...
        {0xb5000202, 1, 2, YES},  // cbnz x2, ...
        {0x35fffde3, 1, 3, NO},   // cbnz w3, ...
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        a64_instr_t decoded = [self decodeInstruction:cases[i].raw];
        XCTAssertEqual(decoded.subtype, A64_BRANCH_CMP);
        XCTAssertEqual(decoded.op, cases[i].op);
        XCTAssertEqual(decoded.Rd, cases[i].rd);
        XCTAssertEqual(decoded.is_64bit, cases[i].is64Bit);

        tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
        a64_gen_state_t state;
        [self generateInstruction:cases[i].raw
                             atPC:0x6c084 + (uint64_t)(i * 4)
                            state:&state
                          gadgets:gadgets];
        XCTAssertTrue(state.is_complete);
    }
}

- (void)testLoweringContract_TestBranchFamilyDecodesAndLowers
{
    struct {
        uint32_t raw;
        int op;
        int rd;
        int bitIndex;
    } cases[] = {
        {0x36f805a0, 0, 0, 31}, // tbz w0, #31, ...
        {0x37f80400, 1, 0, 31}, // tbnz w0, #31, ...
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        a64_instr_t decoded = [self decodeInstruction:cases[i].raw];
        XCTAssertEqual(decoded.subtype, A64_BRANCH_TEST);
        XCTAssertEqual(decoded.op, cases[i].op);
        XCTAssertEqual(decoded.Rd, cases[i].rd);
        XCTAssertEqual(decoded.imm_shift, cases[i].bitIndex);

        tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
        a64_gen_state_t state;
        [self generateInstruction:cases[i].raw
                             atPC:0x6d298 + (uint64_t)(i * 4)
                            state:&state
                          gadgets:gadgets];
        XCTAssertTrue(state.is_complete);
    }
}

- (void)testLoweringContract_CSINCAliasCSETLowers
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    uint32_t csincX3XzrXzrNe = 0x9a9f17e3;
    [self generateInstruction:csincX3XzrXzrNe atPC:0x6bfb0 state:&state gadgets:gadgets];
}

- (void)testLoweringContract_ConditionalSelectFamilyAliasesDecodeAndLower
{
    struct {
        uint32_t raw;
        int subtype;
        BOOL is64Bit;
    } cases[] = {
        {0x9a9f07e0, 1, YES}, // cset x0, ne -> csinc x0, xzr, xzr, eq
        {0xda9f03e1, 2, YES}, // csetm x1, ne -> csinv x1, xzr, xzr, eq
        {0x9a820442, 1, YES}, // cinc x2, x2, ne
        {0xda830063, 2, YES}, // cinv x3, x3, ne
        {0xda840484, 3, YES}, // cneg x4, x4, ne
        {0x1a9f07e5, 1, NO},  // cset w5, ne
        {0x5a9f03e6, 2, NO},  // csetm w6, ne
        {0x1a8704e7, 1, NO},  // cinc w7, w7, ne
        {0x5a880108, 2, NO},  // cinv w8, w8, ne
        {0x5a890529, 3, NO},  // cneg w9, w9, ne
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        a64_instr_t decoded = [self decodeInstruction:cases[i].raw];
        XCTAssertEqual(decoded.subtype, cases[i].subtype);
        XCTAssertEqual(decoded.is_64bit, cases[i].is64Bit);

        tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
        a64_gen_state_t state;
        [self generateInstruction:cases[i].raw
                             atPC:0x6bfb4 + (uint64_t)(i * 4)
                            state:&state
                          gadgets:gadgets];
    }
}

- (void)testLoweringContract_ADDShiftedMemoryBackedRnLowers
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    XCTAssertEqual(a64_gen_init(&state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(&state, 0x6b1c8);

    uint32_t addX14X20X0 = 0x8b00028e;
    XCTAssertEqual(a64_gen_instruction(&state, addX14X20X0, 0x6b1c8), A64_GEN_OK);
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
}

- (void)testLoweringContract_SMULLAliasDecodesAndLowersAsThreeSourceMultiply
{
    uint32_t smullX0W0W1 = 0x9b217c00;
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(smullX0W0W1, &decoded), 0);
    XCTAssertEqual(decoded.subtype, A64_DP_REG_SMADDL);
    XCTAssertEqual(decoded.Rd, 0);
    XCTAssertEqual(decoded.Rn, 0);
    XCTAssertEqual(decoded.Rm, 1);
    XCTAssertEqual(decoded.Ra, 31);

    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    XCTAssertEqual(a64_gen_init(&state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(&state, 0x6b1c4);

    XCTAssertEqual(a64_gen_instruction(&state, smullX0W0W1, 0x6b1c4), A64_GEN_OK);
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
}

- (void)testLoweringContract_CCMPRegisterDecodesAndLowers
{
    uint32_t ccmpX14X0Nzcv4Ne = 0xfa4011c4;
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(ccmpX14X0Nzcv4Ne, &decoded), 0);
    XCTAssertEqual(decoded.subtype, A64_DP_REG_CCMP);
    XCTAssertEqual(decoded.Rn, 14);
    XCTAssertEqual(decoded.Rm, 0);
    XCTAssertEqual(decoded.cond, A64_NE);
    XCTAssertEqual(decoded.imm, 4);

    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    XCTAssertEqual(a64_gen_init(&state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(&state, 0x6bd48);

    XCTAssertEqual(a64_gen_instruction(&state, ccmpX14X0Nzcv4Ne, 0x6bd48), A64_GEN_OK);
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
    XCTAssertNotEqual((uintptr_t)gadgets[0], (uintptr_t)gadget_ccmp_fallback_impl);
}

- (void)testLoweringContract_CCMPImmediateDecodesAndLowers
{
    uint32_t ccmpW15Imm2Nzcv0Eq = 0x7a4209e0;
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(ccmpW15Imm2Nzcv0Eq, &decoded), 0);
    XCTAssertEqual(decoded.subtype, A64_DP_REG_CCMP_IMM);
    XCTAssertEqual(decoded.Rn, 15);
    XCTAssertEqual(decoded.imm_shift, 2);
    XCTAssertEqual(decoded.cond, A64_EQ);
    XCTAssertEqual(decoded.imm, 0);

    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    XCTAssertEqual(a64_gen_init(&state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(&state, 0x6bd54);

    XCTAssertEqual(a64_gen_instruction(&state, ccmpW15Imm2Nzcv0Eq, 0x6bd54), A64_GEN_OK);
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
    XCTAssertNotEqual((uintptr_t)gadgets[0], (uintptr_t)gadget_ccmp_fallback_impl);
}

- (void)testLoweringContract_LDPSWPairDecodesAndLowers
{
    uint32_t ldpswX1X0Sp = 0x695603e1;
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(ldpswX1X0Sp, &decoded), 0);
    XCTAssertEqual(decoded.subtype, A64_LDST_PAIR);
    XCTAssertTrue(decoded.is_pair);
    XCTAssertTrue(decoded.is_signed);
    XCTAssertTrue(decoded.is_64bit);
    XCTAssertEqual(decoded.Rd, 1);
    XCTAssertEqual(decoded.Rm, 0);
    XCTAssertEqual(decoded.Rn, 31);

    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    XCTAssertEqual(a64_gen_init(&state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(&state, 0x565b3550);

    XCTAssertEqual(a64_gen_instruction(&state, ldpswX1X0Sp, 0x565b3550), A64_GEN_OK);
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
}

- (void)testLoweringContract_UDIVDecodesAndLowersAsTwoSourceDivide
{
    uint32_t udivW3W1W7 = 0x1ac70823;
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(udivW3W1W7, &decoded), 0);
    XCTAssertEqual(decoded.subtype, A64_DP_REG_UDIV);
    XCTAssertEqual(decoded.Rd, 3);
    XCTAssertEqual(decoded.Rn, 1);
    XCTAssertEqual(decoded.Rm, 7);
    XCTAssertFalse(decoded.is_64bit);

    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    XCTAssertEqual(a64_gen_init(&state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(&state, 0x6a814);

    XCTAssertEqual(a64_gen_instruction(&state, udivW3W1W7, 0x6a814), A64_GEN_OK);
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
}

- (void)testLoweringContract_TSTAliasDecodesAndLowersAsFlagSettingLogicalAnd
{
    uint32_t tstX0X5 = 0xea05001f;
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(tstX0X5, &decoded), 0);
    XCTAssertEqual(decoded.subtype, 3);
    XCTAssertEqual(decoded.Rd, 31);
    XCTAssertEqual(decoded.Rn, 0);
    XCTAssertEqual(decoded.Rm, 5);
    XCTAssertTrue(decoded.set_flags);

    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    XCTAssertEqual(a64_gen_init(&state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(&state, 0x6a8a0);

    XCTAssertEqual(a64_gen_instruction(&state, tstX0X5, 0x6a8a0), A64_GEN_OK);
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
}

- (void)testLoweringContract_ThirtyTwoBitADDShiftedRegisterUsesFallback
{
    uint32_t addW13W13W13Lsl5 = 0x0b0d15ad;
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(addW13W13W13Lsl5, &decoded), 0);
    XCTAssertEqual(decoded.subtype, 0);
    XCTAssertEqual(decoded.Rd, 13);
    XCTAssertEqual(decoded.Rn, 13);
    XCTAssertEqual(decoded.Rm, 13);
    XCTAssertEqual(decoded.imm_shift, 5);
    XCTAssertFalse(decoded.is_64bit);

    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    XCTAssertEqual(a64_gen_init(&state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(&state, 0x6af54);

    XCTAssertEqual(a64_gen_instruction(&state, addW13W13W13Lsl5, 0x6af54), A64_GEN_OK);
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
}

- (void)testLoweringContract_ThirtyTwoBitORRImmediateUsesWidthAwareFallback
{
    uint32_t orrW10W10Imm1 = 0x3200014a;
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(orrW10W10Imm1, &decoded), 0);
    XCTAssertEqual(decoded.subtype, 8);
    XCTAssertEqual(decoded.Rd, 10);
    XCTAssertEqual(decoded.Rn, 10);
    XCTAssertEqual(decoded.imm, 1);
    XCTAssertFalse(decoded.is_64bit);

    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    XCTAssertEqual(a64_gen_init(&state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(&state, 0x6a7c4);

    XCTAssertEqual(a64_gen_instruction(&state, orrW10W10Imm1, 0x6a7c4), A64_GEN_OK);
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
}

- (void)testLoweringContract_LogicalRegisterNotFormsPreserveNBit
{
    uint32_t eonX4X3X6 = 0xca260064;
    uint32_t bicX0X0X3 = 0x8a230000;
    a64_instr_t decoded;

    XCTAssertEqual(a64_decode(eonX4X3X6, &decoded), 0);
    XCTAssertEqual(decoded.subtype, 6);
    XCTAssertEqual(decoded.Rd, 4);
    XCTAssertEqual(decoded.Rn, 3);
    XCTAssertEqual(decoded.Rm, 6);

    XCTAssertEqual(a64_decode(bicX0X0X3, &decoded), 0);
    XCTAssertEqual(decoded.subtype, 4);
    XCTAssertEqual(decoded.Rd, 0);
    XCTAssertEqual(decoded.Rn, 0);
    XCTAssertEqual(decoded.Rm, 3);

    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    XCTAssertEqual(a64_gen_init(&state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(&state, 0x5f9b4);

    XCTAssertEqual(a64_gen_instruction(&state, eonX4X3X6, 0x5f9b4), A64_GEN_OK);
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);

    a64_gen_reset(&state, 0x5f9c4);
    XCTAssertEqual(a64_gen_instruction(&state, bicX0X0X3, 0x5f9c4), A64_GEN_OK);
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
}

- (void)testLoweringContract_MSRTPIDREL0LowersThroughSysregGadget
{
    uint32_t msrTpidrEl0X0 = 0xd51bd040;
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(msrTpidrEl0X0, &decoded), 0);
    XCTAssertEqual(decoded.subtype, 4);
    XCTAssertEqual(decoded.Rd, 0);
    XCTAssertEqual(decoded.sysreg, 0x5e82);

    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    XCTAssertEqual(a64_gen_init(&state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(&state, 0x618e0);

    XCTAssertEqual(a64_gen_instruction(&state, msrTpidrEl0X0, 0x618e0), A64_GEN_OK);
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
}

- (void)testLoweringContract_SVCLowersAsSyscallExit
{
    uint32_t svc0 = 0xd4000001;
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(svc0, &decoded), 0);
    XCTAssertEqual(decoded.subtype, A64_EXCEPTION);
    XCTAssertEqual(decoded.imm, 0);

    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    XCTAssertEqual(a64_gen_init(&state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(&state, 0x20ca8);

    XCTAssertEqual(a64_gen_instruction(&state, svc0, 0x20ca8), A64_GEN_OK);
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
    XCTAssertTrue(state.is_complete);
}

- (void)testLoweringContract_ADDWImmediateLowersWidthAware
{
    uint32_t addW8W8Imm1 = 0x11000508;
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(addW8W8Imm1, &decoded), 0);
    XCTAssertFalse(decoded.is_64bit);
    XCTAssertEqual(decoded.Rd, 8);
    XCTAssertEqual(decoded.Rn, 8);
    XCTAssertEqual(decoded.imm, 1);

    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    XCTAssertEqual(a64_gen_init(&state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(&state, 0x6a900);

    XCTAssertEqual(a64_gen_instruction(&state, addW8W8Imm1, 0x6a900), A64_GEN_OK);
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
}

- (void)testLoweringContract_SUBWImmediateLowersWidthAware
{
    uint32_t subW0W8Imm1 = 0x51000500;
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(subW0W8Imm1, &decoded), 0);
    XCTAssertFalse(decoded.is_64bit);
    XCTAssertEqual(decoded.Rd, 0);
    XCTAssertEqual(decoded.Rn, 8);
    XCTAssertEqual(decoded.imm, 1);

    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    XCTAssertEqual(a64_gen_init(&state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(&state, 0x6a8e8);

    XCTAssertEqual(a64_gen_instruction(&state, subW0W8Imm1, 0x6a8e8), A64_GEN_OK);
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
}

- (void)testLoweringContract_TBNZMemoryBackedRegisterLowers
{
    uint32_t tbnzW21Bit25 = 0x37c80135;
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(tbnzW21Bit25, &decoded), 0);
    XCTAssertEqual(decoded.subtype, A64_BRANCH_TEST);
    XCTAssertEqual(decoded.Rd, 21);
    XCTAssertEqual(decoded.imm_shift, 25);

    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    XCTAssertEqual(a64_gen_init(&state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(&state, 0x6b24c);

    XCTAssertEqual(a64_gen_instruction(&state, tbnzW21Bit25, 0x6b24c), A64_GEN_OK);
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
    XCTAssertTrue(state.is_complete);
}

- (void)testLoweringContract_LDRSBXDecodesDestinationWidth
{
    uint32_t ldrsbX0X1 = 0x39800020;
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(ldrsbX0X1, &decoded), 0);
    XCTAssertTrue(decoded.is_signed);
    XCTAssertTrue(decoded.is_64bit);
    XCTAssertEqual(decoded.size, A64_SIZE_B);
    XCTAssertEqual(decoded.Rd, 0);
    XCTAssertEqual(decoded.Rn, 1);

    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    XCTAssertEqual(a64_gen_init(&state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(&state, 0x70000);

    XCTAssertEqual(a64_gen_instruction(&state, ldrsbX0X1, 0x70000), A64_GEN_OK);
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
}

- (void)testLoweringContract_UDIVXDecodesAsSixtyFourBit
{
    uint32_t udivX2X2X0 = 0x9ac00842;
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(udivX2X2X0, &decoded), 0);
    XCTAssertEqual(decoded.subtype, A64_DP_REG_UDIV);
    XCTAssertTrue(decoded.is_64bit);

    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    XCTAssertEqual(a64_gen_init(&state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(&state, 0x6bc44);

    XCTAssertEqual(a64_gen_instruction(&state, udivX2X2X0, 0x6bc44), A64_GEN_OK);
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
}

- (void)testLoweringContract_ADRPNegativePageDeltaLowers
{
    uint32_t adrpX0Negative = 0xb0ffffe0;
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(adrpX0Negative, &decoded), 0);
    XCTAssertEqual(decoded.cat, A64_SIMD0);
    XCTAssertEqual(decoded.subtype, 1);
    XCTAssertEqual(decoded.Rd, 0);

    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    XCTAssertEqual(a64_gen_init(&state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(&state, 0x6d4f0);

    XCTAssertEqual(a64_gen_instruction(&state, adrpX0Negative, 0x6d4f0), A64_GEN_OK);
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
}

- (void)testLoweringContract_SIMDDUPFromGPRLowers
{
    uint32_t dupV0_16bW1 = 0x4e010c20;
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(dupV0_16bW1, &decoded), 0);
    XCTAssertEqual(decoded.cat, A64_SIMD);
    XCTAssertEqual(decoded.subtype, A64_SIMD_DUP_GPR);
    XCTAssertEqual(decoded.Rd, 0);
    XCTAssertEqual(decoded.Rn, 1);
    XCTAssertEqual(decoded.vec_bytes, 1);

    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    XCTAssertEqual(a64_gen_init(&state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(&state, 0x18420);

    XCTAssertEqual(a64_gen_instruction(&state, dupV0_16bW1, 0x18420), A64_GEN_OK);
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
}

- (void)testLoweringContract_SIMDMoveGPRFromVectorLowers
{
    uint32_t movX1V0D0 = 0x4e083c01;
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(movX1V0D0, &decoded), 0);
    XCTAssertEqual(decoded.cat, A64_SIMD);
    XCTAssertEqual(decoded.subtype, A64_SIMD_MOV_GPR_FROM_VEC);
    XCTAssertEqual(decoded.Rd, 1);
    XCTAssertEqual(decoded.Rn, 0);
    XCTAssertEqual(decoded.vec_bytes, 8);
    XCTAssertEqual(decoded.vec_index, 0);
    XCTAssertTrue(decoded.is_64bit);

    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    XCTAssertEqual(a64_gen_init(&state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(&state, 0x18434);

    XCTAssertEqual(a64_gen_instruction(&state, movX1V0D0, 0x18434), A64_GEN_OK);
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
}

- (void)testLoweringContract_SIMDFMOVGPRToScalarLowers
{
    uint32_t fmovS31W3 = 0x1e27007f;
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(fmovS31W3, &decoded), 0);
    XCTAssertEqual(decoded.cat, A64_SIMD);
    XCTAssertEqual(decoded.subtype, A64_SIMD_FMOV_GPR);
    XCTAssertEqual(decoded.Rd, 31);
    XCTAssertEqual(decoded.Rn, 3);
    XCTAssertEqual(decoded.vec_bytes, 4);
    XCTAssertEqual(decoded.op, 1);

    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    XCTAssertEqual(a64_gen_init(&state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(&state, 0x61a34);

    XCTAssertEqual(a64_gen_instruction(&state, fmovS31W3, 0x61a34), A64_GEN_OK);
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
}

- (void)testLoweringContract_SIMDSTRQUnsignedImmediateLowers
{
    uint32_t strQ0X0Imm16 = 0x3d800400;
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(strQ0X0Imm16, &decoded), 0);
    XCTAssertEqual(decoded.cat, A64_LD_ST);
    XCTAssertEqual(decoded.subtype, A64_LDST_SINGLE);
    XCTAssertTrue(decoded.is_vector);
    XCTAssertEqual(decoded.vec_bytes, 16);
    XCTAssertEqual(decoded.imm, 16);

    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    XCTAssertEqual(a64_gen_init(&state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(&state, 0x18478);

    XCTAssertEqual(a64_gen_instruction(&state, strQ0X0Imm16, 0x18478), A64_GEN_OK);
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
}

- (void)testLoweringContract_SIMDLDRQUnsignedImmediateLowers
{
    uint32_t ldrQ31SPImm48 = 0x3dc00fff;
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(ldrQ31SPImm48, &decoded), 0);
    XCTAssertEqual(decoded.cat, A64_LD_ST);
    XCTAssertEqual(decoded.subtype, A64_LDST_SINGLE);
    XCTAssertTrue(decoded.is_vector);
    XCTAssertEqual(decoded.Rd, 31);
    XCTAssertEqual(decoded.Rn, 31);
    XCTAssertEqual(decoded.vec_bytes, 16);
    XCTAssertEqual(decoded.imm, 48);

    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    XCTAssertEqual(a64_gen_init(&state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(&state, 0x6ad7c);

    XCTAssertEqual(a64_gen_instruction(&state, ldrQ31SPImm48, 0x6ad7c), A64_GEN_OK);
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
}

- (void)testLoweringContract_SIMDSTURQUnscaledLowers
{
    uint32_t sturQ0X4Minus16 = 0x3c9f0080;
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(sturQ0X4Minus16, &decoded), 0);
    XCTAssertEqual(decoded.cat, A64_LD_ST);
    XCTAssertEqual(decoded.subtype, A64_LDST_SINGLE);
    XCTAssertTrue(decoded.is_vector);
    XCTAssertEqual(decoded.vec_bytes, 16);
    XCTAssertEqual(decoded.imm, -16);

    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    XCTAssertEqual(a64_gen_init(&state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(&state, 0x184a4);

    XCTAssertEqual(a64_gen_instruction(&state, sturQ0X4Minus16, 0x184a4), A64_GEN_OK);
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
}

- (void)testLoweringContract_SIMDSTPQPairLowers
{
    uint32_t stpQ0Q0X0Imm32 = 0xad010000;
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(stpQ0Q0X0Imm32, &decoded), 0);
    XCTAssertEqual(decoded.cat, A64_LD_ST);
    XCTAssertEqual(decoded.subtype, A64_LDST_PAIR);
    XCTAssertTrue(decoded.is_vector);
    XCTAssertEqual(decoded.vec_bytes, 16);
    XCTAssertEqual(decoded.pair_offset, 32);

    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    XCTAssertEqual(a64_gen_init(&state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(&state, 0x18488);

    XCTAssertEqual(a64_gen_instruction(&state, stpQ0Q0X0Imm32, 0x18488), A64_GEN_OK);
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
}

- (void)testLoweringContract_VectorMemory_LD1Lowers
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0x4c407020 atPC:0x18490 state:&state gadgets:gadgets];
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
}

- (void)testLoweringContract_VectorMemory_LD1RLowers
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0x4d40c062 atPC:0x18494 state:&state gadgets:gadgets];
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
}

- (void)testLoweringContract_VectorMemory_LD2Lowers
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0x4c4080c4 atPC:0x18498 state:&state gadgets:gadgets];
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
}

- (void)testLoweringContract_VectorMemory_LD3Lowers
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0x4c404147 atPC:0x1849c state:&state gadgets:gadgets];
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
}

- (void)testLoweringContract_VectorMemory_LD4Lowers
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0x4c4001eb atPC:0x184a0 state:&state gadgets:gadgets];
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
}

- (void)testLoweringContract_VectorMemory_LDURQLowers
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0x3cdf0020 atPC:0x184a2 state:&state gadgets:gadgets];
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
}

- (void)testLoweringContract_VectorMemory_LDPQLowers
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0xad400440 atPC:0x184a3 state:&state gadgets:gadgets];
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
}

- (void)testLoweringContract_VectorMemory_ST2Lowers
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0x4c008292 atPC:0x184a4 state:&state gadgets:gadgets];
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
}

- (void)testLoweringContract_VectorMemory_ST3Lowers
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0x4c004315 atPC:0x184a8 state:&state gadgets:gadgets];
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
}

- (void)testLoweringContract_VectorMemory_ST4Lowers
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0x4c0003b9 atPC:0x184ac state:&state gadgets:gadgets];
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
}

- (void)testLoweringContract_DCZVALowers
{
    uint32_t dcZvaX3 = 0xd50b7423;
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(dcZvaX3, &decoded), 0);
    XCTAssertEqual(decoded.cat, A64_BRANCH);
    XCTAssertEqual(decoded.subtype, A64_SYSTEM_DC_ZVA);
    XCTAssertEqual(decoded.Rd, 3);

    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    XCTAssertEqual(a64_gen_init(&state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(&state, 0x184e4);

    XCTAssertEqual(a64_gen_instruction(&state, dcZvaX3, 0x184e4), A64_GEN_OK);
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
}

- (void)testLoweringContract_ADDExtendedSXTWLowers
{
    uint32_t addX22X1W22SXTW = 0x8b36c036;
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(addX22X1W22SXTW, &decoded), 0);
    XCTAssertEqual(decoded.subtype, 0);
    XCTAssertEqual(decoded.Rd, 22);
    XCTAssertEqual(decoded.Rn, 1);
    XCTAssertEqual(decoded.Rm, 22);
    XCTAssertEqual(decoded.extend_type, A64_EXT_SXTW);
    XCTAssertEqual(decoded.imm_shift, 0);
    XCTAssertTrue(decoded.is_64bit);

    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    XCTAssertEqual(a64_gen_init(&state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(&state, 0x29c28);

    XCTAssertEqual(a64_gen_instruction(&state, addX22X1W22SXTW, 0x29c28), A64_GEN_OK);
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
}

- (void)testLoweringContract_CMPExtendedUXTBLowers
{
    uint32_t cmpW2W1UXTB = 0x6b21005f;
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(cmpW2W1UXTB, &decoded), 0);
    XCTAssertEqual(decoded.subtype, 1);
    XCTAssertEqual(decoded.Rd, 31);
    XCTAssertEqual(decoded.Rn, 2);
    XCTAssertEqual(decoded.Rm, 1);
    XCTAssertEqual(decoded.extend_type, A64_EXT_UXTB);
    XCTAssertEqual(decoded.imm_shift, 0);
    XCTAssertTrue(decoded.set_flags);
    XCTAssertFalse(decoded.is_64bit);

    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    XCTAssertEqual(a64_gen_init(&state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(&state, 0x5f93c);

    XCTAssertEqual(a64_gen_instruction(&state, cmpW2W1UXTB, 0x5f93c), A64_GEN_OK);
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
}

- (void)testLoweringContract_STRPostIndexStaysInBlockAfterFallthroughPCAdvance
{
    uint32_t strXzrX2Post8 = 0xf800845f;
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(strXzrX2Post8, &decoded), 0);
    XCTAssertEqual(decoded.cat, A64_LD_ST);
    XCTAssertEqual(decoded.subtype, A64_LDST_SINGLE);
    XCTAssertEqual(decoded.Rd, 31);
    XCTAssertEqual(decoded.Rn, 2);
    XCTAssertEqual(decoded.imm, 8);
    XCTAssertEqual(decoded.idx_mode, A64_POST_INDEX);

    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    XCTAssertEqual(a64_gen_init(&state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(&state, 0x6a650);

    XCTAssertEqual(a64_gen_instruction(&state, strXzrX2Post8, 0x6a650), A64_GEN_OK);
    XCTAssertFalse(state.is_complete);
    XCTAssertGreaterThanOrEqual(state.num_gadgets, (size_t)10);
}

- (void)testLoweringContract_BNELowersThroughNativeTCTIConditionGadget
{
    uint32_t bneBackToDlstartClear = 0x54ffffc1;
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(bneBackToDlstartClear, &decoded), 0);
    XCTAssertEqual(decoded.cat, A64_BRANCH);
    XCTAssertEqual(decoded.subtype, A64_BRANCH_COND);
    XCTAssertEqual(decoded.cond, A64_NE);
    XCTAssertEqual(decoded.imm, -8);

    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    XCTAssertEqual(a64_gen_init(&state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(&state, 0x6a658);

    XCTAssertEqual(a64_gen_instruction(&state, bneBackToDlstartClear, 0x6a658), A64_GEN_OK);
    XCTAssertTrue(state.is_complete);
    XCTAssertEqual(state.num_gadgets, (size_t)3);
}

TCTI_DECLARE_COND_BRANCH_LOWERING_TEST(BEQLowersThroughNativeTCTIConditionGadget, 0x54000000,
                                       0x6a660, 0)
TCTI_DECLARE_COND_BRANCH_LOWERING_TEST(BNEZeroOffsetLowersThroughNativeTCTIConditionGadget,
                                       0x54000001, 0x6a664, 1)
TCTI_DECLARE_COND_BRANCH_LOWERING_TEST(BHSZeroOffsetLowersThroughNativeTCTIConditionGadget,
                                       0x54000002, 0x6a668, 2)
TCTI_DECLARE_COND_BRANCH_LOWERING_TEST(BLOZeroOffsetLowersThroughNativeTCTIConditionGadget,
                                       0x54000003, 0x6a66c, 3)
TCTI_DECLARE_COND_BRANCH_LOWERING_TEST(BMIZeroOffsetLowersThroughNativeTCTIConditionGadget,
                                       0x54000004, 0x6a670, 4)
TCTI_DECLARE_COND_BRANCH_LOWERING_TEST(BPLZeroOffsetLowersThroughNativeTCTIConditionGadget,
                                       0x54000005, 0x6a674, 5)
TCTI_DECLARE_COND_BRANCH_LOWERING_TEST(BVSZeroOffsetLowersThroughNativeTCTIConditionGadget,
                                       0x54000006, 0x6a678, 6)
TCTI_DECLARE_COND_BRANCH_LOWERING_TEST(BVCZeroOffsetLowersThroughNativeTCTIConditionGadget,
                                       0x54000007, 0x6a67c, 7)
TCTI_DECLARE_COND_BRANCH_LOWERING_TEST(BHIZeroOffsetLowersThroughNativeTCTIConditionGadget,
                                       0x54000008, 0x6a680, 8)
TCTI_DECLARE_COND_BRANCH_LOWERING_TEST(BLSZeroOffsetLowersThroughNativeTCTIConditionGadget,
                                       0x54000009, 0x6a684, 9)
TCTI_DECLARE_COND_BRANCH_LOWERING_TEST(BGEZeroOffsetLowersThroughNativeTCTIConditionGadget,
                                       0x5400000a, 0x6a688, 10)
TCTI_DECLARE_COND_BRANCH_LOWERING_TEST(BLTZeroOffsetLowersThroughNativeTCTIConditionGadget,
                                       0x5400000b, 0x6a68c, 11)
TCTI_DECLARE_COND_BRANCH_LOWERING_TEST(BGTZeroOffsetLowersThroughNativeTCTIConditionGadget,
                                       0x5400000c, 0x6a690, 12)
TCTI_DECLARE_COND_BRANCH_LOWERING_TEST(BLEZeroOffsetLowersThroughNativeTCTIConditionGadget,
                                       0x5400000d, 0x6a694, 13)

TCTI_DECLARE_COND_BRANCH_LOWERING_TEST(BCEQLowersThroughNativeTCTIConditionGadget, 0x54000010,
                                       0x6a698, 0)
TCTI_DECLARE_COND_BRANCH_LOWERING_TEST(BCNEZeroOffsetLowersThroughNativeTCTIConditionGadget,
                                       0x54000011, 0x6a69c, 1)
TCTI_DECLARE_COND_BRANCH_LOWERING_TEST(BCSHSZeroOffsetLowersThroughNativeTCTIConditionGadget,
                                       0x54000012, 0x6a6a0, 2)
TCTI_DECLARE_COND_BRANCH_LOWERING_TEST(BCCLOZeroOffsetLowersThroughNativeTCTIConditionGadget,
                                       0x54000013, 0x6a6a4, 3)
TCTI_DECLARE_COND_BRANCH_LOWERING_TEST(BCMIZeroOffsetLowersThroughNativeTCTIConditionGadget,
                                       0x54000014, 0x6a6a8, 4)
TCTI_DECLARE_COND_BRANCH_LOWERING_TEST(BCPLZeroOffsetLowersThroughNativeTCTIConditionGadget,
                                       0x54000015, 0x6a6ac, 5)
TCTI_DECLARE_COND_BRANCH_LOWERING_TEST(BCVSZeroOffsetLowersThroughNativeTCTIConditionGadget,
                                       0x54000016, 0x6a6b0, 6)
TCTI_DECLARE_COND_BRANCH_LOWERING_TEST(BCVCZeroOffsetLowersThroughNativeTCTIConditionGadget,
                                       0x54000017, 0x6a6b4, 7)
TCTI_DECLARE_COND_BRANCH_LOWERING_TEST(BCHIZeroOffsetLowersThroughNativeTCTIConditionGadget,
                                       0x54000018, 0x6a6b8, 8)
TCTI_DECLARE_COND_BRANCH_LOWERING_TEST(BCLSZeroOffsetLowersThroughNativeTCTIConditionGadget,
                                       0x54000019, 0x6a6bc, 9)
TCTI_DECLARE_COND_BRANCH_LOWERING_TEST(BCGEZeroOffsetLowersThroughNativeTCTIConditionGadget,
                                       0x5400001a, 0x6a6c0, 10)
TCTI_DECLARE_COND_BRANCH_LOWERING_TEST(BCLTZeroOffsetLowersThroughNativeTCTIConditionGadget,
                                       0x5400001b, 0x6a6c4, 11)
TCTI_DECLARE_COND_BRANCH_LOWERING_TEST(BCGTZeroOffsetLowersThroughNativeTCTIConditionGadget,
                                       0x5400001c, 0x6a6c8, 12)
TCTI_DECLARE_COND_BRANCH_LOWERING_TEST(BCLEZeroOffsetLowersThroughNativeTCTIConditionGadget,
                                       0x5400001d, 0x6a6cc, 13)

- (void)testLoweringContract_REVWLowersThroughOneSourceGenerationPath
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0x5ac00820 atPC:0x6a6d0 state:&state gadgets:gadgets];
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
    XCTAssertFalse(state.is_complete);
}

- (void)testLoweringContract_REVXLowersThroughOneSourceGenerationPath
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0xdac00c62 atPC:0x6a6d4 state:&state gadgets:gadgets];
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
    XCTAssertFalse(state.is_complete);
}

- (void)testLoweringContract_REV16WLowersThroughOneSourceGenerationPath
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0x5ac004a4 atPC:0x6a6d8 state:&state gadgets:gadgets];
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
    XCTAssertFalse(state.is_complete);
}

- (void)testLoweringContract_REV16XLowersThroughOneSourceGenerationPath
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0xdac004e6 atPC:0x6a6dc state:&state gadgets:gadgets];
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
    XCTAssertFalse(state.is_complete);
}

- (void)testLoweringContract_REV32XLowersThroughOneSourceGenerationPath
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0xdac00928 atPC:0x6a6e0 state:&state gadgets:gadgets];
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
    XCTAssertFalse(state.is_complete);
}

- (void)testLoweringContract_LDAXRDecodesAndLowersAsAtomicTCTI
{
    uint32_t ldaxrW0X3 = 0x885ffc60;
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(ldaxrW0X3, &decoded), 0);
    XCTAssertEqual(decoded.cat, A64_LD_ST);
    XCTAssertEqual(decoded.subtype, A64_LDST_ATOMIC);
    XCTAssertEqual(decoded.Rd, 0);
    XCTAssertEqual(decoded.Rn, 3);
    XCTAssertEqual(decoded.Rm, 31);
    XCTAssertEqual(decoded.size, A64_SIZE_W);
    XCTAssertFalse(decoded.is_64bit);

    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    XCTAssertEqual(a64_gen_init(&state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(&state, 0x29030);

    XCTAssertEqual(a64_gen_instruction(&state, ldaxrW0X3, 0x29030), A64_GEN_OK);
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
    XCTAssertFalse(state.is_complete);
}

- (void)testLoweringContract_STLXRDecodesAndLowersAsAtomicTCTI
{
    uint32_t stlxrW0W1X3 = 0x8800fc61;
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(stlxrW0W1X3, &decoded), 0);
    XCTAssertEqual(decoded.cat, A64_LD_ST);
    XCTAssertEqual(decoded.subtype, A64_LDST_ATOMIC);
    XCTAssertEqual(decoded.Rd, 1);
    XCTAssertEqual(decoded.Rn, 3);
    XCTAssertEqual(decoded.Rm, 0);
    XCTAssertEqual(decoded.size, A64_SIZE_W);
    XCTAssertFalse(decoded.is_64bit);

    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    XCTAssertEqual(a64_gen_init(&state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(&state, 0x2903c);

    XCTAssertEqual(a64_gen_instruction(&state, stlxrW0W1X3, 0x2903c), A64_GEN_OK);
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
    XCTAssertFalse(state.is_complete);
}

- (void)testLoweringContract_CASDecodesAndLowersAsAtomicTCTI
{
    uint32_t casW4W5X6 = 0x88a47cc5;
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(casW4W5X6, &decoded), 0);
    XCTAssertEqual(decoded.cat, A64_LD_ST);
    XCTAssertEqual(decoded.subtype, A64_LDST_ATOMIC);
    XCTAssertEqual(decoded.Rd, 5);
    XCTAssertEqual(decoded.Rn, 6);
    XCTAssertEqual(decoded.Rm, 4);
    XCTAssertEqual(decoded.size, A64_SIZE_W);
    XCTAssertFalse(decoded.is_64bit);

    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    XCTAssertEqual(a64_gen_init(&state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(&state, 0x29048);

    XCTAssertEqual(a64_gen_instruction(&state, casW4W5X6, 0x29048), A64_GEN_OK);
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
    XCTAssertFalse(state.is_complete);
}

- (void)testLoweringContract_CMEQLowersThroughVectorCompareMaskGadget
{
    uint32_t cmeqV0V1V2 = 0x6e228c20;
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(cmeqV0V1V2, &decoded), 0);
    XCTAssertEqual(decoded.subtype, A64_SIMD_CMEQ);

    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    XCTAssertEqual(a64_gen_init(&state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(&state, 0x98180);

    XCTAssertEqual(a64_gen_instruction(&state, cmeqV0V1V2, 0x98180), A64_GEN_OK);
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
    XCTAssertFalse(state.is_complete);
}

- (void)testLoweringContract_CMGTLowersThroughSignedVectorCompareGadget
{
    uint32_t cmgtV3V4V5 = 0x4e253483;
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(cmgtV3V4V5, &decoded), 0);
    XCTAssertEqual(decoded.subtype, A64_SIMD_CMGT);

    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    XCTAssertEqual(a64_gen_init(&state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(&state, 0x981a0);

    XCTAssertEqual(a64_gen_instruction(&state, cmgtV3V4V5, 0x981a0), A64_GEN_OK);
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
    XCTAssertFalse(state.is_complete);
}

- (void)testLoweringContract_CMGELowersThroughSignedVectorGreaterOrEqualGadget
{
    uint32_t insn = 0x4e223c20;
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(insn, &decoded), 0);
    XCTAssertEqual(decoded.subtype, A64_SIMD_CMGE);

    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    XCTAssertEqual(a64_gen_init(&state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(&state, 0x981b0);

    XCTAssertEqual(a64_gen_instruction(&state, insn, 0x981b0), A64_GEN_OK);
    XCTAssertEqual(gadgets[0], gadget_simd_cmge);
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
    XCTAssertFalse(state.is_complete);
}

- (void)testLoweringContract_CMHILowersThroughUnsignedVectorGreaterThanGadget
{
    uint32_t insn = 0x6e223420;
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(insn, &decoded), 0);
    XCTAssertEqual(decoded.subtype, A64_SIMD_CMHI);

    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    XCTAssertEqual(a64_gen_init(&state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(&state, 0x981c0);

    XCTAssertEqual(a64_gen_instruction(&state, insn, 0x981c0), A64_GEN_OK);
    XCTAssertEqual(gadgets[0], gadget_simd_cmhi);
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
    XCTAssertFalse(state.is_complete);
}

- (void)testLoweringContract_CMHSLowersThroughUnsignedVectorGreaterOrEqualGadget
{
    uint32_t insn = 0x6e223c20;
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(insn, &decoded), 0);
    XCTAssertEqual(decoded.subtype, A64_SIMD_CMHS);

    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    XCTAssertEqual(a64_gen_init(&state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(&state, 0x981d0);

    XCTAssertEqual(a64_gen_instruction(&state, insn, 0x981d0), A64_GEN_OK);
    XCTAssertEqual(gadgets[0], gadget_simd_cmhs);
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
    XCTAssertFalse(state.is_complete);
}

- (void)testLoweringContract_CMLELowersThroughSignedVectorLessOrEqualZeroGadget
{
    uint32_t insn = 0x6e209820;
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(insn, &decoded), 0);
    XCTAssertEqual(decoded.subtype, A64_SIMD_CMLE);

    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    XCTAssertEqual(a64_gen_init(&state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(&state, 0x981e0);

    XCTAssertEqual(a64_gen_instruction(&state, insn, 0x981e0), A64_GEN_OK);
    XCTAssertEqual(gadgets[0], gadget_simd_cmle);
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
    XCTAssertFalse(state.is_complete);
}

- (void)testLoweringContract_CMLTLowersThroughSignedVectorLessThanZeroGadget
{
    uint32_t insn = 0x4e20a820;
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(insn, &decoded), 0);
    XCTAssertEqual(decoded.subtype, A64_SIMD_CMLT);

    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    XCTAssertEqual(a64_gen_init(&state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(&state, 0x981f0);

    XCTAssertEqual(a64_gen_instruction(&state, insn, 0x981f0), A64_GEN_OK);
    XCTAssertEqual(gadgets[0], gadget_simd_cmlt);
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
    XCTAssertFalse(state.is_complete);
}

- (void)testLoweringContract_CMTSTLowersThroughBitIntersectionMaskGadget
{
    uint32_t insn = 0x4e228c20;
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(insn, &decoded), 0);
    XCTAssertEqual(decoded.subtype, A64_SIMD_CMTST);

    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    XCTAssertEqual(a64_gen_init(&state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(&state, 0x98200);

    XCTAssertEqual(a64_gen_instruction(&state, insn, 0x98200), A64_GEN_OK);
    XCTAssertEqual(gadgets[0], gadget_simd_cmtst);
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
    XCTAssertFalse(state.is_complete);
}

- (void)testLoweringContract_MLALowersThroughVectorAccumulateGadget
{
    uint32_t mlaV6V7V8 = 0x4e2894e6;
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(mlaV6V7V8, &decoded), 0);
    XCTAssertEqual(decoded.subtype, A64_SIMD_MLA);

    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    XCTAssertEqual(a64_gen_init(&state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(&state, 0x981c0);

    XCTAssertEqual(a64_gen_instruction(&state, mlaV6V7V8, 0x981c0), A64_GEN_OK);
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
    XCTAssertFalse(state.is_complete);
}

- (void)testLoweringContract_MLSLowersThroughVectorMultiplySubtractGadget
{
    uint32_t mlsV9V10V11 = 0x6e2b9549;
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(mlsV9V10V11, &decoded), 0);
    XCTAssertEqual(decoded.subtype, A64_SIMD_MLS);

    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    XCTAssertEqual(a64_gen_init(&state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(&state, 0x981e0);

    XCTAssertEqual(a64_gen_instruction(&state, mlsV9V10V11, 0x981e0), A64_GEN_OK);
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
    XCTAssertFalse(state.is_complete);
}

- (void)testLoweringContract_PACIARejectsUntilArm64eLoweringOwnershipExists
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    uint32_t paciaX0X1 = 0xdac10020;
    [self rejectInstruction:paciaX0X1 atPC:0x96000 state:&state gadgets:gadgets];
}

- (void)testLoweringContract_AESERejectsUntilCryptoLoweringOwnershipExists
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    uint32_t aeseV0V1 = 0x4e284820;
    [self rejectInstruction:aeseV0V1 atPC:0x94000 state:&state gadgets:gadgets];
}

- (void)testLoweringContract_LDGRejectsUntilTaggingLoweringOwnershipExists
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    uint32_t ldgX0X1 = 0xd9600020;
    [self rejectInstruction:ldgX0X1 atPC:0x95000 state:&state gadgets:gadgets];
}

TCTI_DECLARE_REJECT_LOWERING_TEST(Arm64e_PACIBRejectsUntilArm64eLoweringOwnershipExists, 0xdac10462, 0x96004)
TCTI_DECLARE_REJECT_LOWERING_TEST(Arm64e_AUTIBRejectsUntilArm64eLoweringOwnershipExists, 0xdac114e6, 0x96008)
TCTI_DECLARE_REJECT_LOWERING_TEST(Arm64e_XPACIRejectsUntilArm64eLoweringOwnershipExists, 0xdac143e8, 0x9600c)
TCTI_DECLARE_REJECT_LOWERING_TEST(Arm64e_XPACDRejectsUntilArm64eLoweringOwnershipExists, 0xdac147e9, 0x9600e)
TCTI_DECLARE_REJECT_LOWERING_TEST(Arm64e_BRAARejectsUntilArm64eLoweringOwnershipExists, 0xd71f094b, 0x96010)
TCTI_DECLARE_REJECT_LOWERING_TEST(Arm64e_BRABRejectsUntilArm64eLoweringOwnershipExists, 0xd71f0d8d, 0x96012)
TCTI_DECLARE_REJECT_LOWERING_TEST(Arm64e_BLRAARejectsUntilArm64eLoweringOwnershipExists, 0xd73f09cf, 0x96013)
TCTI_DECLARE_REJECT_LOWERING_TEST(Arm64e_BLRABRejectsUntilArm64eLoweringOwnershipExists, 0xd73f0e11, 0x96014)
TCTI_DECLARE_REJECT_LOWERING_TEST(Arm64e_RETAARejectsUntilArm64eLoweringOwnershipExists, 0xd65f0bff, 0x96018)
TCTI_DECLARE_REJECT_LOWERING_TEST(Arm64e_RETABRejectsUntilArm64eLoweringOwnershipExists, 0xd65f0fff, 0x96019)
TCTI_DECLARE_REJECT_LOWERING_TEST(Arm64e_RETAASPPCRRejectsUntilArm64eLoweringOwnershipExists, 0xd65f0bfe, 0x9601a)
TCTI_DECLARE_REJECT_LOWERING_TEST(Arm64e_RETABSPPCRRejectsUntilArm64eLoweringOwnershipExists, 0xd65f0ffe, 0x9601b)
TCTI_DECLARE_REJECT_LOWERING_TEST(Arm64e_LDRAARejectsUntilArm64eLoweringOwnershipExists, 0xf8200420, 0x9601c)
TCTI_DECLARE_REJECT_LOWERING_TEST(Arm64e_LDRABRejectsUntilArm64eLoweringOwnershipExists, 0xf8a00462, 0x96020)
TCTI_DECLARE_REJECT_LOWERING_TEST(Arm64e_BTIRejectsUntilArm64eLoweringOwnershipExists, 0xd503245f, 0x96022)

TCTI_DECLARE_REJECT_LOWERING_TEST(Crypto_AESDRejectsUntilCryptoLoweringOwnershipExists, 0x4e285862, 0x94004)
TCTI_DECLARE_REJECT_LOWERING_TEST(Crypto_AESMCRejectsUntilCryptoLoweringOwnershipExists, 0x4e2868a4, 0x94008)
TCTI_DECLARE_REJECT_LOWERING_TEST(Crypto_AESIMCRejectsUntilCryptoLoweringOwnershipExists, 0x4e2878e6, 0x9400c)
TCTI_DECLARE_REJECT_LOWERING_TEST(Crypto_PMULRejectsUntilCryptoLoweringOwnershipExists, 0x2e229c20, 0x94010)
TCTI_DECLARE_REJECT_LOWERING_TEST(Crypto_SHA1CRejectsUntilCryptoLoweringOwnershipExists, 0x5e020020, 0x94014)
TCTI_DECLARE_REJECT_LOWERING_TEST(Crypto_SHA1MRejectsUntilCryptoLoweringOwnershipExists, 0x5e022020, 0x94015)
TCTI_DECLARE_REJECT_LOWERING_TEST(Crypto_SHA1PRejectsUntilCryptoLoweringOwnershipExists, 0x5e051083, 0x94016)
TCTI_DECLARE_REJECT_LOWERING_TEST(Crypto_SHA1SU0RejectsUntilCryptoLoweringOwnershipExists, 0x5e0830e6, 0x94017)
TCTI_DECLARE_REJECT_LOWERING_TEST(Crypto_SHA1SU1RejectsUntilCryptoLoweringOwnershipExists, 0x5e281949, 0x94018)
TCTI_DECLARE_REJECT_LOWERING_TEST(Crypto_SHA256HRejectsUntilCryptoLoweringOwnershipExists, 0x5e024020, 0x94018)
TCTI_DECLARE_REJECT_LOWERING_TEST(Crypto_SHA256H2RejectsUntilCryptoLoweringOwnershipExists, 0x5e1051ee, 0x94019)
TCTI_DECLARE_REJECT_LOWERING_TEST(Crypto_SHA256SU0RejectsUntilCryptoLoweringOwnershipExists, 0x5e282a51, 0x9401a)
TCTI_DECLARE_REJECT_LOWERING_TEST(Crypto_SHA256SU1RejectsUntilCryptoLoweringOwnershipExists, 0x5e156293, 0x9401b)
TCTI_DECLARE_REJECT_LOWERING_TEST(Crypto_SM3PARTW1RejectsUntilCryptoLoweringOwnershipExists, 0xce62c020, 0x9401c)
TCTI_DECLARE_REJECT_LOWERING_TEST(Crypto_SM3PARTW2RejectsUntilCryptoLoweringOwnershipExists, 0xce65c483, 0x9401d)
TCTI_DECLARE_REJECT_LOWERING_TEST(Crypto_SM3SS1RejectsUntilCryptoLoweringOwnershipExists, 0xce4824e6, 0x9401e)
TCTI_DECLARE_REJECT_LOWERING_TEST(Crypto_SM3TT1ARejectsUntilCryptoLoweringOwnershipExists, 0xce4c816a, 0x9401f)
TCTI_DECLARE_REJECT_LOWERING_TEST(Crypto_SM3TT1BRejectsUntilCryptoLoweringOwnershipExists, 0xce4f95cd, 0x94020)
TCTI_DECLARE_REJECT_LOWERING_TEST(Crypto_SM3TT2ARejectsUntilCryptoLoweringOwnershipExists, 0xce52aa30, 0x94021)
TCTI_DECLARE_REJECT_LOWERING_TEST(Crypto_SM3TT2BRejectsUntilCryptoLoweringOwnershipExists, 0xce55be93, 0x94022)
TCTI_DECLARE_REJECT_LOWERING_TEST(Crypto_SM4ERejectsUntilCryptoLoweringOwnershipExists, 0xcec086f6, 0x94023)
TCTI_DECLARE_REJECT_LOWERING_TEST(Crypto_SM4EKEYRejectsUntilCryptoLoweringOwnershipExists, 0xce7acb38, 0x94024)
TCTI_DECLARE_REJECT_LOWERING_TEST(Crypto_SDOTRejectsUntilCryptoLoweringOwnershipExists, 0x4e829420, 0x94025)
TCTI_DECLARE_REJECT_LOWERING_TEST(Crypto_UDOTRejectsUntilCryptoLoweringOwnershipExists, 0x6e859483, 0x94026)
TCTI_DECLARE_REJECT_LOWERING_TEST(Crypto_USDOTRejectsUntilCryptoLoweringOwnershipExists, 0x4e889ce6, 0x94027)
TCTI_DECLARE_REJECT_LOWERING_TEST(Crypto_SUDOTRejectsUntilCryptoLoweringOwnershipExists, 0x0f0ef1ac, 0x94028)
TCTI_DECLARE_REJECT_LOWERING_TEST(Crypto_FDOTRejectsUntilCryptoLoweringOwnershipExists, 0x0e02fc20, 0x94029)
TCTI_DECLARE_REJECT_LOWERING_TEST(Crypto_BFDOTRejectsUntilCryptoLoweringOwnershipExists, 0x2e48fce6, 0x9402a)
TCTI_DECLARE_REJECT_LOWERING_TEST(Crypto_BCAXRejectsUntilCryptoLoweringOwnershipExists, 0xce2d398b, 0x9401c)
TCTI_DECLARE_REJECT_LOWERING_TEST(Crypto_XARRejectsUntilCryptoLoweringOwnershipExists, 0xce911e0f, 0x94020)

TCTI_DECLARE_REJECT_LOWERING_TEST(Tagging_LD64BRejectsUntilTaggingLoweringOwnershipExists, 0xf83fd020, 0x95004)
TCTI_DECLARE_REJECT_LOWERING_TEST(Tagging_ST64BRejectsUntilTaggingLoweringOwnershipExists, 0xf83f9062, 0x95008)
TCTI_DECLARE_REJECT_LOWERING_TEST(Tagging_LDGMRejectsUntilTaggingLoweringOwnershipExists, 0xd9e000a4, 0x9500c)
TCTI_DECLARE_REJECT_LOWERING_TEST(Tagging_STGRejectsUntilTaggingLoweringOwnershipExists, 0xd92008e6, 0x95010)
TCTI_DECLARE_REJECT_LOWERING_TEST(Tagging_STGMRejectsUntilTaggingLoweringOwnershipExists, 0xd9a00128, 0x95014)
TCTI_DECLARE_REJECT_LOWERING_TEST(Tagging_STZGRejectsUntilTaggingLoweringOwnershipExists, 0xd960096a, 0x95018)
TCTI_DECLARE_REJECT_LOWERING_TEST(Tagging_STZGMRejectsUntilTaggingLoweringOwnershipExists, 0xd92001ac, 0x9501c)
TCTI_DECLARE_REJECT_LOWERING_TEST(Tagging_ST2GRejectsUntilTaggingLoweringOwnershipExists, 0xd9a009ee, 0x95020)
TCTI_DECLARE_REJECT_LOWERING_TEST(Tagging_STZ2GRejectsUntilTaggingLoweringOwnershipExists, 0xd9e00a30, 0x95024)

TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDAPR, 0xb8bfc020, 0x98200, 0, 1, 31,
                                  A64_SIZE_W, 1)
TCTI_DECLARE_PAIR_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDXP, 0x887f0440, 0x98220, 2,
                                       A64_SIZE_W, 1, 1)
TCTI_DECLARE_PAIR_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STXP, 0x88200861, 0x98240, 3,
                                       A64_SIZE_W, 0, 2)
TCTI_DECLARE_PAIR_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDAXP, 0x887f8440, 0x98260, 2,
                                       A64_SIZE_W, 1, 1)
TCTI_DECLARE_PAIR_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STLXP, 0x88208861, 0x98280, 3,
                                       A64_SIZE_W, 0, 2)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_SWP, 0xb8208041, 0x982a0, 1, 2, 0,
                                  A64_SIZE_W, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDADD, 0xb8200041, 0x982c0, 1, 2, 0,
                                  A64_SIZE_W, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDCLR, 0xb8201041, 0x982e0, 1, 2, 0,
                                  A64_SIZE_W, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDEOR, 0xb8202041, 0x98300, 1, 2, 0,
                                  A64_SIZE_W, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDSET, 0xb8203041, 0x98320, 1, 2, 0,
                                  A64_SIZE_W, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDSMAX, 0xb8204041, 0x98340, 1, 2, 0,
                                  A64_SIZE_W, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDSMIN, 0xb8205041, 0x98360, 1, 2, 0,
                                  A64_SIZE_W, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDUMAX, 0xb8206041, 0x98380, 1, 2, 0,
                                  A64_SIZE_W, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDUMIN, 0xb8207041, 0x983a0, 1, 2, 0,
                                  A64_SIZE_W, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STADD, 0xb820005f, 0x983c0, 31, 2, 0,
                                  A64_SIZE_W, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STCLR, 0xb820105f, 0x983e0, 31, 2, 0,
                                  A64_SIZE_W, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STEOR, 0xb820205f, 0x98400, 31, 2, 0,
                                  A64_SIZE_W, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STSET, 0xb820305f, 0x98420, 31, 2, 0,
                                  A64_SIZE_W, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STSMAX, 0xb820405f, 0x98440, 31, 2, 0,
                                  A64_SIZE_W, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STSMIN, 0xb820505f, 0x98460, 31, 2, 0,
                                  A64_SIZE_W, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STUMAX, 0xb820605f, 0x98480, 31, 2, 0,
                                  A64_SIZE_W, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STUMIN, 0xb820705f, 0x984a0, 31, 2, 0,
                                  A64_SIZE_W, 0)

TCTI_DECLARE_SCALAR_LDST_LOWERING_TEST(ScalarLoadStore_LDRB, 0x39400020, 0x100000, 0, 1, A64_SIZE_B,
                                       NO, NO, A64_INDEX_OFFSET, 0)
TCTI_DECLARE_SCALAR_LDST_LOWERING_TEST(ScalarLoadStore_LDRSW, 0xb9800062, 0x100004, 2, 3, A64_SIZE_W,
                                       YES, YES, A64_INDEX_OFFSET, 0)
TCTI_DECLARE_SCALAR_LDST_LOWERING_TEST(ScalarLoadStore_STRB, 0x390000a4, 0x100008, 4, 5, A64_SIZE_B,
                                       NO, NO, A64_INDEX_OFFSET, 0)
TCTI_DECLARE_SCALAR_LDST_LOWERING_TEST(ScalarLoadStore_STRH, 0x790000e6, 0x10000c, 6, 7, A64_SIZE_H,
                                       NO, NO, A64_INDEX_OFFSET, 0)
TCTI_DECLARE_SCALAR_LDST_LOWERING_TEST(ScalarLoadStore_LDURH, 0x785ff128, 0x100010, 8, 9, A64_SIZE_H,
                                       NO, NO, A64_INDEX_OFFSET, -1)
TCTI_DECLARE_SCALAR_LDST_LOWERING_TEST(ScalarLoadStore_LDURSB, 0x389ff16a, 0x100014, 10, 11, A64_SIZE_B,
                                       YES, YES, A64_INDEX_OFFSET, -1)
TCTI_DECLARE_SCALAR_LDST_LOWERING_TEST(ScalarLoadStore_LDURSH, 0x789ff1ac, 0x100018, 12, 13, A64_SIZE_H,
                                       YES, YES, A64_INDEX_OFFSET, -1)
TCTI_DECLARE_SCALAR_LDST_LOWERING_TEST(ScalarLoadStore_LDURSW, 0xb89fc1ee, 0x10001c, 14, 15, A64_SIZE_W,
                                       YES, YES, A64_INDEX_OFFSET, -4)
TCTI_DECLARE_SCALAR_LDST_LOWERING_TEST(ScalarLoadStore_STURB, 0x381ff230, 0x100020, 16, 17, A64_SIZE_B,
                                       NO, NO, A64_INDEX_OFFSET, -1)
TCTI_DECLARE_SCALAR_LDST_LOWERING_TEST(ScalarLoadStore_STURH, 0x781fe272, 0x100024, 18, 19, A64_SIZE_H,
                                       NO, NO, A64_INDEX_OFFSET, -2)
TCTI_DECLARE_SCALAR_LDST_LOWERING_TEST(ScalarLoadStore_LDTR, 0xf8400ab4, 0x100028, 20, 21, A64_SIZE_X,
                                       YES, NO, A64_INDEX_OFFSET, 0)
TCTI_DECLARE_SCALAR_LDST_LOWERING_TEST(ScalarLoadStore_LDTRB, 0x38400af6, 0x10002c, 22, 23, A64_SIZE_B,
                                       NO, NO, A64_INDEX_OFFSET, 0)
TCTI_DECLARE_SCALAR_LDST_LOWERING_TEST(ScalarLoadStore_LDTRH, 0x78400b38, 0x100030, 24, 25, A64_SIZE_H,
                                       NO, NO, A64_INDEX_OFFSET, 0)
TCTI_DECLARE_SCALAR_LDST_LOWERING_TEST(ScalarLoadStore_LDTRSB, 0x38800b7a, 0x100034, 26, 27, A64_SIZE_B,
                                       YES, YES, A64_INDEX_OFFSET, 0)
TCTI_DECLARE_SCALAR_LDST_LOWERING_TEST(ScalarLoadStore_LDTRSH, 0x78800bbc, 0x100038, 28, 29, A64_SIZE_H,
                                       YES, YES, A64_INDEX_OFFSET, 0)
TCTI_DECLARE_SCALAR_LDST_LOWERING_TEST(ScalarLoadStore_LDTRSW, 0xb8800820, 0x10003c, 0, 1, A64_SIZE_W,
                                       YES, YES, A64_INDEX_OFFSET, 0)
TCTI_DECLARE_SCALAR_LDST_LOWERING_TEST(ScalarLoadStore_STTR, 0xf8000862, 0x100040, 2, 3, A64_SIZE_X,
                                       YES, NO, A64_INDEX_OFFSET, 0)
TCTI_DECLARE_SCALAR_LDST_LOWERING_TEST(ScalarLoadStore_STTRB, 0x380008a4, 0x100044, 4, 5, A64_SIZE_B,
                                       NO, NO, A64_INDEX_OFFSET, 0)
TCTI_DECLARE_SCALAR_LDST_LOWERING_TEST(ScalarLoadStore_STTRH, 0x780008e6, 0x100048, 6, 7, A64_SIZE_H,
                                       NO, NO, A64_INDEX_OFFSET, 0)
TCTI_DECLARE_SCALAR_LDST_LOWERING_TEST(ScalarLoadStore_LDAPUR, 0xd9400128, 0x10004c, 8, 9, A64_SIZE_X,
                                       YES, NO, A64_INDEX_OFFSET, 0)
TCTI_DECLARE_SCALAR_LDST_LOWERING_TEST(ScalarLoadStore_LDAPURB, 0x1940016a, 0x100050, 10, 11, A64_SIZE_B,
                                       NO, NO, A64_INDEX_OFFSET, 0)
TCTI_DECLARE_SCALAR_LDST_LOWERING_TEST(ScalarLoadStore_LDAPURH, 0x594001ac, 0x100054, 12, 13, A64_SIZE_H,
                                       NO, NO, A64_INDEX_OFFSET, 0)
TCTI_DECLARE_SCALAR_LDST_LOWERING_TEST(ScalarLoadStore_LDAPURSB, 0x198001ee, 0x100058, 14, 15, A64_SIZE_B,
                                       YES, YES, A64_INDEX_OFFSET, 0)
TCTI_DECLARE_SCALAR_LDST_LOWERING_TEST(ScalarLoadStore_LDAPURSH, 0x59800230, 0x10005c, 16, 17, A64_SIZE_H,
                                       YES, YES, A64_INDEX_OFFSET, 0)
TCTI_DECLARE_SCALAR_LDST_LOWERING_TEST(ScalarLoadStore_LDAPURSW, 0x99800272, 0x100060, 18, 19, A64_SIZE_W,
                                       YES, YES, A64_INDEX_OFFSET, 0)
TCTI_DECLARE_SCALAR_LDST_LOWERING_TEST(ScalarLoadStore_STLUR, 0xd90002b4, 0x100064, 20, 21, A64_SIZE_X,
                                       YES, NO, A64_INDEX_OFFSET, 0)
TCTI_DECLARE_SCALAR_LDST_LOWERING_TEST(ScalarLoadStore_STLURB, 0x190002f6, 0x100068, 22, 23, A64_SIZE_B,
                                       NO, NO, A64_INDEX_OFFSET, 0)
TCTI_DECLARE_SCALAR_LDST_LOWERING_TEST(ScalarLoadStore_STLURH, 0x59000338, 0x10006c, 24, 25, A64_SIZE_H,
                                       NO, NO, A64_INDEX_OFFSET, 0)
TCTI_DECLARE_PREFETCH_LOWERING_TEST(ScalarLoadStore_PRFM, 0xf9800000, 0x100070, 0, A64_INDEX_OFFSET, 0)
TCTI_DECLARE_PREFETCH_LOWERING_TEST(ScalarLoadStore_PRFUM, 0xf8800020, 0x100074, 1, A64_INDEX_OFFSET, 0)

TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDARB, 0x08dffc20, 0x110000, 0, 1, 31,
                                  A64_SIZE_B, 1)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDARH, 0x48dffc62, 0x110004, 2, 3, 31,
                                  A64_SIZE_H, 1)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STLRB, 0x089ffca4, 0x110008, 4, 5, 31,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STLRH, 0x489ffce6, 0x11000c, 6, 7, 31,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDAPRB, 0x38bfc128, 0x110010, 8, 9, 31,
                                  A64_SIZE_B, 1)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDAPRH, 0x78bfc16a, 0x110014, 10, 11, 31,
                                  A64_SIZE_H, 1)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDAXRB, 0x085ffdac, 0x110018, 12, 13, 31,
                                  A64_SIZE_B, 1)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDAXRH, 0x485ffdee, 0x11001c, 14, 15, 31,
                                  A64_SIZE_H, 1)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STLXRB, 0x0810fe51, 0x110020, 17, 18, 16,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STLXRH, 0x4813feb4, 0x110024, 20, 21, 19,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDXRB, 0x085f7ef6, 0x110028, 22, 23, 31,
                                  A64_SIZE_B, 1)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDXRH, 0x485f7f38, 0x11002c, 24, 25, 31,
                                  A64_SIZE_H, 1)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STXRB, 0x081a7f9b, 0x110030, 27, 28, 26,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STXRH, 0x48007c41, 0x110034, 1, 2, 0,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_CASB, 0x08a07c41, 0x110100, 1, 2, 0,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_CASH, 0x48a17c62, 0x110104, 2, 3, 1,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_CASAB, 0x08e37ca4, 0x11010c, 4, 5, 3,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_CASAH, 0x48e47cc5, 0x110110, 5, 6, 4,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_CASLB, 0x08a6fd07, 0x110118, 7, 8, 6,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_CASLH, 0x48a7fd28, 0x11011c, 8, 9, 7,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_CASALB, 0x08e9fd6a, 0x110124, 10, 11, 9,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_CASALH, 0x48eafd8b, 0x110128, 11, 12, 10,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_SWPB, 0x38208041, 0x110130, 1, 2, 0,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_SWPH, 0x78218062, 0x110134, 2, 3, 1,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_SWPAB, 0x38a380a4, 0x11013c, 4, 5, 3,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_SWPAH, 0x78a480c5, 0x110140, 5, 6, 4,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_SWPLB, 0x38668107, 0x110148, 7, 8, 6,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_SWPLH, 0x78678128, 0x11014c, 8, 9, 7,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_SWPALB, 0x38e9816a, 0x110154, 10, 11, 9,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_SWPALH, 0x78ea818b, 0x110158, 11, 12, 10,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDADDB, 0x38200041, 0x110160, 1, 2, 0,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDADDH, 0x78210062, 0x110164, 2, 3, 1,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDADDAB, 0x38a300a4, 0x11016c, 4, 5, 3,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDADDAH, 0x78a400c5, 0x110170, 5, 6, 4,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDADDLB, 0x38660107, 0x110178, 7, 8, 6,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDADDLH, 0x78670128, 0x11017c, 8, 9, 7,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDADDALB, 0x38e9016a, 0x110184, 10, 11, 9,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDADDALH, 0x78ea018b, 0x110188, 11, 12, 10,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDCLRB, 0x38201041, 0x110190, 1, 2, 0,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDCLRH, 0x78211062, 0x110194, 2, 3, 1,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDCLRAB, 0x38a310a4, 0x11019c, 4, 5, 3,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDCLRAH, 0x78a410c5, 0x1101a0, 5, 6, 4,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDCLRLB, 0x38661107, 0x1101a8, 7, 8, 6,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDCLRLH, 0x78671128, 0x1101ac, 8, 9, 7,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDCLRALB, 0x38e9116a, 0x1101b4, 10, 11, 9,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDCLRALH, 0x78ea118b, 0x1101b8, 11, 12, 10,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDEORB, 0x38202041, 0x1101c0, 1, 2, 0,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDEORH, 0x78212062, 0x1101c4, 2, 3, 1,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDEORAB, 0x38a320a4, 0x1101cc, 4, 5, 3,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDEORAH, 0x78a420c5, 0x1101d0, 5, 6, 4,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDEORLB, 0x38662107, 0x1101d8, 7, 8, 6,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDEORLH, 0x78672128, 0x1101dc, 8, 9, 7,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDEORALB, 0x38e9216a, 0x1101e4, 10, 11, 9,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDEORALH, 0x78ea218b, 0x1101e8, 11, 12, 10,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDSETB, 0x38203041, 0x1101f0, 1, 2, 0,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDSETH, 0x78213062, 0x1101f4, 2, 3, 1,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDSETAB, 0x38a330a4, 0x1101fc, 4, 5, 3,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDSETAH, 0x78a430c5, 0x110200, 5, 6, 4,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDSETLB, 0x38663107, 0x110208, 7, 8, 6,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDSETLH, 0x78673128, 0x11020c, 8, 9, 7,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDSETALB, 0x38e9316a, 0x110214, 10, 11, 9,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDSETALH, 0x78ea318b, 0x110218, 11, 12, 10,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDSMAXB, 0x38204041, 0x110220, 1, 2, 0,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDSMAXH, 0x78214062, 0x110224, 2, 3, 1,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDSMAXAB, 0x38a340a4, 0x11022c, 4, 5, 3,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDSMAXAH, 0x78a440c5, 0x110230, 5, 6, 4,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDSMAXLB, 0x38664107, 0x110238, 7, 8, 6,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDSMAXLH, 0x78674128, 0x11023c, 8, 9, 7,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDSMAXALB, 0x38e9416a, 0x110244, 10, 11, 9,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDSMAXALH, 0x78ea418b, 0x110248, 11, 12, 10,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDSMINB, 0x38205041, 0x110250, 1, 2, 0,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDSMINH, 0x78215062, 0x110254, 2, 3, 1,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDSMINAB, 0x38a350a4, 0x11025c, 4, 5, 3,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDSMINAH, 0x78a450c5, 0x110260, 5, 6, 4,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDSMINLB, 0x38665107, 0x110268, 7, 8, 6,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDSMINLH, 0x78675128, 0x11026c, 8, 9, 7,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDSMINALB, 0x38e9516a, 0x110274, 10, 11, 9,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDSMINALH, 0x78ea518b, 0x110278, 11, 12, 10,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDUMAXB, 0x38206041, 0x110280, 1, 2, 0,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDUMAXH, 0x78216062, 0x110284, 2, 3, 1,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDUMAXAB, 0x38a360a4, 0x11028c, 4, 5, 3,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDUMAXAH, 0x78a460c5, 0x110290, 5, 6, 4,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDUMAXLB, 0x38666107, 0x110298, 7, 8, 6,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDUMAXLH, 0x78676128, 0x11029c, 8, 9, 7,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDUMAXALB, 0x38e9616a, 0x1102a4, 10, 11, 9,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDUMAXALH, 0x78ea618b, 0x1102a8, 11, 12, 10,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDUMINB, 0x38207041, 0x1102b0, 1, 2, 0,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDUMINH, 0x78217062, 0x1102b4, 2, 3, 1,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDUMINAB, 0x38a370a4, 0x1102bc, 4, 5, 3,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDUMINAH, 0x78a470c5, 0x1102c0, 5, 6, 4,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDUMINLB, 0x38667107, 0x1102c8, 7, 8, 6,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDUMINLH, 0x78677128, 0x1102cc, 8, 9, 7,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDUMINALB, 0x38e9716a, 0x1102d4, 10, 11, 9,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_LDUMINALH, 0x78ea718b, 0x1102d8, 11, 12, 10,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STADDB, 0x3820003f, 0x1102e0, 31, 1, 0,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STADDH, 0x7821005f, 0x1102e4, 31, 2, 1,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STADDLB, 0x3862007f, 0x1102e8, 31, 3, 2,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STADDLH, 0x7863009f, 0x1102ec, 31, 4, 3,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STADDL, 0xb86400bf, 0x1102f0, 31, 5, 4,
                                  A64_SIZE_W, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STCLRB, 0x3820103f, 0x1102f4, 31, 1, 0,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STCLRH, 0x7821105f, 0x1102f8, 31, 2, 1,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STCLRLB, 0x3862107f, 0x1102fc, 31, 3, 2,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STCLRLH, 0x7863109f, 0x110300, 31, 4, 3,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STCLRL, 0xb86410bf, 0x110304, 31, 5, 4,
                                  A64_SIZE_W, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STEORB, 0x3820203f, 0x110308, 31, 1, 0,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STEORH, 0x7821205f, 0x11030c, 31, 2, 1,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STEORLB, 0x3862207f, 0x110310, 31, 3, 2,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STEORLH, 0x7863209f, 0x110314, 31, 4, 3,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STEORL, 0xb86420bf, 0x110318, 31, 5, 4,
                                  A64_SIZE_W, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STSETB, 0x3820303f, 0x11031c, 31, 1, 0,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STSETH, 0x7821305f, 0x110320, 31, 2, 1,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STSETLB, 0x3862307f, 0x110324, 31, 3, 2,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STSETLH, 0x7863309f, 0x110328, 31, 4, 3,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STSETL, 0xb86430bf, 0x11032c, 31, 5, 4,
                                  A64_SIZE_W, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STSMAXB, 0x3820403f, 0x110330, 31, 1, 0,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STSMAXH, 0x7821405f, 0x110334, 31, 2, 1,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STSMAXLB, 0x3862407f, 0x110338, 31, 3, 2,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STSMAXLH, 0x7863409f, 0x11033c, 31, 4, 3,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STSMAXL, 0xb86440bf, 0x110340, 31, 5, 4,
                                  A64_SIZE_W, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STSMINB, 0x3820503f, 0x110344, 31, 1, 0,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STSMINH, 0x7821505f, 0x110348, 31, 2, 1,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STSMINLB, 0x3862507f, 0x11034c, 31, 3, 2,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STSMINLH, 0x7863509f, 0x110350, 31, 4, 3,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STSMINL, 0xb86450bf, 0x110354, 31, 5, 4,
                                  A64_SIZE_W, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STUMAXB, 0x3820603f, 0x110358, 31, 1, 0,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STUMAXH, 0x7821605f, 0x11035c, 31, 2, 1,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STUMAXLB, 0x3862607f, 0x110360, 31, 3, 2,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STUMAXLH, 0x7863609f, 0x110364, 31, 4, 3,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STUMAXL, 0xb86460bf, 0x110368, 31, 5, 4,
                                  A64_SIZE_W, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STUMINB, 0x3820703f, 0x11036c, 31, 1, 0,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STUMINH, 0x7821705f, 0x110370, 31, 2, 1,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STUMINLB, 0x3862707f, 0x110374, 31, 3, 2,
                                  A64_SIZE_B, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STUMINLH, 0x7863709f, 0x110378, 31, 4, 3,
                                  A64_SIZE_H, 0)
TCTI_DECLARE_ATOMIC_LOWERING_TEST(OrderedExclusiveAtomic_STUMINL, 0xb86470bf, 0x11037c, 31, 5, 4,
                                  A64_SIZE_W, 0)

TCTI_DECLARE_VECTOR_UNARY_LOWERING_TEST(VectorIntegerLogical_CNT, 0x4e205820, 0x98580,
                                        gadget_simd_cnt, 0, 1, 16)
TCTI_DECLARE_VECTOR_LOWERING_TEST(VectorIntegerLogical_TBL, 0x0e020020, 0x985a0,
                                  gadget_simd_tbl, 0, 1, 2, 8)
TCTI_DECLARE_VECTOR_LOWERING_TEST(VectorIntegerLogical_TBX, 0x0e021020, 0x985c0,
                                  gadget_simd_tbx, 0, 1, 2, 8)
TCTI_DECLARE_VECTOR_LOWERING_TEST(VectorIntegerLogical_ZIP1, 0x4e023820, 0x985e0,
                                  gadget_simd_zip1, 0, 1, 2, 16)
TCTI_DECLARE_VECTOR_LOWERING_TEST(VectorIntegerLogical_ZIP2, 0x4e027820, 0x985f0,
                                  gadget_simd_zip2, 0, 1, 2, 16)
TCTI_DECLARE_VECTOR_LOWERING_TEST(VectorIntegerLogical_TRN1, 0x4e022820, 0x98600,
                                  gadget_simd_trn1, 0, 1, 2, 16)
TCTI_DECLARE_VECTOR_LOWERING_TEST(VectorIntegerLogical_TRN2, 0x4e026820, 0x98610,
                                  gadget_simd_trn2, 0, 1, 2, 16)
TCTI_DECLARE_VECTOR_LOWERING_TEST(VectorIntegerLogical_UZP1, 0x4e021820, 0x98620,
                                  gadget_simd_uzp1, 0, 1, 2, 16)
TCTI_DECLARE_VECTOR_LOWERING_TEST(VectorIntegerLogical_UZP2, 0x4e025820, 0x98630,
                                  gadget_simd_uzp2, 0, 1, 2, 16)
TCTI_DECLARE_VECTOR_UNARY_LOWERING_TEST(VectorIntegerLogical_XTN2, 0x4e212820, 0x98635,
                                        gadget_simd_xtn2, 0, 1, 16)
TCTI_DECLARE_VECTOR_LOWERING_TEST(VectorIntegerLogical_AND, 0x4e221c20, 0x98640,
                                  gadget_simd_and, 0, 1, 2, 16)
TCTI_DECLARE_VECTOR_LOWERING_TEST(VectorIntegerLogical_ORR, 0x4ea51c83, 0x98660,
                                  gadget_simd_orr, 3, 4, 5, 16)
TCTI_DECLARE_VECTOR_LOWERING_TEST(VectorIntegerLogical_EOR, 0x6e281ce6, 0x98680,
                                  gadget_simd_eor, 6, 7, 8, 16)
TCTI_DECLARE_VECTOR_LOWERING_TEST(VectorIntegerLogical_ADD, 0x4e2b8549, 0x986a0,
                                  gadget_simd_add, 9, 10, 11, 16)
TCTI_DECLARE_VECTOR_LOWERING_TEST(VectorIntegerLogical_SUB, 0x6e228420, 0x986c0,
                                  gadget_simd_sub, 0, 1, 2, 16)
TCTI_DECLARE_VECTOR_LOWERING_TEST(VectorIntegerLogical_MUL, 0x4e259c83, 0x986e0,
                                  gadget_simd_mul, 3, 4, 5, 16)
TCTI_DECLARE_VECTOR_LOWERING_TEST(VectorIntegerLogical_BIC, 0x4e621c20, 0x98700,
                                  gadget_simd_bic, 0, 1, 2, 16)
TCTI_DECLARE_VECTOR_LOWERING_TEST(VectorIntegerLogical_ORN, 0x4ee21c20, 0x98720,
                                  gadget_simd_orn, 0, 1, 2, 16)
TCTI_DECLARE_VECTOR_LOWERING_TEST(VectorIntegerLogical_BSL, 0x6e621c20, 0x98740,
                                  gadget_simd_bsl, 0, 1, 2, 16)
TCTI_DECLARE_VECTOR_LOWERING_TEST(VectorIntegerLogical_BIT, 0x6ea21c20, 0x98760,
                                  gadget_simd_bit, 0, 1, 2, 16)
TCTI_DECLARE_VECTOR_LOWERING_TEST(VectorIntegerLogical_BIF, 0x6ee21c20, 0x98780,
                                  gadget_simd_bif, 0, 1, 2, 16)

- (void)testLoweringContract_VectorIntegerLogical_EXT
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0x6e024020 atPC:0x987a0 state:&state gadgets:gadgets];
    XCTAssertEqual(gadgets[0], gadget_simd_ext);
    XCTAssertGreaterThanOrEqual(state.num_gadgets, (size_t)6);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], (uint64_t)0);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], (uint64_t)1);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[3], (uint64_t)2);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[4], (uint64_t)8);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[5], (uint64_t)16);
}

- (void)testLoweringContract_VectorIntegerLogical_INS
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0x4e181c20 atPC:0x987c0 state:&state gadgets:gadgets];
    XCTAssertEqual(gadgets[0], gadget_simd_ins_gpr);
    XCTAssertGreaterThanOrEqual(state.num_gadgets, (size_t)5);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], (uint64_t)0);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], (uint64_t)1);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[3], (uint64_t)8);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[4], (uint64_t)1);
}

- (void)testLoweringContract_VectorIntegerLogical_XTN
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0x0e212820 atPC:0x987e0 state:&state gadgets:gadgets];
    XCTAssertEqual(gadgets[0], gadget_simd_xtn);
    XCTAssertGreaterThanOrEqual(state.num_gadgets, (size_t)4);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], (uint64_t)0);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], (uint64_t)1);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[3], (uint64_t)8);
}

@end
