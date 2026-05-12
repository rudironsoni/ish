#import <XCTest/XCTest.h>

#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>
#import <IXLandLinuxRuntime/emu/aarch64/memory.h>
#import <IXLandLinuxRuntime/emu/tlb.h>
#import <IXLandLinuxRuntime/kernel/memory.h>

@interface TCTIVectorMemorySemanticTests : XCTestCase
@end

@implementation TCTIVectorMemorySemanticTests

- (void)testSemanticExecutionContract_LDRQLoadsFull128BitVector
{
    enum {
        textPC = 0x93000,
        dataBase = 0x240000,
    };

    static const uint32_t insn = 0x3dc00000; // ldr q0, [x0]

    struct mem mem;
    mem_init(&mem);
    XCTAssertEqual(pt_map_nothing(&mem, PAGE(dataBase), 1, P_READ | P_WRITE), 0);

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.x[0] = dataBase;

    XCTAssertEqual(a64_guest_write64(&cpu, &tlb, dataBase + 0, 0x0123456789abcdefULL), A64_MEM_OK);
    XCTAssertEqual(a64_guest_write64(&cpu, &tlb, dataBase + 8, 0xfedcba9876543210ULL), A64_MEM_OK);

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                   @"SIMD LDR must load the full 128-bit vector payload through the real TCTI "
                    "memory path so guest vectorized memcpy/memset code can consume it");
    XCTAssertEqual(cpu.vregs[0].d[0], 0x0123456789abcdefULL);
    XCTAssertEqual(cpu.vregs[0].d[1], 0xfedcba9876543210ULL);

    mem_destroy(&mem);
}

- (void)testSemanticExecutionContract_ST1StoresFull128BitVector
{
    enum {
        textPC = 0x93020,
        dataBase = 0x241000,
    };

    static const uint32_t insn = 0x4c007020; // st1 { v0.16b }, [x1]

    struct mem mem;
    mem_init(&mem);
    XCTAssertEqual(pt_map_nothing(&mem, PAGE(dataBase), 1, P_READ | P_WRITE), 0);

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.x[1] = dataBase;
    cpu.vregs[0].d[0] = 0x1111222233334444ULL;
    cpu.vregs[0].d[1] = 0xaaaabbbbccccddddULL;

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                   @"ST1 must publish the full 128-bit SIMD register to guest memory through "
                    "the TCTI vector-memory path");

    uint64_t low = 0;
    uint64_t high = 0;
    XCTAssertEqual(a64_guest_read64(&cpu, &tlb, dataBase + 0, &low), A64_MEM_OK);
    XCTAssertEqual(a64_guest_read64(&cpu, &tlb, dataBase + 8, &high), A64_MEM_OK);
    XCTAssertEqual(low, 0x1111222233334444ULL);
    XCTAssertEqual(high, 0xaaaabbbbccccddddULL);

    mem_destroy(&mem);
}

- (void)testSemanticExecutionContract_LDURQLoadsFull128BitVectorFromNegativeOffset
{
    enum { textPC = 0x93040, dataBase = 0x242000 };
    static const uint32_t insn = 0x3cdf0020; // ldur q0, [x1, #-16]

    struct mem mem;
    mem_init(&mem);
    XCTAssertEqual(pt_map_nothing(&mem, PAGE(dataBase), 1, P_READ | P_WRITE), 0);

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.x[1] = dataBase + 16;

    XCTAssertEqual(a64_guest_write64(&cpu, &tlb, dataBase + 0, 0x0102030405060708ULL), A64_MEM_OK);
    XCTAssertEqual(a64_guest_write64(&cpu, &tlb, dataBase + 8, 0x1112131415161718ULL), A64_MEM_OK);

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0);
    XCTAssertEqual(cpu.vregs[0].d[0], 0x0102030405060708ULL);
    XCTAssertEqual(cpu.vregs[0].d[1], 0x1112131415161718ULL);

    mem_destroy(&mem);
}

- (void)testSemanticExecutionContract_LDPQLoadsAdjacent128BitVectors
{
    enum { textPC = 0x93060, dataBase = 0x243000 };
    static const uint32_t insn = 0xad400440; // ldp q0, q1, [x2]

    struct mem mem;
    mem_init(&mem);
    XCTAssertEqual(pt_map_nothing(&mem, PAGE(dataBase), 1, P_READ | P_WRITE), 0);

    struct tlb tlb = {};
    tlb_refresh(&tlb, &mem.mmu);

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.mmu = &mem.mmu;
    cpu.tlb = &tlb;
    cpu.x[2] = dataBase;

    XCTAssertEqual(a64_guest_write64(&cpu, &tlb, dataBase + 0, 0x0011223344556677ULL), A64_MEM_OK);
    XCTAssertEqual(a64_guest_write64(&cpu, &tlb, dataBase + 8, 0x8899aabbccddeeffULL), A64_MEM_OK);
    XCTAssertEqual(a64_guest_write64(&cpu, &tlb, dataBase + 16, 0x1021324354657687ULL), A64_MEM_OK);
    XCTAssertEqual(a64_guest_write64(&cpu, &tlb, dataBase + 24, 0x98a9babbdcddededULL), A64_MEM_OK);

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0);
    XCTAssertEqual(cpu.vregs[0].d[0], 0x0011223344556677ULL);
    XCTAssertEqual(cpu.vregs[0].d[1], 0x8899aabbccddeeffULL);
    XCTAssertEqual(cpu.vregs[1].d[0], 0x1021324354657687ULL);
    XCTAssertEqual(cpu.vregs[1].d[1], 0x98a9babbdcddededULL);

    mem_destroy(&mem);
}

@end
