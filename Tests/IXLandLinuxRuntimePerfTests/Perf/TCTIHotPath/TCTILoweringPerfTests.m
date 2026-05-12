#import <XCTest/XCTest.h>

#import <IXLandLinuxRuntime/tcti/aarch64/gen.h>

typedef void (*tcti_gadget_t)(void);

@interface TCTILoweringPerfTests : XCTestCase
@end

@implementation TCTILoweringPerfTests

- (void)testLoweringLatency_RealGenerationPath
{
    static const uint32_t instructions[] = {
        0xb8bfc020, 0xb8200041, 0xb8203041, 0x4e223c20, 0x4e228c20,
    };

    [self measureBlock:^{
        for (size_t iter = 0; iter < 128; iter++) {
            tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
            a64_gen_state_t state;
            XCTAssertEqual(a64_gen_init(&state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
            a64_gen_reset(&state, 0x6000);
            for (size_t i = 0; i < sizeof(instructions) / sizeof(instructions[0]); i++) {
                XCTAssertEqual(a64_gen_instruction(&state, instructions[i], 0x6000 + (uint64_t)(i * 4)),
                               A64_GEN_OK);
            }
            XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
        }
    }];
}

@end
