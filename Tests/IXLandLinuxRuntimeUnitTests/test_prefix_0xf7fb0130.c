/*
 * Prefix test for block 0xf7fb0130
 * Tests instruction prefixes to find first one that corrupts guest x4
 *
 * Block 0xf7fb0130 instructions:
 * 0xf7fb0130: stp x3, x4, [sp, #24]     0xa94193e3
 * 0xf7fb0134: ldr x2, [sp, #40]         0xf94017e2
 * 0xf7fb0138: cbz x2, 0xf7fb0150        0xb40000e2
 * 0xf7fb013c: ldr w5, [x3]              0xb9400065
 * 0xf7fb0140: sub x2, x2, #1            0xd1000442
 * 0xf7fb0144: cmp w5, #2                0x710008bf
 * 0xf7fb0148: b.ne 0xf7fb01c4           0x540003e1
 * 0xf7fb014c: ldr x2, [x3, #16]         0xf9400862
 * 0xf7fb0150: sub x2, x1, x2            0xcb020022
 * 0xf7fb0154: stp x3, x1, [sp, #392]    0xa95887e3
 * 0xf7fb0158: add x3, x2, x3            0x8b030043
 * 0xf7fb015c: add x3, x3, x1            0x8b010063
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/mman.h>

#import <IXLandLinuxRuntime/util/misc.h>
#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>
#import <IXLandLinuxRuntime/emu/aarch64/decode.h>
#import <IXLandLinuxRuntime/emu/mmu.h>
#import <IXLandLinuxRuntime/emu/tlb.h>
#import <IXLandLinuxRuntime/tcti/aarch64/gen.h>

// Test memory for stack
static uint8_t *test_mem = NULL;
#define TEST_MEM_SIZE 4096
#define TEST_PC 0xf7fb0130ULL

// TCTI bytecode buffer
static tcti_gadget_t bytecode[64];

// TLB and MMU for test
static struct tlb test_tlb;
static struct mmu test_mmu;

extern void tcti_entry_block(void *gadgets, struct cpu_state *cpu);

// Initial state observed before block 0xf7fb0130 execution
#define INITIAL_X0  0x00000000fffffe10ULL
#define INITIAL_X1  0x00000000f7ff9db0ULL
#define INITIAL_X2  0x0000000000000000ULL
#define INITIAL_X3  0x0000000000000000ULL
#define INITIAL_X4  0x00000000fffffd10ULL
#define INITIAL_SP  0x00000000fffffc10ULL

typedef struct {
    int prefix_num;
    const char *name;
    uint32_t insns[12];
    int num_insns;
} prefix_test_t;

// Define all 12 prefixes
prefix_test_t prefixes[] = {
    {1, "stp x3, x4, [sp, #24]",
        {0xa94193e3}, 1},
    {2, "Prefix 1 + ldr x2, [sp, #40]",
        {0xa94193e3, 0xf94017e2}, 2},
    {3, "Prefix 2 + cbz x2, target",
        {0xa94193e3, 0xf94017e2, 0xb40000e2}, 3},
    {4, "Prefix 3 + ldr w5, [x3]",
        {0xa94193e3, 0xf94017e2, 0xb40000e2, 0xb9400065}, 4},
    {5, "Prefix 4 + sub x2, x2, #1",
        {0xa94193e3, 0xf94017e2, 0xb40000e2, 0xb9400065, 0xd1000442}, 5},
    {6, "Prefix 5 + cmp w5, #2",
        {0xa94193e3, 0xf94017e2, 0xb40000e2, 0xb9400065, 0xd1000442, 0x710008bf}, 6},
    {7, "Prefix 6 + b.ne target",
        {0xa94193e3, 0xf94017e2, 0xb40000e2, 0xb9400065, 0xd1000442, 0x710008bf, 0x540003e1}, 7},
    {8, "Prefix 7 + ldr x2, [x3, #16]",
        {0xa94193e3, 0xf94017e2, 0xb40000e2, 0xb9400065, 0xd1000442, 0x710008bf, 0x540003e1, 0xf9400862}, 8},
    {9, "Prefix 8 + sub x2, x1, x2",
        {0xa94193e3, 0xf94017e2, 0xb40000e2, 0xb9400065, 0xd1000442, 0x710008bf, 0x540003e1, 0xf9400862, 0xcb020022}, 9},
    {10, "Prefix 9 + stp x3, x1, [sp, #392]",
        {0xa94193e3, 0xf94017e2, 0xb40000e2, 0xb9400065, 0xd1000442, 0x710008bf, 0x540003e1, 0xf9400862, 0xcb020022, 0xa95887e3}, 10},
    {11, "Prefix 10 + add x3, x2, x3",
        {0xa94193e3, 0xf94017e2, 0xb40000e2, 0xb9400065, 0xd1000442, 0x710008bf, 0x540003e1, 0xf9400862, 0xcb020022, 0xa95887e3, 0x8b030043}, 11},
    {12, "Prefix 11 + add x3, x3, x1",
        {0xa94193e3, 0xf94017e2, 0xb40000e2, 0xb9400065, 0xd1000442, 0x710008bf, 0x540003e1, 0xf9400862, 0xcb020022, 0xa95887e3, 0x8b030043, 0x8b010063}, 12}
};

static int setup_test_env(struct cpu_state *cpu, void *host_mem) {
    memset(cpu, 0, sizeof(*cpu));
    memset(&test_mmu, 0, sizeof(test_mmu));
    cpu->mmu = &test_mmu;
    memset(&test_tlb, 0, sizeof(test_tlb));
    test_tlb.mmu = &test_mmu;
    cpu->tlb = &test_tlb;

    // Map stack memory
    uint64_t stack_page = INITIAL_SP & ~0xFFFULL;
    int tlb_idx = TLB_INDEX(INITIAL_SP);
    cpu->tlb->entries[tlb_idx].page = stack_page;
    cpu->tlb->entries[tlb_idx].page_if_writable = stack_page;
    cpu->tlb->entries[tlb_idx].data_minus_addr = (uintptr_t)host_mem - (uintptr_t)stack_page;

    // Set initial register state (observed before block 0xf7fb0130)
    cpu->pc = TEST_PC;
    cpu->x[0] = INITIAL_X0;
    cpu->x[1] = INITIAL_X1;
    cpu->x[2] = INITIAL_X2;
    cpu->x[3] = INITIAL_X3;
    cpu->x[4] = INITIAL_X4;
    cpu->sp = INITIAL_SP;
    cpu->pstate = 0;

    return 0;
}

static int generate_prefix(a64_gen_state_t *gen, prefix_test_t *prefix) {
    int ret;

    ret = a64_gen_init(gen, bytecode, sizeof(bytecode) / sizeof(tcti_gadget_t));
    if (ret != 0) {
        printf("FAIL: Generator init error %d\n", ret);
        return -1;
    }

    a64_gen_reset(gen, TEST_PC);

    for (int i = 0; i < prefix->num_insns; i++) {
        ret = a64_gen_instruction(gen, prefix->insns[i], TEST_PC + i * 4);
        if (ret != 0 && ret != 1) {
            printf("FAIL: Generator error for insn %d: %d\n", i, ret);
            return -1;
        }
    }

    extern tcti_gadget_t gadget_exit;
    gen->gadgets[gen->num_gadgets++] = gadget_exit;

    return 0;
}

static int run_prefix_test(int prefix_idx) {
    struct cpu_state cpu;
    a64_gen_state_t gen_state;
    int ret;

    prefix_test_t *prefix = &prefixes[prefix_idx];

    printf("\n=== PREFIX %d: %s ===\n", prefix->prefix_num, prefix->name);

    // Allocate test memory
    test_mem = mmap(NULL, TEST_MEM_SIZE, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (test_mem == MAP_FAILED) {
        printf("FAIL: Could not allocate test memory\n");
        return -1;
    }

    // Initialize stack memory with observed values
    // Stack at INITIAL_SP should have specific values
    memset(test_mem, 0, TEST_MEM_SIZE);
    // Set up stack at offset corresponding to INITIAL_SP within the page
    uint64_t stack_offset = INITIAL_SP & 0xFFF;

    ret = setup_test_env(&cpu, test_mem);
    if (ret != 0) {
        printf("FAIL: Test environment setup failed\n");
        munmap(test_mem, TEST_MEM_SIZE);
        return -1;
    }

    // Capture x4 before
    uint64_t x4_before = cpu.x[4];

    // Generate the prefix
    ret = generate_prefix(&gen_state, prefix);
    if (ret != 0) {
        printf("FAIL: Block generation failed\n");
        munmap(test_mem, TEST_MEM_SIZE);
        return -1;
    }

    // Execute
    tcti_entry_block(gen_state.gadgets, &cpu);
    int exit_reason = cpu.tcti_exit_reason;

    // Capture results
    uint64_t x4_after = cpu.x[4];
    uint64_t final_pc = cpu.pc;

    printf("Results:\n");
    printf("  guest x4 before: 0x%016llx\n", (unsigned long long)x4_before);
    printf("  guest x4 after:  0x%016llx\n", (unsigned long long)x4_after);
    printf("  final next PC:   0x%08llx\n", (unsigned long long)final_pc);
    printf("  exit reason:     %d\n", exit_reason);

    if (x4_after != x4_before) {
        printf("  *** X4 CHANGED ***\n");
    }

    munmap(test_mem, TEST_MEM_SIZE);
    test_mem = NULL;

    return (x4_after != x4_before) ? 1 : 0;
}

int main(void) {
    printf("PREFIX TEST for block 0xf7fb0130\n");
    printf("================================\n");
    printf("Finding first prefix that corrupts guest x4\n");
    printf("Initial x4 before block: 0x%016llx\n\n", (unsigned long long)INITIAL_X4);

    int first_corrupting = -1;

    for (int i = 0; i < 12; i++) {
        int result = run_prefix_test(i);
        if (result == 1 && first_corrupting < 0) {
            first_corrupting = i;
            printf("\n*** FIRST CORRUPTING PREFIX: %d ***\n", i + 1);
            break;
        }
    }

    printf("\n================================\n");
    printf("SUMMARY:\n");
    if (first_corrupting >= 0) {
        printf("First prefix that changes x4: %d\n", first_corrupting + 1);
        printf("Newly added instruction: ");
        if (first_corrupting == 0) {
            printf("stp x3, x4, [sp, #24]\n");
        } else {
            // Show the newly added instruction
            printf("(see prefix %d above)\n", first_corrupting + 1);
        }
    } else {
        printf("No prefix changes x4\n");
        printf("Classification: original runtime capture mismatch\n");
    }

    return 0;
}
