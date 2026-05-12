#import <XCTest/XCTest.h>

#import <IXLandLinuxRuntime/emu/aarch64/decode.h>
#import <IXLandLinuxRuntime/tcti/aarch64/gen.h>

typedef void (*tcti_gadget_t)(void);

extern tcti_gadget_t gadget_atomic_ldst;
extern const tcti_gadget_t gadget_bcond[16];
extern const tcti_gadget_t gadget_cbz_wreg[16];
extern const tcti_gadget_t gadget_cbz_xreg[16];
extern const tcti_gadget_t gadget_cbnz_wreg[16];
extern const tcti_gadget_t gadget_cbnz_xreg[16];
extern const tcti_gadget_t gadget_tbz_wreg[16];
extern const tcti_gadget_t gadget_tbz_xreg[16];
extern const tcti_gadget_t gadget_tbnz_wreg[16];
extern const tcti_gadget_t gadget_tbnz_xreg[16];
extern tcti_gadget_t gadget_sbfm;
extern tcti_gadget_t gadget_bfm;
extern tcti_gadget_t gadget_ubfm;
extern tcti_gadget_t gadget_movk;
extern tcti_gadget_t gadget_write_reg_imm;
extern tcti_gadget_t gadget_addsub_imm_fallback;
extern tcti_gadget_t gadget_addsub_reg_fallback;
extern tcti_gadget_t gadget_addsub_ext_fallback;
extern tcti_gadget_t gadget_logical_imm_fallback;
extern tcti_gadget_t gadget_logical_reg_fallback;
extern tcti_gadget_t gadget_multiply_add_fallback;
extern tcti_gadget_t gadget_shift_reg_fallback;
extern tcti_gadget_t gadget_extract_fallback;
extern tcti_gadget_t gadget_div_fallback;
extern tcti_gadget_t gadget_csel_fallback;
extern tcti_gadget_t gadget_ccmp_native_reg[2][2][16];
extern tcti_gadget_t gadget_ccmp_native_imm[2][2][16];
extern tcti_gadget_t gadget_mrs;
extern tcti_gadget_t gadget_msr;
extern tcti_gadget_t gadget_dc_zva;
extern void gadget_load_sp(void);
extern void gadget_store_sp(void);
extern const tcti_gadget_t gadget_load_xreg_16_to_30[18];
extern const tcti_gadget_t gadget_store_xreg_16_to_30[18];
extern tcti_gadget_t gadget_pc_advance;
extern const tcti_gadget_t gadget_mov_imm[16];
extern const tcti_gadget_t gadget_orr_reg[16][16][16];
extern void gadget_br_impl(void);

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
    XCTAssertEqual(gadgets[0], gadget_atomic_ldst);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], 1ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[3], 2ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[4], 0ULL);
}

- (void)testRegisterCarrierSurface_GPRExclusiveStatusGetsDedicatedCarrierSlot
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0x88007c41 atPC:0xb0004 state:&state gadgets:gadgets];
    XCTAssertEqual(gadgets[0], gadget_atomic_ldst);
    XCTAssertGreaterThanOrEqual(state.num_gadgets, (size_t)7);
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
    XCTAssertEqual(gadgets[0], gadget_mov_imm[2]);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], 0x123ULL);
}

- (void)testRegisterCarrierSurface_GPRLogicalRegisterLoweringCarriesRdRnRm
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0xaa020020 atPC:0xb000c state:&state gadgets:gadgets]; // orr x0,x1,x2
    XCTAssertEqual(gadgets[0], gadget_orr_reg[0][1][2]);
}

- (void)testRegisterCarrierSurface_GPRRegisterOffsetLDRX0X1X0CarriesRegOffsetMeta
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0xf8606820 atPC:0xb000e state:&state gadgets:gadgets];
    XCTAssertGreaterThanOrEqual(state.num_gadgets, (size_t)8);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], 0ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[3], 1ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[7],
                   (1ULL << 8) | ((uint64_t)A64_EXT_LSL << 24) | (1ULL << 40));
}

- (void)testRegisterCarrierSurface_GPRRegisterOffsetLDRBW4X0X3CarriesRegOffsetMeta
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0x38636804 atPC:0xb0012 state:&state gadgets:gadgets];
    XCTAssertGreaterThanOrEqual(state.num_gadgets, (size_t)8);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], 4ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[3], 0ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[7],
                   (1ULL << 8) | ((uint64_t)3 << 16) | ((uint64_t)A64_EXT_LSL << 24));
}

- (void)testRegisterCarrierSurface_GPRRegisterOffsetLDRX0X24X23LSL3CarriesShiftedRegOffsetMeta
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0xf8777b00 atPC:0xb0016 state:&state gadgets:gadgets];
    XCTAssertGreaterThanOrEqual(state.num_gadgets, (size_t)8);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], 0ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[3], 24ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[7],
                   (1ULL << 8) | ((uint64_t)23 << 16) | ((uint64_t)A64_EXT_LSL << 24) |
                       ((uint64_t)3 << 32) | (1ULL << 40));
}

- (void)testRegisterCarrierSurface_GPRCompareBranchLoweringCarriesTargetAndFallthrough
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0x34000040 atPC:0xb0010 state:&state gadgets:gadgets]; // cbz w0, +8
    XCTAssertEqual(gadgets[0], gadget_cbz_wreg[0]);
    XCTAssertGreaterThanOrEqual(state.num_gadgets, (size_t)3);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], 0xb0018ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], 0xb0014ULL);
}

- (void)testRegisterCarrierSurface_GPRTestBitBranchLoweringCarriesBitIndexTargetAndFallthrough
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0x37200042 atPC:0xb0014 state:&state gadgets:gadgets]; // tbnz w2,#4,+8
    XCTAssertEqual(gadgets[0], gadget_tbnz_wreg[2]);
    XCTAssertGreaterThanOrEqual(state.num_gadgets, (size_t)4);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], 4ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], 0xb001cULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[3], 0xb0018ULL);
}

- (void)testRegisterCarrierSurface_GPRBitfieldLoweringCarriesRdRnImmrImmsAndWidth
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0x93407c20 atPC:0xb0018 state:&state gadgets:gadgets];
    XCTAssertEqual(gadgets[0], gadget_ubfm);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], 0xb0018ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], 0ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[3], 1ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[4], 0ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[5], 31ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[6], 1ULL);
}

- (void)testRegisterCarrierSurface_GPRSignedBitfieldLoweringCarriesSignedBitfieldPayloadOrder
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0x9341fc20 atPC:0xb001a state:&state gadgets:gadgets]; // asr x0,x1,#1
    XCTAssertEqual(gadgets[0], gadget_sbfm);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], 0xb001aULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], 0ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[3], 1ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[4], 1ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[5], 63ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[6], 1ULL);
}

- (void)testRegisterCarrierSurface_GPRBitfieldMoveLoweringCarriesBFMPayloadOrder
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0xb3407c20 atPC:0xb001b state:&state gadgets:gadgets];
    XCTAssertEqual(gadgets[0], gadget_bfm);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], 0xb001bULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], 0ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[3], 1ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[4], 0ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[5], 31ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[6], 1ULL);
}

- (void)testRegisterCarrierSurface_GPRConditionalBranchLoweringCarriesConditionTargetAndFallthrough
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0x54000040 atPC:0xb001c state:&state gadgets:gadgets]; // b.eq +8
    XCTAssertEqual(gadgets[0], gadget_bcond[A64_EQ]);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], 0xb0024ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], 0xb0020ULL);
}

- (void)testRegisterCarrierSurface_GPRCompareAndBranchNZCarriesRegisterSpecificGadget
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0xb5000202 atPC:0xb0020 state:&state gadgets:gadgets]; // cbnz x2, +0x40
    XCTAssertEqual(gadgets[0], gadget_cbnz_xreg[2]);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], 0xb0060ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], 0xb0024ULL);
}

- (void)testRegisterCarrierSurface_GPRCompareAndBranchZeroXCarriesRegisterSpecificGadget
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0xb4000042 atPC:0xb0022 state:&state gadgets:gadgets];
    XCTAssertEqual(gadgets[0], gadget_cbz_xreg[2]);
}

- (void)testRegisterCarrierSurface_GPRCompareAndBranchNZWCarriesRegisterSpecificGadget
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0x35fffde3 atPC:0xb0023 state:&state gadgets:gadgets];
    XCTAssertEqual(gadgets[0], gadget_cbnz_wreg[3]);
}

- (void)testRegisterCarrierSurface_GPRTestBitZeroLoweringCarriesBitIndexTargetAndFallthrough
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0x36f805a0 atPC:0xb0024 state:&state gadgets:gadgets]; // tbz w0,#31,+0xb4
    XCTAssertEqual(gadgets[0], gadget_tbz_wreg[0]);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], 31ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[3], 0xb0028ULL);
}

- (void)testRegisterCarrierSurface_GPRTestBitZeroXCarriesRegisterSpecificGadget
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0xb6f805a0 atPC:0xb0026 state:&state gadgets:gadgets];
    XCTAssertEqual(gadgets[0], gadget_tbz_xreg[0]);
}

- (void)testRegisterCarrierSurface_GPRTestBitNZXCarriesRegisterSpecificGadget
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0xb7f80400 atPC:0xb0027 state:&state gadgets:gadgets];
    XCTAssertEqual(gadgets[0], gadget_tbnz_xreg[0]);
}

- (void)testRegisterCarrierSurface_GPRMemoryBackedMoveWideLoweringUsesWriteRegImmCarrierThenStore
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0xd2824694 atPC:0xb0028 state:&state gadgets:gadgets]; // movz x20,#0x1234
    XCTAssertEqual(gadgets[0], gadget_write_reg_imm);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], 20ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], 0x1234ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[3], 1ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[4], 0ULL);
    XCTAssertEqual(gadgets[5], gadget_store_xreg_16_to_30[7]);
}

- (void)testRegisterCarrierSurface_GPRMoveKeepLoweringCarriesDestinationImmediateShiftAndWidth
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0xf2a24683 atPC:0xb002c state:&state gadgets:gadgets];
    XCTAssertEqual(gadgets[0], gadget_movk);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], 3ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], 0x1234ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[3], 16ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[4], 1ULL);
}

- (void)testRegisterCarrierSurface_GPRAddImmediateFallbackCarriesWidthAndSPRoleBits
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0x11000508 atPC:0xb0030 state:&state gadgets:gadgets];
    XCTAssertEqual(gadgets[0], gadget_addsub_imm_fallback);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], 8ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], 8ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[3], 1ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[4], 0ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[5], 0ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[6], 0ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[7], 0ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[8], 0ULL);
}

- (void)testRegisterCarrierSurface_GPRAddShiftedRegisterFallbackCarriesShiftShape
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0x0b0d15ad atPC:0xb0034 state:&state gadgets:gadgets];
    XCTAssertEqual(gadgets[0], gadget_addsub_reg_fallback);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], 13ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], 13ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[3], 13ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[4], 0ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[5], 5ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[6], 0ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[7], 0ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[8], 0ULL);
}

- (void)testRegisterCarrierSurface_GPRExtendedRegisterFallbackCarriesExtendModeAndPackedRoleBits
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0x8b254005 atPC:0xb0038 state:&state gadgets:gadgets];
    XCTAssertEqual(gadgets[0], gadget_addsub_ext_fallback);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], 5ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], 0ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[3], 5ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[4], 2ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[5], 0ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[6], 4ULL);
}

- (void)testRegisterCarrierSurface_GPRLogicalImmediateFallbackCarriesSubtypeAndWidth
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0x3200014a atPC:0xb003c state:&state gadgets:gadgets];
    XCTAssertEqual(gadgets[0], gadget_logical_imm_fallback);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], 10ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], 10ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[3], 1ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[4], 8ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[5], 0ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[6], 0ULL);
}

- (void)testRegisterCarrierSurface_GPRLogicalRegisterFallbackCarriesShiftSubtypeAndWidth
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0xca260064 atPC:0xb0040 state:&state gadgets:gadgets];
    XCTAssertEqual(gadgets[0], gadget_logical_reg_fallback);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], 4ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], 3ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[3], 6ULL);
}

- (void)testRegisterCarrierSurface_GPRMultiplyAddFallbackCarriesAccumulatorSubtypeAndWidth
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0x9b217c00 atPC:0xb0044 state:&state gadgets:gadgets];
    XCTAssertEqual(gadgets[0], gadget_multiply_add_fallback);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], 0ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], 0ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[3], 1ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[4], 31ULL);
}

- (void)testRegisterCarrierSurface_GPRShiftRegisterFallbackCarriesSubtypeAndWidth
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0x9ac020a3 atPC:0xb0048 state:&state gadgets:gadgets];
    XCTAssertEqual(gadgets[0], gadget_shift_reg_fallback);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], 3ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], 5ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[3], 0ULL);
}

- (void)testRegisterCarrierSurface_GPRExtractFallbackCarriesLsbAndWidth
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0x93d30b33 atPC:0xb004c state:&state gadgets:gadgets];
    XCTAssertEqual(gadgets[0], gadget_extract_fallback);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], 19ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], 25ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[3], 19ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[4], 2ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[5], 1ULL);
}

- (void)testRegisterCarrierSurface_GPRDivideFallbackCarriesSubtypeAndWidth
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0x1ac70823 atPC:0xb0050 state:&state gadgets:gadgets];
    XCTAssertEqual(gadgets[0], gadget_div_fallback);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], 3ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], 1ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[3], 7ULL);
}

- (void)testRegisterCarrierSurface_GPRConditionalSelectFallbackCarriesConditionSubtypeAndWidth
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0x9a9f07e0 atPC:0xb0054 state:&state gadgets:gadgets];
    XCTAssertEqual(gadgets[0], gadget_csel_fallback);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], 0ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], 31ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[3], 31ULL);
}

- (void)testRegisterCarrierSurface_GPRNativeCCMPRegisterLoweringCarriesOperandAndNZCVPayload
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0xfa4011c4 atPC:0xb0058 state:&state gadgets:gadgets];
    XCTAssertEqual(gadgets[0], gadget_ccmp_native_reg[1][1][A64_NE]);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], 14ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], 0ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[3], 4ULL);
}

- (void)testRegisterCarrierSurface_GPRNativeCCMPImmediateLoweringCarriesImmediateAndNZCVPayload
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0x7a4209e0 atPC:0xb005c state:&state gadgets:gadgets];
    XCTAssertEqual(gadgets[0], gadget_ccmp_native_imm[1][0][A64_EQ]);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], 15ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], 2ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[3], 0ULL);
}

- (void)testRegisterCarrierSurface_GPRMRSLoweringCarriesSysregAndDestinationRegister
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0xd53bd040 atPC:0xb001c state:&state gadgets:gadgets]; // mrs x0,tpidr_el0
    XCTAssertEqual(gadgets[0], gadget_mrs);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], 0x5e82ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], 0ULL);
}

- (void)testRegisterCarrierSurface_GPRMSRLoweringCarriesSysregAndSourceRegister
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0xd51bd040 atPC:0xb0020 state:&state gadgets:gadgets]; // msr tpidr_el0,x0
    XCTAssertEqual(gadgets[0], gadget_msr);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], 0x5e82ULL);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[2], 0ULL);
}

- (void)testRegisterCarrierSurface_GPRDCZVALoweringCarriesCacheBaseRegister
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0xd50b7420 atPC:0xb0024 state:&state gadgets:gadgets]; // dc zva,x0
    XCTAssertEqual(gadgets[0], gadget_dc_zva);
    XCTAssertEqual((uint64_t)(uintptr_t)gadgets[1], 0ULL);
}

- (void)testRegisterCarrierSurface_GPRMemoryBackedSourceLoadsThroughDedicatedCarrierTable
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0xd61f0200 atPC:0xb0060 state:&state gadgets:gadgets];
    XCTAssertEqual(gadgets[0], gadget_load_xreg_16_to_30[3]);
    XCTAssertEqual(gadgets[1], (tcti_gadget_t)gadget_br_impl);
}

- (void)testRegisterCarrierSurface_GPRSPBaseWritesBackThroughDedicatedLoadStoreAndAdvanceCarriers
{
    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:0xf84007e0 atPC:0xb0064 state:&state gadgets:gadgets]; // ldr x0,[sp],#0
    XCTAssertEqual(gadgets[0], (tcti_gadget_t)gadget_load_sp);
    XCTAssertEqual(gadgets[state.num_gadgets - 2], (tcti_gadget_t)gadget_store_sp);
    XCTAssertEqual(gadgets[state.num_gadgets - 1], gadget_pc_advance);
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
