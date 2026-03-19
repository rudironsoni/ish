//
//  Aarch64DecoderTests.m
//  Tests
//
//  Unit tests for aarch64 instruction decoder
//

#import <XCTest/XCTest.h>

// Include the decoder header
extern "C" {
    #include "emu/aarch64/decode.h"
}

@interface Aarch64DecoderTests : XCTestCase
@end

@implementation Aarch64DecoderTests

// Test: ADD register instruction decoding
- (void)testADDRegisterDecoding {
    // ADD X0, X1, X2
    // sf=1, op0=0, shift=0, Rm=2, imm6=0, Rn=1, Rd=0
    uint32_t insn = 0x8B020020;

    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);

    XCTAssertEqual(ret, 0, "Should decode successfully");
    XCTAssertEqual(instr.Rd, 0, "Rd should be 0");
    XCTAssertEqual(instr.Rn, 1, "Rn should be 1");
    XCTAssertEqual(instr.Rm, 2, "Rm should be 2");
    XCTAssertEqual(instr.is_64bit, 1, "Should be 64-bit");
}

// Test: SUB register instruction
- (void)testSUBRegisterDecoding {
    // SUB X0, X1, X2
    uint32_t insn = 0xCB020020;

    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);

    XCTAssertEqual(ret, 0, "Should decode successfully");
    XCTAssertEqual(instr.Rd, 0, "Rd should be 0");
    XCTAssertEqual(instr.Rn, 1, "Rn should be 1");
    XCTAssertEqual(instr.Rm, 2, "Rm should be 2");
}

// Test: MOVZ immediate
- (void)testMOVZDecoding {
    // MOVZ X0, #0x1234, LSL #0
    // sf=1, opc=10, hw=0, imm16=0x1234, Rd=0
    uint32_t insn = 0xD2802468;  // Corrected encoding

    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);

    XCTAssertEqual(ret, 0, "Should decode successfully");
    XCTAssertEqual(instr.Rd, 0, "Rd should be 0");
    XCTAssertEqual(instr.imm, 0x1234, "Immediate should be 0x1234");
    XCTAssertEqual(instr.is_64bit, 1, "Should be 64-bit");
}

// Test: Branch unconditional
- (void)testBDecoding {
    // B #offset
    // op=0, imm26=offset>>2
    uint32_t insn = 0x14000000;  // B .+0

    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);

    XCTAssertEqual(ret, 0, "Should decode successfully");
}

// Test: Branch with link
- (void)testBLDecoding {
    // BL #offset
    uint32_t insn = 0x94000000;  // BL .+0

    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);

    XCTAssertEqual(ret, 0, "Should decode successfully");
}

// Test: Return instruction
- (void)testRETDecoding {
    // RET
    uint32_t insn = 0xD65F03C0;

    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);

    XCTAssertEqual(ret, 0, "Should decode successfully");
    XCTAssertEqual(instr.Rn, 30, "Should return to X30 (LR)");
}

// Test: NOP (HINT)
- (void)testNOPDecoding {
    // NOP = HINT #0
    uint32_t insn = 0xD503201F;

    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);

    XCTAssertEqual(ret, 0, "Should decode successfully");
}

// Test: Compare and branch (CBZ)
- (void)testCBZDecoding {
    // CBZ X0, #offset
    uint32_t insn = 0xB4000000;

    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);

    XCTAssertEqual(ret, 0, "Should decode successfully");
    XCTAssertEqual(instr.Rd, 0, "Rd should be 0");
    XCTAssertEqual(instr.is_64bit, 1, "Should be 64-bit");
}

// Test: Load register
- (void)testLDRDecoding {
    // LDR X0, [X1, #0]
    uint32_t insn = 0xF9400020;

    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);

    XCTAssertEqual(ret, 0, "Should decode successfully");
    XCTAssertEqual(instr.Rd, 0, "Rd should be 0");
    XCTAssertEqual(instr.Rn, 1, "Rn should be 1");
}

// Test: Store register
- (void)testSTRDecoding {
    // STR X0, [X1, #0]
    uint32_t insn = 0xF9000020;

    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);

    XCTAssertEqual(ret, 0, "Should decode successfully");
}

// Test: Supervisor call
- (void)testSVCDecoding {
    // SVC #0
    uint32_t insn = 0xD4000001;

    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);

    XCTAssertEqual(ret, 0, "Should decode successfully");
}

// Test: Invalid instruction
- (void)testInvalidInstruction {
    uint32_t insn = 0x00000000;  // UNDEFINED

    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);

    // Should return error for undefined
    XCTAssertEqual(ret, -1, "Should fail for undefined instruction");
}

// Test: All 31 registers
- (void)testAllRegisters {
    for (int i = 0; i < 31; i++) {
        // ADD X<i>, X0, X1
        uint32_t insn = 0x8B010000 | i;

        a64_instr_t instr;
        int ret = a64_decode(insn, &instr);

        XCTAssertEqual(ret, 0, "Should decode register %d", i);
        XCTAssertEqual(instr.Rd, i, "Rd should be %d", i);
    }
}

// Test: Category detection
- (void)testCategoryDetection {
    struct {
        uint32_t insn;
        a64_category_t expected_cat;
        const char* desc;
    } tests[] = {
        {0x8B000000, A64_DP_REG2, "ADD register"},
        {0x91000000, A64_DP_IMM, "ADD immediate"},
        {0xD2800000, A64_DP_IMM, "MOVZ"},
        {0x14000000, A64_BRANCH, "B"},
        {0x94000000, A64_BRANCH, "BL"},
        {0xF9400000, A64_LD_ST, "LDR"},
    };

    for (size_t i = 0; i < sizeof(tests)/sizeof(tests[0]); i++) {
        a64_instr_t instr;
        int ret = a64_decode(tests[i].insn, &instr);

        XCTAssertEqual(ret, 0, "%s should decode", tests[i].desc);
        XCTAssertEqual(instr.cat, tests[i].expected_cat,
                       "%s should have correct category", tests[i].desc);
    }
}

// Performance test: Decode 10000 instructions
- (void)testDecodePerformance {
    [self measureBlock:^{
        for (int i = 0; i < 10000; i++) {
            a64_instr_t instr;
            a64_decode(0x8B020020, &instr);  // ADD
        }
    }];
}

@end
