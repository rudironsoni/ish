#import <XCTest/XCTest.h>

#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>

@interface TCTISpecialAndTaggingSemanticTests : XCTestCase
@end

@implementation TCTISpecialAndTaggingSemanticTests

- (void)testSemanticExecutionContract_LDGRejectsUntilTaggingSemanticsExist
{
    enum {
        textPC = 0x95000,
    };

    static const uint32_t insn = 0xd9600020; // ldg x0, [x1]

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));

    XCTAssertLessThan(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                      @"Tagging-family instructions must have explicit ownership. Until guest "
                       "MTE/tag transport exists, LDG must reject through the real TCTI path.");
}

- (void)testSemanticExecutionContract_STGRejectsUntilTaggingSemanticsExist
{
    enum {
        textPC = 0x95020,
    };

    static const uint32_t insn = 0xd9200820; // stg x0, [x1]

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));

    XCTAssertLessThan(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                      @"STG must reject explicitly until the memory-tagging family is supported "
                       "through TCTI.");
}

@end
