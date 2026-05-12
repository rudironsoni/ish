#import <XCTest/XCTest.h>

#import <IXLandLinuxRuntime/emu/aarch64/block-cache.h>
#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>
#import <IXLandLinuxRuntime/emu/aarch64/decode.h>
#import <IXLandLinuxRuntime/emu/aarch64/fetch.h>
#import <IXLandLinuxRuntime/emu/aarch64/memory.h>
#import <IXLandLinuxRuntime/emu/tlb.h>
#import <IXLandLinuxRuntime/kernel/memory.h>
#import <IXLandLinuxRuntime/tcti/aarch64/gen.h>

typedef void (*tcti_gadget_t)(void);

@interface TCTIHotPathPerfTests : XCTestCase
@end

static void tcti_perf_init_context(struct mem *mem, struct tlb *tlb, struct cpu_state *cpu)
{
    mem_init(mem);
    tlb_refresh(tlb, &mem->mmu);
    memset(cpu, 0, sizeof(*cpu));
    cpu->mmu = &mem->mmu;
    cpu->tlb = tlb;
}

@implementation TCTIHotPathPerfTests

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
    tcti_perf_init_context(&mem, &tlb, &cpu);
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

- (void)testDecodeLatency_RealInstructionMix
{
    static const uint32_t instructions[] = {
        0xb8bfc020, 0x887f0440, 0xb8208041, 0xb8204041, 0x4e223c20,
        0x6e223420, 0x6e209820, 0x4e228c20, 0x4e2894e6, 0x6e2b9549,
    };

    [self measureBlock:^{
        for (size_t iter = 0; iter < 512; iter++) {
            for (size_t i = 0; i < sizeof(instructions) / sizeof(instructions[0]); i++) {
                a64_instr_t decoded;
                XCTAssertEqual(a64_decode(instructions[i], &decoded), 0);
                XCTAssertEqual(decoded.raw, instructions[i]);
            }
        }
    }];
}

- (void)testLoweringLatency_RealGenerationPath
{
    static const uint32_t instructions[] = {
        0xb8bfc020, 0xb8200041, 0xb8203041, 0x4e223c20, 0x4e228c20,
    };

    [self measureBlock:^{
        for (size_t iter = 0; iter < 128; iter++) {
            tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
            a64_gen_state_t state;
            XCTAssertEqual(a64_gen_init(&state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
            a64_gen_reset(&state, 0x6000);
            for (size_t i = 0; i < sizeof(instructions) / sizeof(instructions[0]); i++) {
                XCTAssertEqual(a64_gen_instruction(&state, instructions[i], 0x6000 + (uint64_t)(i * 4)),
                               A64_GEN_OK);
            }
            XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
        }
    }];
}

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
    tcti_perf_init_context(&mem, &tlb, &cpu);
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
    tcti_perf_init_context(&mem, &tlb, &cpu);
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
