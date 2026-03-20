//
//  DecoderFunctionalTests.m
//  UnitTests
//
//  Functional tests for aarch64 instruction decoder
//

#import <XCTest/XCTest.h>
#include "emu/aarch64/decode.h"

@interface DecoderFunctionalTests : XCTestCase
@end

@implementation DecoderFunctionalTests

#pragma mark - Branch Functional Tests

- (void)testBranchConditionsDecoding {
    uint32_t condCodes[] = {
        0x54000000, // B.EQ (cond=0)
        0x54000001, // B.NE (cond=1)
        0x54000002, // B.CS/HS (cond=2)
        0x54000003, // B.CC/LO (cond=3)
        0x54000004, // B.MI (cond=4)
        0x54000005, // B.PL (cond=5)
        0x54000006, // B.VS (cond=6)
        0x54000007, // B.VC (cond=7)
        0x54000008, // B.HI (cond=8)
        0x54000009, // B.LS (cond=9)
        0x5400000A, // B.GE (cond=10)
        0x5400000B, // B.LT (cond=11)
        0x5400000C, // B.GT (cond=12)
        0x5400000D, // B.LE (cond=13)
    };
    
    for (int i = 0; i < 14; i++) {
        a64_instr_t instr;
        XCTAssertEqual(a64_decode(condCodes[i], &instr), 0, "Branch condition %d should decode", i);
    }
}

- (void)testCompareBranchZero {
    for (int rt = 0; rt < 31; rt++) {
        uint32_t insn = 0xB4000000 | (rt & 0x1F);
        a64_instr_t instr;
        XCTAssertEqual(a64_decode(insn, &instr), 0, "CBZ X%d should decode", rt);
        XCTAssertEqual(instr.Rd, rt);
    }
}

- (void)testCompareBranchNonZero {
    for (int rt = 0; rt < 31; rt++) {
        uint32_t insn = 0xB8000000 | (rt & 0x1F);
        a64_instr_t instr;
        XCTAssertEqual(a64_decode(insn, &instr), 0, "CBNZ X%d should decode", rt);
    }
}

- (void)testTestBranchZero {
    for (int rt = 0; rt < 31; rt++) {
        uint32_t insn = 0x36000000 | (rt & 0x1F);
        a64_instr_t instr;
        XCTAssertEqual(a64_decode(insn, &instr), 0, "TBZ X%d should decode", rt);
    }
}

#pragma mark - Logical Operations Functional Tests

- (void)testAndRegister {
    uint32_t insn = 0x0A020020; // AND W0, W1, W2
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertEqual(instr.Rd, 0);
    XCTAssertEqual(instr.Rn, 1);
    XCTAssertEqual(instr.Rm, 2);
}

- (void)testAndsRegister {
    uint32_t insn = 0xEA020020; // ANDS X0, X1, X2 (bit 29=1 sets flags)
    a64_instr_t instr;
    int result = a64_decode(insn, &instr);
    XCTAssertEqual(result, 0, @"ANDS should decode");
}

- (void)testOrrRegister {
    uint32_t insn = 0x2A020020; // ORR W0, W1, W2
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertEqual(instr.Rd, 0);
    XCTAssertEqual(instr.Rn, 1);
    XCTAssertEqual(instr.Rm, 2);
}

- (void)testEorRegister {
    uint32_t insn = 0x4A020020; // EOR W0, W1, W2
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertEqual(instr.Rd, 0);
    XCTAssertEqual(instr.Rn, 1);
    XCTAssertEqual(instr.Rm, 2);
}

#pragma mark - Arithmetic Functional Tests

- (void)testAddsRegister {
    uint32_t insn = 0xAB020020; // ADDS X0, X1, X2
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertTrue(instr.set_flags);
}

- (void)testSubsRegister {
    uint32_t insn = 0xCB020020; // SUBS X0, X1, X2
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(insn, &instr), 0);
}

- (void)testAddsImmediate {
    uint32_t insn = 0xB10003FF; // ADDS X0, X0, #0xFFF
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(insn, &instr), 0);
}

- (void)testSubsImmediate {
    uint32_t insn = 0xD10003FF; // SUBS X0, X0, #0xFFF
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(insn, &instr), 0);
}

#pragma mark - Move Functional Tests

- (void)testMovzAllShifts {
    for (int hw = 0; hw < 4; hw++) {
        uint32_t insn = 0xD2800000 | (hw << 21);
        a64_instr_t instr;
        XCTAssertEqual(a64_decode(insn, &instr), 0, "MOVZ with hw=%d should decode", hw);
    }
}

- (void)testMovnAllShifts {
    for (int hw = 0; hw < 4; hw++) {
        uint32_t insn = 0x92800000 | (hw << 21);
        a64_instr_t instr;
        XCTAssertEqual(a64_decode(insn, &instr), 0, "MOVN with hw=%d should decode", hw);
    }
}

- (void)testMovkAllShifts {
    for (int hw = 0; hw < 4; hw++) {
        uint32_t insn = 0x72800000 | (hw << 21);
        a64_instr_t instr;
        XCTAssertEqual(a64_decode(insn, &instr), 0, "MOVK with hw=%d should decode", hw);
    }
}

- (void)testMovnvsMovz {
    uint32_t movn = 0x92800000; // MOVN X0, #0
    uint32_t movz = 0xD2800000; // MOVZ X0, #0
    a64_instr_t instr_n, instr_z;
    
    XCTAssertEqual(a64_decode(movn, &instr_n), 0);
    XCTAssertEqual(a64_decode(movz, &instr_z), 0);
    
    XCTAssertNotEqual(instr_n.raw, instr_z.raw);
}

#pragma mark - Load/Store Functional Tests

- (void)testLdrUnscaled {
    uint32_t insn = 0xF8400020; // LDR X0, [X1, #0]
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(insn, &instr), 0);
}

- (void)testStrUnscaled {
    uint32_t insn = 0xF8000020; // STR X0, [X1, #0]
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(insn, &instr), 0);
}

- (void)testLdrImmediatePreIndex {
    uint32_t insn = 0xF8404020; // LDR X0, [X1, #8]!
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(insn, &instr), 0);
}

- (void)testLdrImmediatePostIndex {
    uint32_t insn = 0xF8400020; // LDR X0, [X1], #8
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(insn, &instr), 0);
}

- (void)testLdrbImmediate {
    uint32_t insn = 0x39400020; // LDRB W0, [X1, #0]
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(insn, &instr), 0);
}

- (void)testStrbImmediate {
    uint32_t insn = 0x39000020; // STRB W0, [X1, #0]
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(insn, &instr), 0);
}

- (void)testLdrhImmediate {
    uint32_t insn = 0x79400020; // LDRH W0, [X1, #0]
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(insn, &instr), 0);
}

- (void)testStrhImmediate {
    uint32_t insn = 0x79000020; // STRH W0, [X1, #0]
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(insn, &instr), 0);
}

#pragma mark - System Instructions Functional Tests

- (void)testSvcSyscall {
    uint32_t insn = 0xD4000001; // SVC #0
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(insn, &instr), 0);
}

- (void)testHint {
    uint32_t insn = 0xD503201F; // NOP/HINT
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(insn, &instr), 0);
}

#pragma mark - Determinism Tests

- (void)testDecodeDeterministic {
    uint32_t testInsns[] = {
        0x8B020020,
        0xD2800000,
        0x14000000,
        0xD4000001,
        0xF9400020,
    };
    
    for (int i = 0; i < 5; i++) {
        a64_instr_t first, second;
        uint32_t insn = testInsns[i];
        
        XCTAssertEqual(a64_decode(insn, &first), 0);
        XCTAssertEqual(a64_decode(insn, &second), 0);
        
        XCTAssertEqual(memcmp(&first, &second, sizeof(first)), 0);
    }
}

- (void)testDecodeConsistentRawField {
    uint32_t insn = 0x8B020060;
    a64_instr_t instr;
    
    XCTAssertEqual(a64_decode(insn, &instr), 0);
    XCTAssertEqual(instr.raw, insn);
}

@end
