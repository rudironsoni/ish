#import <XCTest/XCTest.h>

#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>

@interface TCTIExceptionsBarriersHintsSemanticTests : XCTestCase
@end

@implementation TCTIExceptionsBarriersHintsSemanticTests

- (void)testSemanticExecutionContract_BarriersAdvancePCWithoutCorruptingLiveState
{
    enum {
        textPC = 0x91000,
    };

    static const uint32_t insns[] = {
        0xd5033bbf, // dmb ish
        0xd5033f9f, // dsb sy
        0xd5033fdf, // isb
    };

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.x[0] = 0x1111111111111111ULL;
    cpu.x[3] = 0x3333333333333333ULL;
    cpu.pstate = 0xf0000000ULL;

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, insns, sizeof(insns) / sizeof(insns[0])),
                   0,
                   @"DMB/DSB/ISB must execute through the real TCTI system path and retire "
                    "without corrupting guest architectural state");
    XCTAssertEqual(cpu.pc, textPC + sizeof(insns),
                   @"Barrier execution must retire all instructions and advance PC by 12 bytes");
    XCTAssertEqual(cpu.x[0], 0x1111111111111111ULL);
    XCTAssertEqual(cpu.x[3], 0x3333333333333333ULL);
    XCTAssertEqual(cpu.pstate, 0xf0000000ULL);
}

- (void)testSemanticExecutionContract_YIELDBehavesAsArchitecturalHint
{
    enum {
        textPC = 0x91020,
    };

    static const uint32_t insn = 0xd503203f; // yield

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.x[5] = 0x5555555555555555ULL;

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                   @"YIELD is a userspace hint, not an illegal instruction path; TCTI must "
                    "retire it like NOP so guest spin/yield loops stay architectural");
    XCTAssertEqual(cpu.pc, textPC + 4ULL);
    XCTAssertEqual(cpu.x[5], 0x5555555555555555ULL);
}

- (void)testSemanticExecutionContract_CLREXClearsExclusiveMonitor
{
    enum {
        textPC = 0x91040,
    };

    static const uint32_t insn = 0xd5033f5f; // clrex

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.exclusive_valid = 1;
    cpu.exclusive_addr = 0x220000;
    cpu.exclusive_size = 8;

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                   @"CLREX must clear the CPU's exclusive monitor so failed userspace atomic "
                    "retry paths do not inherit stale exclusives across blocks");
    XCTAssertEqual(cpu.exclusive_valid, 0,
                   @"CLREX must publish a cleared exclusive monitor on block exit");
}

@end
