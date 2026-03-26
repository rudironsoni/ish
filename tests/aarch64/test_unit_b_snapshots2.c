/*
 * TCTI Unit B with Three Snapshots
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

struct snapshot {
    uint64_t x3;
    uint64_t x6;
} g_snap[3] = {{0}};  // 0=entry, 1=after_str, 2=after_cmp

extern void tcti_entry_block(void *gadgets, struct cpu_state *cpu);

__attribute__((naked)) void snap0(void) {
    asm volatile(
        "adrp x26, _g_snap@PAGE\n\t"
        "add x26, x26, _g_snap@PAGEOFF\n\t"
        "str x3, [x26]\n\t"
        "str x6, [x26, #8]\n\t"
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t"
    );
}

__attribute__((naked)) void snap1(void) {
    asm volatile(
        "adrp x26, _g_snap@PAGE\n\t"
        "add x26, x26, _g_snap@PAGEOFF\n\t"
        "str x3, [x26, #16]\n\t"
        "str x6, [x26, #24]\n\t"
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t"
    );
}

__attribute__((naked)) void snap2(void) {
    asm volatile(
        "adrp x26, _g_snap@PAGE\n\t"
        "add x26, x26, _g_snap@PAGEOFF\n\t"
        "str x3, [x26, #32]\n\t"
        "str x6, [x26, #40]\n\t"
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
    int ret;
    
    uint64_t test_pc = 0x1000ULL;
    uint64_t initial_x2 = TEST_MEM_GUEST_ADDR;      // 0x2000
    uint64_t initial_x5 = TEST_MEM_GUEST_ADDR + 8;  // 0x2008
    
    printf("TCTI Unit B: Three-Snapshot Diagnostic\n");
    printf("=======================================\n\n");
    
    memset(g_snap, 0, sizeof(g_snap));
    
    test_mem = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (test_mem == MAP_FAILED) return 1;
    *(uint64_t*)test_mem = 0xDEADBEEFCAFEBABEULL;
    
    ret = setup(&cpu, test_pc, initial_x2, initial_x5, test_mem);
    if (ret != 0) return 1;
    
    a64_gen_init(&gen, bytecode, 64);
    a64_gen_reset(&gen, test_pc);
    
    gen.gadgets[gen.num_gadgets++] = (tcti_gadget_t)snap0;  // Before STR
    ret = a64_gen_instruction(&gen, 0xf800845f, test_pc);
    if (ret != 0 && ret != 1) return 1;
    gen.gadgets[gen.num_gadgets++] = (tcti_gadget_t)snap1;  // After STR
    ret = a64_gen_instruction(&gen, 0xeb05004f, test_pc + 4);
    if (ret != 0 && ret != 1) return 1;
    gen.gadgets[gen.num_gadgets++] = (tcti_gadget_t)snap2;  // After CMP
    extern tcti_gadget_t gadget_exit;
    gen.gadgets[gen.num_gadgets++] = gadget_exit;
    
    printf("Sequence: snap0 -> STR -> snap1 -> CMP -> snap2 -> exit\n\n");
    
    tcti_entry_block(gen.gadgets, &cpu);
    
    printf("=== SNAPSHOTS ===\n\n");
    
    printf("Snapshot 0 (before STR):\n");
    printf("  x3 (x2) = 0x%llx (expected: 0x2000)\n", (unsigned long long)g_snap[0].x3);
    printf("  x6 (x5) = 0x%llx (expected: 0x2008)\n", (unsigned long long)g_snap[0].x6);
    
    printf("\nSnapshot 1 (after STR):\n");
    printf("  x3 (x2) = 0x%llx (expected: 0x2008)\n", (unsigned long long)g_snap[1].x3);
    printf("  x6 (x5) = 0x%llx (expected: 0x2008)\n", (unsigned long long)g_snap[1].x6);
    
    printf("\nSnapshot 2 (after CMP):\n");
    printf("  x3 (x2) = 0x%llx\n", (unsigned long long)g_snap[2].x3);
    printf("  x6 (x5) = 0x%llx\n", (unsigned long long)g_snap[2].x6);
    
    printf("\n=== FINAL STATE ===\n");
    printf("  cpu->x[2] = 0x%llx\n", (unsigned long long)cpu.x[2]);
    printf("  cpu->x[5] = 0x%llx\n", (unsigned long long)cpu.x[5]);
    printf("  pstate    = 0x%llx (N=%d Z=%d C=%d V=%d)\n",
           (unsigned long long)cpu.pstate,
           (int)((cpu.pstate >> 31) & 1),
           (int)((cpu.pstate >> 30) & 1),
           (int)((cpu.pstate >> 29) & 1),
           (int)((cpu.pstate >> 28) & 1));
    
    printf("\n=== ANALYSIS ===\n");
    if (g_snap[0].x3 == 0x2000 && g_snap[0].x6 == 0x2008) {
        printf("Entry: CORRECT (x3=0x2000, x6=0x2008)\n");
    } else {
        printf("Entry: WRONG\n");
    }
    
    if (g_snap[1].x3 == 0x2008 && g_snap[1].x6 == 0x2008) {
        printf("After STR: CORRECT (x3=0x2008, x6=0x2008)\n");
    } else {
        printf("After STR: WRONG\n");
    }
    
    int cmp_eq = ((cpu.pstate >> 30) & 1);  // Z flag
    if (cmp_eq) {
        printf("CMP result: EQUALITY (Z=1)\n");
    } else {
        printf("CMP result: NOT EQUAL (Z=0, N=%d)\n", (int)((cpu.pstate >> 31) & 1));
    }
    
    munmap(test_mem, 4096);
    return cmp_eq ? 0 : 1;
}
