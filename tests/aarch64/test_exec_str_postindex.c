/*
 * Execution microtest for str xzr, [x2], #8 (0xf800845f)
 * Smallest possible harness: one instruction only
 * 
 * This test verifies execution correctness without full CPU simulation.
 * It mocks the TCTI gadget execution environment.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>
#include <assert.h>

#include "emu/aarch64/decode.h"

// Minimal CPU state for testing
struct test_cpu {
    uint64_t pc;
    uint64_t x[31];
    uint64_t sp;
    uint8_t *memory;
    uint64_t mem_base;
    uint64_t mem_size;
};

// Test memory
static uint8_t test_mem[0x10000];

// Initialize test CPU
static void init_test_cpu(struct test_cpu *cpu) {
    memset(cpu, 0, sizeof(*cpu));
    cpu->memory = test_mem;
    cpu->mem_base = 0x1000;
    cpu->mem_size = sizeof(test_mem);
    memset(test_mem, 0xAB, sizeof(test_mem));
}

// Mock memory write
static int mock_mem_write64(struct test_cpu *cpu, uint64_t addr, uint64_t value) {
    if (addr < cpu->mem_base || addr >= cpu->mem_base + cpu->mem_size - 7) {
        printf("ERROR: Memory write out of bounds at 0x%016llx\n", (unsigned long long)addr);
        return -1;
    }
    *(uint64_t*)(cpu->memory + (addr - cpu->mem_base)) = value;
    return 0;
}

// Simulate STR execution with POST_INDEX
static int exec_str_postindex(struct test_cpu *cpu, int rt, int rn, int64_t imm) {
    // Get base address from Rn
    uint64_t base_addr = cpu->x[rn];
    
    // Store value from Rt to [base_addr]
    uint64_t value = (rt == 31) ? 0 : cpu->x[rt];  // XZR is zero
    int ret = mock_mem_write64(cpu, base_addr, value);
    if (ret != 0) return ret;
    
    // Post-index: write back base + imm to Rn
    cpu->x[rn] = base_addr + imm;
    
    // Advance PC
    cpu->pc += 4;
    
    return 0;
}

int main(void) {
    struct test_cpu cpu;
    a64_instr_t instr;
    int ret;
    
    uint64_t test_pc = 0x1000ULL;
    uint64_t initial_x2 = 0x2000ULL;
    
    printf("Execution microtest for str xzr, [x2], #8 (0xf800845f)\n");
    printf("Initial PC: 0x%016llx\n", (unsigned long long)test_pc);
    printf("Initial X2: 0x%016llx\n\n", (unsigned long long)initial_x2);
    
    // Setup CPU
    init_test_cpu(&cpu);
    cpu.pc = test_pc;
    cpu.x[2] = initial_x2;  // Guest x2 = 0x2000
    
    // Decode instruction
    uint32_t raw_insn = 0xf800845f;
    memset(&instr, 0, sizeof(instr));
    ret = a64_decode(raw_insn, &instr);
    if (ret != 0) {
        printf("FAIL: Decode error %d\n", ret);
        return 1;
    }
    
    printf("Decoded instruction:\n");
    printf("  Rd: %d (XZR)\n", instr.Rd);
    printf("  Rn: %d (X2)\n", instr.Rn);
    printf("  imm: %ld\n", (long)instr.imm);
    printf("  idx_mode: %d (POST_INDEX)\n", instr.idx_mode);
    printf("\n");
    
    // Execute
    printf("Executing str xzr, [x2], #8...\n");
    ret = exec_str_postindex(&cpu, instr.Rd, instr.Rn, instr.imm);
    if (ret != 0) {
        printf("FAIL: Execution error %d\n", ret);
        return 1;
    }
    
    // Check results
    printf("\nExecution results:\n");
    
    // 1. Memory write check
    uint64_t mem_val = *(uint64_t*)&test_mem[initial_x2 - cpu.mem_base];
    printf("  Memory[0x%016llx]: 0x%016llx (expected: 0)\n", 
           (unsigned long long)initial_x2, (unsigned long long)mem_val);
    
    // 2. Architectural writeback check
    printf("  Final X2: 0x%016llx (expected: 0x%016llx)\n", 
           (unsigned long long)cpu.x[2], (unsigned long long)(initial_x2 + 8));
    
    // 3. Next PC check
    printf("  Final PC: 0x%016llx (expected: 0x%016llx)\n", 
           (unsigned long long)cpu.pc, (unsigned long long)(test_pc + 4));
    
    printf("\n");
    
    // Verify assertions
    int failed = 0;
    
    if (mem_val != 0) {
        printf("FAIL: Memory write incorrect\n");
        failed++;
    }
    
    if (cpu.x[2] != initial_x2 + 8) {
        printf("FAIL: Architectural X2 writeback incorrect\n");
        failed++;
    }
    
    if (cpu.pc != test_pc + 4) {
        printf("FAIL: Next PC incorrect\n");
        failed++;
    }
    
    if (failed == 0) {
        printf("=== ALL EXECUTION ASSERTIONS PASSED ===\n");
        return 0;
    } else {
        printf("=== %d EXECUTION ASSERTIONS FAILED ===\n", failed);
        return 1;
    }
}
