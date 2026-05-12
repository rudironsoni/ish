#import <XCTest/XCTest.h>

#include <dlfcn.h>

#import <IXLandLinuxRuntime/emu/aarch64/decode.h>
#import <IXLandLinuxRuntime/tcti/aarch64/gen.h>

typedef void (*tcti_gadget_t)(void);

extern tcti_gadget_t gadget_simd_and;
extern tcti_gadget_t gadget_simd_cnt;
extern tcti_gadget_t gadget_simd_dup_gpr;
extern tcti_gadget_t gadget_simd_ins_gpr;
extern tcti_gadget_t gadget_simd_movi_imm;
extern tcti_gadget_t gadget_simd_zip1;
extern tcti_gadget_t gadget_simd_zip2;
extern tcti_gadget_t gadget_simd_tbl;
extern tcti_gadget_t gadget_simd_tbx;
extern tcti_gadget_t gadget_simd_xtn;
extern tcti_gadget_t gadget_simd_xtn2;
extern tcti_gadget_t gadget_simd_trn1;
extern tcti_gadget_t gadget_simd_trn2;
extern tcti_gadget_t gadget_simd_uzp1;
extern tcti_gadget_t gadget_simd_uzp2;
extern tcti_gadget_t gadget_simd_bic;
extern tcti_gadget_t gadget_simd_orn;
extern tcti_gadget_t gadget_simd_bsl;
extern tcti_gadget_t gadget_simd_bit;
extern tcti_gadget_t gadget_simd_bif;
extern tcti_gadget_t gadget_simd_cmeq;
extern tcti_gadget_t gadget_simd_cmgt;
extern tcti_gadget_t gadget_simd_mla;
extern tcti_gadget_t gadget_simd_mls;

@interface TCTISIMDAndSpecialRegisterCarrierSurfaceTests : XCTestCase
@end

#define TCTI_DECLARE_SIMD_BINARY_CARRIER_TEST(_name, _insn, _pc, _gadget, _rd, _rn, _rm, _bytes) \
- (void)testRegisterCarrierSurface_##_name                                                         \
{                                                                                                  \
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];                                              \
    a64_gen_state_t state;                                                                         \
    [self generateInstruction:_insn atPC:_pc state:&state gadgets:gadgets];                       \
    XCTAssertEqual(gadgets[0], _gadget);                                                           \
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], (uint64_t)_rd);                               \
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], (uint64_t)_rn);                               \
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[3], (uint64_t)_rm);                               \
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[4], (uint64_t)_bytes);                            \
}

#define TCTI_DECLARE_SIMD_UNARY_CARRIER_TEST(_name, _insn, _pc, _gadget, _rd, _rn, _bytes)       \
- (void)testRegisterCarrierSurface_##_name                                                         \
{                                                                                                  \
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];                                              \
    a64_gen_state_t state;                                                                         \
    [self generateInstruction:_insn atPC:_pc state:&state gadgets:gadgets];                       \
    XCTAssertEqual(gadgets[0], _gadget);                                                           \
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], (uint64_t)_rd);                               \
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], (uint64_t)_rn);                               \
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[3], (uint64_t)_bytes);                            \
}

@implementation TCTISIMDAndSpecialRegisterCarrierSurfaceTests

- (void)generateInstruction:(uint32_t)insn
                       atPC:(uint64_t)pc
                      state:(a64_gen_state_t *)state
                    gadgets:(tcti_gadget_t *)gadgets
{
    XCTAssertEqual(a64_gen_init(state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(state, pc);
    XCTAssertEqual(a64_gen_instruction(state, insn, pc), A64_GEN_OK);
    XCTAssertGreaterThan(state->num_gadgets, (size_t)0);
}

- (void)testRegisterCarrierSurface_SIMDBinaryLoweringCarriesRdRnRmAndVectorWidth
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0x4e221c20 atPC:0xb0010 state:&state gadgets:gadgets];
    XCTAssertEqual(gadgets[0], gadget_simd_and);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], 0ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], 1ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[3], 2ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[4], 16ULL);
}

- (void)testRegisterCarrierSurface_SIMDUnaryLoweringCarriesDestinationSourceAndWidth
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0x4e205820 atPC:0xb0014 state:&state gadgets:gadgets];
    XCTAssertEqual(gadgets[0], gadget_simd_cnt);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], 0ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], 1ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[3], 16ULL);
}

- (void)testRegisterCarrierSurface_SIMDPairMemoryPreservesVectorPairModeAndBaseRegister
{
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(0xad400440, &decoded), 0);
    XCTAssertTrue(decoded.is_pair);
    XCTAssertTrue(decoded.is_vector);
    XCTAssertEqual(decoded.Rn, 2);
}

- (void)testRegisterCarrierSurface_SIMDDupGPRLoweringCarriesVectorDestinationScalarSourceLaneAndWidth
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0x4e010c20 atPC:0xb0018 state:&state gadgets:gadgets]; // dup v0.16b, w1
    XCTAssertEqual(gadgets[0], gadget_simd_dup_gpr);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], 0ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], 1ULL);
}

- (void)testRegisterCarrierSurface_SIMDInsertFromGPRLoweringCarriesDestinationSourceLaneAndWidth
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0x4e071c20 atPC:0xb001c state:&state gadgets:gadgets]; // ins v0.b[7], w1
    XCTAssertEqual(gadgets[0], gadget_simd_ins_gpr);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], 0ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], 1ULL);
}

- (void)testRegisterCarrierSurface_SIMDImmediateLoweringCarriesDestinationAndEncodedImmediateShape
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0x4f00e400 atPC:0xb0020 state:&state gadgets:gadgets]; // movi v0.16b,#0
    XCTAssertEqual(gadgets[0], gadget_simd_movi_imm);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], 0ULL);
}

- (void)testRegisterCarrierSurface_SIMDZipLoweringCarriesDestinationLeftRightAndVectorWidth
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0x4e021820 atPC:0xb0024 state:&state gadgets:gadgets]; // zip1 v0.16b,v1.16b,v2.16b
    XCTAssertEqual(gadgets[0], gadget_simd_zip1);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], 0ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], 1ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[3], 2ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[4], 16ULL);
}

TCTI_DECLARE_SIMD_BINARY_CARRIER_TEST(SIMDTBLLoweringCarriesRdRnRmAndWidth, 0x0e020020, 0xb0028,
                                      gadget_simd_tbl, 0, 1, 2, 8)
TCTI_DECLARE_SIMD_BINARY_CARRIER_TEST(SIMDTBXLoweringCarriesRdRnRmAndWidth, 0x0e021020, 0xb002c,
                                      gadget_simd_tbx, 0, 1, 2, 8)
TCTI_DECLARE_SIMD_UNARY_CARRIER_TEST(SIMDXTNLoweringCarriesRdRnAndWidth, 0x0e212820, 0xb0030,
                                     gadget_simd_xtn, 0, 1, 8)
TCTI_DECLARE_SIMD_UNARY_CARRIER_TEST(SIMDXTN2LoweringCarriesRdRnAndWidth, 0x4e212820, 0xb0032,
                                     gadget_simd_xtn2, 0, 1, 16)
TCTI_DECLARE_SIMD_BINARY_CARRIER_TEST(SIMDTRN1LoweringCarriesRdRnRmAndWidth, 0x4e022820, 0xb0034,
                                      gadget_simd_trn1, 0, 1, 2, 16)
TCTI_DECLARE_SIMD_BINARY_CARRIER_TEST(SIMDTRN2LoweringCarriesRdRnRmAndWidth, 0x4e026820, 0xb0036,
                                      gadget_simd_trn2, 0, 1, 2, 16)
TCTI_DECLARE_SIMD_BINARY_CARRIER_TEST(SIMDUZP1LoweringCarriesRdRnRmAndWidth, 0x4e021820, 0xb0038,
                                      gadget_simd_uzp1, 0, 1, 2, 16)
TCTI_DECLARE_SIMD_BINARY_CARRIER_TEST(SIMDUZP2LoweringCarriesRdRnRmAndWidth, 0x4e025820, 0xb003a,
                                      gadget_simd_uzp2, 0, 1, 2, 16)
TCTI_DECLARE_SIMD_BINARY_CARRIER_TEST(SIMDZIP2LoweringCarriesRdRnRmAndWidth, 0x4e027820, 0xb003b,
                                      gadget_simd_zip2, 0, 1, 2, 16)
TCTI_DECLARE_SIMD_BINARY_CARRIER_TEST(SIMDBICLoweringCarriesRdRnRmAndWidth, 0x4e621c20, 0xb003c,
                                      gadget_simd_bic, 0, 1, 2, 16)
TCTI_DECLARE_SIMD_BINARY_CARRIER_TEST(SIMDORNLoweringCarriesRdRnRmAndWidth, 0x4ee21c20, 0xb0040,
                                      gadget_simd_orn, 0, 1, 2, 16)
TCTI_DECLARE_SIMD_BINARY_CARRIER_TEST(SIMDBSLLoweringCarriesRdRnRmAndWidth, 0x6e621c20, 0xb0044,
                                      gadget_simd_bsl, 0, 1, 2, 16)
TCTI_DECLARE_SIMD_BINARY_CARRIER_TEST(SIMDBITLoweringCarriesRdRnRmAndWidth, 0x6ea21c20, 0xb0048,
                                      gadget_simd_bit, 0, 1, 2, 16)
TCTI_DECLARE_SIMD_BINARY_CARRIER_TEST(SIMDBIFLoweringCarriesRdRnRmAndWidth, 0x6ee21c20, 0xb004c,
                                      gadget_simd_bif, 0, 1, 2, 16)
TCTI_DECLARE_SIMD_BINARY_CARRIER_TEST(SIMDCMEQLoweringCarriesRdRnRmAndWidth, 0x6e229420, 0xb0050,
                                      gadget_simd_cmeq, 0, 1, 2, 16)
TCTI_DECLARE_SIMD_BINARY_CARRIER_TEST(SIMDCMGTLoweringCarriesRdRnRmAndWidth, 0x4e223420, 0xb0054,
                                      gadget_simd_cmgt, 0, 1, 2, 16)
TCTI_DECLARE_SIMD_BINARY_CARRIER_TEST(SIMDMLALoweringCarriesRdRnRmAndWidth, 0x4e229420, 0xb0058,
                                      gadget_simd_mla, 0, 1, 2, 16)
TCTI_DECLARE_SIMD_BINARY_CARRIER_TEST(SIMDMLSLoweringCarriesRdRnRmAndWidth, 0x4ea29420, 0xb005c,
                                      gadget_simd_mls, 0, 1, 2, 16)

- (void)testRegisterCarrierSurface_SIMDFMOVGPRBridgePreservesScalarFPRegisterIndices
{
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(0x9e670021, &decoded), 0);
    XCTAssertEqual(decoded.Rd, 1);
    XCTAssertEqual(decoded.Rn, 1);
    XCTAssertEqual(decoded.vec_bytes, 8);
}

- (void)testRegisterCarrierSurface_SystemRegisterMovePreservesSysregOperationClass
{
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(0xd53bd040, &decoded), 0);
    XCTAssertEqual(decoded.cat, A64_BRANCH);
}

- (void)testRegisterCarrierSurface_SystemRegisterMovePreservesTargetRegisterAndSystemImmediateFields
{
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(0xd53bd040, &decoded), 0); // mrs x0,tpidr_el0
    XCTAssertEqual(decoded.Rd, 0);
    XCTAssertEqual(decoded.sysreg, 0x5e82);
}

- (void)testRegisterCarrierSurface_SystemBarrierPreservesOperationSelector
{
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(0xd5033bbf, &decoded), 0); // dmb ish
    XCTAssertEqual(decoded.subtype, A64_SYSTEM_BARRIER);
    XCTAssertEqual(decoded.op, 5);
}

@end
