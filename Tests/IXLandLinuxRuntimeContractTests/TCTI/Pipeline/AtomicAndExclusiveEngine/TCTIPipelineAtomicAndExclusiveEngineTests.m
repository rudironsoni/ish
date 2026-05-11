#import <XCTest/XCTest.h>

#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>
#import <IXLandLinuxRuntime/emu/aarch64/decode.h>
#import <IXLandLinuxRuntime/emu/aarch64/memory.h>
#import <IXLandLinuxRuntime/kernel/memory.h>
#import <IXLandLinuxRuntime/emu/tlb.h>
#import <IXLandLinuxRuntime/tcti/aarch64/gen.h>

typedef void (*tcti_gadget_t)(void);

static void tcti_init_atomic_context(struct mem *mem, struct tlb *tlb, struct cpu_state *cpu)
{
    mem_init(mem);
    tlb_refresh(tlb, &mem->mmu);
    memset(cpu, 0, sizeof(*cpu));
    cpu->mmu = &mem->mmu;
    cpu->tlb = tlb;
}

@interface TCTIPipelineAtomicAndExclusiveEngineTests : XCTestCase
@end

@implementation TCTIPipelineAtomicAndExclusiveEngineTests

- (a64_instr_t)decodeInstruction:(uint32_t)insn
{
    a64_instr_t decoded;
    XCTAssertEqual(a64_decode(insn, &decoded), 0);
    return decoded;
}

- (void)generateInstruction:(uint32_t)insn
                       atPC:(uint64_t)pc
                      state:(a64_gen_state_t *)state
                    gadgets:(tcti_gadget_t *)gadgets
{
    XCTAssertEqual(a64_gen_init(state, gadgets, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(state, pc);
    XCTAssertEqual(a64_gen_instruction(state, insn, pc), A64_GEN_OK);
    XCTAssertGreaterThan(state->num_gadgets, (size_t)0);
}

- (void)testAtomicAndExclusiveEngineContract_LDAXRAndSTLXRDecodeAndLowerAsAtomicSequence
{
    struct {
        uint32_t raw;
        uint64_t pc;
        int rd;
        int rn;
        int rm;
    } cases[] = {
        {0x885ffc62, 0x63808, 2, 3, 31}, // ldaxr w2, [x3]
        {0x8800fc61, 0x6380c, 1, 3, 0},  // stlxr w0, w1, [x3]
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        a64_instr_t decoded = [self decodeInstruction:cases[i].raw];
        XCTAssertEqual(decoded.cat, A64_LD_ST);
        XCTAssertEqual(decoded.subtype, A64_LDST_ATOMIC);
        XCTAssertEqual(decoded.Rd, cases[i].rd);
        XCTAssertEqual(decoded.Rn, cases[i].rn);
        XCTAssertEqual(decoded.Rm, cases[i].rm);
        XCTAssertEqual(decoded.size, A64_SIZE_W);
        XCTAssertFalse(decoded.is_64bit);

        tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
        a64_gen_state_t state;
        [self generateInstruction:cases[i].raw atPC:cases[i].pc state:&state gadgets:gadgets];
        XCTAssertFalse(state.is_complete);
    }
}

- (void)testAtomicAndExclusiveEngineContract_SuccessPathPublishesStatusMemoryAndMonitorState
{
    static const uint32_t insns[] = {
        0x885ffc62, // ldaxr w2, [x3]
        0x8800fc61, // stlxr w0, w1, [x3]
    };

    enum {
        guest_addr = 0x220104,
    };
    const uint32_t original = 0x12345678U;
    const uint32_t replacement = 0x89abcdefU;

    struct mem mem;
    struct tlb tlb = {};
    struct cpu_state cpu;
    tcti_init_atomic_context(&mem, &tlb, &cpu);

    XCTAssertEqual(pt_map_nothing(&mem, PAGE(guest_addr), 1, P_READ | P_WRITE), 0);
    tlb_refresh(&tlb, &mem.mmu);

    cpu.pc = 0x63808;
    cpu.x[1] = replacement;
    cpu.x[3] = guest_addr;

    XCTAssertEqual(a64_guest_write32(&cpu, &tlb, guest_addr, original), A64_MEM_OK);
    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, cpu.pc, insns,
                                              sizeof(insns) / sizeof(insns[0])),
                   0);

    uint32_t stored = 0;
    XCTAssertEqual(a64_guest_read32(&cpu, &tlb, guest_addr, &stored), A64_MEM_OK);
    XCTAssertEqual((uint32_t)cpu.x[2], original);
    XCTAssertEqual((uint32_t)cpu.x[0], 0U);
    XCTAssertEqual(stored, replacement);
    XCTAssertEqual(cpu.exclusive_valid, 0,
                   @"successful STLXR must clear the monitor before the block exits");

    mem_destroy(&mem);
}

- (void)testAtomicAndExclusiveEngineContract_FailedSTLXRLeavesMemoryUntouchedAndReturnsFailure
{
    static const uint32_t insn = 0x8800fc61; // stlxr w0, w1, [x3]

    enum {
        guest_addr = 0x220208,
    };
    const uint32_t original = 0x13579bdfU;
    const uint32_t replacement = 0x2468ace0U;

    struct mem mem;
    struct tlb tlb = {};
    struct cpu_state cpu;
    tcti_init_atomic_context(&mem, &tlb, &cpu);

    XCTAssertEqual(pt_map_nothing(&mem, PAGE(guest_addr), 1, P_READ | P_WRITE), 0);
    tlb_refresh(&tlb, &mem.mmu);

    cpu.pc = 0x6380c;
    cpu.x[1] = replacement;
    cpu.x[3] = guest_addr;
    cpu.exclusive_valid = 0;

    XCTAssertEqual(a64_guest_write32(&cpu, &tlb, guest_addr, original), A64_MEM_OK);
    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, cpu.pc, &insn, 1), 0);

    uint32_t stored = 0;
    XCTAssertEqual(a64_guest_read32(&cpu, &tlb, guest_addr, &stored), A64_MEM_OK);
    XCTAssertEqual((uint32_t)cpu.x[0], 1U,
                   @"STLXR without a live exclusive monitor must publish architectural failure "
                    "status");
    XCTAssertEqual(stored, original);
    XCTAssertEqual(cpu.exclusive_valid, 0);

    mem_destroy(&mem);
}

@end
