#import <XCTest/XCTest.h>

#import <IXLandLinuxRuntime/emu/aarch64/fetch.h>
#import <IXLandLinuxRuntime/emu/aarch64/memory.h>
#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>
#import <IXLandLinuxRuntime/emu/tlb.h>
#import <IXLandLinuxRuntime/kernel/memory.h>

@interface TCTIFetchPerfTests : XCTestCase
@end

static void tcti_fetch_perf_init_context(struct mem *mem, struct tlb *tlb, struct cpu_state *cpu)
{
    mem_init(mem);
    tlb_refresh(tlb, &mem->mmu);
    memset(cpu, 0, sizeof(*cpu));
    cpu->mmu = &mem->mmu;
    cpu->tlb = tlb;
}

@implementation TCTIFetchPerfTests

- (void)testFetchLatency_RealGuestFetchPath
{
    enum {
        textPC = 0x5000,
    };
    static const uint32_t program[] = {
        0xd503201f, 0xb8bfc020, 0xb8200041, 0x4e223c20, 0xd65f03c0,
    };

    __block struct mem mem;
    __block struct tlb tlb = {};
    __block struct cpu_state cpu;
    tcti_fetch_perf_init_context(&mem, &tlb, &cpu);
    XCTAssertEqual(a64_cpu_install_code(&cpu, textPC, program, sizeof(program) / sizeof(program[0])),
                   0);
    tlb_refresh(&tlb, &mem.mmu);

    [self measureBlock:^{
        for (size_t iter = 0; iter < 256; iter++) {
            for (size_t i = 0; i < sizeof(program) / sizeof(program[0]); i++) {
                uint32_t fetched = 0;
                XCTAssertEqual(a64_fetch_insn(&cpu, &tlb, textPC + (uint64_t)(i * 4), &fetched), 0);
                XCTAssertEqual(fetched, program[i]);
            }
        }
    }];

    mem_destroy(&mem);
}

@end
