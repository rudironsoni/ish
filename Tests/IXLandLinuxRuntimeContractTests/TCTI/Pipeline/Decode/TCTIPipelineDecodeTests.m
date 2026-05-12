#import <XCTest/XCTest.h>

#import <IXLandLinuxRuntime/emu/aarch64/decode.h>

@interface TCTIPipelineDecodeTests : XCTestCase
@end

#define TCTI_DECLARE_ATOMIC_DECODE_TEST(_name, _insn, _rd, _rn, _rm, _size, _is64)               \
- (void)testDecodeContract_##_name                                                                 \
{                                                                                                  \
    a64_instr_t decoded = [self decodeInstruction:_insn];                                          \
    XCTAssertEqual(decoded.cat, A64_LD_ST);                                                        \
    XCTAssertEqual(decoded.subtype, A64_LDST_ATOMIC);                                              \
    XCTAssertEqual(decoded.Rd, _rd);                                                               \
    XCTAssertEqual(decoded.Rn, _rn);                                                               \
    XCTAssertEqual(decoded.Rm, _rm);                                                               \
    XCTAssertEqual(decoded.size, _size);                                                           \
    XCTAssertEqual(decoded.is_64bit, _is64);                                                       \
    XCTAssertEqual(decoded.idx_mode, A64_INDEX_OFFSET);                                            \
    XCTAssertEqual(decoded.imm, 0LL);                                                              \
}

#define TCTI_DECLARE_PAIR_ATOMIC_DECODE_TEST(_name, _insn, _rn, _size, _is64, _rt2)              \
- (void)testDecodeContract_##_name                                                                 \
{                                                                                                  \
    a64_instr_t decoded = [self decodeInstruction:_insn];                                          \
    XCTAssertEqual(decoded.cat, A64_LD_ST);                                                        \
    XCTAssertEqual(decoded.subtype, A64_LDST_ATOMIC);                                              \
    XCTAssertTrue(decoded.is_pair,                                                                  \
                  @"Pair-exclusive forms must stay marked as pair operations at decode time");     \
    XCTAssertEqual(decoded.Rn, _rn);                                                               \
    XCTAssertEqual(decoded.size, _size);                                                           \
    XCTAssertEqual(decoded.is_64bit, _is64);                                                       \
    XCTAssertEqual(decoded.idx_mode, A64_INDEX_OFFSET);                                            \
    XCTAssertEqual(bits(_insn, 14, 10), _rt2,                                                      \
                   @"The raw instruction must carry the second architectural register operand");   \
}

#define TCTI_DECLARE_VECTOR_COMPARE_DECODE_TEST(_name, _insn, _rd, _rn, _rm, _vecBytes)          \
- (void)testDecodeContract_##_name                                                                 \
{                                                                                                  \
    a64_instr_t decoded = [self decodeInstruction:_insn];                                          \
    XCTAssertEqual(decoded.cat, A64_SIMD2);                                                        \
    XCTAssertEqual(decoded.Rd, _rd);                                                               \
    XCTAssertEqual(decoded.Rn, _rn);                                                               \
    XCTAssertEqual(decoded.Rm, _rm);                                                               \
    XCTAssertEqual(decoded.vec_bytes, _vecBytes);                                                  \
    XCTAssertTrue(decoded.is_vector);                                                              \
}

@implementation TCTIPipelineDecodeTests

- (a64_instr_t)decodeInstruction:(uint32_t)insn
{
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(insn, &decoded), 0);
    return decoded;
}

- (void)testDecodeContract_ADRPClassifiesAsPCRelativePageAddressing
{
    uint32_t adrpX0LocalPage = 0x90000000;

    a64_instr_t decoded = [self decodeInstruction:adrpX0LocalPage];
    XCTAssertEqual(decoded.cat, A64_SIMD0);
    XCTAssertEqual(decoded.subtype, A64_DP_IMM_PC_REL);
    XCTAssertEqual(decoded.Rd, 0);
    XCTAssertEqual(decoded.imm, 0LL,
                   @"ADRP with a zero immediate page delta must remain a page-relative decode "
                    "instead of falling into a generic immediate bucket");
}

- (void)testDecodeContract_CSELNormalizesConditionalSelectFamilyAndWidth
{
    uint32_t cselX2X2XzrNe = 0x9a9f1042;

    a64_instr_t decoded = [self decodeInstruction:cselX2X2XzrNe];
    XCTAssertEqual(decoded.cat, A64_DP_IMM2);
    XCTAssertEqual(decoded.subtype, 0);
    XCTAssertEqual(decoded.Rd, 2);
    XCTAssertEqual(decoded.Rn, 2);
    XCTAssertEqual(decoded.Rm, 31);
    XCTAssertEqual(decoded.cond, A64_NE);
    XCTAssertTrue(decoded.is_64bit,
                  @"64-bit CSEL aliases must stay width-correct at decode time so later "
                   "lowering and writeback do not leak W-width semantics");
}

- (void)testDecodeContract_BNECarriesSignedImmediateAndCondition
{
    uint32_t bneBackToDlstartClear = 0x54ffffc1;

    a64_instr_t decoded = [self decodeInstruction:bneBackToDlstartClear];
    XCTAssertEqual(decoded.cat, A64_BRANCH);
    XCTAssertEqual(decoded.subtype, A64_BRANCH_COND);
    XCTAssertEqual(decoded.cond, A64_NE);
    XCTAssertEqual(decoded.imm, -8LL,
                   @"Conditional branches must publish the signed PC-relative delta expected by "
                    "the real dispatcher, not the raw encoded field");
}

- (void)testDecodeContract_LDAXRClassifiesAsAtomicBeforeImm9SingleRegisterFallback
{
    uint32_t ldaxrW0X3 = 0x885ffc60;

    a64_instr_t decoded = [self decodeInstruction:ldaxrW0X3];
    XCTAssertEqual(decoded.cat, A64_LD_ST);
    XCTAssertEqual(decoded.subtype, A64_LDST_ATOMIC);
    XCTAssertEqual(decoded.Rd, 0);
    XCTAssertEqual(decoded.Rn, 3);
    XCTAssertEqual(decoded.Rm, 31);
    XCTAssertEqual(decoded.size, A64_SIZE_W);
    XCTAssertFalse(decoded.is_64bit,
                   @"LDAXR must stay in the atomic family and must not be misdecoded as an imm9 "
                    "single-register load/store form");
}

- (void)testDecodeContract_CASPublishesCompareAndSwapRegisterRolesThroughAtomicFamily
{
    uint32_t casW4W5X6 = 0x88a47cc5;

    a64_instr_t decoded = [self decodeInstruction:casW4W5X6];
    XCTAssertEqual(decoded.cat, A64_LD_ST);
    XCTAssertEqual(decoded.subtype, A64_LDST_ATOMIC);
    XCTAssertEqual(decoded.Rd, 5);
    XCTAssertEqual(decoded.Rn, 6);
    XCTAssertEqual(decoded.Rm, 4);
    XCTAssertEqual(decoded.size, A64_SIZE_W);
    XCTAssertFalse(decoded.is_64bit,
                   @"CAS must preserve separate compare/result and swap-value register roles at "
                    "decode time so the helper can publish the architectural old value");
}

- (void)testDecodeContract_DUPRepresentativePublishesSIMDFamilyShape
{
    uint32_t dupV0_16bW1 = 0x4e010c20;

    a64_instr_t decoded = [self decodeInstruction:dupV0_16bW1];
    XCTAssertEqual(decoded.cat, A64_SIMD);
    XCTAssertEqual(decoded.subtype, A64_SIMD_DUP_GPR);
    XCTAssertEqual(decoded.Rd, 0);
    XCTAssertEqual(decoded.Rn, 1);
    XCTAssertEqual(decoded.vec_bytes, 1);
    XCTAssertFalse(decoded.is_64bit,
                   @"Representative SIMD DUP decode must keep the byte-lane replication shape "
                    "used by the real TCTI vector path");
}

- (void)testDecodeContract_CMEQClassifiesAsVectorEqualityMaskOperation
{
    uint32_t cmeqV0V1V2 = 0x6e228c20;

    a64_instr_t decoded = [self decodeInstruction:cmeqV0V1V2];
    XCTAssertEqual(decoded.cat, A64_SIMD2);
    XCTAssertEqual(decoded.subtype, A64_SIMD_CMEQ);
    XCTAssertEqual(decoded.Rd, 0);
    XCTAssertEqual(decoded.Rn, 1);
    XCTAssertEqual(decoded.Rm, 2);
    XCTAssertEqual(decoded.vec_bytes, 16);
}

- (void)testDecodeContract_CMGTClassifiesAsSignedVectorComparisonOperation
{
    uint32_t cmgtV3V4V5 = 0x4e253483;

    a64_instr_t decoded = [self decodeInstruction:cmgtV3V4V5];
    XCTAssertEqual(decoded.cat, A64_SIMD2);
    XCTAssertEqual(decoded.subtype, A64_SIMD_CMGT);
    XCTAssertEqual(decoded.Rd, 3);
    XCTAssertEqual(decoded.Rn, 4);
    XCTAssertEqual(decoded.Rm, 5);
    XCTAssertEqual(decoded.vec_bytes, 16);
}

- (void)testDecodeContract_MLAClassifiesAsVectorAccumulateOperation
{
    uint32_t mlaV6V7V8 = 0x4e2894e6;

    a64_instr_t decoded = [self decodeInstruction:mlaV6V7V8];
    XCTAssertEqual(decoded.cat, A64_SIMD2);
    XCTAssertEqual(decoded.subtype, A64_SIMD_MLA);
    XCTAssertEqual(decoded.Rd, 6);
    XCTAssertEqual(decoded.Rn, 7);
    XCTAssertEqual(decoded.Rm, 8);
    XCTAssertEqual(decoded.vec_bytes, 16);
}

- (void)testDecodeContract_MLSClassifiesAsVectorMultiplySubtractOperation
{
    uint32_t mlsV9V10V11 = 0x6e2b9549;

    a64_instr_t decoded = [self decodeInstruction:mlsV9V10V11];
    XCTAssertEqual(decoded.cat, A64_SIMD2);
    XCTAssertEqual(decoded.subtype, A64_SIMD_MLS);
    XCTAssertEqual(decoded.Rd, 9);
    XCTAssertEqual(decoded.Rn, 10);
    XCTAssertEqual(decoded.Rm, 11);
    XCTAssertEqual(decoded.vec_bytes, 16);
}

- (void)testDecodeContract_PACIARejectsUntilArm64eDecodeOwnershipExists
{
    uint32_t paciaX0X1 = 0xdac10020;
    a64_instr_t decoded;

    XCTAssertLessThan(a64_decode(paciaX0X1, &decoded), 0,
                      @"arm64e PAC decode must reject explicitly until the decode/generation "
                       "surface owns that family instead of aliasing an older scalar form");
}

- (void)testDecodeContract_AESERejectsUntilCryptoDecodeOwnershipExists
{
    uint32_t aeseV0V1 = 0x4e284820;
    a64_instr_t decoded;

    XCTAssertLessThan(a64_decode(aeseV0V1, &decoded), 0,
                      @"Crypto-family decode must reject explicitly until AESE ownership exists "
                       "instead of falling through a generic SIMD form");
}

- (void)testDecodeContract_LDGClassifiesAsSingleRegisterLoadUntilTaggingSemanticsRejectLater
{
    uint32_t ldgX0X1 = 0xd9600020;
    a64_instr_t decoded = [self decodeInstruction:ldgX0X1];

    XCTAssertEqual(decoded.cat, A64_LD_ST);
    XCTAssertEqual(decoded.Rd, 0);
    XCTAssertEqual(decoded.raw, ldgX0X1);
    XCTAssertNotEqual(decoded.subtype, A64_LDST_ATOMIC,
                      @"LDG must remain explicitly classified as a non-atomic load/store form "
                       "so the later unsupported-policy path can reject it intentionally");
}

TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDAPR, 0xb8bfc020, 0, 1, 31, A64_SIZE_W, NO)
TCTI_DECLARE_PAIR_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDXP, 0x887f0440, 2, A64_SIZE_W, NO, 1)
TCTI_DECLARE_PAIR_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STXP, 0x88200861, 3, A64_SIZE_W, NO, 2)
TCTI_DECLARE_PAIR_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDAXP, 0x887f8440, 2, A64_SIZE_W, NO, 1)
TCTI_DECLARE_PAIR_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STLXP, 0x88208861, 3, A64_SIZE_W, NO, 2)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_SWP, 0xb8208041, 1, 2, 0, A64_SIZE_W, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDADD, 0xb8200041, 1, 2, 0, A64_SIZE_W, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDCLR, 0xb8201041, 1, 2, 0, A64_SIZE_W, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDEOR, 0xb8202041, 1, 2, 0, A64_SIZE_W, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDSET, 0xb8203041, 1, 2, 0, A64_SIZE_W, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDSMAX, 0xb8204041, 1, 2, 0, A64_SIZE_W, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDSMIN, 0xb8205041, 1, 2, 0, A64_SIZE_W, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDUMAX, 0xb8206041, 1, 2, 0, A64_SIZE_W, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDUMIN, 0xb8207041, 1, 2, 0, A64_SIZE_W, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STADD, 0xb820005f, 31, 2, 0, A64_SIZE_W, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STCLR, 0xb820105f, 31, 2, 0, A64_SIZE_W, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STEOR, 0xb820205f, 31, 2, 0, A64_SIZE_W, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STSET, 0xb820305f, 31, 2, 0, A64_SIZE_W, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STSMAX, 0xb820405f, 31, 2, 0, A64_SIZE_W, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STSMIN, 0xb820505f, 31, 2, 0, A64_SIZE_W, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STUMAX, 0xb820605f, 31, 2, 0, A64_SIZE_W, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STUMIN, 0xb820705f, 31, 2, 0, A64_SIZE_W, NO)

TCTI_DECLARE_VECTOR_COMPARE_DECODE_TEST(VectorIntegerLogical_CMGE, 0x4e223c20, 0, 1, 2, 16)
TCTI_DECLARE_VECTOR_COMPARE_DECODE_TEST(VectorIntegerLogical_CMHI, 0x6e223420, 0, 1, 2, 16)
TCTI_DECLARE_VECTOR_COMPARE_DECODE_TEST(VectorIntegerLogical_CMHS, 0x6e223c20, 0, 1, 2, 16)
TCTI_DECLARE_VECTOR_COMPARE_DECODE_TEST(VectorIntegerLogical_CMLE, 0x6e209820, 0, 1, 0, 16)
TCTI_DECLARE_VECTOR_COMPARE_DECODE_TEST(VectorIntegerLogical_CMLT, 0x4e20a820, 0, 1, 0, 16)
TCTI_DECLARE_VECTOR_COMPARE_DECODE_TEST(VectorIntegerLogical_CMTST, 0x4e228c20, 0, 1, 2, 16)

@end
