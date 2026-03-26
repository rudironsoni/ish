/*
 * TCTI Execution microtest for str xzr, [x2], #8 (0xf800845f)
 * Tests REAL TCTI execution path through tcti_entry_block
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
                                   uint64_t guest_x2, void *host_mem) {
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
    cpu->x[2] = guest_x2;  // Guest x2 = test memory address
    cpu->sp = 0x80000000ULL;  // Valid stack pointer
    cpu->pstate = 0;  // Clear pstate (no flags set)
    
    return 0;
}

static int generate_str_block(a64_gen_state_t *gen, uint32_t raw_insn, 
                               uint64_t pc, void *bytecode_buf, size_t buf_size) {
    a64_instr_t instr;
    int ret;
    
    // Decode
    ret = a64_decode(raw_insn, &instr);
    if (ret != 0) {
        printf("FAIL: Decode error %d\n", ret);
        return -1;
    }
    
    // Initialize generator
    ret = a64_gen_init(gen, bytecode_buf, buf_size / sizeof(tcti_gadget_t));
    if (ret != 0) {
        printf("FAIL: Generator init error %d\n", ret);
        return -1;
    }
    
    // Generate - this emits the str_x gadget with parameters
    a64_gen_reset(gen, pc);
    ret = a64_gen_instruction(gen, raw_insn, pc);
    if (ret != 0 && ret != 1) {
        printf("FAIL: Generator error %d\n", ret);
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
    uint64_t initial_mem_value = 0xDEADBEEFCAFEBABEULL;
    
    printf("TCTI Execution Microtest for str xzr, [x2], #8 (0xf800845f)\n");
    printf("===========================================================\n\n");
    
    // Allocate test memory
    test_mem = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (test_mem == MAP_FAILED) {
        printf("FAIL: Could not allocate test memory\n");
        return 1;
    }
    
    // Initialize memory with non-zero pattern
    *(uint64_t*)test_mem = initial_mem_value;
    
    printf("Initial state:\n");
    printf("  PC: 0x%016llx\n", (unsigned long long)test_pc);
    printf("  X2: 0x%016llx (guest address)\n", (unsigned long long)initial_x2);
    printf("  Host mem: %p\n", (void*)test_mem);
    printf("  Memory[0x2000]: 0x%016llx\n", (unsigned long long)initial_mem_value);
    printf("\n");
    
    // Setup complete TCTI environment
    ret = setup_test_environment(&cpu, test_pc, initial_x2, test_mem);
    if (ret != 0) {
        printf("FAIL: Test environment setup failed\n");
        return 1;
    }
    
    printf("Harness preconditions:\n");
    printf("  cpu->mmu: %p (non-null: YES)\n", (void*)cpu.mmu);
    printf("  cpu->tlb: %p (non-null: YES)\n", (void*)cpu.tlb);
    printf("  cpu->pstate: 0x%016llx\n", (unsigned long long)cpu.pstate);
    printf("\n");
    
    // Generate the block
    ret = generate_str_block(&gen_state, 0xf800845f, test_pc, bytecode, sizeof(bytecode));
    if (ret != 0) {
        printf("FAIL: Block generation error %d\n", ret);
        return 1;
    }
    
    printf("Generated %zu gadgets\n", gen_state.num_gadgets);
    printf("  [0] = %p (str_x)\n", (void*)gen_state.gadgets[0]);
    printf("  [%zu] = %p (exit)\n", gen_state.num_gadgets - 1, 
           (void*)gen_state.gadgets[gen_state.num_gadgets - 1]);
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
    printf("  PC: 0x%016llx (unchanged from entry - expected for direct TCTI call)\n", 
           (unsigned long long)cpu.pc);
    printf("  X2: 0x%016llx (expected: 0x%016llx)\n", 
           (unsigned long long)cpu.x[2], 
           (unsigned long long)(initial_x2 + 8));
    printf("  Memory[0x2000]: 0x%016llx (expected: 0)\n",
           (unsigned long long)(*(uint64_t*)test_mem));
    printf("\n");
    
    // Verify assertions
    // NOTE: This test calls tcti_entry_block() directly, not through a64_execute_block().
    // Therefore, PC advance (to test_pc + 4) happens in the caller after TCTI exit,
    // not inside the gadget stream. We verify only the execution effects here.
    int failed = 0;
    
    // 1. Memory write check
    uint64_t mem_val = *(uint64_t*)test_mem;
    if (mem_val != 0) {
        printf("FAIL: Memory write incorrect (got 0x%016llx, expected 0)\n",
               (unsigned long long)mem_val);
        failed++;
    } else {
        printf("PASS: Memory write correct\n");
    }
    
    // 2. Architectural writeback check
    if (cpu.x[2] != initial_x2 + 8) {
        printf("FAIL: Architectural X2 writeback incorrect (got 0x%016llx, expected 0x%016llx)\n",
               (unsigned long long)cpu.x[2],
               (unsigned long long)(initial_x2 + 8));
        failed++;
    } else {
        printf("PASS: Architectural X2 writeback correct\n");
    }
    
    // 3. Exit reason check (PC advance happens in caller after TCTI exit)
    if (cpu.tcti_exit_reason != TCTI_EXIT_NORMAL) {
        printf("FAIL: Exit reason incorrect (got %d, expected %d = TCTI_EXIT_NORMAL)\n",
               cpu.tcti_exit_reason, TCTI_EXIT_NORMAL);
        failed++;
    } else {
        printf("PASS: Exit reason correct (TCTI_EXIT_NORMAL)\n");
    }
    
    printf("\n");
    
    munmap(test_mem, 4096);
    
    if (failed == 0) {
        printf("=== ALL TCTI EXECUTION ASSERTIONS PASSED ===\n");
        return 0;
    } else {
        printf("=== %d TCTI EXECUTION ASSERTIONS FAILED ===\n", failed);
        return 1;
    }
}
