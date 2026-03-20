//
//  GeneratorTests.m
//  UnitTests
//
//  Tests for aarch64 decoder functionality
//

#import <XCTest/XCTest.h>
#include "emu/aarch64/decode.h"

@interface GeneratorTests : XCTestCase
@end

@implementation GeneratorTests

- (void)testDecodeSingleInstruction {
    uint32_t insn = 0xD2800000; // MOVZ X0, #0
    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);
    XCTAssertEqual(ret, 0);
    XCTAssertEqual(instr.Rd, 0);
    XCTAssertEqual(instr.imm, 0);
}

- (void)testDecodeMultipleInstructions {
    uint32_t insns[] = {
        0xD2800000, // MOVZ X0, #0
        0xD2800001, // MOVZ X1, #0
        0x8B020020, // ADD X0, X1, X2
    };
    
    for (int i = 0; i < 3; i++) {
        a64_instr_t instr;
        int ret = a64_decode(insns[i], &instr);
        XCTAssertEqual(ret, 0, "Instruction %d should decode", i);
    }
}

- (void)testDecodeBranchInstruction {
    uint32_t insn = 0x14000001; // B +4
    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);
    XCTAssertEqual(ret, 0);
    XCTAssertEqual(instr.cat, A64_BRANCH);
}

- (void)testDecodeBlInstruction {
    uint32_t insn = 0x94000000; // BL
    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);
    XCTAssertEqual(ret, 0);
}

- (void)testDecodeConditionalBranch {
    uint32_t insn = 0x54000000; // B.EQ
    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);
    XCTAssertEqual(ret, 0);
    XCTAssertEqual(instr.cond, A64_EQ);
}

- (void)testDecodeAddRegister {
    uint32_t insn = 0x8B020020; // ADD X0, X1, X2
    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);
    XCTAssertEqual(ret, 0);
    XCTAssertEqual(instr.Rd, 0);
    XCTAssertEqual(instr.Rn, 1);
    XCTAssertEqual(instr.Rm, 2);
}

- (void)testDecodeSubRegister {
    uint32_t insn = 0xCB020020; // SUB X0, X1, X2
    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);
    XCTAssertEqual(ret, 0);
}

- (void)testDecodeLogicalInstructions {
    uint32_t insns[] = {
        0x0A020020, // AND W0, W1, W2
        0x2A020020, // ORR W0, W1, W2
        0x4A020020, // EOR W0, W1, W2
    };
    
    for (int i = 0; i < 3; i++) {
        a64_instr_t instr;
        int ret = a64_decode(insns[i], &instr);
        XCTAssertEqual(ret, 0, "Logical op %d should decode", i);
    }
}

- (void)testDecodeLdr {
    uint32_t insn = 0xF9400020; // LDR X0, [X1]
    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);
    XCTAssertEqual(ret, 0);
}

- (void)testDecodeStr {
    uint32_t insn = 0xF9000020; // STR X0, [X1]
    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);
    XCTAssertEqual(ret, 0);
}

- (void)testDecodeSvc {
    uint32_t insn = 0xD4000001; // SVC #0
    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);
    XCTAssertEqual(ret, 0);
}

- (void)testDecodeNop {
    uint32_t insn = 0xD503201F; // NOP
    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);
    XCTAssertEqual(ret, 0);
}

- (void)testDecodeMrs {
    uint32_t insn = 0xD5300000; // MRS X0, NZCV
    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);
    XCTAssertEqual(ret, 0);
}

- (void)testDecodeMsr {
    uint32_t insn = 0xD500401F; // MSR (reg) pattern - not implemented
    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);
    XCTAssertEqual(ret, -1, @"MSR (reg) not implemented");
}

- (void)testDecodeRoundTripPreservesRaw {
    uint32_t insn = 0x8B020020; // ADD X0, X1, X2
    a64_instr_t instr;
    
    int ret = a64_decode(insn, &instr);
    XCTAssertEqual(ret, 0);
    XCTAssertEqual(instr.raw, insn);
}

- (void)testDecodeDeterminism {
    uint32_t insn = 0x8B020020;
    a64_instr_t first, second;
    
    XCTAssertEqual(a64_decode(insn, &first), 0);
    XCTAssertEqual(a64_decode(insn, &second), 0);
    
    XCTAssertEqual(memcmp(&first, &second, sizeof(first)), 0);
}

- (void)testDecodedInstructionHasValidRegisters {
    uint32_t insns[] = {
        0x8B020020, // ADD X0, X1, X2
        0xCB030041, // SUB X1, X2, X3
        0x0A044062, // AND X2, X3, X4
    };
    
    for (int i = 0; i < 3; i++) {
        a64_instr_t instr;
        XCTAssertEqual(a64_decode(insns[i], &instr), 0);
        
        XCTAssertGreaterThanOrEqual(instr.Rd, 0);
        XCTAssertLessThan(instr.Rd, 32);
        XCTAssertGreaterThanOrEqual(instr.Rn, 0);
        XCTAssertLessThan(instr.Rn, 32);
        XCTAssertGreaterThanOrEqual(instr.Rm, 0);
        XCTAssertLessThan(instr.Rm, 32);
    }
}

@end
