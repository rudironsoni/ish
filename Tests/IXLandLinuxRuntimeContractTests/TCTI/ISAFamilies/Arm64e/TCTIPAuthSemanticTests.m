#import <XCTest/XCTest.h>

#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>

@interface TCTIPAuthSemanticTests : XCTestCase
@end

@implementation TCTIPAuthSemanticTests

- (void)testSemanticExecutionContract_PACIARejectsUntilPAuthIsImplemented
{
    enum {
        textPC = 0x96000,
    };

    static const uint32_t insn = 0xdac10020; // pacia x0, x1

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));

    XCTAssertLessThan(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                      @"Arm64e pointer-auth instructions must have explicit support policy. "
                       "Until PAC is implemented, PACIA must reject through TCTI.");
}

- (void)testSemanticExecutionContract_AUTIARejectsUntilPAuthIsImplemented
{
    enum {
        textPC = 0x96020,
    };

    static const uint32_t insn = 0xdac11020; // autia x0, x1

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));

    XCTAssertLessThan(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                      @"AUTIA must reject explicitly until arm64e authenticated-control "
                       "semantics are implemented.");
}

- (void)testSemanticExecutionContract_BTIRejectsUntilBranchTargetIdentificationIsImplemented
{
    enum {
        textPC = 0x96040,
    };

    static const uint32_t insn = 0xd503245f; // bti c

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));

    XCTAssertLessThan(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                      @"BTI must not vanish implicitly. Until branch-target identification is "
                       "implemented, TCTI must reject it explicitly.");
}

- (void)testSemanticExecutionContract_LDRAARejectsUntilAuthenticatedLoadsExist
{
    enum {
        textPC = 0x96060,
    };

    static const uint32_t insn = 0xf8200420; // ldraa x0, [x1]

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));

    XCTAssertLessThan(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                      @"Authenticated loads are part of the arm64e ownership surface. Until "
                       "they exist in TCTI, LDRAA must reject explicitly.");
}

@end
