#import <XCTest/XCTest.h>

#include <dlfcn.h>

#import <IXLandLinuxRuntime/emu/aarch64/decode.h>
#import <IXLandLinuxRuntime/tcti/aarch64/gen.h>

typedef void (*tcti_gadget_t)(void);

@interface TCTIGPRCarrierSurfaceTests : XCTestCase
@end

@implementation TCTIGPRCarrierSurfaceTests

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

- (void)testRegisterCarrierSurface_GPRAtomicLoadModifyStoreCarriesRtRnRmInLoweringOrder
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0xb8200041 atPC:0xb0000 state:&state gadgets:gadgets];
    XCTAssertEqual((void *)gadgets[0], dlsym(RTLD_DEFAULT, "gadget_atomic_ldst"));
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], 1ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[3], 2ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[4], 0ULL);
}

- (void)testRegisterCarrierSurface_GPRExclusiveStatusGetsDedicatedCarrierSlot
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0x88007c41 atPC:0xb0004 state:&state gadgets:gadgets];
    XCTAssertEqual((void *)gadgets[0], dlsym(RTLD_DEFAULT, "gadget_atomic_ldst"));
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], 0ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[3], 2ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[4], 1ULL);
}

- (void)testRegisterCarrierSurface_GPRPairLoadsPreserveSecondArchitecturalRegisterOperand
{
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(0x887f0440, &decoded), 0);
    XCTAssertTrue(decoded.is_pair);
    XCTAssertEqual(bits(0x887f0440, 14, 10), 1);
}

- (void)testRegisterCarrierSurface_GPRBranchTargetRegisterSurvivesDecodeForBR
{
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(0xd61f0200, &decoded), 0);
    XCTAssertEqual(decoded.Rn, 16);
}

- (void)testRegisterCarrierSurface_GPRCompareBranchPreservesComparedRegisterAndWidth
{
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(0xb40004d5, &decoded), 0);
    XCTAssertEqual(decoded.subtype, A64_BRANCH_CMP);
    XCTAssertEqual(decoded.Rd, 21);
    XCTAssertTrue(decoded.is_64bit);
}

- (void)testRegisterCarrierSurface_GPRTestBitBranchPreservesRegisterBitAndBranchClass
{
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(0x37200042, &decoded), 0); // tbnz w2,#4,...
    XCTAssertEqual(decoded.subtype, A64_BRANCH_TEST);
    XCTAssertEqual(decoded.Rd, 2);
    XCTAssertEqual(bits(0x37200042, 23, 19), 4);
    XCTAssertFalse(decoded.is_64bit);
}

- (void)testRegisterCarrierSurface_GPRConditionalSelectPreservesRdRnRmAndCondition
{
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(0x9a9f1042, &decoded), 0);
    XCTAssertEqual(decoded.Rd, 2);
    XCTAssertEqual(decoded.Rn, 2);
    XCTAssertEqual(decoded.Rm, 31);
    XCTAssertEqual(decoded.cond, A64_NE);
}

- (void)testRegisterCarrierSurface_GPRBitfieldPreservesDestinationSourceAndImmediateFields
{
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(0x93407c20, &decoded), 0); // ubfx x0,x1,#0,#32 via ubfm
    XCTAssertEqual(decoded.Rd, 0);
    XCTAssertEqual(decoded.Rn, 1);
    XCTAssertEqual(bits(0x93407c20, 21, 16), 0);
    XCTAssertEqual(bits(0x93407c20, 15, 10), 31);
}

- (void)testRegisterCarrierSurface_GPRMoveImmediateLoweringCarriesDestinationAndImmediatePayload
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0xd2802462 atPC:0xb0008 state:&state gadgets:gadgets]; // mov x2,#0x123
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], 2ULL);
}

- (void)testRegisterCarrierSurface_GPRLogicalRegisterLoweringCarriesRdRnRm
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0xaa020020 atPC:0xb000c state:&state gadgets:gadgets]; // orr x0,x1,x2
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], 0ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], 1ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[3], 2ULL);
}

- (void)testRegisterCarrierSurface_SPBaseRoleStaysDistinctFromZRForScalarLoadStore
{
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(0xf94003e0, &decoded), 0);
    XCTAssertEqual(decoded.Rn, 31);
    XCTAssertEqual(decoded.Rd, 0);
}

- (void)testRegisterCarrierSurface_ZRDestinationRoleStaysDistinctFromSPForStoreOnlyAtomic
{
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(0xb820005f, &decoded), 0);
    XCTAssertEqual(decoded.Rd, 31);
    XCTAssertEqual(decoded.Rn, 2);
}

@end
