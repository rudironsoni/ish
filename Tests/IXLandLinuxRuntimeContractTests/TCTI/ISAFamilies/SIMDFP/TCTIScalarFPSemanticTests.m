#import <XCTest/XCTest.h>

#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>

@interface TCTIScalarFPSemanticTests : XCTestCase
@end

#define TCTI_DECLARE_FP_TO_INT_SEMANTIC_TEST(_name, _insn, _pc, _srcVReg, _dstXReg, _srcBits, _expected) \
- (void)testSemanticExecutionContract_##_name                                                           \
{                                                                                                       \
    struct cpu_state cpu;                                                                               \
    static const uint32_t insn = _insn;                                                                 \
    memset(&cpu, 0, sizeof(cpu));                                                                       \
    cpu.vregs[_srcVReg].d[0] = _srcBits;                                                                \
    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, _pc, &insn, 1), 0);                                \
    XCTAssertEqual(cpu.x[_dstXReg], (uint64_t)_expected);                                               \
}

#define TCTI_DECLARE_INT_TO_FP_SEMANTIC_TEST(_name, _insn, _pc, _srcXReg, _dstVReg, _srcValue, _expectedBits) \
- (void)testSemanticExecutionContract_##_name                                                                 \
{                                                                                                             \
    struct cpu_state cpu;                                                                                     \
    static const uint32_t insn = _insn;                                                                       \
    memset(&cpu, 0, sizeof(cpu));                                                                             \
    cpu.x[_srcXReg] = _srcValue;                                                                              \
    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, _pc, &insn, 1), 0);                                      \
    XCTAssertEqual(cpu.vregs[_dstVReg].d[0], (uint64_t)_expectedBits);                                        \
}

#define TCTI_DECLARE_FP_UNARY_SEMANTIC_TEST(_name, _insn, _pc, _srcVReg, _dstVReg, _srcBits, _expectedBits) \
- (void)testSemanticExecutionContract_##_name                                                                 \
{                                                                                                             \
    struct cpu_state cpu;                                                                                     \
    static const uint32_t insn = _insn;                                                                       \
    memset(&cpu, 0, sizeof(cpu));                                                                             \
    cpu.vregs[_srcVReg].d[0] = _srcBits;                                                                      \
    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, _pc, &insn, 1), 0);                                      \
    XCTAssertEqual(cpu.vregs[_dstVReg].d[0], (uint64_t)_expectedBits);                                        \
}

#define TCTI_DECLARE_FP_BINARY_SEMANTIC_TEST(_name, _insn, _pc, _lhsVReg, _rhsVReg, _dstVReg, _lhsBits, _rhsBits, _expectedBits) \
- (void)testSemanticExecutionContract_##_name                                                                            \
{                                                                                                                        \
    struct cpu_state cpu;                                                                                                \
    static const uint32_t insn = _insn;                                                                                  \
    memset(&cpu, 0, sizeof(cpu));                                                                                        \
    cpu.vregs[_lhsVReg].d[0] = _lhsBits;                                                                                 \
    cpu.vregs[_rhsVReg].d[0] = _rhsBits;                                                                                 \
    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, _pc, &insn, 1), 0);                                                 \
    XCTAssertEqual(cpu.vregs[_dstVReg].d[0], (uint64_t)_expectedBits);                                                   \
}

@implementation TCTIScalarFPSemanticTests

- (void)testSemanticExecutionContract_FMOVGPRToScalarAndBackRoundTripsBits
{
    enum {
        textPC = 0x92000,
    };

    static const uint32_t insns[] = {
        0x9e670000, // fmov d0, x0
        0x9e660002, // fmov x2, d0
    };

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.x[0] = 0x400e000000000000ULL;

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, insns, sizeof(insns) / sizeof(insns[0])),
                   0,
                   @"Scalar FMOV between GPR and FP registers must roundtrip raw bits through "
                    "the real TCTI SIMD/FP path");
    XCTAssertEqual(cpu.vregs[0].d[0], 0x400e000000000000ULL);
    XCTAssertEqual(cpu.x[2], 0x400e000000000000ULL);
}

- (void)testSemanticExecutionContract_FMOVGPRToSecondScalarRegisterAndBackRoundTripsBits
{
    enum {
        textPC = 0x92010,
    };

    static const uint32_t insns[] = {
        0x9e670021, // fmov d1, x1
        0x9e660022, // fmov x2, d1
    };

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.x[1] = 0x4002000000000000ULL;

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, insns, sizeof(insns) / sizeof(insns[0])),
                   0,
                   @"Scalar FMOV must roundtrip through non-zero FP register indices, not only v0");
    XCTAssertEqual(cpu.vregs[1].d[0], 0x4002000000000000ULL);
    XCTAssertEqual(cpu.x[2], 0x4002000000000000ULL);
}

- (void)testSemanticExecutionContract_FADDDoubleAddsIEEE64Operands
{
    enum {
        textPC = 0x92020,
    };

    static const uint32_t insns[] = {
        0x9e670000, // fmov d0, x0
        0x9e670021, // fmov d1, x1
        0x1e612800, // fadd d0, d0, d1
        0x9e660002, // fmov x2, d0
    };

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.x[0] = 0x3ff8000000000000ULL; // 1.5
    cpu.x[1] = 0x4002000000000000ULL; // 2.25

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, insns, sizeof(insns) / sizeof(insns[0])),
                   0,
                   @"Scalar FP arithmetic must execute through TCTI so guest ldso/libc code can "
                    "consume real IEEE64 results without a fallback engine");
    XCTAssertEqual(cpu.vregs[1].d[0], 0x4002000000000000ULL,
                   @"The second scalar FMOV must populate d1 before FADD consumes it");
    XCTAssertEqual(cpu.vregs[0].d[0], 0x400e000000000000ULL,
                   @"FADD d0,d0,d1 must update the destination scalar FP register in place");
    XCTAssertEqual(cpu.x[2], 0x400e000000000000ULL, @"1.5 + 2.25 must produce 3.75");
}

TCTI_DECLARE_FP_TO_INT_SEMANTIC_TEST(FCVTASRoundsNearestAwayToSignedInteger, 0x9e640020, 0x92030,
                                     1, 0, 0x400c000000000000ULL, 4ULL)
TCTI_DECLARE_FP_TO_INT_SEMANTIC_TEST(FCVTAURoundsNearestAwayToUnsignedInteger, 0x9e650062, 0x92034,
                                     3, 2, 0x400c000000000000ULL, 4ULL)
TCTI_DECLARE_FP_TO_INT_SEMANTIC_TEST(FCVTMSRoundsTowardMinusInfinity, 0x9e7000a4, 0x92038,
                                     5, 4, 0x400f333333333333ULL, 3ULL)
TCTI_DECLARE_FP_TO_INT_SEMANTIC_TEST(FCVTMURoundsTowardMinusInfinityUnsigned, 0x9e7100e6, 0x9203c,
                                     7, 6, 0x400f333333333333ULL, 3ULL)
TCTI_DECLARE_FP_TO_INT_SEMANTIC_TEST(FCVTNSRoundsNearestEvenToSignedInteger, 0x9e600128, 0x92040,
                                     9, 8, 0x400c000000000000ULL, 4ULL)
TCTI_DECLARE_FP_TO_INT_SEMANTIC_TEST(FCVTNURoundsNearestEvenToUnsignedInteger, 0x9e61016a, 0x92044,
                                     11, 10, 0x400c000000000000ULL, 4ULL)
TCTI_DECLARE_FP_TO_INT_SEMANTIC_TEST(FCVTPSRoundsTowardPlusInfinitySigned, 0x9e6801ac, 0x92048,
                                     13, 12, 0x4008cccccccccccdULL, 4ULL)
TCTI_DECLARE_FP_TO_INT_SEMANTIC_TEST(FCVTPURoundsTowardPlusInfinityUnsigned, 0x9e6901ee, 0x9204c,
                                     15, 14, 0x4008cccccccccccdULL, 4ULL)
TCTI_DECLARE_FP_TO_INT_SEMANTIC_TEST(FCVTZSTruncatesSignedInteger, 0x9e780230, 0x92050,
                                     17, 16, 0x400f333333333333ULL, 3ULL)
TCTI_DECLARE_FP_TO_INT_SEMANTIC_TEST(FCVTZUTruncatesUnsignedInteger, 0x9e790272, 0x92054,
                                     19, 18, 0x400f333333333333ULL, 3ULL)
TCTI_DECLARE_INT_TO_FP_SEMANTIC_TEST(SCVTFConvertsSignedIntegerToDouble, 0x9e6202b4, 0x92058,
                                     21, 20, 4ULL, 0x4010000000000000ULL)
TCTI_DECLARE_INT_TO_FP_SEMANTIC_TEST(UCVTFConvertsUnsignedIntegerToDouble, 0x9e6302f6, 0x9205c,
                                     23, 22, 5ULL, 0x4014000000000000ULL)
TCTI_DECLARE_FP_UNARY_SEMANTIC_TEST(FRINTARoundsAwayToIntegralDouble, 0x1e664338, 0x92060,
                                    25, 24, 0x400c000000000000ULL, 0x4010000000000000ULL)
TCTI_DECLARE_FP_UNARY_SEMANTIC_TEST(FRINTIRoundsToIntegralUsingCurrentMode, 0x1e67c37a, 0x92064,
                                    27, 26, 0x400c000000000000ULL, 0x4010000000000000ULL)
TCTI_DECLARE_FP_UNARY_SEMANTIC_TEST(FRINTMRoundsTowardMinusInfinity, 0x1e6543bc, 0x92068,
                                    29, 28, 0x400f333333333333ULL, 0x4008000000000000ULL)
TCTI_DECLARE_FP_UNARY_SEMANTIC_TEST(FRINTNRoundsNearestEven, 0x1e644020, 0x9206c,
                                    1, 0, 0x400c000000000000ULL, 0x4010000000000000ULL)
TCTI_DECLARE_FP_UNARY_SEMANTIC_TEST(FRINTPRoundsTowardPlusInfinity, 0x1e64c062, 0x92070,
                                    3, 2, 0x4008cccccccccccdULL, 0x4010000000000000ULL)
TCTI_DECLARE_FP_UNARY_SEMANTIC_TEST(FRINTXRoundsToIntegralExactly, 0x1e6740a4, 0x92074,
                                    5, 4, 0x400c000000000000ULL, 0x4010000000000000ULL)
TCTI_DECLARE_FP_UNARY_SEMANTIC_TEST(FRINTZRoundsTowardZero, 0x1e65c0e6, 0x92078,
                                    7, 6, 0x400f333333333333ULL, 0x4008000000000000ULL)
TCTI_DECLARE_FP_UNARY_SEMANTIC_TEST(FSQRTReturnsSquareRoot, 0x1e61c128, 0x9207c,
                                    9, 8, 0x4010000000000000ULL, 0x4000000000000000ULL)
TCTI_DECLARE_FP_UNARY_SEMANTIC_TEST(FRECPEOfOneReturnsOne, 0x5ee1d96a, 0x92080,
                                    11, 10, 0x3ff0000000000000ULL, 0x3ff0000000000000ULL)
TCTI_DECLARE_FP_BINARY_SEMANTIC_TEST(FRECPSOfOneAndOneReturnsOne, 0x5e6efdac, 0x92084,
                                     13, 14, 12, 0x3ff0000000000000ULL, 0x3ff0000000000000ULL,
                                     0x3ff0000000000000ULL)
TCTI_DECLARE_FP_UNARY_SEMANTIC_TEST(FRECPXOfOneReturnsOne, 0x5ee1fa0f, 0x92088,
                                    16, 15, 0x3ff0000000000000ULL, 0x3ff0000000000000ULL)
TCTI_DECLARE_FP_UNARY_SEMANTIC_TEST(FRSQRTEOfOneReturnsOne, 0x7ee1da51, 0x9208c,
                                    18, 17, 0x3ff0000000000000ULL, 0x3ff0000000000000ULL)
TCTI_DECLARE_FP_BINARY_SEMANTIC_TEST(FRSQRTSOfOneAndOneReturnsOne, 0x5ef5fe93, 0x92090,
                                     20, 21, 19, 0x3ff0000000000000ULL, 0x3ff0000000000000ULL,
                                     0x3ff0000000000000ULL)
TCTI_DECLARE_FP_TO_INT_SEMANTIC_TEST(FJCVTZSTruncatesDoubleToSignedWord, 0x1e7e0359, 0x92094,
                                     26, 25, 0x400f333333333333ULL, 3ULL)
TCTI_DECLARE_FP_BINARY_SEMANTIC_TEST(FSUBDoubleSubtractsIEEE64Operands, 0x1e623820, 0x92098,
                                     1, 2, 0, 0x4014000000000000ULL, 0x4008000000000000ULL,
                                     0x4008000000000000ULL)
TCTI_DECLARE_FP_BINARY_SEMANTIC_TEST(FMULDoubleMultipliesIEEE64Operands, 0x1e620820, 0x9209c,
                                     4, 5, 3, 0x4008000000000000ULL, 0x4000000000000000ULL,
                                     0x4010000000000000ULL)
TCTI_DECLARE_FP_BINARY_SEMANTIC_TEST(FDIVDoubleDividesIEEE64Operands, 0x1e621820, 0x920a0,
                                     7, 8, 6, 0x4010000000000000ULL, 0x4000000000000000ULL,
                                     0x4000000000000000ULL)
TCTI_DECLARE_FP_UNARY_SEMANTIC_TEST(FNEGNegatesIEEE64Operand, 0x1e614020, 0x920a4,
                                    10, 9, 0x4008000000000000ULL, 0xc008000000000000ULL)
TCTI_DECLARE_FP_UNARY_SEMANTIC_TEST(FABSClearsSignBitOfIEEE64Operand, 0x1e60c020, 0x920a8,
                                    12, 11, 0xc008000000000000ULL, 0x4008000000000000ULL)

- (void)testSemanticExecutionContract_FMADDDoubleFusedMultiplyAdd
{
    struct cpu_state cpu;
    static const uint32_t insn = 0x1f420c20; // fmadd d0, d1, d2, d3
    memset(&cpu, 0, sizeof(cpu));
    cpu.vregs[1].d[0] = 0x4000000000000000ULL;
    cpu.vregs[2].d[0] = 0x4008000000000000ULL;
    cpu.vregs[3].d[0] = 0x3ff0000000000000ULL;
    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, 0x920ac, &insn, 1), 0);
    XCTAssertEqual(cpu.vregs[0].d[0], 0x401c000000000000ULL);
}

- (void)testSemanticExecutionContract_FMSUBDoubleFusedMultiplySubtract
{
    struct cpu_state cpu;
    static const uint32_t insn = 0x1f428c20; // fmsub d0, d1, d2, d3
    memset(&cpu, 0, sizeof(cpu));
    cpu.vregs[1].d[0] = 0x4000000000000000ULL;
    cpu.vregs[2].d[0] = 0x4008000000000000ULL;
    cpu.vregs[3].d[0] = 0x3ff0000000000000ULL;
    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, 0x920b0, &insn, 1), 0);
    XCTAssertEqual(cpu.vregs[0].d[0], 0x4014000000000000ULL);
}

- (void)testSemanticExecutionContract_FNMADDDoubleNegatedFusedMultiplyAdd
{
    struct cpu_state cpu;
    static const uint32_t insn = 0x1f620c20; // fnmadd d0, d1, d2, d3
    memset(&cpu, 0, sizeof(cpu));
    cpu.vregs[1].d[0] = 0x4000000000000000ULL;
    cpu.vregs[2].d[0] = 0x4008000000000000ULL;
    cpu.vregs[3].d[0] = 0x3ff0000000000000ULL;
    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, 0x920b4, &insn, 1), 0);
    XCTAssertEqual(cpu.vregs[0].d[0], 0xc014000000000000ULL);
}

- (void)testSemanticExecutionContract_FNMSUBDoubleNegatedFusedMultiplySubtract
{
    struct cpu_state cpu;
    static const uint32_t insn = 0x1f628c20; // fnmsub d0, d1, d2, d3
    memset(&cpu, 0, sizeof(cpu));
    cpu.vregs[1].d[0] = 0x4000000000000000ULL;
    cpu.vregs[2].d[0] = 0x4008000000000000ULL;
    cpu.vregs[3].d[0] = 0x3ff0000000000000ULL;
    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, 0x920b8, &insn, 1), 0);
    XCTAssertEqual(cpu.vregs[0].d[0], 0xc01c000000000000ULL);
}

- (void)testSemanticExecutionContract_FCMPUpdatesNZCVForGreaterThan
{
    struct cpu_state cpu;
    static const uint32_t insn = 0x1e612000; // fcmp d0, d1
    memset(&cpu, 0, sizeof(cpu));
    cpu.vregs[0].d[0] = 0x4010000000000000ULL;
    cpu.vregs[1].d[0] = 0x4000000000000000ULL;
    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, 0x920bc, &insn, 1), 0);
    XCTAssertEqual((cpu.pstate >> 28) & 0xf, 0x2ULL);
}

- (void)testSemanticExecutionContract_FCMPEUpdatesNZCVForEquality
{
    struct cpu_state cpu;
    static const uint32_t insn = 0x1e612010; // fcmpe d0, d1
    memset(&cpu, 0, sizeof(cpu));
    cpu.vregs[0].d[0] = 0x4000000000000000ULL;
    cpu.vregs[1].d[0] = 0x4000000000000000ULL;
    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, 0x920c0, &insn, 1), 0);
    XCTAssertEqual((cpu.pstate >> 28) & 0xf, 0x6ULL);
}

- (void)testSemanticExecutionContract_FCCMPPreservesConditionalComparePath
{
    struct cpu_state cpu;
    static const uint32_t insn = 0x1e611404; // fccmp d0, d1, #4, ne
    memset(&cpu, 0, sizeof(cpu));
    cpu.pstate = 0x20000000U;
    cpu.vregs[0].d[0] = 0x4010000000000000ULL;
    cpu.vregs[1].d[0] = 0x4000000000000000ULL;
    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, 0x920c4, &insn, 1), 0);
}

- (void)testSemanticExecutionContract_FCCMPEPreservesConditionalCompareExceptionPath
{
    struct cpu_state cpu;
    static const uint32_t insn = 0x1e611414; // fccmpe d0, d1, #4, ne
    memset(&cpu, 0, sizeof(cpu));
    cpu.pstate = 0x20000000U;
    cpu.vregs[0].d[0] = 0x4010000000000000ULL;
    cpu.vregs[1].d[0] = 0x4000000000000000ULL;
    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, 0x920c8, &insn, 1), 0);
}

- (void)testSemanticExecutionContract_FCSELChoosesConditionTrueOperand
{
    struct cpu_state cpu;
    static const uint32_t insn = 0x1e621c20; // fcsel d0, d1, d2, ne
    memset(&cpu, 0, sizeof(cpu));
    cpu.pstate = 0x00000000U;
    cpu.vregs[1].d[0] = 0x3ff0000000000000ULL;
    cpu.vregs[2].d[0] = 0x4000000000000000ULL;
    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, 0x920cc, &insn, 1), 0);
    XCTAssertEqual(cpu.vregs[0].d[0], 0x3ff0000000000000ULL);
}

@end
