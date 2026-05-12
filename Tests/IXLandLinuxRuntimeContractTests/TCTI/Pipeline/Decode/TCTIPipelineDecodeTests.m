#import <XCTest/XCTest.h>

#import <IXLandLinuxRuntime/emu/aarch64/decode.h>

@interface TCTIPipelineDecodeTests : XCTestCase
@end

#define TCTI_DECLARE_COND_BRANCH_DECODE_TEST(_name, _insn, _cond)                                 \
- (void)testDecodeContract_##_name                                                                 \
{                                                                                                  \
    a64_instr_t decoded = [self decodeInstruction:_insn];                                          \
    XCTAssertEqual(decoded.cat, A64_BRANCH);                                                       \
    XCTAssertEqual(decoded.subtype, A64_BRANCH_COND);                                              \
    XCTAssertEqual(decoded.cond, _cond);                                                           \
    XCTAssertEqual(decoded.imm, 0LL);                                                              \
}

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

#define TCTI_DECLARE_SCALAR_LDST_DECODE_TEST(_name, _insn, _rd, _rn, _size, _is64, _isSigned,    \
                                             _idxMode, _imm)                                       \
- (void)testDecodeContract_##_name                                                                 \
{                                                                                                  \
    a64_instr_t decoded = [self decodeInstruction:_insn];                                          \
    XCTAssertEqual(decoded.cat, A64_LD_ST);                                                        \
    XCTAssertEqual(decoded.subtype, A64_LDST_SINGLE);                                              \
    XCTAssertEqual(decoded.Rd, _rd);                                                               \
    XCTAssertEqual(decoded.Rn, _rn);                                                               \
    XCTAssertEqual(decoded.size, _size);                                                           \
    XCTAssertEqual(decoded.is_64bit, _is64);                                                       \
    XCTAssertEqual(decoded.is_signed, _isSigned);                                                  \
    XCTAssertEqual(decoded.idx_mode, _idxMode);                                                    \
    XCTAssertEqual(decoded.imm, _imm);                                                             \
    XCTAssertFalse(decoded.is_vector);                                                             \
    XCTAssertNotEqual(decoded.subtype, A64_LDST_ATOMIC);                                           \
}

#define TCTI_DECLARE_PREFETCH_DECODE_TEST(_name, _insn, _rn, _idxMode, _imm)                      \
- (void)testDecodeContract_##_name                                                                 \
{                                                                                                  \
    a64_instr_t decoded = [self decodeInstruction:_insn];                                          \
    XCTAssertEqual(decoded.cat, A64_LD_ST);                                                        \
    XCTAssertEqual(decoded.subtype, A64_LDST_SINGLE);                                              \
    XCTAssertEqual(decoded.Rn, _rn);                                                               \
    XCTAssertEqual(decoded.idx_mode, _idxMode);                                                    \
    XCTAssertEqual(decoded.imm, _imm);                                                             \
    XCTAssertFalse(decoded.is_vector);                                                             \
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

#define TCTI_DECLARE_VECTOR_SUBTYPE_DECODE_TEST(_name, _insn, _subtype, _rd, _rn, _rm, _vecBytes) \
- (void)testDecodeContract_##_name                                                                  \
{                                                                                                   \
    a64_instr_t decoded = [self decodeInstruction:_insn];                                           \
    XCTAssertEqual(decoded.cat, A64_SIMD2);                                                         \
    XCTAssertEqual(decoded.subtype, _subtype);                                                      \
    XCTAssertEqual(decoded.Rd, _rd);                                                                \
    XCTAssertEqual(decoded.Rn, _rn);                                                                \
    XCTAssertEqual(decoded.Rm, _rm);                                                                \
    XCTAssertEqual(decoded.vec_bytes, _vecBytes);                                                   \
    XCTAssertTrue(decoded.is_vector);                                                               \
}

#define TCTI_DECLARE_VECTOR_UNARY_DECODE_TEST(_name, _insn, _subtype, _rd, _rn, _vecBytes)       \
- (void)testDecodeContract_##_name                                                                 \
{                                                                                                  \
    a64_instr_t decoded = [self decodeInstruction:_insn];                                          \
    XCTAssertEqual(decoded.cat, A64_SIMD);                                                         \
    XCTAssertEqual(decoded.subtype, _subtype);                                                     \
    XCTAssertEqual(decoded.Rd, _rd);                                                               \
    XCTAssertEqual(decoded.Rn, _rn);                                                               \
    XCTAssertEqual(decoded.vec_bytes, _vecBytes);                                                  \
}

#define TCTI_DECLARE_UNSUPPORTED_REJECT_DECODE_TEST(_name, _insn)                                 \
- (void)testDecodeContract_##_name                                                                 \
{                                                                                                  \
    a64_instr_t decoded;                                                                           \
    XCTAssertLessThan(a64_decode(_insn, &decoded), 0,                                              \
                      @"Unsupported-family decode must reject `%s` explicitly instead of "         \
                       @"silently aliasing an older form", #_name);                                \
}

#define TCTI_DECLARE_TAGGING_CLASSIFY_DECODE_TEST(_name, _insn, _rd, _rn)                         \
- (void)testDecodeContract_##_name                                                                 \
{                                                                                                  \
    a64_instr_t decoded = [self decodeInstruction:_insn];                                          \
    XCTAssertEqual(decoded.cat, A64_LD_ST);                                                        \
    XCTAssertEqual(decoded.Rd, _rd);                                                               \
    XCTAssertEqual(decoded.Rn, _rn);                                                               \
    XCTAssertNotEqual(decoded.subtype, A64_LDST_ATOMIC);                                           \
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

TCTI_DECLARE_COND_BRANCH_DECODE_TEST(BEQCarriesConditionCode, 0x54000000, 0)
TCTI_DECLARE_COND_BRANCH_DECODE_TEST(BNEZeroOffsetCarriesConditionCode, 0x54000001, 1)
TCTI_DECLARE_COND_BRANCH_DECODE_TEST(BHSZeroOffsetCarriesConditionCode, 0x54000002, 2)
TCTI_DECLARE_COND_BRANCH_DECODE_TEST(BLOZeroOffsetCarriesConditionCode, 0x54000003, 3)
TCTI_DECLARE_COND_BRANCH_DECODE_TEST(BMIZeroOffsetCarriesConditionCode, 0x54000004, 4)
TCTI_DECLARE_COND_BRANCH_DECODE_TEST(BPLZeroOffsetCarriesConditionCode, 0x54000005, 5)
TCTI_DECLARE_COND_BRANCH_DECODE_TEST(BVSZeroOffsetCarriesConditionCode, 0x54000006, 6)
TCTI_DECLARE_COND_BRANCH_DECODE_TEST(BVCZeroOffsetCarriesConditionCode, 0x54000007, 7)
TCTI_DECLARE_COND_BRANCH_DECODE_TEST(BHIZeroOffsetCarriesConditionCode, 0x54000008, 8)
TCTI_DECLARE_COND_BRANCH_DECODE_TEST(BLSZeroOffsetCarriesConditionCode, 0x54000009, 9)
TCTI_DECLARE_COND_BRANCH_DECODE_TEST(BGEZeroOffsetCarriesConditionCode, 0x5400000a, 10)
TCTI_DECLARE_COND_BRANCH_DECODE_TEST(BLTZeroOffsetCarriesConditionCode, 0x5400000b, 11)
TCTI_DECLARE_COND_BRANCH_DECODE_TEST(BGTZeroOffsetCarriesConditionCode, 0x5400000c, 12)
TCTI_DECLARE_COND_BRANCH_DECODE_TEST(BLEZeroOffsetCarriesConditionCode, 0x5400000d, 13)

TCTI_DECLARE_COND_BRANCH_DECODE_TEST(BCEQCarriesConditionCode, 0x54000010, 0)
TCTI_DECLARE_COND_BRANCH_DECODE_TEST(BCNEZeroOffsetCarriesConditionCode, 0x54000011, 1)
TCTI_DECLARE_COND_BRANCH_DECODE_TEST(BCSHSZeroOffsetCarriesConditionCode, 0x54000012, 2)
TCTI_DECLARE_COND_BRANCH_DECODE_TEST(BCCLOZeroOffsetCarriesConditionCode, 0x54000013, 3)
TCTI_DECLARE_COND_BRANCH_DECODE_TEST(BCMIZeroOffsetCarriesConditionCode, 0x54000014, 4)
TCTI_DECLARE_COND_BRANCH_DECODE_TEST(BCPLZeroOffsetCarriesConditionCode, 0x54000015, 5)
TCTI_DECLARE_COND_BRANCH_DECODE_TEST(BCVSZeroOffsetCarriesConditionCode, 0x54000016, 6)
TCTI_DECLARE_COND_BRANCH_DECODE_TEST(BCVCZeroOffsetCarriesConditionCode, 0x54000017, 7)
TCTI_DECLARE_COND_BRANCH_DECODE_TEST(BCHIZeroOffsetCarriesConditionCode, 0x54000018, 8)
TCTI_DECLARE_COND_BRANCH_DECODE_TEST(BCLSZeroOffsetCarriesConditionCode, 0x54000019, 9)
TCTI_DECLARE_COND_BRANCH_DECODE_TEST(BCGEZeroOffsetCarriesConditionCode, 0x5400001a, 10)
TCTI_DECLARE_COND_BRANCH_DECODE_TEST(BCLTZeroOffsetCarriesConditionCode, 0x5400001b, 11)
TCTI_DECLARE_COND_BRANCH_DECODE_TEST(BCGTZeroOffsetCarriesConditionCode, 0x5400001c, 12)
TCTI_DECLARE_COND_BRANCH_DECODE_TEST(BCLEZeroOffsetCarriesConditionCode, 0x5400001d, 13)

- (void)testDecodeContract_REVWClassifiesAsOneSourceBitPermutation
{
    a64_instr_t decoded = [self decodeInstruction:0x5ac00820];
    XCTAssertEqual(decoded.cat, A64_DP_REG);
    XCTAssertEqual(decoded.Rd, 0);
    XCTAssertEqual(decoded.Rn, 1);
    XCTAssertFalse(decoded.is_64bit);
}

- (void)testDecodeContract_REVXClassifiesAsOneSourceBitPermutation
{
    a64_instr_t decoded = [self decodeInstruction:0xdac00c62];
    XCTAssertEqual(decoded.cat, A64_DP_REG);
    XCTAssertEqual(decoded.Rd, 2);
    XCTAssertEqual(decoded.Rn, 3);
    XCTAssertTrue(decoded.is_64bit);
}

- (void)testDecodeContract_REV16WClassifiesAsOneSourceBitPermutation
{
    a64_instr_t decoded = [self decodeInstruction:0x5ac004a4];
    XCTAssertEqual(decoded.cat, A64_DP_REG);
    XCTAssertEqual(decoded.Rd, 4);
    XCTAssertEqual(decoded.Rn, 5);
    XCTAssertFalse(decoded.is_64bit);
}

- (void)testDecodeContract_REV16XClassifiesAsOneSourceBitPermutation
{
    a64_instr_t decoded = [self decodeInstruction:0xdac004e6];
    XCTAssertEqual(decoded.cat, A64_DP_REG);
    XCTAssertEqual(decoded.Rd, 6);
    XCTAssertEqual(decoded.Rn, 7);
    XCTAssertTrue(decoded.is_64bit);
}

- (void)testDecodeContract_REV32XClassifiesAsOneSourceBitPermutation
{
    a64_instr_t decoded = [self decodeInstruction:0xdac00928];
    XCTAssertEqual(decoded.cat, A64_DP_REG);
    XCTAssertEqual(decoded.Rd, 8);
    XCTAssertEqual(decoded.Rn, 9);
    XCTAssertTrue(decoded.is_64bit);
}

- (void)testDecodeContract_VectorMemory_LD1ClassifiesAsVectorLoadStore
{
    a64_instr_t decoded = [self decodeInstruction:0x4c407020];
    XCTAssertEqual(decoded.cat, A64_LD_ST);
    XCTAssertTrue(decoded.is_vector);
}

- (void)testDecodeContract_VectorMemory_LD1RClassifiesAsVectorLoadStore
{
    a64_instr_t decoded = [self decodeInstruction:0x4d40c062];
    XCTAssertEqual(decoded.cat, A64_LD_ST);
    XCTAssertTrue(decoded.is_vector);
}

- (void)testDecodeContract_VectorMemory_LD2ClassifiesAsVectorLoadStore
{
    a64_instr_t decoded = [self decodeInstruction:0x4c4080c4];
    XCTAssertEqual(decoded.cat, A64_LD_ST);
    XCTAssertTrue(decoded.is_vector);
}

- (void)testDecodeContract_VectorMemory_LD3ClassifiesAsVectorLoadStore
{
    a64_instr_t decoded = [self decodeInstruction:0x4c404147];
    XCTAssertEqual(decoded.cat, A64_LD_ST);
    XCTAssertTrue(decoded.is_vector);
}

- (void)testDecodeContract_VectorMemory_LD4ClassifiesAsVectorLoadStore
{
    a64_instr_t decoded = [self decodeInstruction:0x4c4001eb];
    XCTAssertEqual(decoded.cat, A64_LD_ST);
    XCTAssertTrue(decoded.is_vector);
}

- (void)testDecodeContract_VectorMemory_LDURQClassifiesAsVectorLoadStore
{
    a64_instr_t decoded = [self decodeInstruction:0x3cdf0020];
    XCTAssertEqual(decoded.cat, A64_LD_ST);
    XCTAssertTrue(decoded.is_vector);
}

- (void)testDecodeContract_VectorMemory_LDPQClassifiesAsVectorLoadStore
{
    a64_instr_t decoded = [self decodeInstruction:0xad400440];
    XCTAssertEqual(decoded.cat, A64_LD_ST);
    XCTAssertTrue(decoded.is_vector);
    XCTAssertTrue(decoded.is_pair);
}

- (void)testDecodeContract_VectorMemory_ST2ClassifiesAsVectorLoadStore
{
    a64_instr_t decoded = [self decodeInstruction:0x4c008292];
    XCTAssertEqual(decoded.cat, A64_LD_ST);
    XCTAssertTrue(decoded.is_vector);
}

- (void)testDecodeContract_VectorMemory_ST3ClassifiesAsVectorLoadStore
{
    a64_instr_t decoded = [self decodeInstruction:0x4c004315];
    XCTAssertEqual(decoded.cat, A64_LD_ST);
    XCTAssertTrue(decoded.is_vector);
}

- (void)testDecodeContract_VectorMemory_ST4ClassifiesAsVectorLoadStore
{
    a64_instr_t decoded = [self decodeInstruction:0x4c0003b9];
    XCTAssertEqual(decoded.cat, A64_LD_ST);
    XCTAssertTrue(decoded.is_vector);
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

TCTI_DECLARE_UNSUPPORTED_REJECT_DECODE_TEST(Arm64e_PACIBRejectsUntilArm64eDecodeOwnershipExists, 0xdac10462)
TCTI_DECLARE_UNSUPPORTED_REJECT_DECODE_TEST(Arm64e_AUTIBRejectsUntilArm64eDecodeOwnershipExists, 0xdac114e6)
TCTI_DECLARE_UNSUPPORTED_REJECT_DECODE_TEST(Arm64e_XPACIRejectsUntilArm64eDecodeOwnershipExists, 0xdac143e8)
TCTI_DECLARE_UNSUPPORTED_REJECT_DECODE_TEST(Arm64e_XPACDRejectsUntilArm64eDecodeOwnershipExists, 0xdac147e9)
TCTI_DECLARE_UNSUPPORTED_REJECT_DECODE_TEST(Arm64e_BRAARejectsUntilArm64eDecodeOwnershipExists, 0xd71f094b)
TCTI_DECLARE_UNSUPPORTED_REJECT_DECODE_TEST(Arm64e_BRABRejectsUntilArm64eDecodeOwnershipExists, 0xd71f0d8d)
TCTI_DECLARE_UNSUPPORTED_REJECT_DECODE_TEST(Arm64e_BLRAARejectsUntilArm64eDecodeOwnershipExists, 0xd73f09cf)
TCTI_DECLARE_UNSUPPORTED_REJECT_DECODE_TEST(Arm64e_BLRABRejectsUntilArm64eDecodeOwnershipExists, 0xd73f0e11)
TCTI_DECLARE_UNSUPPORTED_REJECT_DECODE_TEST(Arm64e_RETAARejectsUntilArm64eDecodeOwnershipExists, 0xd65f0bff)
TCTI_DECLARE_UNSUPPORTED_REJECT_DECODE_TEST(Arm64e_RETABRejectsUntilArm64eDecodeOwnershipExists, 0xd65f0fff)
TCTI_DECLARE_UNSUPPORTED_REJECT_DECODE_TEST(Arm64e_RETAASPPCRRejectsUntilArm64eDecodeOwnershipExists, 0xd65f0bfe)
TCTI_DECLARE_UNSUPPORTED_REJECT_DECODE_TEST(Arm64e_RETABSPPCRRejectsUntilArm64eDecodeOwnershipExists, 0xd65f0ffe)
TCTI_DECLARE_UNSUPPORTED_REJECT_DECODE_TEST(Arm64e_LDRAARejectsUntilArm64eDecodeOwnershipExists, 0xf8200420)
TCTI_DECLARE_UNSUPPORTED_REJECT_DECODE_TEST(Arm64e_LDRABRejectsUntilArm64eDecodeOwnershipExists, 0xf8a00462)
TCTI_DECLARE_UNSUPPORTED_REJECT_DECODE_TEST(Arm64e_BTIRejectsUntilArm64eDecodeOwnershipExists, 0xd503245f)

TCTI_DECLARE_UNSUPPORTED_REJECT_DECODE_TEST(Crypto_AESDRejectsUntilCryptoDecodeOwnershipExists, 0x4e285862)
TCTI_DECLARE_UNSUPPORTED_REJECT_DECODE_TEST(Crypto_AESMCRejectsUntilCryptoDecodeOwnershipExists, 0x4e2868a4)
TCTI_DECLARE_UNSUPPORTED_REJECT_DECODE_TEST(Crypto_AESIMCRejectsUntilCryptoDecodeOwnershipExists, 0x4e2878e6)
TCTI_DECLARE_UNSUPPORTED_REJECT_DECODE_TEST(Crypto_PMULRejectsUntilCryptoDecodeOwnershipExists, 0x2e229c20)
TCTI_DECLARE_UNSUPPORTED_REJECT_DECODE_TEST(Crypto_SHA1CRejectsUntilCryptoDecodeOwnershipExists, 0x5e020020)
TCTI_DECLARE_UNSUPPORTED_REJECT_DECODE_TEST(Crypto_SHA1MRejectsUntilCryptoDecodeOwnershipExists, 0x5e022020)
TCTI_DECLARE_UNSUPPORTED_REJECT_DECODE_TEST(Crypto_SHA1PRejectsUntilCryptoDecodeOwnershipExists, 0x5e051083)
TCTI_DECLARE_UNSUPPORTED_REJECT_DECODE_TEST(Crypto_SHA1SU0RejectsUntilCryptoDecodeOwnershipExists, 0x5e0830e6)
TCTI_DECLARE_UNSUPPORTED_REJECT_DECODE_TEST(Crypto_SHA1SU1RejectsUntilCryptoDecodeOwnershipExists, 0x5e281949)
TCTI_DECLARE_UNSUPPORTED_REJECT_DECODE_TEST(Crypto_SHA256HRejectsUntilCryptoDecodeOwnershipExists, 0x5e024020)
TCTI_DECLARE_UNSUPPORTED_REJECT_DECODE_TEST(Crypto_SHA256H2RejectsUntilCryptoDecodeOwnershipExists, 0x5e1051ee)
TCTI_DECLARE_UNSUPPORTED_REJECT_DECODE_TEST(Crypto_SHA256SU0RejectsUntilCryptoDecodeOwnershipExists, 0x5e282a51)
TCTI_DECLARE_UNSUPPORTED_REJECT_DECODE_TEST(Crypto_SHA256SU1RejectsUntilCryptoDecodeOwnershipExists, 0x5e156293)
TCTI_DECLARE_UNSUPPORTED_REJECT_DECODE_TEST(Crypto_SM3PARTW1RejectsUntilCryptoDecodeOwnershipExists, 0xce62c020)
TCTI_DECLARE_UNSUPPORTED_REJECT_DECODE_TEST(Crypto_SM3PARTW2RejectsUntilCryptoDecodeOwnershipExists, 0xce65c483)
TCTI_DECLARE_UNSUPPORTED_REJECT_DECODE_TEST(Crypto_SM3SS1RejectsUntilCryptoDecodeOwnershipExists, 0xce4824e6)
TCTI_DECLARE_UNSUPPORTED_REJECT_DECODE_TEST(Crypto_SM3TT1ARejectsUntilCryptoDecodeOwnershipExists, 0xce4c816a)
TCTI_DECLARE_UNSUPPORTED_REJECT_DECODE_TEST(Crypto_SM3TT1BRejectsUntilCryptoDecodeOwnershipExists, 0xce4f95cd)
TCTI_DECLARE_UNSUPPORTED_REJECT_DECODE_TEST(Crypto_SM3TT2ARejectsUntilCryptoDecodeOwnershipExists, 0xce52aa30)
TCTI_DECLARE_UNSUPPORTED_REJECT_DECODE_TEST(Crypto_SM3TT2BRejectsUntilCryptoDecodeOwnershipExists, 0xce55be93)
TCTI_DECLARE_UNSUPPORTED_REJECT_DECODE_TEST(Crypto_SM4ERejectsUntilCryptoDecodeOwnershipExists, 0xcec086f6)
TCTI_DECLARE_UNSUPPORTED_REJECT_DECODE_TEST(Crypto_SM4EKEYRejectsUntilCryptoDecodeOwnershipExists, 0xce7acb38)
TCTI_DECLARE_UNSUPPORTED_REJECT_DECODE_TEST(Crypto_SDOTRejectsUntilCryptoDecodeOwnershipExists, 0x4e829420)
TCTI_DECLARE_UNSUPPORTED_REJECT_DECODE_TEST(Crypto_UDOTRejectsUntilCryptoDecodeOwnershipExists, 0x6e859483)
TCTI_DECLARE_UNSUPPORTED_REJECT_DECODE_TEST(Crypto_USDOTRejectsUntilCryptoDecodeOwnershipExists, 0x4e889ce6)
TCTI_DECLARE_UNSUPPORTED_REJECT_DECODE_TEST(Crypto_SUDOTRejectsUntilCryptoDecodeOwnershipExists, 0x0f0ef1ac)
TCTI_DECLARE_UNSUPPORTED_REJECT_DECODE_TEST(Crypto_FDOTRejectsUntilCryptoDecodeOwnershipExists, 0x0e02fc20)
TCTI_DECLARE_UNSUPPORTED_REJECT_DECODE_TEST(Crypto_BFDOTRejectsUntilCryptoDecodeOwnershipExists, 0x2e48fce6)
TCTI_DECLARE_UNSUPPORTED_REJECT_DECODE_TEST(Crypto_BCAXRejectsUntilCryptoDecodeOwnershipExists, 0xce2d398b)
TCTI_DECLARE_UNSUPPORTED_REJECT_DECODE_TEST(Crypto_XARRejectsUntilCryptoDecodeOwnershipExists, 0xce911e0f)

TCTI_DECLARE_TAGGING_CLASSIFY_DECODE_TEST(Tagging_LD64BClassifiesAsLoadStoreUntilRejectLater, 0xf83fd020, 0, 1)
TCTI_DECLARE_TAGGING_CLASSIFY_DECODE_TEST(Tagging_ST64BClassifiesAsLoadStoreUntilRejectLater, 0xf83f9062, 2, 3)
TCTI_DECLARE_TAGGING_CLASSIFY_DECODE_TEST(Tagging_LDGMClassifiesAsLoadStoreUntilRejectLater, 0xd9e000a4, 4, 5)
TCTI_DECLARE_TAGGING_CLASSIFY_DECODE_TEST(Tagging_STGClassifiesAsLoadStoreUntilRejectLater, 0xd92008e6, 6, 7)
TCTI_DECLARE_TAGGING_CLASSIFY_DECODE_TEST(Tagging_STGMClassifiesAsLoadStoreUntilRejectLater, 0xd9a00128, 8, 9)
TCTI_DECLARE_TAGGING_CLASSIFY_DECODE_TEST(Tagging_STZGClassifiesAsLoadStoreUntilRejectLater, 0xd960096a, 10, 11)
TCTI_DECLARE_TAGGING_CLASSIFY_DECODE_TEST(Tagging_STZGMClassifiesAsLoadStoreUntilRejectLater, 0xd92001ac, 12, 13)
TCTI_DECLARE_TAGGING_CLASSIFY_DECODE_TEST(Tagging_ST2GClassifiesAsLoadStoreUntilRejectLater, 0xd9a009ee, 14, 15)
TCTI_DECLARE_TAGGING_CLASSIFY_DECODE_TEST(Tagging_STZ2GClassifiesAsLoadStoreUntilRejectLater, 0xd9e00a30, 16, 17)

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

TCTI_DECLARE_SCALAR_LDST_DECODE_TEST(ScalarLoadStore_LDRB, 0x39400020, 0, 1, A64_SIZE_B, NO, NO,
                                     A64_INDEX_OFFSET, 0)
TCTI_DECLARE_SCALAR_LDST_DECODE_TEST(ScalarLoadStore_LDRSW, 0xb9800062, 2, 3, A64_SIZE_W, YES, YES,
                                     A64_INDEX_OFFSET, 0)
TCTI_DECLARE_SCALAR_LDST_DECODE_TEST(ScalarLoadStore_STRB, 0x390000a4, 4, 5, A64_SIZE_B, NO, NO,
                                     A64_INDEX_OFFSET, 0)
TCTI_DECLARE_SCALAR_LDST_DECODE_TEST(ScalarLoadStore_STRH, 0x790000e6, 6, 7, A64_SIZE_H, NO, NO,
                                     A64_INDEX_OFFSET, 0)
TCTI_DECLARE_SCALAR_LDST_DECODE_TEST(ScalarLoadStore_LDURH, 0x785ff128, 8, 9, A64_SIZE_H, NO, NO,
                                     A64_INDEX_OFFSET, -1)
TCTI_DECLARE_SCALAR_LDST_DECODE_TEST(ScalarLoadStore_LDURSB, 0x389ff16a, 10, 11, A64_SIZE_B, YES,
                                     YES, A64_INDEX_OFFSET, -1)
TCTI_DECLARE_SCALAR_LDST_DECODE_TEST(ScalarLoadStore_LDURSH, 0x789ff1ac, 12, 13, A64_SIZE_H, YES,
                                     YES, A64_INDEX_OFFSET, -1)
TCTI_DECLARE_SCALAR_LDST_DECODE_TEST(ScalarLoadStore_LDURSW, 0xb89fc1ee, 14, 15, A64_SIZE_W, YES,
                                     YES, A64_INDEX_OFFSET, -4)
TCTI_DECLARE_SCALAR_LDST_DECODE_TEST(ScalarLoadStore_STURB, 0x381ff230, 16, 17, A64_SIZE_B, NO, NO,
                                     A64_INDEX_OFFSET, -1)
TCTI_DECLARE_SCALAR_LDST_DECODE_TEST(ScalarLoadStore_STURH, 0x781fe272, 18, 19, A64_SIZE_H, NO, NO,
                                     A64_INDEX_OFFSET, -2)
TCTI_DECLARE_SCALAR_LDST_DECODE_TEST(ScalarLoadStore_LDTR, 0xf8400ab4, 20, 21, A64_SIZE_X, YES, NO,
                                     A64_INDEX_OFFSET, 0)
TCTI_DECLARE_SCALAR_LDST_DECODE_TEST(ScalarLoadStore_LDTRB, 0x38400af6, 22, 23, A64_SIZE_B, NO, NO,
                                     A64_INDEX_OFFSET, 0)
TCTI_DECLARE_SCALAR_LDST_DECODE_TEST(ScalarLoadStore_LDTRH, 0x78400b38, 24, 25, A64_SIZE_H, NO, NO,
                                     A64_INDEX_OFFSET, 0)
TCTI_DECLARE_SCALAR_LDST_DECODE_TEST(ScalarLoadStore_LDTRSB, 0x38800b7a, 26, 27, A64_SIZE_B, YES,
                                     YES, A64_INDEX_OFFSET, 0)
TCTI_DECLARE_SCALAR_LDST_DECODE_TEST(ScalarLoadStore_LDTRSH, 0x78800bbc, 28, 29, A64_SIZE_H, YES,
                                     YES, A64_INDEX_OFFSET, 0)
TCTI_DECLARE_SCALAR_LDST_DECODE_TEST(ScalarLoadStore_LDTRSW, 0xb8800820, 0, 1, A64_SIZE_W, YES, YES,
                                     A64_INDEX_OFFSET, 0)
TCTI_DECLARE_SCALAR_LDST_DECODE_TEST(ScalarLoadStore_STTR, 0xf8000862, 2, 3, A64_SIZE_X, YES, NO,
                                     A64_INDEX_OFFSET, 0)
TCTI_DECLARE_SCALAR_LDST_DECODE_TEST(ScalarLoadStore_STTRB, 0x380008a4, 4, 5, A64_SIZE_B, NO, NO,
                                     A64_INDEX_OFFSET, 0)
TCTI_DECLARE_SCALAR_LDST_DECODE_TEST(ScalarLoadStore_STTRH, 0x780008e6, 6, 7, A64_SIZE_H, NO, NO,
                                     A64_INDEX_OFFSET, 0)
TCTI_DECLARE_SCALAR_LDST_DECODE_TEST(ScalarLoadStore_LDAPUR, 0xd9400128, 8, 9, A64_SIZE_X, YES, NO,
                                     A64_INDEX_OFFSET, 0)
TCTI_DECLARE_SCALAR_LDST_DECODE_TEST(ScalarLoadStore_LDAPURB, 0x1940016a, 10, 11, A64_SIZE_B, NO,
                                     NO, A64_INDEX_OFFSET, 0)
TCTI_DECLARE_SCALAR_LDST_DECODE_TEST(ScalarLoadStore_LDAPURH, 0x594001ac, 12, 13, A64_SIZE_H, NO,
                                     NO, A64_INDEX_OFFSET, 0)
TCTI_DECLARE_SCALAR_LDST_DECODE_TEST(ScalarLoadStore_LDAPURSB, 0x198001ee, 14, 15, A64_SIZE_B, YES,
                                     YES, A64_INDEX_OFFSET, 0)
TCTI_DECLARE_SCALAR_LDST_DECODE_TEST(ScalarLoadStore_LDAPURSH, 0x59800230, 16, 17, A64_SIZE_H, YES,
                                     YES, A64_INDEX_OFFSET, 0)
TCTI_DECLARE_SCALAR_LDST_DECODE_TEST(ScalarLoadStore_LDAPURSW, 0x99800272, 18, 19, A64_SIZE_W, YES,
                                     YES, A64_INDEX_OFFSET, 0)
TCTI_DECLARE_SCALAR_LDST_DECODE_TEST(ScalarLoadStore_STLUR, 0xd90002b4, 20, 21, A64_SIZE_X, YES, NO,
                                     A64_INDEX_OFFSET, 0)
TCTI_DECLARE_SCALAR_LDST_DECODE_TEST(ScalarLoadStore_STLURB, 0x190002f6, 22, 23, A64_SIZE_B, NO, NO,
                                     A64_INDEX_OFFSET, 0)
TCTI_DECLARE_SCALAR_LDST_DECODE_TEST(ScalarLoadStore_STLURH, 0x59000338, 24, 25, A64_SIZE_H, NO, NO,
                                     A64_INDEX_OFFSET, 0)
TCTI_DECLARE_PREFETCH_DECODE_TEST(ScalarLoadStore_PRFM, 0xf9800000, 0, A64_INDEX_OFFSET, 0)
TCTI_DECLARE_PREFETCH_DECODE_TEST(ScalarLoadStore_PRFUM, 0xf8800020, 1, A64_INDEX_OFFSET, 0)

TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDARB, 0x08dffc20, 0, 1, 31, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDARH, 0x48dffc62, 2, 3, 31, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STLRB, 0x089ffca4, 4, 5, 31, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STLRH, 0x489ffce6, 6, 7, 31, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDAPRB, 0x38bfc128, 8, 9, 31, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDAPRH, 0x78bfc16a, 10, 11, 31, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDAXRB, 0x085ffdac, 12, 13, 31, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDAXRH, 0x485ffdee, 14, 15, 31, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STLXRB, 0x0810fe51, 17, 18, 16, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STLXRH, 0x4813feb4, 20, 21, 19, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDXRB, 0x085f7ef6, 22, 23, 31, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDXRH, 0x485f7f38, 24, 25, 31, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STXRB, 0x081a7f9b, 27, 28, 26, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STXRH, 0x48007c41, 1, 2, 0, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_CASB, 0x08a07c41, 1, 2, 0, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_CASH, 0x48a17c62, 2, 3, 1, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_CASAB, 0x08e37ca4, 4, 5, 3, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_CASAH, 0x48e47cc5, 5, 6, 4, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_CASLB, 0x08a6fd07, 7, 8, 6, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_CASLH, 0x48a7fd28, 8, 9, 7, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_CASALB, 0x08e9fd6a, 10, 11, 9, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_CASALH, 0x48eafd8b, 11, 12, 10, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_SWPB, 0x38208041, 1, 2, 0, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_SWPH, 0x78218062, 2, 3, 1, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_SWPAB, 0x38a380a4, 4, 5, 3, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_SWPAH, 0x78a480c5, 5, 6, 4, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_SWPLB, 0x38668107, 7, 8, 6, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_SWPLH, 0x78678128, 8, 9, 7, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_SWPALB, 0x38e9816a, 10, 11, 9, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_SWPALH, 0x78ea818b, 11, 12, 10, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDADDB, 0x38200041, 1, 2, 0, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDADDH, 0x78210062, 2, 3, 1, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDADDAB, 0x38a300a4, 4, 5, 3, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDADDAH, 0x78a400c5, 5, 6, 4, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDADDLB, 0x38660107, 7, 8, 6, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDADDLH, 0x78670128, 8, 9, 7, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDADDALB, 0x38e9016a, 10, 11, 9, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDADDALH, 0x78ea018b, 11, 12, 10, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDCLRB, 0x38201041, 1, 2, 0, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDCLRH, 0x78211062, 2, 3, 1, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDCLRAB, 0x38a310a4, 4, 5, 3, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDCLRAH, 0x78a410c5, 5, 6, 4, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDCLRLB, 0x38661107, 7, 8, 6, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDCLRLH, 0x78671128, 8, 9, 7, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDCLRALB, 0x38e9116a, 10, 11, 9, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDCLRALH, 0x78ea118b, 11, 12, 10, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDEORB, 0x38202041, 1, 2, 0, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDEORH, 0x78212062, 2, 3, 1, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDEORAB, 0x38a320a4, 4, 5, 3, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDEORAH, 0x78a420c5, 5, 6, 4, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDEORLB, 0x38662107, 7, 8, 6, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDEORLH, 0x78672128, 8, 9, 7, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDEORALB, 0x38e9216a, 10, 11, 9, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDEORALH, 0x78ea218b, 11, 12, 10, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDSETB, 0x38203041, 1, 2, 0, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDSETH, 0x78213062, 2, 3, 1, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDSETAB, 0x38a330a4, 4, 5, 3, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDSETAH, 0x78a430c5, 5, 6, 4, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDSETLB, 0x38663107, 7, 8, 6, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDSETLH, 0x78673128, 8, 9, 7, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDSETALB, 0x38e9316a, 10, 11, 9, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDSETALH, 0x78ea318b, 11, 12, 10, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDSMAXB, 0x38204041, 1, 2, 0, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDSMAXH, 0x78214062, 2, 3, 1, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDSMAXAB, 0x38a340a4, 4, 5, 3, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDSMAXAH, 0x78a440c5, 5, 6, 4, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDSMAXLB, 0x38664107, 7, 8, 6, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDSMAXLH, 0x78674128, 8, 9, 7, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDSMAXALB, 0x38e9416a, 10, 11, 9, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDSMAXALH, 0x78ea418b, 11, 12, 10, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDSMINB, 0x38205041, 1, 2, 0, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDSMINH, 0x78215062, 2, 3, 1, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDSMINAB, 0x38a350a4, 4, 5, 3, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDSMINAH, 0x78a450c5, 5, 6, 4, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDSMINLB, 0x38665107, 7, 8, 6, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDSMINLH, 0x78675128, 8, 9, 7, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDSMINALB, 0x38e9516a, 10, 11, 9, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDSMINALH, 0x78ea518b, 11, 12, 10, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDUMAXB, 0x38206041, 1, 2, 0, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDUMAXH, 0x78216062, 2, 3, 1, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDUMAXAB, 0x38a360a4, 4, 5, 3, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDUMAXAH, 0x78a460c5, 5, 6, 4, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDUMAXLB, 0x38666107, 7, 8, 6, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDUMAXLH, 0x78676128, 8, 9, 7, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDUMAXALB, 0x38e9616a, 10, 11, 9, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDUMAXALH, 0x78ea618b, 11, 12, 10, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDUMINB, 0x38207041, 1, 2, 0, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDUMINH, 0x78217062, 2, 3, 1, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDUMINAB, 0x38a370a4, 4, 5, 3, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDUMINAH, 0x78a470c5, 5, 6, 4, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDUMINLB, 0x38667107, 7, 8, 6, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDUMINLH, 0x78677128, 8, 9, 7, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDUMINALB, 0x38e9716a, 10, 11, 9, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_LDUMINALH, 0x78ea718b, 11, 12, 10, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STADDB, 0x3820003f, 31, 1, 0, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STADDH, 0x7821005f, 31, 2, 1, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STADDLB, 0x3862007f, 31, 3, 2, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STADDLH, 0x7863009f, 31, 4, 3, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STADDL, 0xb86400bf, 31, 5, 4, A64_SIZE_W, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STCLRB, 0x3820103f, 31, 1, 0, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STCLRH, 0x7821105f, 31, 2, 1, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STCLRLB, 0x3862107f, 31, 3, 2, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STCLRLH, 0x7863109f, 31, 4, 3, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STCLRL, 0xb86410bf, 31, 5, 4, A64_SIZE_W, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STEORB, 0x3820203f, 31, 1, 0, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STEORH, 0x7821205f, 31, 2, 1, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STEORLB, 0x3862207f, 31, 3, 2, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STEORLH, 0x7863209f, 31, 4, 3, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STEORL, 0xb86420bf, 31, 5, 4, A64_SIZE_W, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STSETB, 0x3820303f, 31, 1, 0, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STSETH, 0x7821305f, 31, 2, 1, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STSETLB, 0x3862307f, 31, 3, 2, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STSETLH, 0x7863309f, 31, 4, 3, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STSETL, 0xb86430bf, 31, 5, 4, A64_SIZE_W, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STSMAXB, 0x3820403f, 31, 1, 0, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STSMAXH, 0x7821405f, 31, 2, 1, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STSMAXLB, 0x3862407f, 31, 3, 2, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STSMAXLH, 0x7863409f, 31, 4, 3, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STSMAXL, 0xb86440bf, 31, 5, 4, A64_SIZE_W, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STSMINB, 0x3820503f, 31, 1, 0, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STSMINH, 0x7821505f, 31, 2, 1, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STSMINLB, 0x3862507f, 31, 3, 2, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STSMINLH, 0x7863509f, 31, 4, 3, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STSMINL, 0xb86450bf, 31, 5, 4, A64_SIZE_W, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STUMAXB, 0x3820603f, 31, 1, 0, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STUMAXH, 0x7821605f, 31, 2, 1, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STUMAXLB, 0x3862607f, 31, 3, 2, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STUMAXLH, 0x7863609f, 31, 4, 3, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STUMAXL, 0xb86460bf, 31, 5, 4, A64_SIZE_W, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STUMINB, 0x3820703f, 31, 1, 0, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STUMINH, 0x7821705f, 31, 2, 1, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STUMINLB, 0x3862707f, 31, 3, 2, A64_SIZE_B, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STUMINLH, 0x7863709f, 31, 4, 3, A64_SIZE_H, NO)
TCTI_DECLARE_ATOMIC_DECODE_TEST(OrderedExclusiveAtomic_STUMINL, 0xb86470bf, 31, 5, 4, A64_SIZE_W, NO)

TCTI_DECLARE_VECTOR_COMPARE_DECODE_TEST(VectorIntegerLogical_CMGE, 0x4e223c20, 0, 1, 2, 16)
TCTI_DECLARE_VECTOR_COMPARE_DECODE_TEST(VectorIntegerLogical_CMHI, 0x6e223420, 0, 1, 2, 16)
TCTI_DECLARE_VECTOR_COMPARE_DECODE_TEST(VectorIntegerLogical_CMHS, 0x6e223c20, 0, 1, 2, 16)
TCTI_DECLARE_VECTOR_COMPARE_DECODE_TEST(VectorIntegerLogical_CMLE, 0x6e209820, 0, 1, 0, 16)
TCTI_DECLARE_VECTOR_COMPARE_DECODE_TEST(VectorIntegerLogical_CMLT, 0x4e20a820, 0, 1, 0, 16)
TCTI_DECLARE_VECTOR_COMPARE_DECODE_TEST(VectorIntegerLogical_CMTST, 0x4e228c20, 0, 1, 2, 16)
TCTI_DECLARE_VECTOR_UNARY_DECODE_TEST(VectorIntegerLogical_CNT, 0x4e205820, A64_SIMD_CNT, 0, 1, 16)
TCTI_DECLARE_VECTOR_UNARY_DECODE_TEST(VectorIntegerLogical_XTN, 0x0e212820, A64_SIMD_XTN, 0, 1, 8)
TCTI_DECLARE_VECTOR_SUBTYPE_DECODE_TEST(VectorIntegerLogical_TBL, 0x0e020020, A64_SIMD_TBL, 0, 1, 2, 8)
TCTI_DECLARE_VECTOR_SUBTYPE_DECODE_TEST(VectorIntegerLogical_TBX, 0x0e021020, A64_SIMD_TBX, 0, 1, 2, 8)
TCTI_DECLARE_VECTOR_SUBTYPE_DECODE_TEST(VectorIntegerLogical_ZIP1, 0x4e023820, A64_SIMD_ZIP1, 0, 1, 2, 16)
TCTI_DECLARE_VECTOR_SUBTYPE_DECODE_TEST(VectorIntegerLogical_ZIP2, 0x4e027820, A64_SIMD_ZIP1, 0, 1, 2, 16)
TCTI_DECLARE_VECTOR_SUBTYPE_DECODE_TEST(VectorIntegerLogical_TRN1, 0x4e022820, A64_SIMD_TRN1, 0, 1, 2, 16)
TCTI_DECLARE_VECTOR_SUBTYPE_DECODE_TEST(VectorIntegerLogical_TRN2, 0x4e026820, A64_SIMD_TRN1, 0, 1, 2, 16)
TCTI_DECLARE_VECTOR_SUBTYPE_DECODE_TEST(VectorIntegerLogical_UZP1, 0x4e021820, A64_SIMD_UZP1, 0, 1, 2, 16)
TCTI_DECLARE_VECTOR_SUBTYPE_DECODE_TEST(VectorIntegerLogical_UZP2, 0x4e025820, A64_SIMD_UZP1, 0, 1, 2, 16)
TCTI_DECLARE_VECTOR_UNARY_DECODE_TEST(VectorIntegerLogical_XTN2, 0x4e212820, A64_SIMD_XTN, 0, 1, 16)
TCTI_DECLARE_VECTOR_SUBTYPE_DECODE_TEST(VectorIntegerLogical_AND, 0x4e221c20, A64_SIMD_AND, 0, 1, 2, 16)
TCTI_DECLARE_VECTOR_SUBTYPE_DECODE_TEST(VectorIntegerLogical_ORR, 0x4ea51c83, A64_SIMD_ORR, 3, 4, 5, 16)
TCTI_DECLARE_VECTOR_SUBTYPE_DECODE_TEST(VectorIntegerLogical_EOR, 0x6e281ce6, A64_SIMD_EOR, 6, 7, 8, 16)
TCTI_DECLARE_VECTOR_SUBTYPE_DECODE_TEST(VectorIntegerLogical_ADD, 0x4e2b8549, A64_SIMD_ADD, 9, 10, 11, 16)
TCTI_DECLARE_VECTOR_SUBTYPE_DECODE_TEST(VectorIntegerLogical_SUB, 0x6e228420, A64_SIMD_SUB, 0, 1, 2, 16)
TCTI_DECLARE_VECTOR_SUBTYPE_DECODE_TEST(VectorIntegerLogical_MUL, 0x4e259c83, A64_SIMD_MUL, 3, 4, 5, 16)
TCTI_DECLARE_VECTOR_SUBTYPE_DECODE_TEST(VectorIntegerLogical_BIC, 0x4e621c20, A64_SIMD_BIC, 0, 1, 2, 16)
TCTI_DECLARE_VECTOR_SUBTYPE_DECODE_TEST(VectorIntegerLogical_ORN, 0x4ee21c20, A64_SIMD_ORN, 0, 1, 2, 16)
TCTI_DECLARE_VECTOR_SUBTYPE_DECODE_TEST(VectorIntegerLogical_BSL, 0x6e621c20, A64_SIMD_BSL, 0, 1, 2, 16)
TCTI_DECLARE_VECTOR_SUBTYPE_DECODE_TEST(VectorIntegerLogical_BIT, 0x6ea21c20, A64_SIMD_BIT, 0, 1, 2, 16)
TCTI_DECLARE_VECTOR_SUBTYPE_DECODE_TEST(VectorIntegerLogical_BIF, 0x6ee21c20, A64_SIMD_BIF, 0, 1, 2, 16)

- (void)testDecodeContract_VectorIntegerLogical_EXT
{
    a64_instr_t decoded = [self decodeInstruction:0x6e024020];
    XCTAssertEqual(decoded.cat, A64_SIMD);
    XCTAssertEqual(decoded.subtype, A64_SIMD_EXT);
    XCTAssertEqual(decoded.Rd, 0);
    XCTAssertEqual(decoded.Rn, 1);
    XCTAssertEqual(decoded.Rm, 2);
    XCTAssertEqual(decoded.imm, 8LL);
    XCTAssertEqual(decoded.vec_bytes, 16);
}

- (void)testDecodeContract_VectorIntegerLogical_INS
{
    a64_instr_t decoded = [self decodeInstruction:0x4e181c20];
    XCTAssertEqual(decoded.cat, A64_SIMD);
    XCTAssertEqual(decoded.subtype, A64_SIMD_INS_GPR);
    XCTAssertEqual(decoded.Rd, 0);
    XCTAssertEqual(decoded.Rn, 1);
    XCTAssertEqual(decoded.vec_bytes, 8);
    XCTAssertEqual(decoded.vec_index, 1);
}

@end
