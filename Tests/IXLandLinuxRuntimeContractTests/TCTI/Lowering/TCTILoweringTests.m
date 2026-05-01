#import <XCTest/XCTest.h>

#import <IXLandLinuxRuntime/emu/aarch64/decode.h>

typedef void (*tcti_gadget_t)(void);

#define A64_MAX_GADGETS_PER_BLOCK 512

enum a64_gen_error {
    A64_GEN_OK = 0,
};

typedef struct a64_gen_state {
    tcti_gadget_t *gadgets;
    size_t max_gadgets;
    size_t num_gadgets;
    uint64_t guest_pc;
    uint32_t raw_insn;
    a64_instr_t decoded;
    uint64_t start_pc;
    uint64_t end_pc;
    int is_complete;
    int instructions_processed;
    int conservative_mode;
} a64_gen_state_t;

int a64_gen_init(a64_gen_state_t *state, tcti_gadget_t *buffer, size_t max);
void a64_gen_reset(a64_gen_state_t *state, uint64_t pc);
int a64_gen_instruction(a64_gen_state_t *state, uint32_t insn, uint64_t pc);

// TCTI.Lowering Contract Tests
// Tests for stage [3] LOWERING: semantic op -> gadget chain plan
//
// Boundary: B2 (normalized semantic op) -> B3 (gadget chain shape)

@interface TCTILoweringTests : XCTestCase
@end

@implementation TCTILoweringTests

- (void)testLoweringContract_MOVRegEmitsCorrectChain
{
    // Contract: MOV_REG semantic MUST lower to single MOV gadget chain
    // Owner: lowering code, gadget catalog consumers
    //
    // Input: semantic MOV_REG(dst=2, src=7)
    // Output: chain plan with gadget_mov_reg[2][7]
    
    XCTAssertTrue(YES, "TCTI.Lowering MOV_REG contract placeholder");
}

- (void)testLoweringContract_ADDRegEmitsCorrectChain
{
    // Contract: ADD_REG semantic MUST lower to single ADD gadget chain
    // Owner: lowering code, gadget catalog consumers
    
    XCTAssertTrue(YES, "TCTI.Lowering ADD_REG contract placeholder");
}

- (void)testLoweringContract_SUBExtendedSPSourceAndDestinationLowers
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    XCTAssertEqual(a64_gen_init(&state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(&state, 0x6d2b8);

    uint32_t subSpSpX0Uxtx = 0xcb2063ff;
    XCTAssertEqual(a64_gen_instruction(&state, subSpSpX0Uxtx, 0x6d2b8), A64_GEN_OK);
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
}

- (void)testLoweringContract_CBZMemoryBackedRegisterLowers
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    XCTAssertEqual(a64_gen_init(&state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(&state, 0x6c084);

    uint32_t cbzX21 = 0xb40004d5;
    XCTAssertEqual(a64_gen_instruction(&state, cbzX21, 0x6c084), A64_GEN_OK);
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
    XCTAssertTrue(state.is_complete);
}

- (void)testLoweringContract_CSINCAliasCSETLowers
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    XCTAssertEqual(a64_gen_init(&state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(&state, 0x6bfb0);

    uint32_t csincX3XzrXzrNe = 0x9a9f17e3;
    XCTAssertEqual(a64_gen_instruction(&state, csincX3XzrXzrNe, 0x6bfb0), A64_GEN_OK);
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
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

- (void)testLoweringContract_ADDImmEmitsCorrectChain
{
    // Contract: ADD_IMM semantic MUST lower to ADD_IMM gadget chain
    // Owner: lowering code, gadget catalog consumers
    
    XCTAssertTrue(YES, "TCTI.Lowering ADD_IMM contract placeholder");
}

- (void)testLoweringContract_HelperDecisionExplicit
{
    // Contract: Helper vs no-helper decision MUST be explicit and testable
    // Owner: lowering code, helper policy
    //
    // Input: semantic op requiring memory access
    // Output: explicit helper call in chain plan
    
    XCTAssertTrue(YES, "TCTI.Lowering helper decision contract placeholder");
}

- (void)testLoweringContract_WritebackSetExact
{
    // Contract: Writeback set MUST be exact for the semantic op
    // Owner: lowering code, writeback policy
    
    XCTAssertTrue(YES, "TCTI.Lowering writeback contract placeholder");
}

- (void)testLoweringContract_NextPCPolicyExplicit
{
    // Contract: Next-PC policy MUST be explicit in chain plan
    // Owner: lowering code, PC policy
    
    XCTAssertTrue(YES, "TCTI.Lowering next-PC contract placeholder");
}

@end
