#import <XCTest/XCTest.h>

#include <dlfcn.h>

#import <IXLandLinuxRuntime/emu/aarch64/decode.h>
#import <IXLandLinuxRuntime/tcti/aarch64/gen.h>

typedef void (*tcti_gadget_t)(void);
extern void gadget_br_impl(void);
extern void gadget_ccmp_fallback_impl(void);

#define TCTI_DECLARE_ATOMIC_LOWERING_TEST(_name, _insn, _pc, _rd, _rn, _rm, _size, _isLoad)      \
- (void)testLoweringContract_##_name                                                               \
{                                                                                                  \
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];                                              \
    a64_gen_state_t state;                                                                         \
    [self generateInstruction:_insn atPC:_pc state:&state gadgets:gadgets];                       \
    void *expected = dlsym(RTLD_DEFAULT, "gadget_atomic_ldst");                                    \
    XCTAssertNotEqual(expected, NULL, @"gadget_atomic_ldst must be link-visible for lowering");   \
    XCTAssertEqual((void *)gadgets[0], expected);                                                  \
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
    void *expected = dlsym(RTLD_DEFAULT, "gadget_atomic_ldst");                                    \
    XCTAssertNotEqual(expected, NULL);                                                              \
    XCTAssertEqual((void *)gadgets[0], expected);                                                  \
    XCTAssertGreaterThanOrEqual(state.num_gadgets, (size_t)7);                                     \
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], (uint64_t)_pc);                               \
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[3], (uint64_t)_rn);                               \
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[5], (uint64_t)_size);                             \
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[6], (uint64_t)_isLoad);                           \
}

#define TCTI_DECLARE_VECTOR_LOWERING_TEST(_name, _insn, _pc, _symbol, _rd, _rn, _rm, _vecBytes)  \
- (void)testLoweringContract_##_name                                                               \
{                                                                                                  \
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];                                              \
    a64_gen_state_t state;                                                                         \
    [self generateInstruction:_insn atPC:_pc state:&state gadgets:gadgets];                       \
    void *expected = dlsym(RTLD_DEFAULT, _symbol);                                                 \
    XCTAssertNotEqual(expected, NULL, @"%s must be link-visible for lowering", _symbol);          \
    XCTAssertEqual((void *)gadgets[0], expected);                                                  \
    XCTAssertGreaterThanOrEqual(state.num_gadgets, (size_t)5);                                     \
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], (uint64_t)_rd);                               \
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], (uint64_t)_rn);                               \
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[3], (uint64_t)_rm);                               \
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[4], (uint64_t)_vecBytes);                         \
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

TCTI_DECLARE_VECTOR_LOWERING_TEST(VectorIntegerLogical_CMGE, 0x4e223c20, 0x984c0,
                                  "gadget_simd_cmge", 0, 1, 2, 16)
TCTI_DECLARE_VECTOR_LOWERING_TEST(VectorIntegerLogical_CMHI, 0x6e223420, 0x984e0,
                                  "gadget_simd_cmhi", 0, 1, 2, 16)
TCTI_DECLARE_VECTOR_LOWERING_TEST(VectorIntegerLogical_CMHS, 0x6e223c20, 0x98500,
                                  "gadget_simd_cmhs", 0, 1, 2, 16)
TCTI_DECLARE_VECTOR_LOWERING_TEST(VectorIntegerLogical_CMLE, 0x6e209820, 0x98520,
                                  "gadget_simd_cmle", 0, 1, 0, 16)
TCTI_DECLARE_VECTOR_LOWERING_TEST(VectorIntegerLogical_CMLT, 0x4e20a820, 0x98540,
                                  "gadget_simd_cmlt", 0, 1, 0, 16)
TCTI_DECLARE_VECTOR_LOWERING_TEST(VectorIntegerLogical_CMTST, 0x4e228c20, 0x98560,
                                  "gadget_simd_cmtst", 0, 1, 2, 16)

@end
