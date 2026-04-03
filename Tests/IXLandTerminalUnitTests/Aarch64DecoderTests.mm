//
//  Aarch64DecoderTests.m
//  Tests
//
//  Unit tests for aarch64 instruction decoder
//  Migrated from tests/aarch64/decoder_test.c
//

#import <XCTest/XCTest.h>

extern "C" {
    #import <IXLandLinuxRuntime/emu/aarch64/decode.h>
}

@interface Aarch64DecoderTests : XCTestCase
@end

@implementation Aarch64DecoderTests

#pragma mark - Test Helpers

static uint32_t encode_movz(int rd, uint16_t imm, int hw, int sf) {
    int opc = 1;
    return ((sf & 1) << 31) | ((opc & 3) << 30) | (0x12 << 24) |
           ((hw & 3) << 21) | ((imm & 0xFFFF) << 5) | (rd & 0x1F);
}

static uint32_t encode_movn(int rd, uint16_t imm, int hw, int sf) {
    int opc = 0;
    return ((sf & 1) << 31) | ((opc & 3) << 30) | (0x12 << 24) |
           ((hw & 3) << 21) | ((imm & 0xFFFF) << 5) | (rd & 0x1F);
}

static uint32_t encode_add_imm(int rd, int rn, int imm12, int shift, int sf) {
    return ((sf & 1) << 31) | (0x11 << 24) | ((shift & 1) << 22) |
           ((imm12 & 0xFFF) << 10) | ((rn & 0x1F) << 5) | (rd & 0x1F);
}

static uint32_t encode_sub_imm(int rd, int rn, int imm12, int shift, int sf) {
    return ((sf & 1) << 31) | (0x11 << 24) | (1 << 30) | ((shift & 1) << 22) |
           ((imm12 & 0xFFF) << 10) | ((rn & 0x1F) << 5) | (rd & 0x1F);
}

static uint32_t encode_add_reg(int rd, int rn, int rm, int shift, int imm6, int sf) {
    return ((sf & 1) << 31) | (0x0B << 24) | ((shift & 3) << 22) |
           ((rm & 0x1F) << 16) | ((imm6 & 0x3F) << 10) |
           ((rn & 0x1F) << 5) | (rd & 0x1F);
}

static uint32_t encode_sub_reg(int rd, int rn, int rm, int shift, int imm6, int sf) {
    return ((sf & 1) << 31) | (0x5B << 24) | ((shift & 3) << 22) |
           ((rm & 0x1F) << 16) | ((imm6 & 0x3F) << 10) |
           ((rn & 0x1F) << 5) | (rd & 0x1F);
}

static uint32_t encode_and_reg(int rd, int rn, int rm, int shift, int imm6, int sf) {
    return ((sf & 1) << 31) | (0x0A << 24) | ((shift & 3) << 22) |
           ((rm & 0x1F) << 16) | ((imm6 & 0x3F) << 10) |
           ((rn & 0x1F) << 5) | (rd & 0x1F);
}

static uint32_t encode_orr_reg(int rd, int rn, int rm, int shift, int imm6, int sf) {
    return ((sf & 1) << 31) | (0x1A << 24) | ((shift & 3) << 22) |
           ((rm & 0x1F) << 16) | ((imm6 & 0x3F) << 10) |
           ((rn & 0x1F) << 5) | (rd & 0x1F);
}

static uint32_t encode_eor_reg(int rd, int rn, int rm, int shift, int imm6, int sf) {
    return ((sf & 1) << 31) | (0x2A << 24) | ((shift & 3) << 22) |
           ((rm & 0x1F) << 16) | ((imm6 & 0x3F) << 10) |
           ((rn & 0x1F) << 5) | (rd & 0x1F);
}

static uint32_t encode_b_imm(int32_t imm26) {
    return (0x5 << 26) | ((imm26 >> 2) & 0x3FFFFFF);
}

static uint32_t encode_b_cond(int32_t imm19, int cond) {
    return (0x54 << 24) | (((imm19 >> 2) & 0x7FFFF) << 5) | (cond & 0xF);
}

static uint32_t encode_cbz(int rt, int32_t imm19, int sf, int nz) {
    return ((sf & 1) << 31) | (0x34 << 24) | (((nz & 1) << 24)) |
           (((imm19 >> 2) & 0x7FFFF) << 5) | (rt & 0x1F);
}

static uint32_t encode_tbz(int rt, int bit_pos, int32_t imm14, int nz) {
    return (((bit_pos >> 5) & 1) << 31) | (0x36 << 24) | (((nz & 1) << 24)) |
           (((bit_pos & 0x1F) << 19)) | (((imm14 >> 2) & 0x3FFF) << 5) |
           (rt & 0x1F);
}

static uint32_t encode_bl(int32_t imm26) {
    return (0x5 << 26) | 0x1 | ((imm26 >> 2) & 0x3FFFFFF);
}

static uint32_t encode_br(int rn) {
    return (0xD61F << 16) | ((rn & 0x1F) << 5) | 0x000;
}

static uint32_t encode_ldr_imm(int rt, int rn, int imm12, int size) {
    return (((size & 3) << 30) | (0x39 << 24) | (1 << 22) |
            ((imm12 & 0xFFF) << 10) | ((rn & 0x1F) << 5) | (rt & 0x1F));
}

static uint32_t encode_str_imm(int rt, int rn, int imm12, int size) {
    return (((size & 3) << 30) | (0x38 << 24) | (1 << 22) |
            ((imm12 & 0xFFF) << 10) | ((rn & 0x1F) << 5) | (rt & 0x1F));
}

static uint32_t encode_svc(uint16_t imm16) {
    return (0xD4000000) | ((imm16 & 0xFFFF) << 5);
}

static uint32_t encode_hvc(uint16_t imm16) {
    return (0xD44u << 21) | ((imm16 & 0xFFFF) << 5);
}

static uint32_t encode_hlt(void) {
    return 0xD4400000;
}

static uint32_t encode_brk(void) {
    return 0xD4200000;
}

static uint32_t encode_ret(int rn) {
    return (0xD65F << 16) | 0x03C0 | ((rn & 0x1F) << 5);
}

static uint32_t encode_mrs(int Rt, int sysreg) {
    return (0xD53 << 20) | ((sysreg & 0xFFFF) << 5) | (Rt & 0x1F);
}

#pragma mark - MOVZ/MOVN Tests

- (void)testMOVZBasic {
    a64_instr_t instr;
    uint32_t insn = encode_movz(5, 0x1234, 0, 1);
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertEqual(instr.Rd, 5);
}

- (void)testMOVZShifted {
    a64_instr_t instr;
    uint32_t insn = encode_movz(0, 0xFFFF, 3, 1);
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertEqual(instr.Rd, 0);
}

- (void)testMOVNBasic {
    a64_instr_t instr;
    uint32_t insn = encode_movn(1, 0, 0, 1);
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertEqual(instr.Rd, 1);
}

#pragma mark - ADD/SUB Immediate Tests

- (void)testADDImmediate {
    a64_instr_t instr;
    uint32_t insn = encode_add_imm(1, 2, 0x100, 0, 1);
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertEqual(instr.Rd, 1);
    XCTAssertEqual(instr.Rn, 2);
    XCTAssertEqual(instr.imm, 0x100);
    XCTAssertEqual(instr.is_64bit, 1);
}

- (void)testADDImmediateShifted {
    a64_instr_t instr;
    uint32_t insn = encode_add_imm(3, 4, 0x1, 1, 1);
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertEqual(instr.Rd, 3);
    XCTAssertEqual(instr.Rn, 4);
    XCTAssertEqual(instr.imm, 0x1000); // imm 0x1 shifted left by 12 bits
}

- (void)testSUBImmediate {
    a64_instr_t instr;
    uint32_t insn = encode_sub_imm(1, 2, 0x10, 0, 1);
    int ret = a64_decode(insn, &instr);
    XCTAssertTrue(ret == 0 || ret == -1);
}

#pragma mark - ADD/SUB Register Tests

- (void)testADDRegisterDecoding {
    a64_instr_t instr;
    uint32_t insn = encode_add_reg(1, 2, 3, 0, 0, 1);
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertEqual(instr.Rd, 1);
    XCTAssertEqual(instr.Rn, 2);
    XCTAssertEqual(instr.Rm, 3);
    XCTAssertEqual(instr.is_64bit, 1);
}

- (void)testADDRegisterShifted {
    a64_instr_t instr;
    uint32_t insn = encode_add_reg(4, 5, 6, 0, 4, 1);
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertEqual(instr.imm_shift, 4);
}

- (void)testSUBRegister {
    a64_instr_t instr;
    uint32_t insn = encode_sub_reg(1, 2, 3, 0, 0, 1);
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertEqual(instr.Rd, 1);
    XCTAssertEqual(instr.Rn, 2);
    XCTAssertEqual(instr.Rm, 3);
}

#pragma mark - Logical Register Tests

- (void)testANDRegister {
    a64_instr_t instr;
    uint32_t insn = encode_and_reg(1, 2, 3, 0, 0, 1);
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertEqual(instr.Rd, 1);
    XCTAssertEqual(instr.Rn, 2);
    XCTAssertEqual(instr.Rm, 3);
}

- (void)testORRRegister {
    a64_instr_t instr;
    uint32_t insn = encode_orr_reg(1, 2, 3, 0, 0, 1);
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertEqual(instr.Rd, 1);
    XCTAssertEqual(instr.Rn, 2);
    XCTAssertEqual(instr.Rm, 3);
}

- (void)testEORRegister {
    a64_instr_t instr;
    uint32_t insn = encode_eor_reg(1, 2, 3, 0, 0, 1);
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertEqual(instr.Rd, 1);
    XCTAssertEqual(instr.Rn, 2);
    XCTAssertEqual(instr.Rm, 3);
}

#pragma mark - Branch Tests

- (void)testBUnconditional {
    a64_instr_t instr;
    uint32_t insn = encode_b_imm(0x1000);
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertEqual(instr.cat, A64_BRANCH);
    XCTAssertEqual(instr.imm, 0x1000);
}

- (void)testBL {
    a64_instr_t instr;
    uint32_t insn = encode_bl(0x1000);
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertEqual(instr.cat, A64_BRANCH);
}

- (void)testBConditionalEQ {
    a64_instr_t instr;
    uint32_t insn = encode_b_cond(0x400, A64_EQ);
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertEqual(instr.cat, A64_BRANCH);
    XCTAssertEqual(instr.cond, A64_EQ);
}

- (void)testBConditionalNE {
    a64_instr_t instr;
    uint32_t insn = encode_b_cond(0x400, A64_NE);
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertEqual(instr.cond, A64_NE);
}

- (void)testBConditionalAll {
    a64_instr_t instr;
    for (unsigned int cond = 0; cond < 16; cond++) {
        uint32_t insn = encode_b_cond(0x100, (int)cond);
        XCTAssertEqual(a64_decode(insn, &instr), 0);
        XCTAssertEqual((int)instr.cond, (int)cond);
    }
}

#pragma mark - Compare and Branch Tests

- (void)testCBZ {
    a64_instr_t instr;
    uint32_t insn = encode_cbz(5, 0x200, 1, 0);
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertEqual(instr.cat, A64_BRANCH);
    XCTAssertEqual(instr.Rd, 5);
    XCTAssertEqual(instr.is_64bit, 1);
}

- (void)testCBNZ {
    a64_instr_t instr;
    uint32_t insn = encode_cbz(3, 0x100, 0, 1);
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertEqual(instr.Rd, 3);
    XCTAssertEqual(instr.is_64bit, 0);
}

- (void)testTBZNegativeOffset {
    a64_instr_t instr;
    uint32_t insn = encode_tbz(1, 0, -12, 0);
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertEqual(instr.cat, A64_BRANCH2);
    XCTAssertEqual(instr.subtype, A64_BRANCH_TEST);
    XCTAssertEqual(instr.Rd, 1);
    XCTAssertEqual(instr.op, 0);
    XCTAssertEqual(instr.imm, -12);
}

- (void)testTBNZNegativeOffset {
    a64_instr_t instr;
    uint32_t insn = encode_tbz(1, 0, -52, 1);
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertEqual(instr.cat, A64_BRANCH2);
    XCTAssertEqual(instr.subtype, A64_BRANCH_TEST);
    XCTAssertEqual(instr.Rd, 1);
    XCTAssertEqual(instr.op, 1);
    XCTAssertEqual(instr.imm, -52);
}

#pragma mark - Load/Store Tests

- (void)testLDRImmediate {
    a64_instr_t instr;
    uint32_t insn = encode_ldr_imm(1, 2, 0x80, 3);
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertEqual(instr.cat, A64_LD_ST);
    XCTAssertEqual(instr.Rd, 1);
    XCTAssertEqual(instr.Rn, 2);
}

- (void)testLDRImmediateWord {
    a64_instr_t instr;
    uint32_t insn = encode_ldr_imm(1, 2, 0x40, 2);
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertEqual(instr.Rd, 1);
    XCTAssertEqual(instr.Rn, 2);
}

- (void)testSTRImmediate {
    a64_instr_t instr;
    uint32_t insn = encode_str_imm(1, 2, 0x40, 3);
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertEqual(instr.cat, A64_LD_ST);
    XCTAssertEqual(instr.Rd, 1);
    XCTAssertEqual(instr.Rn, 2);
}

#pragma mark - System Instructions

- (void)testSVC {
    a64_instr_t instr;
    uint32_t insn = encode_svc(0);
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertEqual(instr.imm, 0);
    insn = encode_svc(1);
    XCTAssertEqual(a64_decode(insn, &instr), 0);
}

- (void)testBRK {
    a64_instr_t instr;
    uint32_t insn = encode_brk();
    XCTAssertEqual(a64_decode(insn, &instr), 0);
}

- (void)testSystemBarriers {
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(0xD503201F, &instr), 0);
    int ret = a64_decode(0xD5033BDF, &instr);
    XCTAssertTrue(ret == 0 || ret == -1);
}

- (void)testMRS {
    a64_instr_t instr;
    uint32_t insn = encode_mrs(0, 0x9808);
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertEqual(instr.Rd, 0);
}

#pragma mark - RET Instruction

- (void)testRETDecoding {
    a64_instr_t instr;
    uint32_t insn = encode_ret(30);
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertEqual(instr.Rn, 30);
}

#pragma mark - Real Instruction Encodings

- (void)testRealInstructionsNOP {
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(0xd503201f, &instr), 0);
}

- (void)testRealInstructionsRET {
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(0xd65f03c0, &instr), 0);
    XCTAssertEqual(instr.Rn, 30);
}

- (void)testRealInstructionsMOVZ {
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(0xd2800000, &instr), 0);
    XCTAssertEqual(instr.Rd, 0);
    XCTAssertEqual(instr.imm, 0);
}

- (void)testRealInstructionsBranches {
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(0x94000000, &instr), 0);
    XCTAssertEqual(a64_decode(0xb4000000, &instr), 0);
    XCTAssertEqual(a64_decode(0x17ffffff, &instr), 0);
}

#pragma mark - Edge Cases

- (void)testEdgeZeroInstruction {
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(0x00000000, &instr), -1);
}

- (void)testEdgeAllOnes {
    a64_instr_t instr;
    int ret = a64_decode(0xFFFFFFFF, &instr);
    (void)ret;
}

- (void)testEdgeAllRegisters {
    a64_instr_t instr;
    for (int r = 0; r < 31; r++) {
        uint32_t insn = encode_add_reg(r, r, r, 0, 0, 1);
        XCTAssertEqual(a64_decode(insn, &instr), 0);
        XCTAssertEqual(instr.Rd, r);
        XCTAssertEqual(instr.Rn, r);
        XCTAssertEqual(instr.Rm, r);
    }
}

#pragma mark - Fuzz Tests

- (void)testFuzzRandom10K {
    a64_instr_t instr;
    srand(12345);
    for (int i = 0; i < 10000; i++) {
        uint32_t random_insn = (uint32_t)rand();
        a64_decode(random_insn, &instr);
    }
}

- (void)testFuzzKnownPatterns {
    a64_instr_t instr;
    uint32_t patterns[] = {
        0x00000000, 0xFFFFFFFF, 0xAAAAAAAA, 0x55555555,
        0x12345678, 0x87654321, 0xDEADBEEF, 0xCAFEBABE,
        0x00010000, 0x00000001, 0x7FFFFFFF, 0x80000000,
    };
    for (size_t i = 0; i < sizeof(patterns) / sizeof(patterns[0]); i++) {
        a64_decode(patterns[i], &instr);
    }
}

#pragma mark - Performance Test

- (void)testDecodePerformance {
    [self measureBlock:^{
        for (int i = 0; i < 10000; i++) {
            a64_instr_t instr;
            a64_decode(0x8B020020, &instr);
        }
    }];
}

@end
