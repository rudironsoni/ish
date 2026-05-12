#import <XCTest/XCTest.h>

#import <IXLandLinuxRuntime/emu/aarch64/decode.h>

@interface TCTIDecodePerfTests : XCTestCase
@end

@implementation TCTIDecodePerfTests

- (void)testDecodeLatency_RealInstructionMix
{
    static const uint32_t instructions[] = {
        0xb8bfc020, 0x887f0440, 0xb8208041, 0xb8204041, 0x4e223c20,
        0x6e223420, 0x6e209820, 0x4e228c20, 0x4e2894e6, 0x6e2b9549,
    };

    [self measureBlock:^{
        for (size_t iter = 0; iter < 512; iter++) {
            for (size_t i = 0; i < sizeof(instructions) / sizeof(instructions[0]); i++) {
                a64_instr_t decoded;
                XCTAssertEqual(a64_decode(instructions[i], &decoded), 0);
                XCTAssertEqual(decoded.raw, instructions[i]);
            }
        }
    }];
}

@end
