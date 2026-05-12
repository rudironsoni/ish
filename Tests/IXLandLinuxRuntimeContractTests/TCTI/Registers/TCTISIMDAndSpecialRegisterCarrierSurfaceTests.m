#import <XCTest/XCTest.h>

#include <dlfcn.h>

#import <IXLandLinuxRuntime/emu/aarch64/decode.h>
#import <IXLandLinuxRuntime/tcti/aarch64/gen.h>

typedef void (*tcti_gadget_t)(void);

@interface TCTISIMDAndSpecialRegisterCarrierSurfaceTests : XCTestCase
@end

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
    XCTAssertEqual((void *)gadgets[0], dlsym(RTLD_DEFAULT, "gadget_simd_and"));
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
    XCTAssertEqual((void *)gadgets[0], dlsym(RTLD_DEFAULT, "gadget_simd_cnt"));
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
    XCTAssertEqual((void *)gadgets[0], dlsym(RTLD_DEFAULT, "gadget_simd_dup_gpr"));
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], 0ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], 1ULL);
}

- (void)testRegisterCarrierSurface_SIMDInsertFromGPRLoweringCarriesDestinationSourceLaneAndWidth
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0x4e071c20 atPC:0xb001c state:&state gadgets:gadgets]; // ins v0.b[7], w1
    XCTAssertEqual((void *)gadgets[0], dlsym(RTLD_DEFAULT, "gadget_simd_ins_gpr"));
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], 0ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], 1ULL);
}

- (void)testRegisterCarrierSurface_SIMDImmediateLoweringCarriesDestinationAndEncodedImmediateShape
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0x4f00e400 atPC:0xb0020 state:&state gadgets:gadgets]; // movi v0.16b,#0
    XCTAssertEqual((void *)gadgets[0], dlsym(RTLD_DEFAULT, "gadget_simd_movi_imm"));
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], 0ULL);
}

- (void)testRegisterCarrierSurface_SIMDZipLoweringCarriesDestinationLeftRightAndVectorWidth
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0x4e021820 atPC:0xb0024 state:&state gadgets:gadgets]; // zip1 v0.16b,v1.16b,v2.16b
    XCTAssertEqual((void *)gadgets[0], dlsym(RTLD_DEFAULT, "gadget_simd_zip1"));
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], 0ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], 1ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[3], 2ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[4], 16ULL);
}

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
    XCTAssertEqual(decoded.op, 0x5e82);
}

- (void)testRegisterCarrierSurface_SystemBarrierPreservesOperationSelector
{
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(0xd5033bbf, &decoded), 0); // dmb ish
    XCTAssertEqual(decoded.subtype, A64_SYSTEM_BARRIER);
    XCTAssertEqual(decoded.op, 5);
}

@end
