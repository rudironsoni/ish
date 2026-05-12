#import <XCTest/XCTest.h>

#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>
#import <IXLandLinuxRuntime/emu/aarch64/memory.h>
#import <IXLandLinuxRuntime/emu/tlb.h>
#import <IXLandLinuxRuntime/kernel/memory.h>

#include "../../Support/ISAFamilies/BaseScalar/tcti_scalar_runtime_semantic_scenarios.h"
#include "../../Support/ISAFamilies/Memory/tcti_memory_atomic_runtime_semantic_scenarios.h"

@interface TCTIAtomicAndExclusiveSemanticTests : XCTestCase
@end

static void tcti_init_atomic_semantic_context(struct mem *mem, struct tlb *tlb, struct cpu_state *cpu)
{
    mem_init(mem);
    tlb_refresh(tlb, &mem->mmu);
    memset(cpu, 0, sizeof(*cpu));
    cpu->mmu = &mem->mmu;
    cpu->tlb = tlb;
}

@implementation TCTIAtomicAndExclusiveSemanticTests

- (void)assertAtomicPairLoadInstruction:(uint32_t)insn
                                   textPC:(uint64_t)textPC
                                 guestAddr:(uint64_t)guestAddr
                                  baseReg:(int)baseReg
                                   firstRt:(int)firstRt
                                  secondRt:(int)secondRt
                                    first:(uint32_t)first
                                   second:(uint32_t)second
                                  mnemonic:(const char *)mnemonic
{
    struct mem mem;
    struct tlb tlb = {};
    struct cpu_state cpu;
    tcti_init_atomic_semantic_context(&mem, &tlb, &cpu);

    XCTAssertEqual(pt_map_nothing(&mem, PAGE(guestAddr), 1, P_READ | P_WRITE), 0);
    tlb_refresh(&tlb, &mem.mmu);

    cpu.pc = textPC;
    cpu.x[baseReg] = guestAddr;
    XCTAssertEqual(a64_guest_write32(&cpu, &tlb, guestAddr, first), A64_MEM_OK);
    XCTAssertEqual(a64_guest_write32(&cpu, &tlb, guestAddr + 4, second), A64_MEM_OK);

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                   @"%s must execute through the real TCTI atomic path", mnemonic);
    XCTAssertEqual((uint32_t)cpu.x[firstRt], first,
                   @"%s must publish the first loaded word in Rt", mnemonic);
    XCTAssertEqual((uint32_t)cpu.x[secondRt], second,
                   @"%s must publish the second loaded word in Rt2", mnemonic);

    mem_destroy(&mem);
}

- (void)assertAtomicPairStoreInstruction:(uint32_t)insn
                                    textPC:(uint64_t)textPC
                                  guestAddr:(uint64_t)guestAddr
                                   baseReg:(int)baseReg
                                 statusReg:(int)statusReg
                                  firstReg:(int)firstReg
                                 secondReg:(int)secondReg
                                     first:(uint32_t)first
                                    second:(uint32_t)second
                                  mnemonic:(const char *)mnemonic
{
    struct mem mem;
    struct tlb tlb = {};
    struct cpu_state cpu;
    tcti_init_atomic_semantic_context(&mem, &tlb, &cpu);

    XCTAssertEqual(pt_map_nothing(&mem, PAGE(guestAddr), 1, P_READ | P_WRITE), 0);
    tlb_refresh(&tlb, &mem.mmu);

    cpu.pc = textPC;
    cpu.x[baseReg] = guestAddr;
    cpu.x[firstReg] = first;
    cpu.x[secondReg] = second;
    cpu.x[statusReg] = 0xffffffffU;
    cpu.exclusive_addr = guestAddr;
    cpu.exclusive_size = 8;
    cpu.exclusive_valid = 1;
    XCTAssertEqual(a64_guest_write32(&cpu, &tlb, guestAddr, 0xaaaaaaaaU), A64_MEM_OK);
    XCTAssertEqual(a64_guest_write32(&cpu, &tlb, guestAddr + 4, 0xbbbbbbbbU), A64_MEM_OK);

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                   @"%s must execute through the real TCTI atomic path", mnemonic);

    uint32_t storedFirst = 0;
    uint32_t storedSecond = 0;
    XCTAssertEqual(a64_guest_read32(&cpu, &tlb, guestAddr, &storedFirst), A64_MEM_OK);
    XCTAssertEqual(a64_guest_read32(&cpu, &tlb, guestAddr + 4, &storedSecond), A64_MEM_OK);
    XCTAssertEqual((uint32_t)cpu.x[statusReg], 0U,
                   @"%s must report success when the exclusive monitor is live", mnemonic);
    XCTAssertEqual(storedFirst, first, @"%s must store the first payload word", mnemonic);
    XCTAssertEqual(storedSecond, second, @"%s must store the second payload word", mnemonic);

    mem_destroy(&mem);
}

- (void)assertAtomicLoadModifyStoreInstruction:(uint32_t)insn
                                          textPC:(uint64_t)textPC
                                       guestAddr:(uint64_t)guestAddr
                                        sourceReg:(int)sourceReg
                                        resultReg:(int)resultReg
                                          baseReg:(int)baseReg
                                     initialValue:(uint32_t)initialValue
                                      sourceValue:(uint32_t)sourceValue
                                    expectedValue:(uint32_t)expectedValue
                                         mnemonic:(const char *)mnemonic
{
    struct mem mem;
    struct tlb tlb = {};
    struct cpu_state cpu;
    tcti_init_atomic_semantic_context(&mem, &tlb, &cpu);

    XCTAssertEqual(pt_map_nothing(&mem, PAGE(guestAddr), 1, P_READ | P_WRITE), 0);
    tlb_refresh(&tlb, &mem.mmu);

    cpu.pc = textPC;
    cpu.x[sourceReg] = sourceValue;
    cpu.x[resultReg] = 0xdeadbeefU;
    cpu.x[baseReg] = guestAddr;
    XCTAssertEqual(a64_guest_write32(&cpu, &tlb, guestAddr, initialValue), A64_MEM_OK);

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                   @"%s must execute through the real TCTI atomic path", mnemonic);

    uint32_t stored = 0;
    XCTAssertEqual(a64_guest_read32(&cpu, &tlb, guestAddr, &stored), A64_MEM_OK);
    XCTAssertEqual((uint32_t)cpu.x[resultReg], initialValue,
                   @"%s must publish the old memory value in Rt", mnemonic);
    XCTAssertEqual(stored, expectedValue,
                   @"%s must publish the architectural RMW result into guest memory", mnemonic);

    mem_destroy(&mem);
}

- (void)assertAtomicStoreOnlyInstruction:(uint32_t)insn
                                    textPC:(uint64_t)textPC
                                 guestAddr:(uint64_t)guestAddr
                                  sourceReg:(int)sourceReg
                                    baseReg:(int)baseReg
                               initialValue:(uint32_t)initialValue
                                sourceValue:(uint32_t)sourceValue
                              expectedValue:(uint32_t)expectedValue
                                   mnemonic:(const char *)mnemonic
{
    struct mem mem;
    struct tlb tlb = {};
    struct cpu_state cpu;
    tcti_init_atomic_semantic_context(&mem, &tlb, &cpu);

    XCTAssertEqual(pt_map_nothing(&mem, PAGE(guestAddr), 1, P_READ | P_WRITE), 0);
    tlb_refresh(&tlb, &mem.mmu);

    cpu.pc = textPC;
    cpu.x[sourceReg] = sourceValue;
    cpu.x[baseReg] = guestAddr;
    cpu.x[4] = 0xfeedfaceU;
    XCTAssertEqual(a64_guest_write32(&cpu, &tlb, guestAddr, initialValue), A64_MEM_OK);

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                   @"%s must execute through the real TCTI atomic path", mnemonic);

    uint32_t stored = 0;
    XCTAssertEqual(a64_guest_read32(&cpu, &tlb, guestAddr, &stored), A64_MEM_OK);
    XCTAssertEqual(stored, expectedValue,
                   @"%s must publish the architectural write-only RMW result into guest memory",
                   mnemonic);
    XCTAssertEqual((uint32_t)cpu.x[4], 0xfeedfaceU,
                   @"%s must not clobber unrelated general registers", mnemonic);

    mem_destroy(&mem);
}

- (void)testSemanticExecutionContract_MuslMutexLDAXRSTLXRRoundtripsLockWord
{
    XCTAssertEqual(tcti_semantic_case_musl_mutex_ldaxr_stlxr_roundtrip(), 0ULL,
                   @"TCTI must execute musl pthread mutex LDAXR/STLXR lock/unlock/relock "
                    "sequences without leaving the lock word permanently busy");
}

- (void)testSemanticExecutionContract_MuslPthreadMutexLockFastPathReusesStatusRegister
{
    XCTAssertEqual(tcti_semantic_case_musl_pthread_mutex_lock_fast_path(), 0ULL,
                   @"TCTI must execute musl pthread_mutex_lock's fast LDAXR/STLXR path when "
                    "the loaded register is reused as the exclusive-store status register");
}

- (void)testSemanticExecutionContract_LDARWLoadsWordWithoutRequiringPreexistingExclusiveMonitor
{
    enum {
        textPC = 0xa2000,
        guestAddr = 0x230100,
    };

    static const uint32_t insn = 0x88dffc20; // ldar w0, [x1]

    struct mem mem;
    struct tlb tlb = {};
    struct cpu_state cpu;
    tcti_init_atomic_semantic_context(&mem, &tlb, &cpu);

    XCTAssertEqual(pt_map_nothing(&mem, PAGE(guestAddr), 1, P_READ | P_WRITE), 0);
    tlb_refresh(&tlb, &mem.mmu);

    cpu.pc = textPC;
    cpu.x[1] = guestAddr;
    XCTAssertEqual(a64_guest_write32(&cpu, &tlb, guestAddr, 0x7f00aa55U), A64_MEM_OK);

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                   @"LDAR must execute through the real atomic/ordered load/store path");
    XCTAssertEqual((uint32_t)cpu.x[0], 0x7f00aa55U,
                   @"LDAR must publish the loaded word in the destination register");

    mem_destroy(&mem);
}

- (void)testSemanticExecutionContract_STLRWWritesWordWithoutPublishingExclusiveStatusRegister
{
    enum {
        textPC = 0xa2020,
        guestAddr = 0x230200,
    };

    static const uint32_t insn = 0x889ffc62; // stlr w2, [x3]

    struct mem mem;
    struct tlb tlb = {};
    struct cpu_state cpu;
    tcti_init_atomic_semantic_context(&mem, &tlb, &cpu);

    XCTAssertEqual(pt_map_nothing(&mem, PAGE(guestAddr), 1, P_READ | P_WRITE), 0);
    tlb_refresh(&tlb, &mem.mmu);

    cpu.pc = textPC;
    cpu.x[2] = 0x2468ace0U;
    cpu.x[3] = guestAddr;
    cpu.x[0] = 0xfeedfaceU;
    XCTAssertEqual(a64_guest_write32(&cpu, &tlb, guestAddr, 0x11111111U), A64_MEM_OK);

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                   @"STLR must execute through the real atomic/ordered load/store path");

    uint32_t stored = 0;
    XCTAssertEqual(a64_guest_read32(&cpu, &tlb, guestAddr, &stored), A64_MEM_OK);
    XCTAssertEqual(stored, 0x2468ace0U,
                   @"STLR must store the source word to guest memory");
    XCTAssertEqual((uint32_t)cpu.x[0], 0xfeedfaceU,
                   @"STLR is ordered store, not STLXR; it must not clobber an unrelated status "
                    "register");

    mem_destroy(&mem);
}

- (void)testSemanticExecutionContract_CASWWAtomicallySwapsMatchingValue
{
    enum {
        textPC = 0xa2040,
        guestAddr = 0x230300,
    };

    static const uint32_t insn = 0x88a47cc5; // cas w4, w5, [x6]

    struct mem mem;
    struct tlb tlb = {};
    struct cpu_state cpu;
    tcti_init_atomic_semantic_context(&mem, &tlb, &cpu);

    XCTAssertEqual(pt_map_nothing(&mem, PAGE(guestAddr), 1, P_READ | P_WRITE), 0);
    tlb_refresh(&tlb, &mem.mmu);

    cpu.pc = textPC;
    cpu.x[4] = 0x12345678U;
    cpu.x[5] = 0xabcdef01U;
    cpu.x[6] = guestAddr;
    XCTAssertEqual(a64_guest_write32(&cpu, &tlb, guestAddr, 0x12345678U), A64_MEM_OK);

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                   @"CAS must execute through the real atomic/ordered load/store path");

    uint32_t stored = 0;
    XCTAssertEqual(a64_guest_read32(&cpu, &tlb, guestAddr, &stored), A64_MEM_OK);
    XCTAssertEqual(stored, 0xabcdef01U,
                   @"CAS must replace guest memory when the compare register matches");
    XCTAssertEqual((uint32_t)cpu.x[4], 0x12345678U,
                   @"CAS must return the old memory value in the compare register");

    mem_destroy(&mem);
}

- (void)testSemanticExecutionContract_LDAPRWLoadsWordThroughOrderedAcquirePath
{
    [self assertAtomicLoadModifyStoreInstruction:0xb8bfc020
                                          textPC:0xa2060
                                       guestAddr:0x230380
                                        sourceReg:31
                                        resultReg:0
                                          baseReg:1
                                     initialValue:0x13572468U
                                      sourceValue:0
                                    expectedValue:0x13572468U
                                         mnemonic:"ldapr w0, [x1]"];
}

- (void)testSemanticExecutionContract_LDXPWLoadsAdjacentExclusivePair
{
    [self assertAtomicPairLoadInstruction:0x887f0440
                                   textPC:0xa2080
                                 guestAddr:0x230400
                                  baseReg:2
                                   firstRt:0
                                  secondRt:1
                                    first:0x11223344U
                                   second:0x55667788U
                                  mnemonic:"ldxp w0, w1, [x2]"];
}

- (void)testSemanticExecutionContract_STXPWStoresExclusivePairAndPublishesSuccess
{
    [self assertAtomicPairStoreInstruction:0x88200861
                                    textPC:0xa20a0
                                  guestAddr:0x230440
                                   baseReg:3
                                 statusReg:0
                                  firstReg:1
                                 secondReg:2
                                     first:0x89abcdefU
                                    second:0x10293847U
                                  mnemonic:"stxp w0, w1, w2, [x3]"];
}

- (void)testSemanticExecutionContract_LDAXPWLoadsAdjacentAcquirePair
{
    [self assertAtomicPairLoadInstruction:0x887f8440
                                   textPC:0xa20c0
                                 guestAddr:0x230480
                                  baseReg:2
                                   firstRt:0
                                  secondRt:1
                                    first:0xaabbccddu
                                   second:0xeeff0011U
                                  mnemonic:"ldaxp w0, w1, [x2]"];
}

- (void)testSemanticExecutionContract_STLXPWStoresReleasePairAndPublishesSuccess
{
    [self assertAtomicPairStoreInstruction:0x88208861
                                    textPC:0xa20e0
                                  guestAddr:0x2304c0
                                   baseReg:3
                                 statusReg:0
                                  firstReg:1
                                 secondReg:2
                                     first:0x01020304U
                                    second:0x05060708U
                                  mnemonic:"stlxp w0, w1, w2, [x3]"];
}

- (void)testSemanticExecutionContract_SWPWReturnsOldValueAndStoresSource
{
    [self assertAtomicLoadModifyStoreInstruction:0xb8208041
                                          textPC:0xa2100
                                       guestAddr:0x230500
                                        sourceReg:0
                                        resultReg:1
                                          baseReg:2
                                     initialValue:0x11112222U
                                      sourceValue:0x33334444U
                                    expectedValue:0x33334444U
                                         mnemonic:"swp w0, w1, [x2]"];
}

- (void)testSemanticExecutionContract_LDADDWReturnsOldValueAndAccumulates
{
    [self assertAtomicLoadModifyStoreInstruction:0xb8200041
                                          textPC:0xa2120
                                       guestAddr:0x230540
                                        sourceReg:0
                                        resultReg:1
                                          baseReg:2
                                     initialValue:7U
                                      sourceValue:9U
                                    expectedValue:16U
                                         mnemonic:"ldadd w0, w1, [x2]"];
}

- (void)testSemanticExecutionContract_LDCLRWReturnsOldValueAndClearsBits
{
    [self assertAtomicLoadModifyStoreInstruction:0xb8201041
                                          textPC:0xa2140
                                       guestAddr:0x230580
                                        sourceReg:0
                                        resultReg:1
                                          baseReg:2
                                     initialValue:0xff00ff00U
                                      sourceValue:0x0f0f0f0fU
                                    expectedValue:(0xff00ff00U & ~0x0f0f0f0fU)
                                         mnemonic:"ldclr w0, w1, [x2]"];
}

- (void)testSemanticExecutionContract_LDEORWReturnsOldValueAndXorsBits
{
    [self assertAtomicLoadModifyStoreInstruction:0xb8202041
                                          textPC:0xa2160
                                       guestAddr:0x2305c0
                                        sourceReg:0
                                        resultReg:1
                                          baseReg:2
                                     initialValue:0x0ff00ff0U
                                      sourceValue:0x00ff00ffU
                                    expectedValue:(0x0ff00ff0U ^ 0x00ff00ffU)
                                         mnemonic:"ldeor w0, w1, [x2]"];
}

- (void)testSemanticExecutionContract_LDSETWReturnsOldValueAndSetsBits
{
    [self assertAtomicLoadModifyStoreInstruction:0xb8203041
                                          textPC:0xa2180
                                       guestAddr:0x230600
                                        sourceReg:0
                                        resultReg:1
                                          baseReg:2
                                     initialValue:0x00ff0000U
                                      sourceValue:0x0000ff55U
                                    expectedValue:(0x00ff0000U | 0x0000ff55U)
                                         mnemonic:"ldset w0, w1, [x2]"];
}

- (void)testSemanticExecutionContract_LDSMAXWReturnsOldValueAndPublishesSignedMax
{
    [self assertAtomicLoadModifyStoreInstruction:0xb8204041
                                          textPC:0xa21a0
                                       guestAddr:0x230640
                                        sourceReg:0
                                        resultReg:1
                                          baseReg:2
                                     initialValue:(uint32_t)(int32_t)-4
                                      sourceValue:(uint32_t)(int32_t)9
                                    expectedValue:(uint32_t)(int32_t)9
                                         mnemonic:"ldsmax w0, w1, [x2]"];
}

- (void)testSemanticExecutionContract_LDSMINWReturnsOldValueAndPublishesSignedMin
{
    [self assertAtomicLoadModifyStoreInstruction:0xb8205041
                                          textPC:0xa21c0
                                       guestAddr:0x230680
                                        sourceReg:0
                                        resultReg:1
                                          baseReg:2
                                     initialValue:(uint32_t)(int32_t)14
                                      sourceValue:(uint32_t)(int32_t)-7
                                    expectedValue:(uint32_t)(int32_t)-7
                                         mnemonic:"ldsmin w0, w1, [x2]"];
}

- (void)testSemanticExecutionContract_LDUMAXWReturnsOldValueAndPublishesUnsignedMax
{
    [self assertAtomicLoadModifyStoreInstruction:0xb8206041
                                          textPC:0xa21e0
                                       guestAddr:0x2306c0
                                        sourceReg:0
                                        resultReg:1
                                          baseReg:2
                                     initialValue:4U
                                      sourceValue:29U
                                    expectedValue:29U
                                         mnemonic:"ldumax w0, w1, [x2]"];
}

- (void)testSemanticExecutionContract_LDUMINWReturnsOldValueAndPublishesUnsignedMin
{
    [self assertAtomicLoadModifyStoreInstruction:0xb8207041
                                          textPC:0xa2200
                                       guestAddr:0x230700
                                        sourceReg:0
                                        resultReg:1
                                          baseReg:2
                                     initialValue:19U
                                      sourceValue:11U
                                    expectedValue:11U
                                         mnemonic:"ldumin w0, w1, [x2]"];
}

- (void)testSemanticExecutionContract_STADDWAccumulatesWithoutPublishingOldValue
{
    [self assertAtomicStoreOnlyInstruction:0xb820005f
                                    textPC:0xa2220
                                 guestAddr:0x230740
                                  sourceReg:0
                                    baseReg:2
                               initialValue:8U
                                sourceValue:5U
                              expectedValue:13U
                                   mnemonic:"stadd w0, [x2]"];
}

- (void)testSemanticExecutionContract_STCLRWClearsBitsWithoutPublishingOldValue
{
    [self assertAtomicStoreOnlyInstruction:0xb820105f
                                    textPC:0xa2240
                                 guestAddr:0x230780
                                  sourceReg:0
                                    baseReg:2
                               initialValue:0xffff0000U
                                sourceValue:0x00ff0f0fU
                              expectedValue:(0xffff0000U & ~0x00ff0f0fU)
                                   mnemonic:"stclr w0, [x2]"];
}

- (void)testSemanticExecutionContract_STEORWXorsBitsWithoutPublishingOldValue
{
    [self assertAtomicStoreOnlyInstruction:0xb820205f
                                    textPC:0xa2260
                                 guestAddr:0x2307c0
                                  sourceReg:0
                                    baseReg:2
                               initialValue:0x12345678U
                                sourceValue:0x00ff00ffU
                              expectedValue:(0x12345678U ^ 0x00ff00ffU)
                                   mnemonic:"steor w0, [x2]"];
}

- (void)testSemanticExecutionContract_STSETWSetsBitsWithoutPublishingOldValue
{
    [self assertAtomicStoreOnlyInstruction:0xb820305f
                                    textPC:0xa2280
                                 guestAddr:0x230800
                                  sourceReg:0
                                    baseReg:2
                               initialValue:0x12340000U
                                sourceValue:0x0000ff00U
                              expectedValue:(0x12340000U | 0x0000ff00U)
                                   mnemonic:"stset w0, [x2]"];
}

- (void)testSemanticExecutionContract_STSMAXWPublishesSignedMaxWithoutPublishingOldValue
{
    [self assertAtomicStoreOnlyInstruction:0xb820405f
                                    textPC:0xa22a0
                                 guestAddr:0x230840
                                  sourceReg:0
                                    baseReg:2
                               initialValue:(uint32_t)(int32_t)-2
                                sourceValue:(uint32_t)(int32_t)6
                              expectedValue:(uint32_t)(int32_t)6
                                   mnemonic:"stsmax w0, [x2]"];
}

- (void)testSemanticExecutionContract_STSMINWPublishesSignedMinWithoutPublishingOldValue
{
    [self assertAtomicStoreOnlyInstruction:0xb820505f
                                    textPC:0xa22c0
                                 guestAddr:0x230880
                                  sourceReg:0
                                    baseReg:2
                               initialValue:(uint32_t)(int32_t)22
                                sourceValue:(uint32_t)(int32_t)-8
                              expectedValue:(uint32_t)(int32_t)-8
                                   mnemonic:"stsmin w0, [x2]"];
}

- (void)testSemanticExecutionContract_STUMAXWPublishesUnsignedMaxWithoutPublishingOldValue
{
    [self assertAtomicStoreOnlyInstruction:0xb820605f
                                    textPC:0xa22e0
                                 guestAddr:0x2308c0
                                  sourceReg:0
                                    baseReg:2
                               initialValue:2U
                                sourceValue:99U
                              expectedValue:99U
                                   mnemonic:"stumax w0, [x2]"];
}

- (void)testSemanticExecutionContract_STUMINWPublishesUnsignedMinWithoutPublishingOldValue
{
    [self assertAtomicStoreOnlyInstruction:0xb820705f
                                    textPC:0xa2300
                                 guestAddr:0x230900
                                  sourceReg:0
                                    baseReg:2
                               initialValue:33U
                                sourceValue:12U
                              expectedValue:12U
                                   mnemonic:"stumin w0, [x2]"];
}

@end
