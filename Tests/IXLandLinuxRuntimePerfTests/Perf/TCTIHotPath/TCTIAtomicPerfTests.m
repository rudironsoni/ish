#import <XCTest/XCTest.h>

#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>
#import <IXLandLinuxRuntime/emu/aarch64/memory.h>
#import <IXLandLinuxRuntime/emu/tlb.h>
#import <IXLandLinuxRuntime/kernel/memory.h>
#import <IXLandLinuxRuntime/kernel/memory.h>

@interface TCTIAtomicPerfTests : XCTestCase
@end

static void tcti_atomic_perf_init_context(struct mem *mem, struct tlb *tlb, struct cpu_state *cpu)
{
    mem_init(mem);
    tlb_refresh(tlb, &mem->mmu);
    memset(cpu, 0, sizeof(*cpu));
    cpu->mmu = &mem->mmu;
    cpu->tlb = tlb;
}

@implementation TCTIAtomicPerfTests

- (void)testAtomicAndExclusiveEngineLatency_RealAtomicExecutionPath
{
    enum {
        textPC = 0x8000,
        guestAddr = 0x240000,
    };
    static const uint32_t program[] = {
        0x88dffc20, // ldar w0, [x1]
        0x88a47cc5, // cas w4, w5, [x6]
        0x889ffc62, // stlr w2, [x3]
    };

    __block struct mem mem;
    __block struct tlb tlb = {};
    __block struct cpu_state cpu;
    tcti_atomic_perf_init_context(&mem, &tlb, &cpu);
    XCTAssertEqual(pt_map_nothing(&mem, PAGE(guestAddr), 1, P_READ | P_WRITE), 0);
    tlb_refresh(&tlb, &mem.mmu);

    [self measureBlock:^{
        cpu.pc = textPC;
        cpu.x[1] = guestAddr;
        cpu.x[2] = 0x2468ace0U;
        cpu.x[3] = guestAddr + 4;
        cpu.x[4] = 0x12345678U;
        cpu.x[5] = 0xabcdef01U;
        cpu.x[6] = guestAddr + 8;
        XCTAssertEqual(a64_guest_write32(&cpu, &tlb, guestAddr, 0x7f00aa55U), A64_MEM_OK);
        XCTAssertEqual(a64_guest_write32(&cpu, &tlb, guestAddr + 4, 0x11111111U), A64_MEM_OK);
        XCTAssertEqual(a64_guest_write32(&cpu, &tlb, guestAddr + 8, 0x12345678U), A64_MEM_OK);
        XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, program,
                                                  sizeof(program) / sizeof(program[0])),
                       0);
    }];

    mem_destroy(&mem);
}

@end
