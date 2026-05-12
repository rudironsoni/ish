#import <XCTest/XCTest.h>

#import <IXLandLinuxRuntime/emu/aarch64/block-cache.h>
#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>
#import <IXLandLinuxRuntime/emu/aarch64/memory.h>
#import <IXLandLinuxRuntime/emu/tlb.h>
#import <IXLandLinuxRuntime/kernel/memory.h>

@interface TCTIBlockCachePerfTests : XCTestCase
@end

static void tcti_block_cache_perf_init_context(struct mem *mem, struct tlb *tlb,
                                               struct cpu_state *cpu)
{
    mem_init(mem);
    tlb_refresh(tlb, &mem->mmu);
    memset(cpu, 0, sizeof(*cpu));
    cpu->mmu = &mem->mmu;
    cpu->tlb = tlb;
}

@implementation TCTIBlockCachePerfTests

- (void)testBlockCacheAndGenerationLatency_RealCompileAndCachePath
{
    enum {
        textPC = 0x7000,
    };
    static const uint32_t program[] = {
        0xb8bfc020, 0xb8208041, 0x4e223c20, 0xd65f03c0,
    };

    __block struct mem mem;
    __block struct tlb tlb = {};
    __block struct cpu_state cpu;
    tcti_block_cache_perf_init_context(&mem, &tlb, &cpu);
    XCTAssertEqual(a64_cpu_install_code(&cpu, textPC, program, sizeof(program) / sizeof(program[0])),
                   0);
    tlb_refresh(&tlb, &mem.mmu);

    [self measureBlock:^{
        cpu.pc = textPC;
        a64_cpu_run_limited(&cpu, &tlb, 1);
        XCTAssertNotEqual(cpu.mmu->block_cache, NULL);
        struct a64_block *block =
            a64_cache_lookup(cpu.mmu->block_cache, textPC, cpu.mmu->generation);
        XCTAssertNotEqual(block, NULL);
    }];

    mem_destroy(&mem);
}

@end
