/*
 * TCTI Unit B with Atomic Capture
 * Tests str xzr, [x2], #8 + cmp x2, x5 with atomic register capture
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
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
static tcti_gadget_t bytecode[32];
static struct tlb test_tlb;
static struct mmu test_mmu;

// External atomic capture buffer
extern struct atomic_cmp_capture {
    uint64_t x3_before;
    uint64_t x6_before;
    uint64_t nzcv_after_subs;
} g_atomic_cmp_capture;

extern void tcti_entry_block(void *gadgets, struct cpu_state *cpu);

static int setup(struct cpu_state *cpu, uint64_t pc, uint64_t x2, uint64_t x5, void *mem) {
    memset(cpu, 0, sizeof(*cpu));
    memset(&test_mmu, 0, sizeof(test_mmu));
    cpu->mmu = &test_mmu;
    memset(&test_tlb, 0, sizeof(test_tlb));
    test_tlb.mmu = &test_mmu;
    cpu->tlb = &test_tlb;
    
    uint64_t page = TEST_MEM_GUEST_ADDR & ~0xFFFULL;
    int idx = TLB_INDEX(TEST_MEM_GUEST_ADDR);
    cpu->tlb->entries[idx].page = page;
    cpu->tlb->entries[idx].page_if_writable = page;
    cpu->tlb->entries[idx].data_minus_addr = (uintptr_t)mem - (uintptr_t)page;
    
    cpu->pc = pc;
    cpu->x[2] = x2;
    cpu->x[5] = x5;
    cpu->sp = 0x80000000ULL;
    cpu->pstate = 0;
    return 0;
}

int main(void) {
    struct cpu_state cpu;
    a64_gen_state_t gen;
    int ret;
    
    uint64_t test_pc = 0x1000ULL;
    uint64_t initial_x2 = TEST_MEM_GUEST_ADDR;
    uint64_t initial_x5 = TEST_MEM_GUEST_ADDR + 8;
    
    printf("TCTI Unit B: Atomic Capture Test\n");
    printf("=================================\n\n");
    
    // Clear atomic capture buffer
    g_atomic_cmp_capture.x3_before = 0;
    g_atomic_cmp_capture.x6_before = 0;
    g_atomic_cmp_capture.nzcv_after_subs = 0;
    
    test_mem = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (test_mem == MAP_FAILED) {
        printf("FAIL: Could not allocate memory\n");
        return 1;
    }
    *(uint64_t*)test_mem = 0xDEADBEEFCAFEBABEULL;
    
    ret = setup(&cpu, test_pc, initial_x2, initial_x5, test_mem);
    if (ret != 0) {
        printf("FAIL: Setup failed\n");
        return 1;
    }
    
    // Generate: STR + CMP + exit
    a64_gen_init(&gen, bytecode, 32);
    a64_gen_reset(&gen, test_pc);
    
    ret = a64_gen_instruction(&gen, 0xf800845f, test_pc);
    if (ret != 0 && ret != 1) {
        printf("FAIL: STR generation error\n");
        return 1;
    }
    
    ret = a64_gen_instruction(&gen, 0xeb05004f, test_pc + 4);
    if (ret != 0 && ret != 1) {
        printf("FAIL: CMP generation error\n");
        return 1;
    }
    
    extern tcti_gadget_t gadget_exit;
    gen.gadgets[gen.num_gadgets++] = gadget_exit;
    
    printf("Executing...\n\n");
    tcti_entry_block(gen.gadgets, &cpu);
    
    printf("=== ATOMIC CAPTURE RESULTS ===\n");
    printf("x3_before:      0x%016llx (expected: 0x0000000000002008)\n",
           (unsigned long long)g_atomic_cmp_capture.x3_before);
    printf("x6_before:      0x%016llx (expected: 0x0000000000002008)\n",
           (unsigned long long)g_atomic_cmp_capture.x6_before);
    printf("nzcv_after_subs: 0x%016llx (expected: 0x60000000 for Z=1,C=1)\n",
           (unsigned long long)g_atomic_cmp_capture.nzcv_after_subs);
    printf("\n");
    
    int pass = 1;
    
    if (g_atomic_cmp_capture.x3_before != 0x2008) {
        printf("FAIL: x3_before is wrong\n");
        pass = 0;
    }
    if (g_atomic_cmp_capture.x6_before != 0x2008) {
        printf("FAIL: x6_before is wrong\n");
        pass = 0;
    }
    if ((g_atomic_cmp_capture.nzcv_after_subs & 0x40000000) == 0) {
        printf("FAIL: Z flag not set (got 0x%llx, expected bit 30 set)\n",
               (unsigned long long)g_atomic_cmp_capture.nzcv_after_subs);
        pass = 0;
    } else {
        printf("PASS: Z flag is set (equality detected)\n");
    }
    
    printf("\n=== CPU STATE ===\n");
    printf("x2: 0x%llx\n", (unsigned long long)cpu.x[2]);
    printf("x5: 0x%llx\n", (unsigned long long)cpu.x[5]);
    printf("pstate: 0x%llx\n", (unsigned long long)cpu.pstate);
    
    munmap(test_mem, 4096);
    
    if (pass) {
        printf("\n=== UNIT B ATOMIC CAPTURE: PASS ===\n");
        return 0;
    } else {
        printf("\n=== UNIT B ATOMIC CAPTURE: FAIL ===\n");
        return 1;
    }
}
