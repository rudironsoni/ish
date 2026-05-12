#import <XCTest/XCTest.h>

#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>

@interface TCTISpecialAndTaggingSemanticTests : XCTestCase
@end

#define TCTI_DECLARE_TAGGING_REJECT_TEST(_name, _insn, _pc)                                        \
- (void)testSemanticExecutionContract_##_name                                                      \
{                                                                                                  \
    enum {                                                                                         \
        textPC = _pc,                                                                              \
    };                                                                                             \
    static const uint32_t insn = _insn;                                                            \
    struct cpu_state cpu;                                                                          \
    memset(&cpu, 0, sizeof(cpu));                                                                  \
    XCTAssertLessThan(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,                      \
                      @"Tagging-family instruction `%s` must reject explicitly until the "         \
                       @"real TCTI ownership exists", #_name);                                     \
}

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

TCTI_DECLARE_TAGGING_REJECT_TEST(LD64BRejectsUntilTaggingSemanticsExist, 0xf83fd020, 0x95040)
TCTI_DECLARE_TAGGING_REJECT_TEST(ST64BRejectsUntilTaggingSemanticsExist, 0xf83f9062, 0x95044)
TCTI_DECLARE_TAGGING_REJECT_TEST(LDGMRejectsUntilTaggingSemanticsExist, 0xd9e000a4, 0x95048)
TCTI_DECLARE_TAGGING_REJECT_TEST(STGMRejectsUntilTaggingSemanticsExist, 0xd9a00128, 0x9504c)
TCTI_DECLARE_TAGGING_REJECT_TEST(STZGRejectsUntilTaggingSemanticsExist, 0xd960096a, 0x95050)
TCTI_DECLARE_TAGGING_REJECT_TEST(STZGMRejectsUntilTaggingSemanticsExist, 0xd92001ac, 0x95054)
TCTI_DECLARE_TAGGING_REJECT_TEST(ST2GRejectsUntilTaggingSemanticsExist, 0xd9a009ee, 0x95058)
TCTI_DECLARE_TAGGING_REJECT_TEST(STZ2GRejectsUntilTaggingSemanticsExist, 0xd9e00a30, 0x9505c)

@end
