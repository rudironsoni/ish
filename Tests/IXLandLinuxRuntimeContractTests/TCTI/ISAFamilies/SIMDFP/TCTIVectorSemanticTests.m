#import <XCTest/XCTest.h>

#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>

#include "../../Support/ISAFamilies/BaseScalar/tcti_scalar_runtime_semantic_scenarios.h"
#include "../../Support/ISAFamilies/BaseScalar/tcti_control_and_flags_semantic_scenarios.h"

@interface TCTIVectorSemanticTests : XCTestCase
@end

@implementation TCTIVectorSemanticTests

- (void)assertVectorMaskInstruction:(uint32_t)insn
                               textPC:(uint64_t)textPC
                              destReg:(int)destReg
                               lhsReg:(int)lhsReg
                               rhsReg:(int)rhsReg
                                  lhs:(const uint8_t[16])lhs
                                  rhs:(const uint8_t[16])rhs
                             expected:(const uint8_t[16])expected
                             mnemonic:(const char *)mnemonic
{
    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    memcpy(cpu.vregs[lhsReg].b, lhs, 16);
    memcpy(cpu.vregs[rhsReg].b, rhs, 16);

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                   @"%s must execute through TCTI", mnemonic);

    for (int i = 0; i < 16; i++) {
        XCTAssertEqual(cpu.vregs[destReg].b[i], expected[i],
                       @"%s must publish the architectural byte-lane mask", mnemonic);
    }
}

- (void)assertVectorMaskAgainstZeroInstruction:(uint32_t)insn
                                          textPC:(uint64_t)textPC
                                         destReg:(int)destReg
                                          srcReg:(int)srcReg
                                             src:(const uint8_t[16])src
                                        expected:(const uint8_t[16])expected
                                        mnemonic:(const char *)mnemonic
{
    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    memcpy(cpu.vregs[srcReg].b, src, 16);

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                   @"%s must execute through TCTI", mnemonic);

    for (int i = 0; i < 16; i++) {
        XCTAssertEqual(cpu.vregs[destReg].b[i], expected[i],
                       @"%s must publish the architectural byte-lane mask", mnemonic);
    }
}

- (void)testSemanticExecutionContract_MuslSnprintfFILEWposInit
{
    XCTAssertEqual(tcti_semantic_case_musl_snprintf_file_wpos_init(), 0ULL,
                   @"TCTI must preserve musl's stack FILE wpos initialization across STP and "
                    "SIMD copy setup before fwrite_unlocked calls memcpy");
}

- (void)testSemanticExecutionContract_MuslVdprintfStackFILEZeroInit
{
    XCTAssertEqual(tcti_semantic_case_musl_vdprintf_stack_file_zero_init(), 0ULL,
                   @"TCTI must lower AdvSIMD MOVI/MVNI immediates so musl's stack FILE starts "
                    "with zeroed buffer pointers before relocation diagnostics reach the PTY");
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

- (void)testSemanticExecutionContract_EXTConcatenatesTwoVectorsAtByteOffset
{
    enum {
        textPC = 0x98000,
    };

    static const uint32_t insn = 0x6e024020; // ext v0.16b, v1.16b, v2.16b, #8

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    for (int i = 0; i < 16; i++) {
        cpu.vregs[1].b[i] = (uint8_t)(0x10 + i);
        cpu.vregs[2].b[i] = (uint8_t)(0x80 + i);
    }

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                   @"EXT must execute through TCTI and splice the byte tail of vn with the "
                    "byte head of vm; it must not alias to MOVI/MVNI.");

    static const uint8_t expected[16] = {
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
        0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    };

    for (int i = 0; i < 16; i++) {
        XCTAssertEqual(cpu.vregs[0].b[i], expected[i],
                       @"EXT must concatenate v1:v2 at byte offset 8 and publish the result in v0");
    }
}

- (void)testSemanticExecutionContract_CNTComputesPerBytePopulationCount
{
    enum {
        textPC = 0x98020,
    };

    static const uint32_t insn = 0x4e205820; // cnt v0.16b, v1.16b

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));

    static const uint8_t source[16] = {
        0x00, 0x01, 0x03, 0x07, 0x0f, 0x10, 0x1f, 0x55,
        0x80, 0x81, 0xaa, 0xf0, 0xfe, 0xff, 0x7f, 0x33,
    };
    static const uint8_t expected[16] = {
        0, 1, 2, 3, 4, 1, 5, 4,
        1, 2, 4, 4, 7, 8, 7, 4,
    };

    for (int i = 0; i < 16; i++)
        cpu.vregs[1].b[i] = source[i];

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                   @"CNT must execute through TCTI and compute the population count of each "
                    "byte in the source vector.");

    for (int i = 0; i < 16; i++) {
        XCTAssertEqual(cpu.vregs[0].b[i], expected[i],
                       @"CNT must publish the per-byte bit count into the destination vector");
    }
}

- (void)testSemanticExecutionContract_INSInsertsGPRIntoSelectedVectorLane
{
    enum {
        textPC = 0x98040,
    };

    static const uint32_t insn = 0x4e181c20; // ins v0.d[1], x1

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.vregs[0].d[0] = 0x0123456789abcdefULL;
    cpu.vregs[0].d[1] = 0xfedcba9876543210ULL;
    cpu.x[1] = 0x1122334455667788ULL;

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                   @"INS must execute through TCTI and write the selected vector lane from the "
                    "source GPR without corrupting the untouched lane.");
    XCTAssertEqual(cpu.vregs[0].d[0], 0x0123456789abcdefULL,
                   @"INS v0.d[1], x1 must preserve the low 64-bit lane");
    XCTAssertEqual(cpu.vregs[0].d[1], 0x1122334455667788ULL,
                   @"INS v0.d[1], x1 must replace only lane 1 with the GPR value");
}

- (void)testSemanticExecutionContract_TBLLooksUpBytesAndZeroesOutOfRangeIndices
{
    enum {
        textPC = 0x98060,
    };

    static const uint32_t insn = 0x0e020020; // tbl v0.8b, { v1.16b }, v2.8b

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));

    for (int i = 0; i < 16; i++)
        cpu.vregs[1].b[i] = (uint8_t)(0xa0 + i);

    static const uint8_t indices[8] = {0, 1, 7, 8, 15, 16, 3, 31};
    static const uint8_t expected[8] = {0xa0, 0xa1, 0xa7, 0xa8, 0xaf, 0x00, 0xa3, 0x00};
    for (int i = 0; i < 8; i++)
        cpu.vregs[2].b[i] = indices[i];

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                   @"TBL must execute through TCTI and publish byte lookups from the table "
                    "vector while zeroing indices outside the table width.");

    for (int i = 0; i < 8; i++) {
        XCTAssertEqual(cpu.vregs[0].b[i], expected[i],
                       @"TBL must map each byte index through v1 and zero indices past lane 15");
    }
}

- (void)testSemanticExecutionContract_TBXPreservesDestinationForOutOfRangeIndices
{
    enum {
        textPC = 0x98080,
    };

    static const uint32_t insn = 0x0e021020; // tbx v0.8b, { v1.16b }, v2.8b

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));

    static const uint8_t initial[8] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17};
    static const uint8_t indices[8] = {0, 16, 5, 31, 15, 7, 8, 20};
    static const uint8_t expected[8] = {0xa0, 0x11, 0xa5, 0x13, 0xaf, 0xa7, 0xa8, 0x17};

    for (int i = 0; i < 8; i++) {
        cpu.vregs[0].b[i] = initial[i];
        cpu.vregs[2].b[i] = indices[i];
    }
    for (int i = 0; i < 16; i++)
        cpu.vregs[1].b[i] = (uint8_t)(0xa0 + i);

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                   @"TBX must execute through TCTI and preserve destination bytes when the "
                    "index is outside the source table width.");

    for (int i = 0; i < 8; i++) {
        XCTAssertEqual(cpu.vregs[0].b[i], expected[i],
                       @"TBX must replace only in-range lookups and leave out-of-range lanes unchanged");
    }
}

- (void)testSemanticExecutionContract_XTNNarrowsHalfwordsIntoBytesAndZeroesUpperHalf
{
    enum {
        textPC = 0x980a0,
    };

    static const uint32_t insn = 0x0e212820; // xtn v0.8b, v1.8h

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));

    static const uint16_t source[8] = {
        0x0102, 0x0304, 0x0506, 0x0708, 0x090a, 0x0b0c, 0x0d0e, 0x0f10,
    };
    static const uint8_t expected[16] = {
        0x02, 0x04, 0x06, 0x08, 0x0a, 0x0c, 0x0e, 0x10,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };

    for (int i = 0; i < 8; i++)
        cpu.vregs[1].h[i] = source[i];
    memset(cpu.vregs[0].b, 0xcc, sizeof(cpu.vregs[0].b));

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                   @"XTN must execute through TCTI and narrow the low byte of each halfword "
                    "into the destination vector.");

    for (int i = 0; i < 16; i++) {
        XCTAssertEqual(cpu.vregs[0].b[i], expected[i],
                       @"XTN v0.8b, v1.8h must publish the narrowed low bytes and zero the upper half");
    }
}

- (void)testSemanticExecutionContract_ZIP1InterleavesLowHalvesOfTwoVectors
{
    enum {
        textPC = 0x980c0,
    };

    static const uint32_t insn = 0x4e023820; // zip1 v0.16b, v1.16b, v2.16b

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));

    for (int i = 0; i < 16; i++) {
        cpu.vregs[1].b[i] = (uint8_t)(0x10 + i);
        cpu.vregs[2].b[i] = (uint8_t)(0x80 + i);
    }

    static const uint8_t expected[16] = {
        0x10, 0x80, 0x11, 0x81, 0x12, 0x82, 0x13, 0x83,
        0x14, 0x84, 0x15, 0x85, 0x16, 0x86, 0x17, 0x87,
    };

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                   @"ZIP1 must execute through TCTI and interleave the low halves of the two "
                    "source vectors into the destination.");

    for (int i = 0; i < 16; i++) {
        XCTAssertEqual(cpu.vregs[0].b[i], expected[i],
                       @"ZIP1 v0.16b, v1.16b, v2.16b must alternate bytes from the low halves");
    }
}

- (void)testSemanticExecutionContract_TRN1InterleavesEvenLanesFromTwoVectors
{
    enum {
        textPC = 0x980e0,
    };

    static const uint32_t insn = 0x4e022820; // trn1 v0.16b, v1.16b, v2.16b

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));

    for (int i = 0; i < 16; i++) {
        cpu.vregs[1].b[i] = (uint8_t)(0x10 + i);
        cpu.vregs[2].b[i] = (uint8_t)(0x80 + i);
    }

    static const uint8_t expected[16] = {
        0x10, 0x80, 0x12, 0x82, 0x14, 0x84, 0x16, 0x86,
        0x18, 0x88, 0x1a, 0x8a, 0x1c, 0x8c, 0x1e, 0x8e,
    };

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                   @"TRN1 must execute through TCTI and interleave the even-indexed lanes of "
                    "the two source vectors into the destination.");

    for (int i = 0; i < 16; i++) {
        XCTAssertEqual(cpu.vregs[0].b[i], expected[i],
                       @"TRN1 v0.16b, v1.16b, v2.16b must alternate even lanes from both vectors");
    }
}

- (void)testSemanticExecutionContract_UZP1PacksEvenLanesFromBothVectors
{
    enum {
        textPC = 0x98100,
    };

    static const uint32_t insn = 0x4e021820; // uzp1 v0.16b, v1.16b, v2.16b

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));

    for (int i = 0; i < 16; i++) {
        cpu.vregs[1].b[i] = (uint8_t)(0x10 + i);
        cpu.vregs[2].b[i] = (uint8_t)(0x80 + i);
    }

    static const uint8_t expected[16] = {
        0x10, 0x12, 0x14, 0x16, 0x18, 0x1a, 0x1c, 0x1e,
        0x80, 0x82, 0x84, 0x86, 0x88, 0x8a, 0x8c, 0x8e,
    };

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                   @"UZP1 must execute through TCTI and pack the even-indexed lanes of both "
                    "source vectors into the destination.");

    for (int i = 0; i < 16; i++) {
        XCTAssertEqual(cpu.vregs[0].b[i], expected[i],
                       @"UZP1 v0.16b, v1.16b, v2.16b must pack even lanes from v1 then v2");
    }
}

- (void)testSemanticExecutionContract_ZIP2InterleavesHighHalvesOfTwoVectors
{
    enum { textPC = 0x98120 };
    static const uint32_t insn = 0x4e027820; // zip2 v0.16b, v1.16b, v2.16b
    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));

    for (int i = 0; i < 16; i++) {
        cpu.vregs[1].b[i] = (uint8_t)(0x10 + i);
        cpu.vregs[2].b[i] = (uint8_t)(0x80 + i);
    }

    static const uint8_t expected[16] = {
        0x18, 0x88, 0x19, 0x89, 0x1a, 0x8a, 0x1b, 0x8b,
        0x1c, 0x8c, 0x1d, 0x8d, 0x1e, 0x8e, 0x1f, 0x8f,
    };

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0);
    for (int i = 0; i < 16; i++)
        XCTAssertEqual(cpu.vregs[0].b[i], expected[i]);
}

- (void)testSemanticExecutionContract_TRN2InterleavesOddLanesFromTwoVectors
{
    enum { textPC = 0x98140 };
    static const uint32_t insn = 0x4e026820; // trn2 v0.16b, v1.16b, v2.16b
    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));

    for (int i = 0; i < 16; i++) {
        cpu.vregs[1].b[i] = (uint8_t)(0x10 + i);
        cpu.vregs[2].b[i] = (uint8_t)(0x80 + i);
    }

    static const uint8_t expected[16] = {
        0x11, 0x81, 0x13, 0x83, 0x15, 0x85, 0x17, 0x87,
        0x19, 0x89, 0x1b, 0x8b, 0x1d, 0x8d, 0x1f, 0x8f,
    };

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0);
    for (int i = 0; i < 16; i++)
        XCTAssertEqual(cpu.vregs[0].b[i], expected[i]);
}

- (void)testSemanticExecutionContract_UZP2PacksOddLanesFromBothVectors
{
    enum { textPC = 0x98160 };
    static const uint32_t insn = 0x4e025820; // uzp2 v0.16b, v1.16b, v2.16b
    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));

    for (int i = 0; i < 16; i++) {
        cpu.vregs[1].b[i] = (uint8_t)(0x10 + i);
        cpu.vregs[2].b[i] = (uint8_t)(0x80 + i);
    }

    static const uint8_t expected[16] = {
        0x11, 0x13, 0x15, 0x17, 0x19, 0x1b, 0x1d, 0x1f,
        0x81, 0x83, 0x85, 0x87, 0x89, 0x8b, 0x8d, 0x8f,
    };

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0);
    for (int i = 0; i < 16; i++)
        XCTAssertEqual(cpu.vregs[0].b[i], expected[i]);
}

- (void)testSemanticExecutionContract_XTN2NarrowsHalfwordsIntoUpperByteHalf
{
    enum { textPC = 0x98180 };
    static const uint32_t insn = 0x4e212820; // xtn2 v0.16b, v1.8h
    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    memset(cpu.vregs[0].b, 0xaa, sizeof(cpu.vregs[0].b));

    static const uint16_t source[8] = {
        0x0102, 0x0304, 0x0506, 0x0708, 0x090a, 0x0b0c, 0x0d0e, 0x0f10,
    };
    for (int i = 0; i < 8; i++)
        cpu.vregs[1].h[i] = source[i];

    static const uint8_t expected[16] = {
        0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
        0x02, 0x04, 0x06, 0x08, 0x0a, 0x0c, 0x0e, 0x10,
    };

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0);
    for (int i = 0; i < 16; i++)
        XCTAssertEqual(cpu.vregs[0].b[i], expected[i]);
}

- (void)testSemanticExecutionContract_ANDComputesPerByteVectorMask
{
    enum {
        textPC = 0x98120,
    };

    static const uint32_t insn = 0x4e221c20; // and v0.16b, v1.16b, v2.16b

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));

    static const uint8_t lhs[16] = {
        0xff, 0x0f, 0xf0, 0x55, 0xaa, 0x33, 0xcc, 0x81,
        0x7e, 0x18, 0xe7, 0x5a, 0xa5, 0x3c, 0xc3, 0x99,
    };
    static const uint8_t rhs[16] = {
        0x0f, 0xff, 0x0f, 0xaa, 0x55, 0xf0, 0x3c, 0x7e,
        0x81, 0xe7, 0x18, 0xa5, 0x5a, 0xc3, 0x3c, 0x66,
    };
    static const uint8_t expected[16] = {
        0x0f, 0x0f, 0x00, 0x00, 0x00, 0x30, 0x0c, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };

    memcpy(cpu.vregs[1].b, lhs, sizeof(lhs));
    memcpy(cpu.vregs[2].b, rhs, sizeof(rhs));

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                   @"AND must execute through TCTI and apply the bytewise mask across the full "
                    "vector register.");

    for (int i = 0; i < 16; i++) {
        XCTAssertEqual(cpu.vregs[0].b[i], expected[i],
                       @"AND v0.16b, v1.16b, v2.16b must publish the per-byte bitwise AND result");
    }
}

- (void)testSemanticExecutionContract_ORRComputesPerByteVectorUnion
{
    enum {
        textPC = 0x98140,
    };

    static const uint32_t insn = 0x4ea51c83; // orr v3.16b, v4.16b, v5.16b

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));

    static const uint8_t lhs[16] = {
        0xff, 0x0f, 0xf0, 0x55, 0xaa, 0x33, 0xcc, 0x81,
        0x7e, 0x18, 0xe7, 0x5a, 0xa5, 0x3c, 0xc3, 0x99,
    };
    static const uint8_t rhs[16] = {
        0x0f, 0xff, 0x0f, 0xaa, 0x55, 0xf0, 0x3c, 0x7e,
        0x81, 0xe7, 0x18, 0xa5, 0x5a, 0xc3, 0x3c, 0x66,
    };
    static const uint8_t expected[16] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xf3, 0xfc, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    };

    memcpy(cpu.vregs[4].b, lhs, sizeof(lhs));
    memcpy(cpu.vregs[5].b, rhs, sizeof(rhs));

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                   @"ORR must execute through TCTI and combine the set bits from both source "
                    "vectors across the full register width.");

    for (int i = 0; i < 16; i++) {
        XCTAssertEqual(cpu.vregs[3].b[i], expected[i],
                       @"ORR v3.16b, v4.16b, v5.16b must publish the per-byte bitwise OR result");
    }
}

- (void)testSemanticExecutionContract_EORComputesPerByteVectorXor
{
    enum {
        textPC = 0x98160,
    };

    static const uint32_t insn = 0x6e281ce6; // eor v6.16b, v7.16b, v8.16b

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));

    static const uint8_t lhs[16] = {
        0xff, 0x0f, 0xf0, 0x55, 0xaa, 0x33, 0xcc, 0x81,
        0x7e, 0x18, 0xe7, 0x5a, 0xa5, 0x3c, 0xc3, 0x99,
    };
    static const uint8_t rhs[16] = {
        0x0f, 0xff, 0x0f, 0xaa, 0x55, 0xf0, 0x3c, 0x7e,
        0x81, 0xe7, 0x18, 0xa5, 0x5a, 0xc3, 0x3c, 0x66,
    };
    static const uint8_t expected[16] = {
        0xf0, 0xf0, 0xff, 0xff, 0xff, 0xc3, 0xf0, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    };

    memcpy(cpu.vregs[7].b, lhs, sizeof(lhs));
    memcpy(cpu.vregs[8].b, rhs, sizeof(rhs));

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                   @"EOR must execute through TCTI and toggle differing bits across the full "
                    "vector register.");

    for (int i = 0; i < 16; i++) {
        XCTAssertEqual(cpu.vregs[6].b[i], expected[i],
                       @"EOR v6.16b, v7.16b, v8.16b must publish the per-byte bitwise XOR result");
    }
}

- (void)testSemanticExecutionContract_ADDComputesPerByteVectorSums
{
    enum {
        textPC = 0x98180,
    };

    static const uint32_t insn = 0x4e2b8549; // add v9.16b, v10.16b, v11.16b

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));

    static const uint8_t lhs[16] = {
        0x00, 0x01, 0x7f, 0x80, 0xfe, 0xff, 0x10, 0x20,
        0x33, 0x44, 0x55, 0x66, 0xaa, 0xbb, 0xcc, 0xdd,
    };
    static const uint8_t rhs[16] = {
        0x00, 0x02, 0x01, 0x80, 0x02, 0x01, 0xf0, 0xe0,
        0xcd, 0xbc, 0xab, 0x9a, 0x56, 0x45, 0x34, 0x23,
    };
    static const uint8_t expected[16] = {
        0x00, 0x03, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };

    memcpy(cpu.vregs[10].b, lhs, sizeof(lhs));
    memcpy(cpu.vregs[11].b, rhs, sizeof(rhs));

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                   @"ADD must execute through TCTI and sum each byte lane with normal AdvSIMD "
                    "wraparound semantics.");

    for (int i = 0; i < 16; i++) {
        XCTAssertEqual(cpu.vregs[9].b[i], expected[i],
                       @"ADD v9.16b, v10.16b, v11.16b must publish the per-byte wraparound sum");
    }
}

- (void)testSemanticExecutionContract_SUBComputesPerByteVectorDifferences
{
    enum {
        textPC = 0x981a0,
    };

    static const uint32_t insn = 0x6e228420; // sub v0.16b, v1.16b, v2.16b

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));

    static const uint8_t lhs[16] = {
        0x00, 0x01, 0x7f, 0x80, 0xfe, 0xff, 0x10, 0x20,
        0x33, 0x44, 0x55, 0x66, 0xaa, 0xbb, 0xcc, 0xdd,
    };
    static const uint8_t rhs[16] = {
        0x00, 0x02, 0x01, 0x80, 0x02, 0x01, 0xf0, 0xe0,
        0xcd, 0xbc, 0xab, 0x9a, 0x56, 0x45, 0x34, 0x23,
    };
    static const uint8_t expected[16] = {
        0x00, 0xff, 0x7e, 0x00, 0xfc, 0xfe, 0x20, 0x40,
        0x66, 0x88, 0xaa, 0xcc, 0x54, 0x76, 0x98, 0xba,
    };

    memcpy(cpu.vregs[1].b, lhs, sizeof(lhs));
    memcpy(cpu.vregs[2].b, rhs, sizeof(rhs));

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                   @"SUB must execute through TCTI and subtract each byte lane with normal "
                    "AdvSIMD wraparound semantics.");

    for (int i = 0; i < 16; i++) {
        XCTAssertEqual(cpu.vregs[0].b[i], expected[i],
                       @"SUB v0.16b, v1.16b, v2.16b must publish the per-byte wraparound difference");
    }
}

- (void)testSemanticExecutionContract_MULComputesPerByteVectorProducts
{
    enum {
        textPC = 0x981c0,
    };

    static const uint32_t insn = 0x4e259c83; // mul v3.16b, v4.16b, v5.16b

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));

    static const uint8_t lhs[16] = {
        0x00, 0x02, 0x03, 0x10, 0xff, 0x80, 0x7f, 0x11,
        0x12, 0x20, 0x21, 0x33, 0x40, 0x55, 0xaa, 0xf0,
    };
    static const uint8_t rhs[16] = {
        0x05, 0x03, 0x04, 0x10, 0x02, 0x02, 0x03, 0x0f,
        0x10, 0x08, 0x09, 0x07, 0x04, 0x03, 0x02, 0x10,
    };
    static const uint8_t expected[16] = {
        0x00, 0x06, 0x0c, 0x00, 0xfe, 0x00, 0x7d, 0xff,
        0x20, 0x00, 0x29, 0x65, 0x00, 0xff, 0x54, 0x00,
    };

    memcpy(cpu.vregs[4].b, lhs, sizeof(lhs));
    memcpy(cpu.vregs[5].b, rhs, sizeof(rhs));

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                   @"MUL must execute through TCTI and multiply each byte lane with normal "
                    "AdvSIMD wraparound semantics.");

    for (int i = 0; i < 16; i++) {
        XCTAssertEqual(cpu.vregs[3].b[i], expected[i],
                       @"MUL v3.16b, v4.16b, v5.16b must publish the per-byte wraparound product");
    }
}

- (void)testSemanticExecutionContract_BICClearsMaskedBitsPerByte
{
    enum {
        textPC = 0x981e0,
    };

    static const uint32_t insn = 0x4e621c20; // bic v0.16b, v1.16b, v2.16b

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));

    static const uint8_t lhs[16] = {
        0xff, 0x0f, 0xf0, 0xaa, 0x55, 0x3c, 0xc3, 0x5a,
        0xa5, 0x81, 0x7e, 0x18, 0xe7, 0x42, 0x99, 0x66,
    };
    static const uint8_t rhs[16] = {
        0x00, 0xf0, 0x0f, 0xcc, 0x33, 0x0f, 0x3c, 0xa5,
        0x5a, 0xff, 0x81, 0x24, 0x18, 0xbd, 0x66, 0x99,
    };
    static const uint8_t expected[16] = {
        0xff, 0x0f, 0xf0, 0x22, 0x44, 0x30, 0xc3, 0x5a,
        0xa5, 0x00, 0x7e, 0x18, 0xe7, 0x42, 0x99, 0x66,
    };

    memcpy(cpu.vregs[1].b, lhs, sizeof(lhs));
    memcpy(cpu.vregs[2].b, rhs, sizeof(rhs));

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                   @"BIC must execute through TCTI and clear source bits selected by the mask "
                    "vector on each byte lane.");

    for (int i = 0; i < 16; i++) {
        XCTAssertEqual(cpu.vregs[0].b[i], expected[i],
                       @"BIC v0.16b, v1.16b, v2.16b must publish lhs & ~rhs for every byte lane");
    }
}

- (void)testSemanticExecutionContract_ORNComputesPerByteOrWithInvertedMask
{
    enum {
        textPC = 0x98200,
    };

    static const uint32_t insn = 0x4ee21c20; // orn v0.16b, v1.16b, v2.16b

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));

    static const uint8_t lhs[16] = {
        0xff, 0x0f, 0xf0, 0xaa, 0x55, 0x3c, 0xc3, 0x5a,
        0xa5, 0x81, 0x7e, 0x18, 0xe7, 0x42, 0x99, 0x66,
    };
    static const uint8_t rhs[16] = {
        0x00, 0xf0, 0x0f, 0xcc, 0x33, 0x0f, 0x3c, 0xa5,
        0x5a, 0xff, 0x81, 0x24, 0x18, 0xbd, 0x66, 0x99,
    };
    static const uint8_t expected[16] = {
        0xff, 0x0f, 0xf0, 0xbb, 0xdd, 0xfc, 0xc3, 0x5a,
        0xa5, 0x81, 0x7e, 0xdb, 0xe7, 0x42, 0x99, 0x66,
    };

    memcpy(cpu.vregs[1].b, lhs, sizeof(lhs));
    memcpy(cpu.vregs[2].b, rhs, sizeof(rhs));

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                   @"ORN must execute through TCTI and compute lhs | ~rhs on each byte lane.");

    for (int i = 0; i < 16; i++) {
        XCTAssertEqual(cpu.vregs[0].b[i], expected[i],
                       @"ORN v0.16b, v1.16b, v2.16b must publish lhs | ~rhs for every byte lane");
    }
}

- (void)testSemanticExecutionContract_BSLSelectsBitsUsingDestinationMask
{
    enum {
        textPC = 0x98220,
    };

    static const uint32_t insn = 0x6e621c20; // bsl v0.16b, v1.16b, v2.16b

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));

    static const uint8_t mask[16] = {
        0xff, 0x00, 0xf0, 0x0f, 0xaa, 0x55, 0x81, 0x18,
        0x3c, 0xc3, 0x5a, 0xa5, 0x7e, 0xe7, 0x24, 0xdb,
    };
    static const uint8_t lhs[16] = {
        0x10, 0x21, 0x32, 0x43, 0x54, 0x65, 0x76, 0x87,
        0x98, 0xa9, 0xba, 0xcb, 0xdc, 0xed, 0xfe, 0x0f,
    };
    static const uint8_t rhs[16] = {
        0xf0, 0xe1, 0xd2, 0xc3, 0xb4, 0xa5, 0x96, 0x78,
        0x69, 0x5a, 0x4b, 0x3c, 0x2d, 0x1e, 0x0f, 0xf1,
    };
    static const uint8_t expected[16] = {
        0x10, 0xe1, 0x32, 0xc3, 0x14, 0xe5, 0x16, 0x60,
        0x59, 0x99, 0x1b, 0x99, 0x5d, 0xfd, 0x2f, 0x2b,
    };

    memcpy(cpu.vregs[0].b, mask, sizeof(mask));
    memcpy(cpu.vregs[1].b, lhs, sizeof(lhs));
    memcpy(cpu.vregs[2].b, rhs, sizeof(rhs));

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                   @"BSL must execute through TCTI and use the original destination register "
                    "contents as the per-bit selection mask.");

    for (int i = 0; i < 16; i++) {
        XCTAssertEqual(cpu.vregs[0].b[i], expected[i],
                       @"BSL v0.16b, v1.16b, v2.16b must publish (mask & lhs) | (~mask & rhs)");
    }
}

- (void)testSemanticExecutionContract_BITInsertsSourceBitsWhereMaskIsSet
{
    enum {
        textPC = 0x98240,
    };

    static const uint32_t insn = 0x6ea21c20; // bit v0.16b, v1.16b, v2.16b

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));

    static const uint8_t oldDestination[16] = {
        0xff, 0x0f, 0xf0, 0xaa, 0x55, 0x3c, 0xc3, 0x5a,
        0xa5, 0x81, 0x7e, 0x18, 0xe7, 0x42, 0x99, 0x66,
    };
    static const uint8_t insertedBits[16] = {
        0x10, 0x21, 0x32, 0x43, 0x54, 0x65, 0x76, 0x87,
        0x98, 0xa9, 0xba, 0xcb, 0xdc, 0xed, 0xfe, 0x0f,
    };
    static const uint8_t mask[16] = {
        0xf0, 0xe1, 0xd2, 0xc3, 0xb4, 0xa5, 0x96, 0x78,
        0x69, 0x5a, 0x4b, 0x3c, 0x2d, 0x1e, 0x0f, 0xf1,
    };
    static const uint8_t expected[16] = {
        0x1f, 0x2f, 0x32, 0x6b, 0x55, 0x3d, 0x57, 0x02,
        0x8c, 0x89, 0x3e, 0x08, 0xce, 0x4c, 0x9e, 0x07,
    };

    memcpy(cpu.vregs[0].b, oldDestination, sizeof(oldDestination));
    memcpy(cpu.vregs[1].b, insertedBits, sizeof(insertedBits));
    memcpy(cpu.vregs[2].b, mask, sizeof(mask));

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                   @"BIT must execute through TCTI and insert source bits into the original "
                    "destination where the mask has ones.");

    for (int i = 0; i < 16; i++) {
        XCTAssertEqual(cpu.vregs[0].b[i], expected[i],
                       @"BIT v0.16b, v1.16b, v2.16b must publish (old & ~mask) | (src & mask)");
    }
}

- (void)testSemanticExecutionContract_BIFInsertsSourceBitsWhereMaskIsClear
{
    enum {
        textPC = 0x98260,
    };

    static const uint32_t insn = 0x6ee21c20; // bif v0.16b, v1.16b, v2.16b

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));

    static const uint8_t oldDestination[16] = {
        0xff, 0x0f, 0xf0, 0xaa, 0x55, 0x3c, 0xc3, 0x5a,
        0xa5, 0x81, 0x7e, 0x18, 0xe7, 0x42, 0x99, 0x66,
    };
    static const uint8_t insertedBits[16] = {
        0x10, 0x21, 0x32, 0x43, 0x54, 0x65, 0x76, 0x87,
        0x98, 0xa9, 0xba, 0xcb, 0xdc, 0xed, 0xfe, 0x0f,
    };
    static const uint8_t mask[16] = {
        0xf0, 0xe1, 0xd2, 0xc3, 0xb4, 0xa5, 0x96, 0x78,
        0x69, 0x5a, 0x4b, 0x3c, 0x2d, 0x1e, 0x0f, 0xf1,
    };
    static const uint8_t expected[16] = {
        0xf0, 0x01, 0xf0, 0x82, 0x54, 0x64, 0xe2, 0xdf,
        0xb1, 0xa1, 0xfa, 0xdb, 0xf5, 0xe3, 0xf9, 0x6e,
    };

    memcpy(cpu.vregs[0].b, oldDestination, sizeof(oldDestination));
    memcpy(cpu.vregs[1].b, insertedBits, sizeof(insertedBits));
    memcpy(cpu.vregs[2].b, mask, sizeof(mask));

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                   @"BIF must execute through TCTI and insert source bits into the original "
                    "destination where the mask has zero bits.");

    for (int i = 0; i < 16; i++) {
        XCTAssertEqual(cpu.vregs[0].b[i], expected[i],
                       @"BIF v0.16b, v1.16b, v2.16b must publish (old & mask) | (src & ~mask)");
    }
}

- (void)testSemanticExecutionContract_CMEQPublishesAllOnesForEqualByteLanes
{
    enum {
        textPC = 0x98180,
    };

    static const uint32_t insn = 0x6e228c20; // cmeq v0.16b, v1.16b, v2.16b

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));

    for (int i = 0; i < 16; i++) {
        cpu.vregs[1].b[i] = (uint8_t)(0x20 + i);
        cpu.vregs[2].b[i] = (uint8_t)(0x20 + i);
    }
    cpu.vregs[2].b[3] ^= 0x1;
    cpu.vregs[2].b[11] ^= 0x80;

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                   @"CMEQ must execute through TCTI and publish per-byte equality masks");

    for (int i = 0; i < 16; i++) {
        uint8_t expected = (i == 3 || i == 11) ? 0x00 : 0xff;
        XCTAssertEqual(cpu.vregs[0].b[i], expected,
                       @"CMEQ must write 0xff for equal lanes and 0x00 for mismatches");
    }
}

- (void)testSemanticExecutionContract_CMGTPublishesAllOnesOnlyForGreaterThanByteLanes
{
    enum {
        textPC = 0x981a0,
    };

    static const uint32_t insn = 0x4e253483; // cmgt v3.16b, v4.16b, v5.16b

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));

    static const int8_t lhs[16] = { 5, 4, 3, 2, 1, 0, -1, -2, 10, 20, 30, 40, -10, -20, 100, -100 };
    static const int8_t rhs[16] = { 4, 4, 2, 3, 1, -1, -2, -2, 11, 19, 30, 39, -11, -10, 99, -90 };
    for (int i = 0; i < 16; i++) {
        cpu.vregs[4].b[i] = (uint8_t)lhs[i];
        cpu.vregs[5].b[i] = (uint8_t)rhs[i];
    }

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                   @"CMGT must execute through TCTI and compare signed byte lanes");

    for (int i = 0; i < 16; i++) {
        uint8_t expected = lhs[i] > rhs[i] ? 0xff : 0x00;
        XCTAssertEqual(cpu.vregs[3].b[i], expected,
                       @"CMGT must write 0xff only where the signed lhs byte is greater");
    }
}

- (void)testSemanticExecutionContract_MLAAccumulatesVectorProductsIntoDestination
{
    enum {
        textPC = 0x981c0,
    };

    static const uint32_t insn = 0x4e2894e6; // mla v6.16b, v7.16b, v8.16b

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));

    for (int i = 0; i < 16; i++) {
        cpu.vregs[6].b[i] = (uint8_t)(i + 1);
        cpu.vregs[7].b[i] = (uint8_t)(i + 2);
        cpu.vregs[8].b[i] = (uint8_t)(i + 3);
    }

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                   @"MLA must execute through TCTI and accumulate byte-lane products into the "
                    "destination vector");

    for (int i = 0; i < 16; i++) {
        uint8_t expected = (uint8_t)((i + 1) + ((i + 2) * (i + 3)));
        XCTAssertEqual(cpu.vregs[6].b[i], expected,
                       @"MLA must add each per-byte product into the destination lane");
    }
}

- (void)testSemanticExecutionContract_MLSSubtractsVectorProductsFromDestination
{
    enum {
        textPC = 0x981e0,
    };

    static const uint32_t insn = 0x6e2b9549; // mls v9.16b, v10.16b, v11.16b

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));

    for (int i = 0; i < 16; i++) {
        cpu.vregs[9].b[i] = (uint8_t)(0x80 + i);
        cpu.vregs[10].b[i] = (uint8_t)(i + 1);
        cpu.vregs[11].b[i] = (uint8_t)(i + 2);
    }

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                   @"MLS must execute through TCTI and subtract byte-lane products from the "
                    "destination vector");

    for (int i = 0; i < 16; i++) {
        uint8_t expected = (uint8_t)((0x80 + i) - ((i + 1) * (i + 2)));
        XCTAssertEqual(cpu.vregs[9].b[i], expected,
                       @"MLS must subtract each per-byte product from the destination lane");
    }
}

- (void)testSemanticExecutionContract_CMGEPublishesSignedGreaterOrEqualMask
{
    static const uint8_t lhs[16] = {
        0xff, 0x80, 0x81, 0x00, 0x01, 0x7f, 0x7e, 0xfe,
        0x10, 0x20, 0x30, 0xf0, 0xd0, 0x40, 0x90, 0x05,
    };
    static const uint8_t rhs[16] = {
        0xff, 0x7f, 0x81, 0x01, 0x00, 0x7f, 0x7f, 0xfd,
        0x11, 0x10, 0x30, 0xef, 0xe0, 0x50, 0x90, 0x06,
    };
    static const uint8_t expected[16] = {
        0xff, 0x00, 0xff, 0x00, 0xff, 0xff, 0x00, 0xff,
        0x00, 0xff, 0xff, 0xff, 0x00, 0x00, 0xff, 0x00,
    };
    [self assertVectorMaskInstruction:0x4e223c20
                               textPC:0x98200
                              destReg:0
                               lhsReg:1
                               rhsReg:2
                                  lhs:lhs
                                  rhs:rhs
                             expected:expected
                             mnemonic:"cmge v0.16b, v1.16b, v2.16b"];
}

- (void)testSemanticExecutionContract_CMHIPublishesUnsignedGreaterThanMask
{
    static const uint8_t lhs[16] = {
        0x10, 0x20, 0x30, 0x40, 0xf0, 0xff, 0x00, 0x80,
        0x7f, 0x01, 0x02, 0x03, 0xaa, 0x55, 0x11, 0x99,
    };
    static const uint8_t rhs[16] = {
        0x0f, 0x20, 0x31, 0x3f, 0xe0, 0xff, 0x01, 0x7f,
        0x80, 0x00, 0x03, 0x02, 0xaa, 0x54, 0x12, 0x98,
    };
    static const uint8_t expected[16] = {
        0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff,
        0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff,
    };
    [self assertVectorMaskInstruction:0x6e223420
                               textPC:0x98220
                              destReg:0
                               lhsReg:1
                               rhsReg:2
                                  lhs:lhs
                                  rhs:rhs
                             expected:expected
                             mnemonic:"cmhi v0.16b, v1.16b, v2.16b"];
}

- (void)testSemanticExecutionContract_CMHSPublishesUnsignedGreaterOrEqualMask
{
    static const uint8_t lhs[16] = {
        0x00, 0x01, 0x7f, 0x80, 0xfe, 0xff, 0x10, 0x20,
        0x30, 0x40, 0x50, 0x60, 0x70, 0x81, 0x90, 0xa0,
    };
    static const uint8_t rhs[16] = {
        0x00, 0x02, 0x7f, 0x7f, 0xff, 0xfe, 0x10, 0x21,
        0x20, 0x40, 0x51, 0x60, 0x71, 0x81, 0x8f, 0xa1,
    };
    static const uint8_t expected[16] = {
        0xff, 0x00, 0xff, 0xff, 0x00, 0xff, 0xff, 0x00,
        0xff, 0xff, 0x00, 0xff, 0x00, 0xff, 0xff, 0x00,
    };
    [self assertVectorMaskInstruction:0x6e223c20
                               textPC:0x98240
                              destReg:0
                               lhsReg:1
                               rhsReg:2
                                  lhs:lhs
                                  rhs:rhs
                             expected:expected
                             mnemonic:"cmhs v0.16b, v1.16b, v2.16b"];
}

- (void)testSemanticExecutionContract_CMLEPublishesSignedLessOrEqualZeroMask
{
    static const uint8_t src[16] = {
        0x00, 0x01, 0xff, 0x80, 0x7f, 0xfe, 0x02, 0xfd,
        0x10, 0xf0, 0x20, 0xe0, 0x30, 0xd0, 0x40, 0xc0,
    };
    static const uint8_t expected[16] = {
        0xff, 0x00, 0xff, 0xff, 0x00, 0xff, 0x00, 0xff,
        0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff,
    };
    [self assertVectorMaskAgainstZeroInstruction:0x6e209820
                                          textPC:0x98260
                                         destReg:0
                                          srcReg:1
                                             src:src
                                        expected:expected
                                        mnemonic:"cmle v0.16b, v1.16b, #0"];
}

- (void)testSemanticExecutionContract_CMLTPublishesSignedLessThanZeroMask
{
    static const uint8_t src[16] = {
        0x00, 0x01, 0xff, 0x80, 0x7f, 0xfe, 0x02, 0xfd,
        0x10, 0xf0, 0x20, 0xe0, 0x30, 0xd0, 0x40, 0xc0,
    };
    static const uint8_t expected[16] = {
        0x00, 0x00, 0xff, 0xff, 0x00, 0xff, 0x00, 0xff,
        0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff,
    };
    [self assertVectorMaskAgainstZeroInstruction:0x4e20a820
                                          textPC:0x98280
                                         destReg:0
                                          srcReg:1
                                             src:src
                                        expected:expected
                                        mnemonic:"cmlt v0.16b, v1.16b, #0"];
}

- (void)testSemanticExecutionContract_CMTSTPublishesNonzeroBitIntersectionMask
{
    static const uint8_t lhs[16] = {
        0x00, 0x01, 0x03, 0x04, 0x08, 0x10, 0x30, 0x40,
        0x80, 0xff, 0x55, 0xaa, 0x0f, 0xf0, 0x11, 0x22,
    };
    static const uint8_t rhs[16] = {
        0x00, 0x02, 0x01, 0x04, 0x00, 0x08, 0x10, 0x80,
        0x80, 0x01, 0xaa, 0x55, 0xf0, 0x0f, 0x10, 0x44,
    };
    static const uint8_t expected[16] = {
        0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0x00,
        0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00,
    };
    [self assertVectorMaskInstruction:0x4e228c20
                               textPC:0x982a0
                              destReg:0
                               lhsReg:1
                               rhsReg:2
                                  lhs:lhs
                                  rhs:rhs
                             expected:expected
                             mnemonic:"cmtst v0.16b, v1.16b, v2.16b"];
}

@end
