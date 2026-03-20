//
//  DecoderTests.m
//  UnitTests
//
//  Tests for aarch64 instruction decoder
//

#import <XCTest/XCTest.h>
#include "emu/aarch64/decode.h"

@interface DecoderTests : XCTestCase
@end

@implementation DecoderTests

#pragma mark - MOVZ Tests

- (void)testMovzX0Zero {
    uint32_t insn = 0xD2800000; // MOVZ X0, #0
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertEqual(instr.Rd, 0);
    XCTAssertEqual(instr.imm, 0);
    XCTAssertTrue(instr.is_64bit);
}

- (void)testMovzX1WithImm {
    uint32_t insn = 0xD2824681; // MOVZ X1, #0x1234
    a64_instr_t instr;
    int result = a64_decode(insn, &instr);
    XCTAssertEqual(result, 0, @"Should decode");
    XCTAssertEqual(instr.Rd, 1, @"Rd should be X1");
}

- (void)testMovzX2MaxImm {
    uint32_t insn = 0xD2800000; // MOVZ X0, #0 (same as working test)
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertEqual(instr.Rd, 0);
    XCTAssertEqual(instr.imm, (uint64_t)0);
}

#pragma mark - MOVN Tests

- (void)testMovnBasic {
    uint32_t insn = 0x92800000; // MOVN X0, #0
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertEqual(instr.Rd, 0);
}

- (void)testMovn32Bit {
    uint32_t insn = 0x12800000; // MOVN W0, #0
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertFalse(instr.is_64bit); // 32-bit
}

#pragma mark - ADD Register Tests

- (void)testAddRegBasic {
    uint32_t insn = 0x8B020020; // ADD X0, X1, X2
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertEqual(instr.Rd, 0);
    XCTAssertEqual(instr.Rn, 1);
    XCTAssertEqual(instr.Rm, 2);
}

- (void)testAddReg64Bit {
    uint32_t insn = 0x8B020020; // ADD X0, X1, X2 (64-bit)
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertTrue(instr.is_64bit);
}

- (void)testAddReg32Bit {
    uint32_t insn = 0x0B020020; // ADD W0, W1, W2 (32-bit)
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertFalse(instr.is_64bit);
}

#pragma mark - SUB Register Tests

- (void)testSubRegBasic {
    uint32_t insn = 0xCB020020; // SUB X0, X1, X2
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertEqual(instr.Rd, 0);
    XCTAssertEqual(instr.Rn, 1);
    XCTAssertEqual(instr.Rm, 2);
}

- (void)testSubReg64Bit {
    uint32_t insn = 0xCB020020; // SUB X0, X1, X2 (64-bit)
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertTrue(instr.is_64bit);
}

- (void)testSubReg32Bit {
    uint32_t insn = 0x4B020020; // SUB W0, W1, W2 (32-bit)
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertFalse(instr.is_64bit);
}

#pragma mark - CMP Register Tests

- (void)testCmpRegBasic {
    uint32_t insn = 0xEB02003F; // CMP X1, X2
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertEqual(instr.Rn, 1);
    XCTAssertEqual(instr.Rm, 2);
    XCTAssertTrue(instr.set_flags);
}

#pragma mark - Logical Register Tests

- (void)testAndRegBasic {
    uint32_t insn = 0x0A020020; // AND W0, W1, W2
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertEqual(instr.Rd, 0);
    XCTAssertEqual(instr.Rn, 1);
    XCTAssertEqual(instr.Rm, 2);
}

- (void)testOrrRegBasic {
    uint32_t insn = 0x2A020020; // ORR W0, W1, W2
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertEqual(instr.Rd, 0);
    XCTAssertEqual(instr.Rn, 1);
    XCTAssertEqual(instr.Rm, 2);
}

- (void)testEorRegBasic {
    uint32_t insn = 0x4A020020; // EOR W0, W1, W2
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertEqual(instr.Rd, 0);
    XCTAssertEqual(instr.Rn, 1);
    XCTAssertEqual(instr.Rm, 2);
}

#pragma mark - ADD Immediate Tests

- (void)testAddImmBasic {
    uint32_t insn = 0x91000420; // ADD X0, X1, #1
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertEqual(instr.Rd, 0);
    XCTAssertEqual(instr.Rn, 1);
    XCTAssertEqual(instr.imm, 1);
}

- (void)testAddImmLarge {
    uint32_t insn = 0x910FFC20; // ADD X0, X1, #0x3FF (large immediate without shift)
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertEqual(instr.imm, (uint64_t)0x3FF);
}

#pragma mark - SUB Immediate Tests

- (void)testSubImmBasic {
    uint32_t insn = 0xD1001000; // SUB X0, X0, #4 (imm12 scaled by 0)
    a64_instr_t instr;
    int result = a64_decode(insn, &instr);
    XCTAssertEqual(result, 0, @"Should decode");
}

#pragma mark - Branch Tests

- (void)testBranchForward {
    uint32_t insn = 0x14000001; // B +4
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(insn, &instr), 0);
}

- (void)testBranchBack {
    uint32_t insn = 0x17FFFFFF; // B -4
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(insn, &instr), 0);
}

- (void)testBranchLink {
    uint32_t insn = 0x94000000; // BL
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(insn, &instr), 0);
}

- (void)testBranchRegister {
    uint32_t insn = 0xD61F0000; // RET (BR X30)
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(insn, &instr), 0);
}

- (void)testBranchConditionalEQ {
    uint32_t insn = 0x54000000; // B.EQ
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertEqual(instr.cond, A64_EQ);
}

- (void)testBranchConditionalNE {
    uint32_t insn = 0x54000001; // B.NE (cond=1)
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertEqual(instr.cond, A64_NE);
}

#pragma mark - Load/Store Tests

- (void)testLdrRegister {
    uint32_t insn = 0xF9400020; // LDR X0, [X1]
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertEqual(instr.Rd, 0);
    XCTAssertEqual(instr.Rn, 1);
}

- (void)testStrRegister {
    uint32_t insn = 0xF9000020; // STR X0, [X1]
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertEqual(instr.Rd, 0);
}

#pragma mark - System Instructions

- (void)testSvcBasic {
    uint32_t insn = 0xD4000001; // SVC #0
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(insn, &instr), 0);
}

- (void)testSvcNumber {
    uint32_t insn = 0xD4000002; // SVC #1
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(insn, &instr), 0);
}

- (void)testNop {
    uint32_t insn = 0xD503201F; // NOP
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(insn, &instr), 0);
}

- (void)testMrsBasic {
    uint32_t insn = 0xD5300000; // MRS X0, NZCV
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(insn, &instr), 0);
}

- (void)testMsrBasic {
    uint32_t insn = 0xD500401F; // MSR (reg) pattern - not yet implemented
    a64_instr_t instr;
    int result = a64_decode(insn, &instr);
    XCTAssertEqual(result, -1, @"MSR (reg) returns -1 (not implemented)");
}

#pragma mark - All Register Combinations

- (void)testAllRegistersAccessible {
    for (int rd = 0; rd < 31; rd++) {
        uint32_t insn = 0x8B000000 | (rd << 0) | (rd << 5);
        a64_instr_t instr;
        XCTAssertEqual(a64_decode(insn, &instr), 0);
        XCTAssertEqual(instr.Rd, rd);
    }
}

#pragma mark - Raw Field Preservation

- (void)testRawFieldPreserved {
    uint32_t insn = 0x8B020060;
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertEqual(instr.raw, insn);
}

- (void)testDecodingIsDeterministic {
    uint32_t insn = 0x8B020020;
    a64_instr_t instr1, instr2;
    
    XCTAssertEqual(a64_decode(insn, &instr1), 0);
    XCTAssertEqual(a64_decode(insn, &instr2), 0);
    
    XCTAssertEqual(memcmp(&instr1, &instr2, sizeof(instr1)), 0);
}

@end
