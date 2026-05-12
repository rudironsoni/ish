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

static uint64_t tcti_atomic_mask_for_size(int size)
{
    switch (size) {
    case A64_SIZE_B:
        return 0xffULL;
    case A64_SIZE_H:
        return 0xffffULL;
    case A64_SIZE_W:
    default:
        return 0xffffffffULL;
    }
}

static uint64_t tcti_atomic_sign_extend_for_size(uint64_t value, int size)
{
    switch (size) {
    case A64_SIZE_B:
        return (uint64_t)(int64_t)(int8_t)(uint8_t)value;
    case A64_SIZE_H:
        return (uint64_t)(int64_t)(int16_t)(uint16_t)value;
    case A64_SIZE_W:
    default:
        return (uint64_t)(int64_t)(int32_t)(uint32_t)value;
    }
}

static int tcti_atomic_guest_write_width(struct cpu_state *cpu, struct tlb *tlb, uint64_t addr, int size,
                                         uint64_t value)
{
    switch (size) {
    case A64_SIZE_B:
        return a64_guest_write8(cpu, tlb, addr, (uint8_t)value);
    case A64_SIZE_H:
        return a64_guest_write16(cpu, tlb, addr, (uint16_t)value);
    case A64_SIZE_W:
    default:
        return a64_guest_write32(cpu, tlb, addr, (uint32_t)value);
    }
}

static int tcti_atomic_guest_read_width(struct cpu_state *cpu, struct tlb *tlb, uint64_t addr, int size,
                                        uint64_t *value)
{
    switch (size) {
    case A64_SIZE_B: {
        uint8_t tmp = 0;
        int rc = a64_guest_read8(cpu, tlb, addr, &tmp);
        *value = tmp;
        return rc;
    }
    case A64_SIZE_H: {
        uint16_t tmp = 0;
        int rc = a64_guest_read16(cpu, tlb, addr, &tmp);
        *value = tmp;
        return rc;
    }
    case A64_SIZE_W:
    default: {
        uint32_t tmp = 0;
        int rc = a64_guest_read32(cpu, tlb, addr, &tmp);
        *value = tmp;
        return rc;
    }
    }
}

typedef NS_ENUM(NSUInteger, TCTIAtomicRMWKind) {
    TCTIAtomicRMWKindSwap,
    TCTIAtomicRMWKindAdd,
    TCTIAtomicRMWKindClear,
    TCTIAtomicRMWKindEor,
    TCTIAtomicRMWKindSet,
    TCTIAtomicRMWKindSMax,
    TCTIAtomicRMWKindSMin,
    TCTIAtomicRMWKindUMax,
    TCTIAtomicRMWKindUMin,
};

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

- (uint64_t)applyAtomicRMWKind:(TCTIAtomicRMWKind)kind oldValue:(uint64_t)oldValue sourceValue:(uint64_t)sourceValue size:(int)size
{
    uint64_t mask = tcti_atomic_mask_for_size(size);
    uint64_t oldMasked = oldValue & mask;
    uint64_t sourceMasked = sourceValue & mask;
    switch (kind) {
    case TCTIAtomicRMWKindSwap:
        return sourceMasked;
    case TCTIAtomicRMWKindAdd:
        return (oldMasked + sourceMasked) & mask;
    case TCTIAtomicRMWKindClear:
        return oldMasked & ~sourceMasked;
    case TCTIAtomicRMWKindEor:
        return (oldMasked ^ sourceMasked) & mask;
    case TCTIAtomicRMWKindSet:
        return oldMasked | sourceMasked;
    case TCTIAtomicRMWKindSMax:
        return ((int64_t)tcti_atomic_sign_extend_for_size(oldMasked, size) >
                (int64_t)tcti_atomic_sign_extend_for_size(sourceMasked, size))
                   ? oldMasked
                   : sourceMasked;
    case TCTIAtomicRMWKindSMin:
        return ((int64_t)tcti_atomic_sign_extend_for_size(oldMasked, size) <
                (int64_t)tcti_atomic_sign_extend_for_size(sourceMasked, size))
                   ? oldMasked
                   : sourceMasked;
    case TCTIAtomicRMWKindUMax:
        return oldMasked > sourceMasked ? oldMasked : sourceMasked;
    case TCTIAtomicRMWKindUMin:
        return oldMasked < sourceMasked ? oldMasked : sourceMasked;
    }
}

- (void)assertAtomicWidthLoadInstruction:(uint32_t)insn
                                   textPC:(uint64_t)textPC
                                guestAddr:(uint64_t)guestAddr
                                  baseReg:(int)baseReg
                                   destReg:(int)destReg
                                     size:(int)size
                                memValue:(uint64_t)memValue
                               expected:(uint64_t)expected
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
    XCTAssertEqual(tcti_atomic_guest_write_width(&cpu, &tlb, guestAddr, size, memValue), A64_MEM_OK);
    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                   @"%s must execute through the real TCTI atomic path", mnemonic);
    XCTAssertEqual(cpu.x[destReg], expected);
    mem_destroy(&mem);
}

- (void)assertAtomicOrderedStoreInstruction:(uint32_t)insn
                                       textPC:(uint64_t)textPC
                                    guestAddr:(uint64_t)guestAddr
                                      baseReg:(int)baseReg
                                    sourceReg:(int)sourceReg
                                        size:(int)size
                                   sourceValue:(uint64_t)sourceValue
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
    cpu.x[sourceReg] = sourceValue;
    cpu.x[30] = 0xfeedfaceU;
    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                   @"%s must execute through the real TCTI atomic path", mnemonic);
    uint64_t stored = 0;
    XCTAssertEqual(tcti_atomic_guest_read_width(&cpu, &tlb, guestAddr, size, &stored), A64_MEM_OK);
    XCTAssertEqual(stored, (sourceValue & tcti_atomic_mask_for_size(size)));
    XCTAssertEqual((uint32_t)cpu.x[30], 0xfeedfaceU);
    mem_destroy(&mem);
}

- (void)assertAtomicExclusiveStoreInstruction:(uint32_t)insn
                                         textPC:(uint64_t)textPC
                                      guestAddr:(uint64_t)guestAddr
                                        baseReg:(int)baseReg
                                      statusReg:(int)statusReg
                                      sourceReg:(int)sourceReg
                                          size:(int)size
                                     sourceValue:(uint64_t)sourceValue
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
    cpu.x[statusReg] = UINT32_MAX;
    cpu.x[sourceReg] = sourceValue;
    cpu.exclusive_addr = guestAddr;
    cpu.exclusive_size = (size == A64_SIZE_B ? 1 : size == A64_SIZE_H ? 2 : 4);
    cpu.exclusive_valid = 1;
    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                   @"%s must execute through the real TCTI atomic path", mnemonic);
    uint64_t stored = 0;
    XCTAssertEqual(tcti_atomic_guest_read_width(&cpu, &tlb, guestAddr, size, &stored), A64_MEM_OK);
    XCTAssertEqual((uint32_t)cpu.x[statusReg], 0U);
    XCTAssertEqual(stored, (sourceValue & tcti_atomic_mask_for_size(size)));
    mem_destroy(&mem);
}

- (void)assertAtomicWidthCompareSwapInstruction:(uint32_t)insn
                                           textPC:(uint64_t)textPC
                                        guestAddr:(uint64_t)guestAddr
                                         baseReg:(int)baseReg
                                      compareReg:(int)compareReg
                                         swapReg:(int)swapReg
                                           size:(int)size
                                      initialValue:(uint64_t)initialValue
                                       swapValue:(uint64_t)swapValue
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
    cpu.x[compareReg] = initialValue;
    cpu.x[swapReg] = swapValue;
    XCTAssertEqual(tcti_atomic_guest_write_width(&cpu, &tlb, guestAddr, size, initialValue), A64_MEM_OK);
    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                   @"%s must execute through the real TCTI atomic path", mnemonic);
    uint64_t stored = 0;
    XCTAssertEqual(tcti_atomic_guest_read_width(&cpu, &tlb, guestAddr, size, &stored), A64_MEM_OK);
    XCTAssertEqual(stored, (swapValue & tcti_atomic_mask_for_size(size)));
    XCTAssertEqual(cpu.x[compareReg], (initialValue & tcti_atomic_mask_for_size(size)));
    mem_destroy(&mem);
}

- (void)assertAtomicWidthRMWInstruction:(uint32_t)insn
                                   textPC:(uint64_t)textPC
                                guestAddr:(uint64_t)guestAddr
                                  baseReg:(int)baseReg
                                sourceReg:(int)sourceReg
                                resultReg:(int)resultReg
                                    size:(int)size
                               initialValue:(uint64_t)initialValue
                                sourceValue:(uint64_t)sourceValue
                                       kind:(TCTIAtomicRMWKind)kind
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
    cpu.x[sourceReg] = sourceValue;
    cpu.x[resultReg] = 0;
    XCTAssertEqual(tcti_atomic_guest_write_width(&cpu, &tlb, guestAddr, size, initialValue), A64_MEM_OK);
    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                   @"%s must execute through the real TCTI atomic path", mnemonic);
    uint64_t stored = 0;
    XCTAssertEqual(tcti_atomic_guest_read_width(&cpu, &tlb, guestAddr, size, &stored), A64_MEM_OK);
    XCTAssertEqual(cpu.x[resultReg], (initialValue & tcti_atomic_mask_for_size(size)));
    XCTAssertEqual(stored, [self applyAtomicRMWKind:kind
                                           oldValue:initialValue
                                        sourceValue:sourceValue
                                               size:size]);
    mem_destroy(&mem);
}

- (void)assertAtomicWidthStoreOnlyInstruction:(uint32_t)insn
                                         textPC:(uint64_t)textPC
                                      guestAddr:(uint64_t)guestAddr
                                        baseReg:(int)baseReg
                                      sourceReg:(int)sourceReg
                                          size:(int)size
                                     initialValue:(uint64_t)initialValue
                                      sourceValue:(uint64_t)sourceValue
                                             kind:(TCTIAtomicRMWKind)kind
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
    cpu.x[sourceReg] = sourceValue;
    cpu.x[30] = 0xfeedfaceU;
    XCTAssertEqual(tcti_atomic_guest_write_width(&cpu, &tlb, guestAddr, size, initialValue), A64_MEM_OK);
    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                   @"%s must execute through the real TCTI atomic path", mnemonic);
    uint64_t stored = 0;
    XCTAssertEqual(tcti_atomic_guest_read_width(&cpu, &tlb, guestAddr, size, &stored), A64_MEM_OK);
    XCTAssertEqual(stored, [self applyAtomicRMWKind:kind
                                           oldValue:initialValue
                                        sourceValue:sourceValue
                                               size:size]);
    XCTAssertEqual((uint32_t)cpu.x[30], 0xfeedfaceU);
    mem_destroy(&mem);
}

#define TCTI_DECLARE_ATOMIC_WIDTH_LOAD_SEMANTIC_TEST(_name, _insn, _pc, _guestAddr, _baseReg, _destReg, _size, _memValue, _expected, _mnemonic) \
- (void)testSemanticExecutionContract_##_name                                                                                                        \
{                                                                                                                                                    \
    [self assertAtomicWidthLoadInstruction:_insn                                                                                                     \
                                   textPC:_pc                                                                                                        \
                                guestAddr:_guestAddr                                                                                                 \
                                  baseReg:_baseReg                                                                                                   \
                                   destReg:_destReg                                                                                                  \
                                     size:_size                                                                                                      \
                                  memValue:_memValue                                                                                                 \
                                 expected:_expected                                                                                                  \
                                  mnemonic:_mnemonic];                                                                                               \
}

#define TCTI_DECLARE_ATOMIC_ORDERED_STORE_SEMANTIC_TEST(_name, _insn, _pc, _guestAddr, _baseReg, _sourceReg, _size, _sourceValue, _mnemonic) \
- (void)testSemanticExecutionContract_##_name                                                                                                      \
{                                                                                                                                                  \
    [self assertAtomicOrderedStoreInstruction:_insn                                                                                               \
                                       textPC:_pc                                                                                                  \
                                    guestAddr:_guestAddr                                                                                           \
                                      baseReg:_baseReg                                                                                             \
                                    sourceReg:_sourceReg                                                                                           \
                                        size:_size                                                                                                 \
                                   sourceValue:_sourceValue                                                                                        \
                                     mnemonic:_mnemonic];                                                                                          \
}

#define TCTI_DECLARE_ATOMIC_EXCLUSIVE_STORE_SEMANTIC_TEST(_name, _insn, _pc, _guestAddr, _baseReg, _statusReg, _sourceReg, _size, _sourceValue, _mnemonic) \
- (void)testSemanticExecutionContract_##_name                                                                                                                    \
{                                                                                                                                                                        \
    [self assertAtomicExclusiveStoreInstruction:_insn                                                                                                                 \
                                         textPC:_pc                                                                                                                    \
                                      guestAddr:_guestAddr                                                                                                             \
                                        baseReg:_baseReg                                                                                                               \
                                      statusReg:_statusReg                                                                                                             \
                                      sourceReg:_sourceReg                                                                                                             \
                                          size:_size                                                                                                                   \
                                     sourceValue:_sourceValue                                                                                                          \
                                       mnemonic:_mnemonic];                                                                                                            \
}

#define TCTI_DECLARE_ATOMIC_CAS_SEMANTIC_TEST(_name, _insn, _pc, _guestAddr, _baseReg, _compareReg, _swapReg, _size, _initialValue, _swapValue, _mnemonic) \
- (void)testSemanticExecutionContract_##_name                                                                                                                         \
{                                                                                                                                                                     \
    [self assertAtomicWidthCompareSwapInstruction:_insn                                                                                                              \
                                           textPC:_pc                                                                                                                 \
                                        guestAddr:_guestAddr                                                                                                          \
                                         baseReg:_baseReg                                                                                                             \
                                      compareReg:_compareReg                                                                                                          \
                                         swapReg:_swapReg                                                                                                             \
                                           size:_size                                                                                                                 \
                                      initialValue:_initialValue                                                                                                      \
                                       swapValue:_swapValue                                                                                                           \
                                        mnemonic:_mnemonic];                                                                                                          \
}

#define TCTI_DECLARE_ATOMIC_RMW_SEMANTIC_TEST(_name, _insn, _pc, _guestAddr, _baseReg, _sourceReg, _resultReg, _size, _initialValue, _sourceValue, _kind, _mnemonic) \
- (void)testSemanticExecutionContract_##_name                                                                                                                                  \
{                                                                                                                                                                              \
    [self assertAtomicWidthRMWInstruction:_insn                                                                                                                               \
                                   textPC:_pc                                                                                                                                  \
                                guestAddr:_guestAddr                                                                                                                           \
                                  baseReg:_baseReg                                                                                                                             \
                                sourceReg:_sourceReg                                                                                                                           \
                                resultReg:_resultReg                                                                                                                           \
                                    size:_size                                                                                                                                 \
                               initialValue:_initialValue                                                                                                                      \
                                sourceValue:_sourceValue                                                                                                                       \
                                       kind:_kind                                                                                                                              \
                                   mnemonic:_mnemonic];                                                                                                                        \
}

#define TCTI_DECLARE_ATOMIC_STORE_ONLY_SEMANTIC_TEST(_name, _insn, _pc, _guestAddr, _baseReg, _sourceReg, _size, _initialValue, _sourceValue, _kind, _mnemonic) \
- (void)testSemanticExecutionContract_##_name                                                                                                                                \
{                                                                                                                                                                            \
    [self assertAtomicWidthStoreOnlyInstruction:_insn                                                                                                                        \
                                         textPC:_pc                                                                                                                           \
                                      guestAddr:_guestAddr                                                                                                                    \
                                        baseReg:_baseReg                                                                                                                      \
                                      sourceReg:_sourceReg                                                                                                                    \
                                          size:_size                                                                                                                          \
                                     initialValue:_initialValue                                                                                                               \
                                      sourceValue:_sourceValue                                                                                                                \
                                             kind:_kind                                                                                                                       \
                                         mnemonic:_mnemonic];                                                                                                                 \
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

TCTI_DECLARE_ATOMIC_WIDTH_LOAD_SEMANTIC_TEST(OrderedExclusiveAtomic_LDARBLoadsByte, 0x08dffc20, 0x110000,
                                             0x250000, 1, 0, A64_SIZE_B, 0x81U, 0x81ULL, "ldarb w0, [x1]")
TCTI_DECLARE_ATOMIC_WIDTH_LOAD_SEMANTIC_TEST(OrderedExclusiveAtomic_LDARHLoadsHalfword, 0x48dffc62, 0x110004,
                                             0x250040, 3, 2, A64_SIZE_H, 0x90abU, 0x90abULL, "ldarh w2, [x3]")
TCTI_DECLARE_ATOMIC_ORDERED_STORE_SEMANTIC_TEST(OrderedExclusiveAtomic_STLRBStoresByte, 0x089ffca4, 0x110008,
                                                0x250080, 5, 4, A64_SIZE_B, 0x5aU, "stlrb w4, [x5]")
TCTI_DECLARE_ATOMIC_ORDERED_STORE_SEMANTIC_TEST(OrderedExclusiveAtomic_STLRHStoresHalfword, 0x489ffce6, 0x11000c,
                                                0x2500c0, 7, 6, A64_SIZE_H, 0x7bcdU, "stlrh w6, [x7]")
TCTI_DECLARE_ATOMIC_WIDTH_LOAD_SEMANTIC_TEST(OrderedExclusiveAtomic_LDAPRBLoadsByte, 0x38bfc128, 0x110010,
                                             0x250100, 9, 8, A64_SIZE_B, 0x44U, 0x44ULL, "ldaprb w8, [x9]")
TCTI_DECLARE_ATOMIC_WIDTH_LOAD_SEMANTIC_TEST(OrderedExclusiveAtomic_LDAPRHLoadsHalfword, 0x78bfc16a, 0x110014,
                                             0x250140, 11, 10, A64_SIZE_H, 0x1234U, 0x1234ULL, "ldaprh w10, [x11]")
TCTI_DECLARE_ATOMIC_WIDTH_LOAD_SEMANTIC_TEST(OrderedExclusiveAtomic_LDAXRBLoadsByte, 0x085ffdac, 0x110018,
                                             0x250180, 13, 12, A64_SIZE_B, 0x6eU, 0x6eULL, "ldaxrb w12, [x13]")
TCTI_DECLARE_ATOMIC_WIDTH_LOAD_SEMANTIC_TEST(OrderedExclusiveAtomic_LDAXRHLoadsHalfword, 0x485ffdee, 0x11001c,
                                             0x2501c0, 15, 14, A64_SIZE_H, 0x5eedU, 0x5eedULL, "ldaxrh w14, [x15]")
TCTI_DECLARE_ATOMIC_EXCLUSIVE_STORE_SEMANTIC_TEST(OrderedExclusiveAtomic_STLXRBStoresByteAndPublishesStatus, 0x0810fe51, 0x110020,
                                                  0x250200, 18, 16, 17, A64_SIZE_B, 0xabU, "stlxrb w16, w17, [x18]")
TCTI_DECLARE_ATOMIC_EXCLUSIVE_STORE_SEMANTIC_TEST(OrderedExclusiveAtomic_STLXRHStoresHalfwordAndPublishesStatus, 0x4813feb4, 0x110024,
                                                  0x250240, 21, 19, 20, A64_SIZE_H, 0xfaceU, "stlxrh w19, w20, [x21]")
TCTI_DECLARE_ATOMIC_WIDTH_LOAD_SEMANTIC_TEST(OrderedExclusiveAtomic_LDXRBLoadsByte, 0x085f7ef6, 0x110028,
                                             0x250280, 23, 22, A64_SIZE_B, 0xeeU, 0xeeULL, "ldxrb w22, [x23]")
TCTI_DECLARE_ATOMIC_WIDTH_LOAD_SEMANTIC_TEST(OrderedExclusiveAtomic_LDXRHLoadsHalfword, 0x485f7f38, 0x11002c,
                                             0x2502c0, 25, 24, A64_SIZE_H, 0xbeefu, 0xbeefULL, "ldxrh w24, [x25]")
TCTI_DECLARE_ATOMIC_EXCLUSIVE_STORE_SEMANTIC_TEST(OrderedExclusiveAtomic_STXRBStoresByteAndPublishesStatus, 0x081a7f9b, 0x110030,
                                                  0x250300, 28, 26, 27, A64_SIZE_B, 0x91U, "stxrb w26, w27, [x28]")
TCTI_DECLARE_ATOMIC_EXCLUSIVE_STORE_SEMANTIC_TEST(OrderedExclusiveAtomic_STXRHStoresHalfwordAndPublishesStatus, 0x48007c41, 0x110034,
                                                  0x250340, 2, 0, 1, A64_SIZE_H, 0x1357U, "stxrh w0, w1, [x2]")
TCTI_DECLARE_ATOMIC_CAS_SEMANTIC_TEST(OrderedExclusiveAtomic_CASBMatchesAndSwapsByte, 0x08a07c41, 0x110100,
                                      0x250400, 2, 0, 1, A64_SIZE_B, 0x12U, 0x34U, "casb w0, w1, [x2]")
TCTI_DECLARE_ATOMIC_CAS_SEMANTIC_TEST(OrderedExclusiveAtomic_CASHMatchesAndSwapsHalfword, 0x48a17c62, 0x110104,
                                      0x250440, 3, 1, 2, A64_SIZE_H, 0x1234U, 0xabcdU, "cash w1, w2, [x3]")
TCTI_DECLARE_ATOMIC_CAS_SEMANTIC_TEST(OrderedExclusiveAtomic_CASABMatchesAndSwapsByte, 0x08e37ca4, 0x11010c,
                                      0x250480, 5, 3, 4, A64_SIZE_B, 0x45U, 0x67U, "casab w3, w4, [x5]")
TCTI_DECLARE_ATOMIC_CAS_SEMANTIC_TEST(OrderedExclusiveAtomic_CASAHMatchesAndSwapsHalfword, 0x48e47cc5, 0x110110,
                                      0x2504c0, 6, 4, 5, A64_SIZE_H, 0x4567U, 0x89abU, "casah w4, w5, [x6]")
TCTI_DECLARE_ATOMIC_CAS_SEMANTIC_TEST(OrderedExclusiveAtomic_CASLBMatchesAndSwapsByte, 0x08a6fd07, 0x110118,
                                      0x250500, 8, 6, 7, A64_SIZE_B, 0x55U, 0x11U, "caslb w6, w7, [x8]")
TCTI_DECLARE_ATOMIC_CAS_SEMANTIC_TEST(OrderedExclusiveAtomic_CASLHMatchesAndSwapsHalfword, 0x48a7fd28, 0x11011c,
                                      0x250540, 9, 7, 8, A64_SIZE_H, 0x2222U, 0x9999U, "caslh w7, w8, [x9]")
TCTI_DECLARE_ATOMIC_CAS_SEMANTIC_TEST(OrderedExclusiveAtomic_CASALBMatchesAndSwapsByte, 0x08e9fd6a, 0x110124,
                                      0x250580, 11, 9, 10, A64_SIZE_B, 0x66U, 0x33U, "casalb w9, w10, [x11]")
TCTI_DECLARE_ATOMIC_CAS_SEMANTIC_TEST(OrderedExclusiveAtomic_CASALHMatchesAndSwapsHalfword, 0x48eafd8b, 0x110128,
                                      0x2505c0, 12, 10, 11, A64_SIZE_H, 0x0a0bU, 0x1c1dU, "casalh w10, w11, [x12]")
TCTI_DECLARE_ATOMIC_RMW_SEMANTIC_TEST(OrderedExclusiveAtomic_SWPBReturnsOldValueAndStoresByte, 0x38208041, 0x110130,
                                      0x250600, 2, 0, 1, A64_SIZE_B, 0x21U, 0x43U, TCTIAtomicRMWKindSwap,
                                      "swpb w0, w1, [x2]")
TCTI_DECLARE_ATOMIC_RMW_SEMANTIC_TEST(OrderedExclusiveAtomic_SWPHReturnsOldValueAndStoresHalfword, 0x78218062, 0x110134,
                                      0x250640, 3, 1, 2, A64_SIZE_H, 0x1234U, 0x5678U, TCTIAtomicRMWKindSwap,
                                      "swph w1, w2, [x3]")
TCTI_DECLARE_ATOMIC_RMW_SEMANTIC_TEST(OrderedExclusiveAtomic_SWPABReturnsOldValueAndStoresByte, 0x38a380a4, 0x11013c,
                                      0x250680, 5, 3, 4, A64_SIZE_B, 0x99U, 0x12U, TCTIAtomicRMWKindSwap,
                                      "swpab w3, w4, [x5]")
TCTI_DECLARE_ATOMIC_RMW_SEMANTIC_TEST(OrderedExclusiveAtomic_SWPAHReturnsOldValueAndStoresHalfword, 0x78a480c5, 0x110140,
                                      0x2506c0, 6, 4, 5, A64_SIZE_H, 0x2222U, 0x3333U, TCTIAtomicRMWKindSwap,
                                      "swpah w4, w5, [x6]")
TCTI_DECLARE_ATOMIC_RMW_SEMANTIC_TEST(OrderedExclusiveAtomic_SWPLBReturnsOldValueAndStoresByte, 0x38668107, 0x110148,
                                      0x250700, 8, 6, 7, A64_SIZE_B, 0xaaU, 0xbbU, TCTIAtomicRMWKindSwap,
                                      "swplb w6, w7, [x8]")
TCTI_DECLARE_ATOMIC_RMW_SEMANTIC_TEST(OrderedExclusiveAtomic_SWPLHReturnsOldValueAndStoresHalfword, 0x78678128, 0x11014c,
                                      0x250740, 9, 7, 8, A64_SIZE_H, 0x4444U, 0x5555U, TCTIAtomicRMWKindSwap,
                                      "swplh w7, w8, [x9]")
TCTI_DECLARE_ATOMIC_RMW_SEMANTIC_TEST(OrderedExclusiveAtomic_SWPALBReturnsOldValueAndStoresByte, 0x38e9816a, 0x110154,
                                      0x250780, 11, 9, 10, A64_SIZE_B, 0x10U, 0x20U, TCTIAtomicRMWKindSwap,
                                      "swpalb w9, w10, [x11]")
TCTI_DECLARE_ATOMIC_RMW_SEMANTIC_TEST(OrderedExclusiveAtomic_SWPALHReturnsOldValueAndStoresHalfword, 0x78ea818b, 0x110158,
                                      0x2507c0, 12, 10, 11, A64_SIZE_H, 0x1111U, 0xaaaaU, TCTIAtomicRMWKindSwap,
                                      "swpalh w10, w11, [x12]")
TCTI_DECLARE_ATOMIC_RMW_SEMANTIC_TEST(OrderedExclusiveAtomic_LDADDBReturnsOldValueAndAddsByte, 0x38200041, 0x110160,
                                      0x250800, 2, 0, 1, A64_SIZE_B, 7U, 9U, TCTIAtomicRMWKindAdd,
                                      "ldaddb w0, w1, [x2]")
TCTI_DECLARE_ATOMIC_RMW_SEMANTIC_TEST(OrderedExclusiveAtomic_LDADDHReturnsOldValueAndAddsHalfword, 0x78210062, 0x110164,
                                      0x250840, 3, 1, 2, A64_SIZE_H, 17U, 19U, TCTIAtomicRMWKindAdd,
                                      "ldaddh w1, w2, [x3]")
TCTI_DECLARE_ATOMIC_RMW_SEMANTIC_TEST(OrderedExclusiveAtomic_LDCLRBReturnsOldValueAndClearsByteBits, 0x38201041, 0x110190,
                                      0x250880, 2, 0, 1, A64_SIZE_B, 0xf3U, 0x33U, TCTIAtomicRMWKindClear,
                                      "ldclrb w0, w1, [x2]")
TCTI_DECLARE_ATOMIC_RMW_SEMANTIC_TEST(OrderedExclusiveAtomic_LDCLRHReturnsOldValueAndClearsHalfwordBits, 0x78211062, 0x110194,
                                      0x2508c0, 3, 1, 2, A64_SIZE_H, 0xff00U, 0x0f0fU, TCTIAtomicRMWKindClear,
                                      "ldclrh w1, w2, [x3]")
TCTI_DECLARE_ATOMIC_RMW_SEMANTIC_TEST(OrderedExclusiveAtomic_LDEORBReturnsOldValueAndXorsByteBits, 0x38202041, 0x1101c0,
                                      0x250900, 2, 0, 1, A64_SIZE_B, 0x55U, 0xaaU, TCTIAtomicRMWKindEor,
                                      "ldeorb w0, w1, [x2]")
TCTI_DECLARE_ATOMIC_RMW_SEMANTIC_TEST(OrderedExclusiveAtomic_LDEORHReturnsOldValueAndXorsHalfwordBits, 0x78212062, 0x1101c4,
                                      0x250940, 3, 1, 2, A64_SIZE_H, 0x0ff0U, 0x00ffU, TCTIAtomicRMWKindEor,
                                      "ldeorh w1, w2, [x3]")
TCTI_DECLARE_ATOMIC_RMW_SEMANTIC_TEST(OrderedExclusiveAtomic_LDSETBReturnsOldValueAndSetsByteBits, 0x38203041, 0x1101f0,
                                      0x250980, 2, 0, 1, A64_SIZE_B, 0x0fU, 0xf0U, TCTIAtomicRMWKindSet,
                                      "ldsetb w0, w1, [x2]")
TCTI_DECLARE_ATOMIC_RMW_SEMANTIC_TEST(OrderedExclusiveAtomic_LDSETHReturnsOldValueAndSetsHalfwordBits, 0x78213062, 0x1101f4,
                                      0x2509c0, 3, 1, 2, A64_SIZE_H, 0x00ffU, 0xff00U, TCTIAtomicRMWKindSet,
                                      "ldseth w1, w2, [x3]")
TCTI_DECLARE_ATOMIC_RMW_SEMANTIC_TEST(OrderedExclusiveAtomic_LDSMAXBReturnsOldValueAndPublishesSignedByteMax, 0x38204041, 0x110220,
                                      0x250a00, 2, 0, 1, A64_SIZE_B, (uint8_t)-4, (uint8_t)9, TCTIAtomicRMWKindSMax,
                                      "ldsmaxb w0, w1, [x2]")
TCTI_DECLARE_ATOMIC_RMW_SEMANTIC_TEST(OrderedExclusiveAtomic_LDSMAXHReturnsOldValueAndPublishesSignedHalfwordMax, 0x78214062, 0x110224,
                                      0x250a40, 3, 1, 2, A64_SIZE_H, (uint16_t)-14, (uint16_t)9, TCTIAtomicRMWKindSMax,
                                      "ldsmaxh w1, w2, [x3]")
TCTI_DECLARE_ATOMIC_RMW_SEMANTIC_TEST(OrderedExclusiveAtomic_LDSMINBReturnsOldValueAndPublishesSignedByteMin, 0x38205041, 0x110250,
                                      0x250a80, 2, 0, 1, A64_SIZE_B, (uint8_t)14, (uint8_t)-7, TCTIAtomicRMWKindSMin,
                                      "ldsminb w0, w1, [x2]")
TCTI_DECLARE_ATOMIC_RMW_SEMANTIC_TEST(OrderedExclusiveAtomic_LDSMINHReturnsOldValueAndPublishesSignedHalfwordMin, 0x78215062, 0x110254,
                                      0x250ac0, 3, 1, 2, A64_SIZE_H, (uint16_t)140, (uint16_t)-70, TCTIAtomicRMWKindSMin,
                                      "ldsminh w1, w2, [x3]")
TCTI_DECLARE_ATOMIC_RMW_SEMANTIC_TEST(OrderedExclusiveAtomic_LDUMAXBReturnsOldValueAndPublishesUnsignedByteMax, 0x38206041, 0x110280,
                                      0x250b00, 2, 0, 1, A64_SIZE_B, 4U, 29U, TCTIAtomicRMWKindUMax,
                                      "ldumaxb w0, w1, [x2]")
TCTI_DECLARE_ATOMIC_RMW_SEMANTIC_TEST(OrderedExclusiveAtomic_LDUMAXHReturnsOldValueAndPublishesUnsignedHalfwordMax, 0x78216062, 0x110284,
                                      0x250b40, 3, 1, 2, A64_SIZE_H, 40U, 290U, TCTIAtomicRMWKindUMax,
                                      "ldumaxh w1, w2, [x3]")
TCTI_DECLARE_ATOMIC_RMW_SEMANTIC_TEST(OrderedExclusiveAtomic_LDUMINBReturnsOldValueAndPublishesUnsignedByteMin, 0x38207041, 0x1102b0,
                                      0x250b80, 2, 0, 1, A64_SIZE_B, 19U, 11U, TCTIAtomicRMWKindUMin,
                                      "lduminb w0, w1, [x2]")
TCTI_DECLARE_ATOMIC_RMW_SEMANTIC_TEST(OrderedExclusiveAtomic_LDUMINHReturnsOldValueAndPublishesUnsignedHalfwordMin, 0x78217062, 0x1102b4,
                                      0x250bc0, 3, 1, 2, A64_SIZE_H, 190U, 110U, TCTIAtomicRMWKindUMin,
                                      "lduminh w1, w2, [x3]")
TCTI_DECLARE_ATOMIC_STORE_ONLY_SEMANTIC_TEST(OrderedExclusiveAtomic_STADDBAddsByteWithoutPublishingOldValue, 0x3820003f, 0x1102e0,
                                             0x250c00, 1, 0, A64_SIZE_B, 8U, 5U, TCTIAtomicRMWKindAdd,
                                             "staddb w0, [x1]")
TCTI_DECLARE_ATOMIC_STORE_ONLY_SEMANTIC_TEST(OrderedExclusiveAtomic_STADDHAddsHalfwordWithoutPublishingOldValue, 0x7821005f, 0x1102e4,
                                             0x250c40, 2, 1, A64_SIZE_H, 80U, 50U, TCTIAtomicRMWKindAdd,
                                             "staddh w1, [x2]")
TCTI_DECLARE_ATOMIC_STORE_ONLY_SEMANTIC_TEST(OrderedExclusiveAtomic_STCLRBclearsByteBitsWithoutPublishingOldValue, 0x3820103f, 0x1102f4,
                                             0x250c80, 1, 0, A64_SIZE_B, 0xffU, 0x0fU, TCTIAtomicRMWKindClear,
                                             "stclrb w0, [x1]")
TCTI_DECLARE_ATOMIC_STORE_ONLY_SEMANTIC_TEST(OrderedExclusiveAtomic_STCLRHclearsHalfwordBitsWithoutPublishingOldValue, 0x7821105f, 0x1102f8,
                                             0x250cc0, 2, 1, A64_SIZE_H, 0xffffU, 0x00ffU, TCTIAtomicRMWKindClear,
                                             "stclrh w1, [x2]")
TCTI_DECLARE_ATOMIC_STORE_ONLY_SEMANTIC_TEST(OrderedExclusiveAtomic_STEORBxorsByteBitsWithoutPublishingOldValue, 0x3820203f, 0x110308,
                                             0x250d00, 1, 0, A64_SIZE_B, 0x12U, 0x34U, TCTIAtomicRMWKindEor,
                                             "steorb w0, [x1]")
TCTI_DECLARE_ATOMIC_STORE_ONLY_SEMANTIC_TEST(OrderedExclusiveAtomic_STEORHxorsHalfwordBitsWithoutPublishingOldValue, 0x7821205f, 0x11030c,
                                             0x250d40, 2, 1, A64_SIZE_H, 0x1234U, 0x00ffU, TCTIAtomicRMWKindEor,
                                             "steorh w1, [x2]")
TCTI_DECLARE_ATOMIC_STORE_ONLY_SEMANTIC_TEST(OrderedExclusiveAtomic_STSETBsetsByteBitsWithoutPublishingOldValue, 0x3820303f, 0x11031c,
                                             0x250d80, 1, 0, A64_SIZE_B, 0x01U, 0x80U, TCTIAtomicRMWKindSet,
                                             "stsetb w0, [x1]")
TCTI_DECLARE_ATOMIC_STORE_ONLY_SEMANTIC_TEST(OrderedExclusiveAtomic_STSETHsetsHalfwordBitsWithoutPublishingOldValue, 0x7821305f, 0x110320,
                                             0x250dc0, 2, 1, A64_SIZE_H, 0x0001U, 0x8000U, TCTIAtomicRMWKindSet,
                                             "stseth w1, [x2]")
TCTI_DECLARE_ATOMIC_STORE_ONLY_SEMANTIC_TEST(OrderedExclusiveAtomic_STSMAXBPublishesSignedByteMaxWithoutPublishingOldValue, 0x3820403f, 0x110330,
                                             0x250e00, 1, 0, A64_SIZE_B, (uint8_t)-2, (uint8_t)6, TCTIAtomicRMWKindSMax,
                                             "stsmaxb w0, [x1]")
TCTI_DECLARE_ATOMIC_STORE_ONLY_SEMANTIC_TEST(OrderedExclusiveAtomic_STSMAXHPublishesSignedHalfwordMaxWithoutPublishingOldValue, 0x7821405f, 0x110334,
                                             0x250e40, 2, 1, A64_SIZE_H, (uint16_t)-20, (uint16_t)60, TCTIAtomicRMWKindSMax,
                                             "stsmaxh w1, [x2]")
TCTI_DECLARE_ATOMIC_STORE_ONLY_SEMANTIC_TEST(OrderedExclusiveAtomic_STSMINBPublishesSignedByteMinWithoutPublishingOldValue, 0x3820503f, 0x110344,
                                             0x250e80, 1, 0, A64_SIZE_B, (uint8_t)22, (uint8_t)-8, TCTIAtomicRMWKindSMin,
                                             "stsminb w0, [x1]")
TCTI_DECLARE_ATOMIC_STORE_ONLY_SEMANTIC_TEST(OrderedExclusiveAtomic_STSMINHPublishesSignedHalfwordMinWithoutPublishingOldValue, 0x7821505f, 0x110348,
                                             0x250ec0, 2, 1, A64_SIZE_H, (uint16_t)220, (uint16_t)-80, TCTIAtomicRMWKindSMin,
                                             "stsminh w1, [x2]")
TCTI_DECLARE_ATOMIC_STORE_ONLY_SEMANTIC_TEST(OrderedExclusiveAtomic_STUMAXBPublishesUnsignedByteMaxWithoutPublishingOldValue, 0x3820603f, 0x110358,
                                             0x250f00, 1, 0, A64_SIZE_B, 2U, 99U, TCTIAtomicRMWKindUMax,
                                             "stumaxb w0, [x1]")
TCTI_DECLARE_ATOMIC_STORE_ONLY_SEMANTIC_TEST(OrderedExclusiveAtomic_STUMAXHPublishesUnsignedHalfwordMaxWithoutPublishingOldValue, 0x7821605f, 0x11035c,
                                             0x250f40, 2, 1, A64_SIZE_H, 20U, 990U, TCTIAtomicRMWKindUMax,
                                             "stumaxh w1, [x2]")
TCTI_DECLARE_ATOMIC_STORE_ONLY_SEMANTIC_TEST(OrderedExclusiveAtomic_STUMINBPublishesUnsignedByteMinWithoutPublishingOldValue, 0x3820703f, 0x11036c,
                                             0x250f80, 1, 0, A64_SIZE_B, 33U, 12U, TCTIAtomicRMWKindUMin,
                                             "stuminb w0, [x1]")
TCTI_DECLARE_ATOMIC_STORE_ONLY_SEMANTIC_TEST(OrderedExclusiveAtomic_STUMINHPublishesUnsignedHalfwordMinWithoutPublishingOldValue, 0x7821705f, 0x110370,
                                             0x250fc0, 2, 1, A64_SIZE_H, 330U, 120U, TCTIAtomicRMWKindUMin,
                                             "stuminh w1, [x2]")

@end
