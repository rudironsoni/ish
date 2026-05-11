#import <XCTest/XCTest.h>

#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>
#import <IXLandLinuxRuntime/emu/aarch64/decode.h>
#import <IXLandLinuxRuntime/tcti/aarch64/gen.h>

typedef void (*tcti_gadget_t)(void);

@interface TCTIPipelineDispatchPreservationTests : XCTestCase
@end

@implementation TCTIPipelineDispatchPreservationTests

- (a64_instr_t)decodeInstruction:(uint32_t)insn
{
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(insn, &decoded), 0);
    return decoded;
}

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

- (void)testDispatchPreservationContract_CMPAddCSELBlocksDecodeAndLower
{
    struct {
        uint32_t raw;
        uint64_t pc;
    } cases[] = {
        {0xf100009f, 0x51f1c}, // cmp x4, #0
        {0x910103e2, 0x51f20}, // add x2, sp, #0x40
        {0x9a9f1042, 0x51f24}, // csel x2, x2, xzr, ne
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        a64_instr_t decoded = [self decodeInstruction:cases[i].raw];
        XCTAssertNotEqual(decoded.cat, A64_RESERVED);

        tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
        a64_gen_state_t state;
        [self generateInstruction:cases[i].raw atPC:cases[i].pc state:&state gadgets:gadgets];
    }
}

- (void)testDispatchPreservationContract_FlagsPublishedByOneBlockRemainVisibleToLaterBlocks
{
    static const uint32_t cmpX4Zero = 0xf100009f;
    static const uint32_t addX2Sp40 = 0x910103e2;
    static const uint32_t cselX2X2XzrNe = 0x9a9f1042;

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.sp = 0x200000;
    cpu.x[2] = 0x1111111111111111ULL;
    cpu.x[4] = 0;

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, 0x51f1c, &cmpX4Zero, 1), 0);
    XCTAssertEqual(cpu.pc, 0x51f20ULL);
    XCTAssertTrue(cpu.z == 1U, @"cmp must publish the zero flag for the next dispatched block");

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, 0x51f20, &addX2Sp40, 1), 0);
    XCTAssertEqual(cpu.pc, 0x51f24ULL);
    XCTAssertEqual(cpu.x[2], cpu.sp + 0x40ULL);
    XCTAssertTrue(cpu.z == 1U,
                  @"non-S arithmetic in an intervening block must not clobber flags");

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, 0x51f24, &cselX2X2XzrNe, 1), 0);
    XCTAssertEqual(cpu.pc, 0x51f28ULL);
    XCTAssertEqual(cpu.x[2], 0ULL,
                   @"later dispatched blocks must observe the live flags published by prior "
                    "blocks through the real TCTI exit/dispatch path");
    XCTAssertTrue(cpu.z == 1U);
}

@end
