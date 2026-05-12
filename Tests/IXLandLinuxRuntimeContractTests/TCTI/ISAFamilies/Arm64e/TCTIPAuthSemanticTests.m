#import <XCTest/XCTest.h>

#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>

@interface TCTIPAuthSemanticTests : XCTestCase
@end

#define TCTI_DECLARE_ARM64E_REJECT_TEST(_name, _insn, _pc)                                         \
- (void)testSemanticExecutionContract_##_name                                                      \
{                                                                                                  \
    enum {                                                                                         \
        textPC = _pc,                                                                              \
    };                                                                                             \
    static const uint32_t insn = _insn;                                                            \
    struct cpu_state cpu;                                                                          \
    memset(&cpu, 0, sizeof(cpu));                                                                  \
    XCTAssertLessThan(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,                      \
                      @"arm64e instruction `%s` must reject explicitly until the real TCTI "      \
                       @"ownership exists", #_name);                                               \
}

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

TCTI_DECLARE_ARM64E_REJECT_TEST(PACIBRejectsUntilPAuthIsImplemented, 0xdac10462, 0x96080)
TCTI_DECLARE_ARM64E_REJECT_TEST(AUTIBRejectsUntilPAuthIsImplemented, 0xdac114e6, 0x96084)
TCTI_DECLARE_ARM64E_REJECT_TEST(XPACIRejectsUntilPAuthIsImplemented, 0xdac143e8, 0x96088)
TCTI_DECLARE_ARM64E_REJECT_TEST(XPACDRejectsUntilPAuthIsImplemented, 0xdac147e9, 0x9608c)
TCTI_DECLARE_ARM64E_REJECT_TEST(BRAARejectsUntilAuthenticatedBranchesExist, 0xd71f094b, 0x96090)
TCTI_DECLARE_ARM64E_REJECT_TEST(BRABRejectsUntilAuthenticatedBranchesExist, 0xd71f0d8d, 0x96094)
TCTI_DECLARE_ARM64E_REJECT_TEST(BLRAARejectsUntilAuthenticatedBranchesExist, 0xd73f09cf, 0x96098)
TCTI_DECLARE_ARM64E_REJECT_TEST(BLRABRejectsUntilAuthenticatedBranchesExist, 0xd73f0e11, 0x9609c)
TCTI_DECLARE_ARM64E_REJECT_TEST(RETAARejectsUntilAuthenticatedReturnsExist, 0xd65f0bff, 0x960a0)
TCTI_DECLARE_ARM64E_REJECT_TEST(RETABRejectsUntilAuthenticatedReturnsExist, 0xd65f0fff, 0x960a4)
TCTI_DECLARE_ARM64E_REJECT_TEST(RETAASPPCRRejectsUntilAuthenticatedReturnsExist, 0xd65f0bfe, 0x960a6)
TCTI_DECLARE_ARM64E_REJECT_TEST(RETABSPPCRRejectsUntilAuthenticatedReturnsExist, 0xd65f0ffe, 0x960a7)
TCTI_DECLARE_ARM64E_REJECT_TEST(LDRABRejectsUntilAuthenticatedLoadsExist, 0xf8a00462, 0x960a8)
TCTI_DECLARE_ARM64E_REJECT_TEST(BTIRejectsUntilBranchTargetIdentificationIsImplemented_Explicit, 0xd503245f, 0x960ac)

@end
