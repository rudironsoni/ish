#import <XCTest/XCTest.h>

#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>

@interface TCTICryptoAndDotProductSemanticTests : XCTestCase
@end

@implementation TCTICryptoAndDotProductSemanticTests

- (void)testSemanticExecutionContract_AESERejectsUntilCryptoFamilyIsImplemented
{
    enum {
        textPC = 0x94000,
    };

    static const uint32_t insn = 0x4e284820; // aese v0.16b, v1.16b

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));

    XCTAssertLessThan(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                      @"Crypto instructions must not disappear by omission. Until AESE is "
                       "implemented, the real TCTI path must reject it explicitly.");
}

- (void)testSemanticExecutionContract_PMULLRejectsUntilCryptoFamilyIsImplemented
{
    enum {
        textPC = 0x94020,
    };

    static const uint32_t insn = 0x0ee2e020; // pmull v0.1q, v1.1d, v2.1d

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));

    XCTAssertLessThan(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                      @"PMULL must have an explicit reject policy until the crypto/dot-product "
                       "family is implemented through TCTI.");
}

@end
