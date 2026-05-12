#import <XCTest/XCTest.h>

#include <dlfcn.h>

#import <IXLandLinuxRuntime/emu/aarch64/decode.h>
#import <IXLandLinuxRuntime/tcti/aarch64/gen.h>

typedef void (*tcti_gadget_t)(void);

@interface TCTIRegisterCarrierSurfaceTests : XCTestCase
@end

@implementation TCTIRegisterCarrierSurfaceTests

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

- (void)testRegisterCarrierSurface_HotGPRLoadStoreCarriesRtRnRmInOrder
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0xb8200041 atPC:0xa0000 state:&state gadgets:gadgets]; // ldadd w0,w1,[x2]
    XCTAssertEqual((void *)gadgets[0], dlsym(RTLD_DEFAULT, "gadget_atomic_ldst"));
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], 1ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[3], 2ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[4], 0ULL);
}

- (void)testRegisterCarrierSurface_ExclusiveStatusRegisterGetsOwnCarrierSlot
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0x88007c41 atPC:0xa0004 state:&state gadgets:gadgets]; // stxr w0,w1,[x2]
    XCTAssertEqual((void *)gadgets[0], dlsym(RTLD_DEFAULT, "gadget_atomic_ldst"));
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], 0ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[3], 2ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[4], 1ULL);
}

- (void)testRegisterCarrierSurface_VectorBinaryLoweringCarriesRdRnRmVecBytes
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0x4e221c20 atPC:0xa0008 state:&state gadgets:gadgets]; // and v0.16b,v1.16b,v2.16b
    XCTAssertEqual((void *)gadgets[0], dlsym(RTLD_DEFAULT, "gadget_simd_and"));
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], 0ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], 1ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[3], 2ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[4], 16ULL);
}

- (void)testRegisterCarrierSurface_VectorUnaryLoweringCarriesDestinationSourceAndWidth
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0x4e205820 atPC:0xa000c state:&state gadgets:gadgets]; // cnt v0.16b,v1.16b
    XCTAssertEqual((void *)gadgets[0], dlsym(RTLD_DEFAULT, "gadget_simd_cnt"));
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], 0ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], 1ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[3], 16ULL);
}

- (void)testRegisterCarrierSurface_SPRoleStaysDistinctFromZRInLoadStoreLowering
{
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(0xf94003e0, &decoded), 0); // ldr x0, [sp]
    XCTAssertEqual(decoded.Rn, 31);
    XCTAssertEqual(decoded.Rd, 0);
    XCTAssertFalse(decoded.is_pair);
}

- (void)testRegisterCarrierSurface_ZRStoreRoleStaysDistinctFromSPInAtomicLowering
{
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(0xb820005f, &decoded), 0); // stadd w0,[x2]
    XCTAssertEqual(decoded.Rd, 31);
    XCTAssertEqual(decoded.Rn, 2);
}

- (void)testRegisterCarrierSurface_PairSecondRegisterIsPreservedAtDecode
{
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(0x887f0440, &decoded), 0); // ldxp w0,w1,[x2]
    XCTAssertTrue(decoded.is_pair);
    XCTAssertEqual(bits(0x887f0440, 14, 10), 1);
}

- (void)testRegisterCarrierSurface_VectorPairMemoryCarriesPairModeAndBase
{
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(0xad400440, &decoded), 0); // ldp q0,q1,[x2]
    XCTAssertTrue(decoded.is_pair);
    XCTAssertTrue(decoded.is_vector);
    XCTAssertEqual(decoded.Rn, 2);
}

@end
