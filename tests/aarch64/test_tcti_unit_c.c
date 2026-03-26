/*
 * TCTI Unit C: Three-instruction sequence with branch
 * str xzr, [x2], #8
 * cmp x2, x5
 * b.ne loop
 *
 * Purpose: Verify branch falls through when cmp detects equality.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/mman.h>

#include "misc.h"
#include "emu/aarch64/cpu.h"
#include "emu/aarch64/decode.h"
#include "emu/mmu.h"
#include "emu/tlb.h"
#include "tcti/aarch64/gen.h"

static uint8_t *test_mem = NULL;
#define TEST_MEM_GUEST_ADDR 0x2000ULL

static tcti_gadget_t bytecode[32];

static struct tlb test_tlb;
static struct mmu test_mmu;

extern void tcti_entry_block(void *gadgets, struct cpu_state *cpu);

static int setup_test_env(struct cpu_state *cpu, uint64_t test_pc,
                           uint64_t guest_x2, uint64_t guest_x5, void *host_mem) {
    memset(cpu, 0, sizeof(*cpu));
    memset(&test_mmu, 0, sizeof(test_mmu));
    cpu->mmu = &test_mmu;
    memset(&test_tlb, 0, sizeof(test_tlb));
    test_tlb.mmu = &test_mmu;
    cpu->tlb = &test_tlb;

    uint64_t page_base = TEST_MEM_GUEST_ADDR & ~0xFFFULL;
    int tlb_idx = TLB_INDEX(TEST_MEM_GUEST_ADDR);
    cpu->tlb->entries[tlb_idx].page = page_base;
    cpu->tlb->entries[tlb_idx].page_if_writable = page_base;
    cpu->tlb->entries[tlb_idx].data_minus_addr = (uintptr_t)host_mem - (uintptr_t)page_base;

    cpu->pc = test_pc;
    cpu->x[2] = guest_x2;
    cpu->x[5] = guest_x5;
    cpu->sp = 0x80000000ULL;
    cpu->pstate = 0;

    return 0;
}

static int generate_block(a64_gen_state_t *gen, uint64_t test_pc, uint64_t loop_pc) {
    int ret;

    ret = a64_gen_init(gen, bytecode, sizeof(bytecode) / sizeof(tcti_gadget_t));
    if (ret != 0) {
        printf("FAIL: Generator init error %d\n", ret);
        return -1;
    }

    a64_gen_reset(gen, test_pc);

    ret = a64_gen_instruction(gen, 0xf800845f, test_pc);
    if (ret != 0 && ret != 1) {
        printf("FAIL: Generator error for STR: %d\n", ret);
        return -1;
    }

    ret = a64_gen_instruction(gen, 0xeaa5005f, test_pc + 4);
    if (ret != 0 && ret != 1) {
        printf("FAIL: Generator error for CMP: %d\n", ret);
        return -1;
    }

    // B.NE encoding: base=0x54000000 (B.cond), cond=1 (NE), imm19=offset/4
    // For backward branch to loop_pc (equal to test_pc), offset = -12 bytes = -3 instructions
    int32_t bne_offset = (int32_t)((loop_pc - (test_pc + 8)) / 4);
    uint32_t bne_insn = 0x54000001 | ((bne_offset & 0x7ffff) << 5);
    ret = a64_gen_instruction(gen, bne_insn, test_pc + 8);
    if (ret != 0 && ret != 1) {
        printf("FAIL: Generator error for B.NE: %d\n", ret);
        return -1;
    }

    extern tcti_gadget_t gadget_exit;
    gen->gadgets[gen->num_gadgets++] = gadget_exit;

    return 0;
}

int main(void) {
    struct cpu_state cpu;
    a64_gen_state_t gen_state;
    int ret;

    uint64_t test_pc = 0x1000ULL;
    uint64_t loop_pc = 0x1000ULL;
    uint64_t fallthrough_pc = 0x1000ULL + 12;
    uint64_t initial_x2 = TEST_MEM_GUEST_ADDR;
    uint64_t initial_x5 = TEST_MEM_GUEST_ADDR + 8;
    uint64_t initial_mem_value = 0xDEADBEEFCAFEBABEULL;

    printf("TCTI UNIT C: str + cmp + b.ne three-instruction sequence\n");
    printf("=========================================================\n\n");
    printf("Initial: PC=0x%llx, X2=0x%llx, X5=0x%llx\n",
           (unsigned long long)test_pc,
           (unsigned long long)initial_x2,
           (unsigned long long)initial_x5);
    printf("Expected: STR writes 0, post-index updates X2, CMP sees equality, B.NE falls through\n\n");

    test_mem = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (test_mem == MAP_FAILED) {
        printf("FAIL: Could not allocate test memory\n");
        return 1;
    }
    *(uint64_t*)test_mem = initial_mem_value;

    ret = setup_test_env(&cpu, test_pc, initial_x2, initial_x5, test_mem);
    if (ret != 0) {
        printf("FAIL: Test environment setup failed\n");
        return 1;
    }

    ret = generate_block(&gen_state, test_pc, loop_pc);
    if (ret != 0) {
        printf("FAIL: Block generation error %d\n", ret);
        return 1;
    }

    printf("Generated %zu gadgets\n", gen_state.num_gadgets);
    printf("\n");

    printf("Executing via TCTI...\n");
    fflush(stdout);

    tcti_entry_block(gen_state.gadgets, &cpu);

    printf("\nExecution complete.\n");
    printf("Exit reason: %d\n", cpu.tcti_exit_reason);
    printf("\n");

    printf("Final state:\n");
    printf("  PC: 0x%016llx\n", (unsigned long long)cpu.pc);
    printf("  X2: 0x%016llx (expected: 0x%016llx)\n",
           (unsigned long long)cpu.x[2],
           (unsigned long long)(initial_x2 + 8));
    printf("  X5: 0x%016llx (expected: 0x%016llx)\n",
           (unsigned long long)cpu.x[5],
           (unsigned long long)(initial_x2 + 8));
    printf("  Memory[0x2000]: 0x%016llx (expected: 0)\n",
           (unsigned long long)(*(uint64_t*)test_mem));
    printf("  PSTATE: 0x%016llx (expected: 0x60000000)\n",
           (unsigned long long)cpu.pstate);
    printf("  NZCV: N=%d Z=%d C=%d V=%d\n", cpu.n, cpu.z, cpu.c, cpu.v);
    printf("\n");

    int failed = 0;
    int first_fail = 0;

    uint64_t mem_val = *(uint64_t*)test_mem;
    if (mem_val != 0) {
        printf("FAIL: Memory write incorrect (got 0x%016llx, expected 0)\n",
               (unsigned long long)mem_val);
        failed++;
        if (!first_fail) first_fail = 1;
    } else {
        printf("PASS: Memory write correct\n");
    }

    if (cpu.x[2] != initial_x2 + 8) {
        printf("FAIL: Architectural X2 writeback incorrect (got 0x%016llx, expected 0x%016llx)\n",
               (unsigned long long)cpu.x[2],
               (unsigned long long)(initial_x2 + 8));
        failed++;
        if (!first_fail) first_fail = 2;
    } else {
        printf("PASS: Architectural X2 writeback correct\n");
    }

    if (cpu.x[5] != initial_x2 + 8) {
        printf("FAIL: X5 incorrect (got 0x%016llx, expected 0x%016llx)\n",
               (unsigned long long)cpu.x[5],
               (unsigned long long)(initial_x2 + 8));
        failed++;
        if (!first_fail) first_fail = 3;
    } else {
        printf("PASS: X5 correct\n");
    }

    if (!cpu.z) {
        printf("FAIL: NZCV Z flag not set (got PSTATE=0x%llx, expected Z=1)\n",
               (unsigned long long)cpu.pstate);
        failed++;
        if (!first_fail) first_fail = 4;
    } else {
        printf("PASS: NZCV Z flag set (equality detected)\n");
    }

    if (cpu.pstate != 0x60000000) {
        printf("FAIL: PSTATE incorrect (got 0x%llx, expected 0x60000000)\n",
               (unsigned long long)cpu.pstate);
        failed++;
        if (!first_fail) first_fail = 5;
    } else {
        printf("PASS: PSTATE correct (Z=1, C=1)\n");
    }

    if (cpu.tcti_exit_reason != TCTI_EXIT_NORMAL) {
        printf("FAIL: Exit reason incorrect (got %d, expected %d)\n",
               cpu.tcti_exit_reason, TCTI_EXIT_NORMAL);
        failed++;
        if (!first_fail) first_fail = 6;
    } else {
        printf("PASS: Exit reason correct (TCTI_EXIT_NORMAL)\n");
    }

    munmap(test_mem, 4096);

    printf("\n");
    printf("=== UNIT C RESULT ===\n");
    if (failed == 0) {
        printf("Result: PASS\n");
        return 0;
    } else {
        printf("Result: FAIL (%d assertions)\n", failed);
        return 1;
    }
}
