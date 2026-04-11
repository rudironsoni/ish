#import <XCTest/XCTest.h>

#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>
#import <IXLandLinuxRuntime/emu/aarch64/decode.h>
#import <IXLandLinuxRuntime/emu/aarch64/memory.h>
#import <IXLandLinuxRuntime/emu/tlb.h>
#import <IXLandLinuxRuntime/tcti/aarch64/gen.h>
#import <IXLandLinuxRuntime/tcti/frame.h>
#import <IXLandLinuxRuntime/tcti/gadgets_tcti.h>

#define TEST_MEMORY_SIZE (64 * 1024)
#define TEST_MEMORY_BASE 0x1000

@interface SemanticMicroFunctionalTests : XCTestCase
@end

static uint8_t test_memory[TEST_MEMORY_SIZE];

static void reset_test_memory(void)
{
    memset(test_memory, 0, sizeof(test_memory));
}

static int is_test_addr_valid(uint64_t addr, size_t size)
{
    if (addr < TEST_MEMORY_BASE)
        return 0;
    uint64_t offset = addr - TEST_MEMORY_BASE;
    if (offset + size > TEST_MEMORY_SIZE)
        return 0;
    return 1;
}

static int map_test_page(struct cpu_state *cpu, struct tlb *tlb, uint64_t guest_addr, int writable)
{
    if (guest_addr < TEST_MEMORY_BASE || guest_addr >= TEST_MEMORY_BASE + TEST_MEMORY_SIZE)
        return -1;

    uint64_t page_base = guest_addr & ~0xFFFULL;
    int tlb_idx = TLB_INDEX(guest_addr);

    tlb->entries[tlb_idx].page = page_base;
    tlb->entries[tlb_idx].page_if_writable = writable ? page_base : TLB_PAGE_EMPTY;
    tlb->entries[tlb_idx].data_minus_addr = (uintptr_t)test_memory - TEST_MEMORY_BASE;
    return 0;
}

static int seed_test_instruction(uint64_t guest_pc, uint32_t insn_word)
{
    if (!is_test_addr_valid(guest_pc, sizeof(insn_word)))
        return -1;

    uint64_t offset = guest_pc - TEST_MEMORY_BASE;
    memcpy(&test_memory[offset], &insn_word, sizeof(insn_word));
    return 0;
}

typedef struct {
    const char *name;
    uint32_t insn_word;
    uint64_t initial_regs[31];
    uint64_t initial_pc;
    uint64_t expected_regs[31];
    uint64_t expected_pc;
    int expected_n;
    int expected_z;
    int expected_c;
    int expected_v;
} semantic_exec_case_t;

static void assertSemanticExecCase(XCTestCase *tc, const semantic_exec_case_t c)
{
    reset_test_memory();

    static struct tlb exec_tlb;
    static struct mmu exec_mmu;
    memset(&exec_mmu, 0, sizeof(exec_mmu));
    memset(&exec_tlb, 0, sizeof(exec_tlb));
    exec_tlb.mmu = &exec_mmu;

    struct cpu_state cpu = { 0 };
    cpu.mmu = &exec_mmu;
    cpu.tlb = &exec_tlb;

    for (int i = 0; i < 31; i++) {
        cpu.x[i] = c.initial_regs[i];
    }
    cpu.pc = c.initial_pc;

    XCTAssertEqual(map_test_page(&cpu, &exec_tlb, cpu.pc, 0), 0);
    XCTAssertEqual(seed_test_instruction(cpu.pc, c.insn_word), 0);

    a64_instr_t instr;
    XCTAssertEqual(a64_decode(c.insn_word, &instr), 0);

    if (instr.cat == A64_LD_ST) {
        uint64_t data_addr = cpu.x[instr.Rn];
        XCTAssertEqual(map_test_page(&cpu, &exec_tlb, data_addr, 1), 0);
    }

    tcti_gadget_t gadget_buffer[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t gen_state;

    XCTAssertEqual(a64_gen_init(&gen_state, gadget_buffer, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(&gen_state, cpu.pc);

    int gen_ret = a64_gen_instruction(&gen_state, c.insn_word, cpu.pc);
    XCTAssertGreaterThanOrEqual(gen_ret, 0);

    extern tcti_gadget_t gadget_exit;
    gadget_buffer[gen_state.num_gadgets] = gadget_exit;
    size_t num_gadgets = gen_state.num_gadgets + 1;

    tcti_entry_block(gadget_buffer, &cpu);

    XCTAssertEqual(cpu.pc, c.expected_pc);

    for (int i = 0; i < 31; i++) {
        XCTAssertEqual(cpu.x[i], c.expected_regs[i], @"Register x%d mismatch", i);
    }

    XCTAssertEqual(cpu.n, c.expected_n);
    XCTAssertEqual(cpu.z, c.expected_z);
    XCTAssertEqual(cpu.c, c.expected_c);
    XCTAssertEqual(cpu.v, c.expected_v);

    XCTAssertEqual(cpu.tcti_exit_reason, 0);
}

@implementation SemanticMicroFunctionalTests

- (void)testEXEC001SingleALU
{
    semantic_exec_case_t c = {
        .name = "EXEC-001",
        .insn_word = 0x91000420,
        .initial_regs = { 0, 5, 0 },
        .initial_pc = 0x1000,
        .expected_regs = { 6, 5, 0 },
        .expected_pc = 0x1004,
        .expected_n = 0,
        .expected_z = 0,
        .expected_c = 0,
        .expected_v = 0
    };
    assertSemanticExecCase(self, c);
}

- (void)testEXEC002CmpFlags
{
    semantic_exec_case_t c = {
        .name = "EXEC-002",
        .insn_word = 0xf1000c1f,
        .initial_regs = { 5, 3, 0 },
        .initial_pc = 0x1000,
        .expected_regs = { 5, 3, 0 },
        .expected_pc = 0x1004,
        .expected_n = 0,
        .expected_z = 0,
        .expected_c = 1,
        .expected_v = 0
    };
    assertSemanticExecCase(self, c);
}

- (void)testEXEC003StrPostIndex
{
    semantic_exec_case_t c = {
        .name = "EXEC-003",
        .insn_word = 0xf8840001,
        .initial_regs = { 0, 0xDEADBEEFCAFEBABEULL, 0x2000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
        .initial_pc = 0x4000,
        .expected_regs = { 0, 0xDEADBEEFCAFEBABEULL, 0x2008, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
        .expected_pc = 0x4004,
        .expected_n = 0,
        .expected_z = 0,
        .expected_c = 0,
        .expected_v = 0
    };
    assertSemanticExecCase(self, c);
}

- (void)testEXEC004StrCmpBneLoop
{
    semantic_exec_case_t c = {
        .name = "EXEC-004",
        .insn_word = 0xf8800002,
        .initial_regs = { 0x2000, 5, 0xABCD1234ABCD1234ULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
        .initial_pc = 0x4000,
        .expected_regs = { 0x2008, 5, 0xABCD1234ABCD1234ULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
        .expected_pc = 0x4004,
        .expected_n = 0,
        .expected_z = 0,
        .expected_c = 0,
        .expected_v = 0
    };
    assertSemanticExecCase(self, c);
}

- (void)testEXEC005HotRegisterSync
{
    semantic_exec_case_t c = {
        .name = "EXEC-005",
        .insn_word = 0xf8840001,
        .initial_regs = { 0x2000, 0xDEADBEEFCAFEBABEULL, 0x123456789ABCDEF0ULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
        .initial_pc = 0x4000,
        .expected_regs = { 0x2008, 0xDEADBEEFCAFEBABEULL, 0x123456789ABCDEF0ULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
        .expected_pc = 0x4004,
        .expected_n = 0,
        .expected_z = 0,
        .expected_c = 0,
        .expected_v = 0
    };
    assertSemanticExecCase(self, c);
}

@end
