#import <XCTest/XCTest.h>

#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>
#import <IXLandLinuxRuntime/emu/aarch64/decode.h>
#import <IXLandLinuxRuntime/emu/aarch64/memory.h>
#import <IXLandLinuxRuntime/kernel/memory.h>
#import <IXLandLinuxRuntime/emu/tlb.h>
#import <IXLandLinuxRuntime/tcti/aarch64/gen.h>

typedef void (*tcti_gadget_t)(void);

static void tcti_init_guest_context(struct mem *mem, struct tlb *tlb, struct cpu_state *cpu)
{
    mem_init(mem);
    tlb_refresh(tlb, &mem->mmu);
    memset(cpu, 0, sizeof(*cpu));
    cpu->mmu = &mem->mmu;
    cpu->tlb = tlb;
}

@interface TCTIPipelineExitWritebackTests : XCTestCase
@end

@implementation TCTIPipelineExitWritebackTests

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

- (void)testExitWritebackContract_STRPostIndexDecodesLowersAndPublishesUpdatedBaseOnExit
{
    static const uint32_t insn = 0xf800841f; // str xzr, [x0], #8

    a64_instr_t decoded = [self decodeInstruction:insn];
    XCTAssertEqual(decoded.cat, A64_LD_ST);
    XCTAssertEqual(decoded.subtype, A64_LDST_SINGLE);
    XCTAssertEqual(decoded.idx_mode, A64_POST_INDEX);
    XCTAssertEqual(decoded.Rd, 31);
    XCTAssertEqual(decoded.Rn, 0);

    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:insn atPC:0x7c034 state:&state gadgets:gadgets];

    enum {
        guest_addr = 0x150000,
    };

    struct mem mem;
    struct tlb tlb = {};
    struct cpu_state cpu;
    tcti_init_guest_context(&mem, &tlb, &cpu);

    XCTAssertEqual(pt_map_nothing(&mem, PAGE(guest_addr), 1, P_READ | P_WRITE), 0);
    tlb_refresh(&tlb, &mem.mmu);

    cpu.pc = 0x7c034;
    cpu.x[0] = guest_addr;

    XCTAssertEqual(a64_guest_write64(&cpu, &tlb, guest_addr, 0xaaaaaaaaaaaaaaaaULL), A64_MEM_OK);
    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, cpu.pc, &insn, 1), 0);

    uint64_t stored = UINT64_MAX;
    XCTAssertEqual(a64_guest_read64(&cpu, &tlb, guest_addr, &stored), A64_MEM_OK);
    XCTAssertEqual(stored, 0ULL);
    XCTAssertEqual(cpu.x[0], (uint64_t)guest_addr + 8ULL,
                   @"exit writeback must publish the post-indexed base update when the block "
                    "returns through the real runtime path");

    mem_destroy(&mem);
}

- (void)testExitWritebackContract_LDPPostIndexAliasBaseUsesOriginalAddressThenPublishesWriteback
{
    static const uint32_t insn = 0xa8c10400; // ldp x0, x1, [x0], #16

    a64_instr_t decoded = [self decodeInstruction:insn];
    XCTAssertEqual(decoded.cat, A64_LD_ST);
    XCTAssertEqual(decoded.subtype, A64_LDST_PAIR);
    XCTAssertEqual(decoded.idx_mode, A64_POST_INDEX);
    XCTAssertTrue(decoded.is_pair);
    XCTAssertEqual(decoded.Rd, 0);
    XCTAssertEqual(decoded.Rn, 0);
    XCTAssertEqual(decoded.Rm, 1);

    tcti_gadget_t gadgets[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    [self generateInstruction:insn atPC:0x565b3550 state:&state gadgets:gadgets];

    enum {
        guest_addr = 0x160000,
    };
    const uint64_t first_value = 0x170000ULL;
    const uint64_t second_value = 0x4444555566667777ULL;

    struct mem mem;
    struct tlb tlb = {};
    struct cpu_state cpu;
    tcti_init_guest_context(&mem, &tlb, &cpu);

    XCTAssertEqual(pt_map_nothing(&mem, PAGE(guest_addr), 1, P_READ | P_WRITE), 0);
    tlb_refresh(&tlb, &mem.mmu);

    cpu.pc = 0x565b3550;
    cpu.x[0] = guest_addr;

    XCTAssertEqual(a64_guest_write64(&cpu, &tlb, guest_addr, first_value), A64_MEM_OK);
    XCTAssertEqual(a64_guest_write64(&cpu, &tlb, guest_addr + 8, second_value), A64_MEM_OK);
    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, cpu.pc, &insn, 1), 0);

    XCTAssertEqual(cpu.x[0], (uint64_t)guest_addr + 16ULL,
                   @"writeback must use the architectural base update, not the aliased first "
                    "destination load result");
    XCTAssertEqual(cpu.x[1], second_value);

    mem_destroy(&mem);
}

@end
