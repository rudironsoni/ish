//
//  IntegrationTests.m
//  UnitTests
//
//  Integration tests for decode functionality
//

#import <XCTest/XCTest.h>
#include "emu/aarch64/decode.h"

@interface IntegrationTests : XCTestCase
@end

@implementation IntegrationTests

#pragma mark - Basic Decode Flow

- (void)testDecodeAddRegister {
    uint32_t insn = 0x8B020020; // ADD X0, X1, X2
    a64_instr_t instr;
    
    int ret = a64_decode(insn, &instr);
    XCTAssertEqual(ret, 0);
    XCTAssertEqual(instr.Rd, 0);
    XCTAssertEqual(instr.Rn, 1);
    XCTAssertEqual(instr.Rm, 2);
}

- (void)testDecodeMovz {
    uint32_t insn = 0xD2800000; // MOVZ X0, #0
    a64_instr_t instr;
    
    int ret = a64_decode(insn, &instr);
    XCTAssertEqual(ret, 0);
    XCTAssertEqual(instr.Rd, 0);
}

- (void)testDecodeBranch {
    uint32_t insn = 0x14000001; // B +4
    a64_instr_t instr;
    
    int ret = a64_decode(insn, &instr);
    XCTAssertEqual(ret, 0);
    XCTAssertEqual(instr.cat, A64_BRANCH);
}

- (void)testDecodeSvc {
    uint32_t insn = 0xD4000001; // SVC #0
    a64_instr_t instr;
    
    int ret = a64_decode(insn, &instr);
    XCTAssertEqual(ret, 0);
}

#pragma mark - Multiple Instruction Sequence

- (void)testMultipleInstructionSequence {
    uint32_t insns[] = {
        0xD2800000, // MOVZ X0, #0
        0xD2800001, // MOVZ X1, #0
        0x8B020020, // ADD X0, X1, X2
    };
    
    int decoded = 0;
    for (int i = 0; i < 3; i++) {
        a64_instr_t instr;
        int ret = a64_decode(insns[i], &instr);
        XCTAssertEqual(ret, 0, "Instruction %d should decode", i);
        if (ret == 0) decoded++;
    }
    
    XCTAssertEqual(decoded, 3);
}

- (void)testSequenceWithBranchTermination {
    uint32_t insns[] = {
        0xD2800000, // MOVZ X0, #0
        0x14000000, // B (terminates)
    };
    
    for (int i = 0; i < 2; i++) {
        a64_instr_t instr;
        int ret = a64_decode(insns[i], &instr);
        XCTAssertEqual(ret, 0, "Instruction %d should decode", i);
    }
}

#pragma mark - Round Trip Consistency

- (void)testDecodePreservesRawInstruction {
    uint32_t insn = 0x8B020060;
    a64_instr_t instr;
    
    int ret = a64_decode(insn, &instr);
    XCTAssertEqual(ret, 0);
    XCTAssertEqual(instr.raw, insn);
}

- (void)testDecodeDeterminism {
    uint32_t insn = 0xD2800000;
    a64_instr_t first, second;
    
    XCTAssertEqual(a64_decode(insn, &first), 0);
    XCTAssertEqual(a64_decode(insn, &second), 0);
    
    XCTAssertEqual(memcmp(&first, &second, sizeof(first)), 0);
}

#pragma mark - Error Handling

- (void)testDecodeInvalidInstruction {
    a64_instr_t instr;
    uint32_t invalidInsn = 0x00000000; // All zeros - RESERVED
    int ret = a64_decode(invalidInsn, &instr);
    XCTAssertEqual(ret, -1, @"RESERVED instruction should return error");
}

- (void)testDecodeAllCategories {
    struct {
        uint32_t insn;
        a64_category_t cat;
    } testCases[] = {
        { 0x8B020020, A64_DP_REG2 },      // ADD register
        { 0xD2800000, A64_DP_IMM },       // MOVZ immediate
        { 0x14000001, A64_BRANCH },       // B branch
        { 0xF9400020, A64_LD_ST },        // LDR load
    };
    
    for (int i = 0; i < 4; i++) {
        a64_instr_t instr;
        int ret = a64_decode(testCases[i].insn, &instr);
        XCTAssertEqual(ret, 0, "Test case %d should decode", i);
        XCTAssertEqual(instr.cat, testCases[i].cat, "Test case %d category", i);
    }
}

@end
