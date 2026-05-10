#import <XCTest/XCTest.h>

#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>
#import <IXLandLinuxRuntime/kernel/memory.h>
#import <IXLandLinuxRuntime/emu/tlb.h>

@interface TCTIPipelineBlockCacheGenerationTests : XCTestCase
@end

@implementation TCTIPipelineBlockCacheGenerationTests

- (void)testExecutableGuestWriteInvalidatesCompiledBlockAtSamePC
{
    enum {
        textPC = 0x4000,
    };

    static const uint32_t firstProgram[] = {
        0xd2800020, // mov x0, #1
        0xd65f03c0, // ret
    };
    static const uint32_t secondProgram[] = {
        0xd2800040, // mov x0, #2
        0xd65f03c0, // ret
    };

    struct mem mem;
    mem_init(&mem);
    XCTAssertEqual(pt_map_nothing(&mem, PAGE(textPC), 1, P_RWX), 0);

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.pc = textPC;
    cpu.x[30] = 0;

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, firstProgram,
                                              sizeof(firstProgram) / sizeof(firstProgram[0])),
                   0);
    XCTAssertEqual(cpu.x[0], 1ULL, @"first synthetic block must materialize the first payload");

    cpu.pc = textPC;
    cpu.x[0] = 0;
    cpu.x[30] = 0;

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, secondProgram,
                                              sizeof(secondProgram) / sizeof(secondProgram[0])),
                   0);
    XCTAssertEqual(cpu.x[0], 2ULL,
                   @"rewriting executable guest bytes at the same PC must invalidate stale "
                    "compiled blocks before the second execution");

    mem_destroy(&mem);
}

@end
