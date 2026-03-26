/*
 * TCTI Unit B Final: Two-instruction sequence with diagnostic capture
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

static uint8_t *test_mem = NULL;
#define TEST_MEM_GUEST_ADDR 0x2000ULL
static tcti_gadget_t bytecode[64];
static struct tlb test_tlb;
static struct mmu test_mmu;

struct capture {
    uint64_t x3_at_cmp;
    uint64_t x6_at_cmp;
    uint64_t nzcv_from_subs;
    int captured;
} g_cap = {0};

extern void tcti_entry_block(void *gadgets, struct cpu_state *cpu);

__attribute__((naked)) void capture_gadget(void) {
    asm volatile(
        "adrp x26, _g_cap@PAGE\n\t"
        "add x26, x26, _g_cap@PAGEOFF\n\t"
        "ldr x18, [x26, #24]\n\t"
        // Flag-neutral: cbz/cbnz don't modify NZCV
        "cbnz x18, 1f\n\t"
        "str x3, [x26]\n\t"
        "str x6, [x26, #8]\n\t"
        "mrs x18, nzcv\n\t"
        "str x18, [x26, #16]\n\t"
        "mov x18, #1\n\t"
        "str x18, [x26, #24]\n\t"
        "1:\n\t"
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t"
    );
}

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
    
    uint64_t test_pc = 0x1000ULL;
    uint64_t x2 = TEST_MEM_GUEST_ADDR;
    uint64_t x5 = TEST_MEM_GUEST_ADDR + 8;
    
    printf("TCTI Unit B Final\n");
    printf("=================\n\n");
    
    test_mem = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (test_mem == MAP_FAILED) return 1;
    *(uint64_t*)test_mem = 0xDEADBEEFCAFEBABEULL;
    
    setup(&cpu, test_pc, x2, x5, test_mem);
    
    // Generate: STR, capture, CMP, capture, exit
    a64_gen_init(&gen, bytecode, 64);
    a64_gen_reset(&gen, test_pc);
    a64_gen_instruction(&gen, 0xf800845f, test_pc);
    gen.gadgets[gen.num_gadgets++] = (tcti_gadget_t)capture_gadget;
    a64_gen_instruction(&gen, 0xeb05004f, test_pc + 4);
    gen.gadgets[gen.num_gadgets++] = (tcti_gadget_t)capture_gadget;
    extern tcti_gadget_t gadget_exit;
    gen.gadgets[gen.num_gadgets++] = gadget_exit;
    
    printf("Executing...\n\n");
    tcti_entry_block(gen.gadgets, &cpu);
    
    printf("Final: x2=0x%llx x5=0x%llx pstate=0x%llx\n",
           (unsigned long long)cpu.x[2], (unsigned long long)cpu.x[5],
           (unsigned long long)cpu.pstate);
    printf("NZCV: N=%d Z=%d C=%d V=%d\n\n", cpu.n, cpu.z, cpu.c, cpu.v);
    
    printf("Capture at CMP:\n");
    printf("  x3 (x2): 0x%llx (expected 0x2008)\n", (unsigned long long)g_cap.x3_at_cmp);
    printf("  x6 (x5): 0x%llx (expected 0x2008)\n", (unsigned long long)g_cap.x6_at_cmp);
    printf("  NZCV after subs: 0x%llx\n", (unsigned long long)g_cap.nzcv_from_subs);
    
    int pass = 1;
    if (g_cap.x3_at_cmp != 0x2008 || g_cap.x6_at_cmp != 0x2008) {
        printf("\nFAIL: Register values wrong at CMP time\n");
        pass = 0;
    } else if ((g_cap.nzcv_from_subs & 0x40000000) == 0) {
        printf("\nFAIL: Z flag not set after subs (got 0x%llx)\n",
               (unsigned long long)g_cap.nzcv_from_subs);
        pass = 0;
    } else {
        printf("\nPASS: subs correctly computed equality\n");
    }
    
    munmap(test_mem, 4096);
    return pass ? 0 : 1;
}
