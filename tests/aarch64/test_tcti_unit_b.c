/*
 * TCTI Unit B: Two-instruction sequence
 * str xzr, [x2], #8
 * cmp x2, x5
 * 
 * Purpose: Prove whether compare sees updated x2 after post-index store.
 * 
 * This is a distro-agnostic core execution correctness test (Category A)
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

// Test memory
static uint8_t *test_mem = NULL;
#define TEST_MEM_GUEST_ADDR 0x2000ULL

// TCTI bytecode buffer
static tcti_gadget_t bytecode[32];

// TLB and MMU for test
static struct tlb test_tlb;
static struct mmu test_mmu;

// External TCTI entry point
extern void tcti_entry_block(void *gadgets, struct cpu_state *cpu);

static int setup_test_environment(struct cpu_state *cpu, uint64_t test_pc, 
                                   uint64_t guest_x2, uint64_t guest_x5,
                                   void *host_mem) {
    // Clear CPU state
    memset(cpu, 0, sizeof(*cpu));
    
    // Initialize MMU
    memset(&test_mmu, 0, sizeof(test_mmu));
    cpu->mmu = &test_mmu;
    
    // Initialize TLB
    memset(&test_tlb, 0, sizeof(test_tlb));
    test_tlb.mmu = &test_mmu;
    cpu->tlb = &test_tlb;
    
    // Map guest address to host memory using TLB entry
    uint64_t page_base = TEST_MEM_GUEST_ADDR & ~0xFFFULL;
    int tlb_idx = TLB_INDEX(TEST_MEM_GUEST_ADDR);
    cpu->tlb->entries[tlb_idx].page = page_base;
    cpu->tlb->entries[tlb_idx].page_if_writable = page_base;
    cpu->tlb->entries[tlb_idx].data_minus_addr = (uintptr_t)host_mem - (uintptr_t)page_base;
    
    // Initialize CPU registers
    cpu->pc = test_pc;
    cpu->x[2] = guest_x2;   // Guest x2 = 0x2000 (base address)
    cpu->x[5] = guest_x5;   // Guest x5 = 0x2008 (expected post-index value)
    cpu->sp = 0x80000000ULL; // Valid stack pointer
    cpu->pstate = 0;         // Clear pstate (no flags set)
    
    return 0;
}

static int generate_block(a64_gen_state_t *gen, uint64_t test_pc, 
                          void *bytecode_buf, size_t buf_size) {
    int ret;
    
    // Initialize generator
    ret = a64_gen_init(gen, bytecode_buf, buf_size / sizeof(tcti_gadget_t));
    if (ret != 0) {
        printf("FAIL: Generator init error %d\n", ret);
        return -1;
    }
    
    // Generate first instruction: str xzr, [x2], #8
    a64_gen_reset(gen, test_pc);
    ret = a64_gen_instruction(gen, 0xf800845f, test_pc);
    if (ret != 0 && ret != 1) {
        printf("FAIL: Generator error for STR: %d\n", ret);
        return -1;
    }
    
    // Generate second instruction: cmp x2, x5
    // CMP is encoded as SUBS with Rd=x31, Rn=x2, Rm=x5
    // Format: sf=1, op=1, S=1, rm=5, imm6=0, rn=2, rd=31
    // This is SUBS (shifted register), so op2 = 5 (0101)
    // Correct encoding: 0xEAA5005F
    ret = a64_gen_instruction(gen, 0xeaa5005f, test_pc + 4);
    if (ret != 0 && ret != 1) {
        printf("FAIL: Generator error for CMP: %d\n", ret);
        return -1;
    }
    
    // Add exit gadget
    extern tcti_gadget_t gadget_exit;
    gen->gadgets[gen->num_gadgets++] = gadget_exit;
    
    return 0;
}

int main(void) {
    struct cpu_state cpu;
    a64_gen_state_t gen_state;
    int ret;
    
    uint64_t test_pc = 0x1000ULL;
    uint64_t initial_x2 = TEST_MEM_GUEST_ADDR;
    uint64_t initial_x5 = TEST_MEM_GUEST_ADDR + 8;  // x5 = 0x2008
    uint64_t initial_mem_value = 0xDEADBEEFCAFEBABEULL;
    
    printf("TCTI UNIT B: str xzr, [x2], #8  +  cmp x2, x5\n");
    printf("==============================================\n\n");
    
    printf("Purpose: Verify compare sees updated x2 after post-index store\n");
    printf("Initial: x2=0x%llx, x5=0x%llx (expecting equality after STR)\n\n",
           (unsigned long long)initial_x2, (unsigned long long)initial_x5);
    
    // Allocate test memory
    test_mem = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (test_mem == MAP_FAILED) {
        printf("FAIL: Could not allocate test memory\n");
        return 1;
    }
    
    // Initialize memory with non-zero pattern
    *(uint64_t*)test_mem = initial_mem_value;
    
    // Setup complete TCTI environment
    ret = setup_test_environment(&cpu, test_pc, initial_x2, initial_x5, test_mem);
    if (ret != 0) {
        printf("FAIL: Test environment setup failed\n");
        return 1;
    }
    
    printf("Harness preconditions:\n");
    printf("  cpu->mmu: %p (non-null: YES)\n", (void*)cpu.mmu);
    printf("  cpu->tlb: %p (non-null: YES)\n", (void*)cpu.tlb);
    printf("  cpu->pstate: 0x%016llx (NZCV cleared)\n", (unsigned long long)cpu.pstate);
    printf("\n");
    
    // Generate the block (two instructions + exit)
    ret = generate_block(&gen_state, test_pc, bytecode, sizeof(bytecode));
    if (ret != 0) {
        printf("FAIL: Block generation error %d\n", ret);
        return 1;
    }
    
    printf("Generated %zu gadgets\n", gen_state.num_gadgets);
    printf("\n");
    
    // Execute via TCTI
    printf("Executing via TCTI...\n");
    fflush(stdout);
    
    tcti_entry_block(gen_state.gadgets, &cpu);
    
    printf("\nExecution complete.\n");
    printf("Exit reason: %d\n", cpu.tcti_exit_reason);
    printf("\n");
    
    // Check results
    printf("Final state:\n");
    printf("  PC: 0x%016llx\n", (unsigned long long)cpu.pc);
    printf("  X2: 0x%016llx (expected: 0x%016llx)\n", 
           (unsigned long long)cpu.x[2], 
           (unsigned long long)(initial_x2 + 8));
    printf("  X5: 0x%016llx\n", (unsigned long long)cpu.x[5]);
    printf("  Memory[0x2000]: 0x%016llx (expected: 0)\n",
           (unsigned long long)(*(uint64_t*)test_mem));
    printf("  PSTATE: 0x%016llx\n", (unsigned long long)cpu.pstate);
    printf("  NZCV: N=%d Z=%d C=%d V=%d\n", cpu.n, cpu.z, cpu.c, cpu.v);
    printf("\n");
    
    // Verify assertions
    int failed = 0;
    int first_fail = 0;
    
    // 1. Memory write check
    uint64_t mem_val = *(uint64_t*)test_mem;
    if (mem_val != 0) {
        printf("FAIL: Memory write incorrect (got 0x%016llx, expected 0)\n",
               (unsigned long long)mem_val);
        failed++;
        if (!first_fail) first_fail = 1;
    } else {
        printf("PASS: Memory write correct\n");
    }
    
    // 2. Architectural writeback check
    if (cpu.x[2] != initial_x2 + 8) {
        printf("FAIL: Architectural X2 writeback incorrect (got 0x%016llx, expected 0x%016llx)\n",
               (unsigned long long)cpu.x[2],
               (unsigned long long)(initial_x2 + 8));
        failed++;
        if (!first_fail) first_fail = 2;
    } else {
        printf("PASS: Architectural X2 writeback correct\n");
    }
    
    // 3. NZCV equality check (Z flag should be set after CMP)
    if (!cpu.z) {
        printf("FAIL: NZCV equality not observed (Z=0, expected Z=1)\n");
        printf("      CMP x2, x5 should see x2=0x%llx == x5=0x%llx\n",
               (unsigned long long)cpu.x[2], (unsigned long long)cpu.x[5]);
        failed++;
        if (!first_fail) first_fail = 3;
    } else {
        printf("PASS: NZCV equality observed (Z=1)\n");
    }
    
    // 4. Exit reason check
    if (cpu.tcti_exit_reason != TCTI_EXIT_NORMAL) {
        printf("FAIL: Exit reason incorrect (got %d, expected %d)\n",
               cpu.tcti_exit_reason, TCTI_EXIT_NORMAL);
        failed++;
        if (!first_fail) first_fail = 4;
    } else {
        printf("PASS: Exit reason correct (TCTI_EXIT_NORMAL)\n");
    }
    
    printf("\n");
    
    munmap(test_mem, 4096);
    
    // Dump STR writeback diagnostic
    extern void dump_str_wb_diag(void);
    dump_str_wb_diag();
    
    // Report
    printf("=== UNIT B RESULT ===\n");
    if (failed == 0) {
        printf("Result: PASS\n");
        printf("\nNext unit: str + cmp + b.ne three-instruction sequence\n");
        return 0;
    } else {
        printf("Result: FAIL (%d assertions)\n", failed);
        printf("\nFirst failing property: ");
        switch (first_fail) {
            case 1: printf("memory write\n"); break;
            case 2: printf("architectural writeback\n"); break;
            case 3: printf("compare input handoff / hot-register sync\n"); break;
            case 4: printf("exit reason\n"); break;
            default: printf("unknown\n"); break;
        }
        return 1;
    }
}
