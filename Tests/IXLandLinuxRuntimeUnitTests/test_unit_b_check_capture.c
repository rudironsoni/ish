/*
 * Unit B - Check atomic capture buffer
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>

#import <IXLandLinuxRuntime/util/misc.h>
#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>
#import <IXLandLinuxRuntime/emu/aarch64/decode.h>
#import <IXLandLinuxRuntime/emu/mmu.h>
#import <IXLandLinuxRuntime/emu/tlb.h>
#import <IXLandLinuxRuntime/tcti/aarch64/gen.h>

static uint8_t *test_mem = NULL;
#define TEST_MEM_GUEST_ADDR 0x2000ULL
static tcti_gadget_t bytecode[32];
static struct tlb test_tlb;
static struct mmu test_mmu;

extern struct atomic_cmp_capture {
    uint64_t x3_before;
    uint64_t x6_before;
    uint64_t nzcv_after_subs;
} g_atomic_cmp_capture;

extern void tcti_entry_block(void *gadgets, struct cpu_state *cpu);

static int setup(struct cpu_state *cpu, uint64_t test_pc, 
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

int main(void) {
    struct cpu_state cpu;
    a64_gen_state_t gen_state;
    int ret;
    
    uint64_t test_pc = 0x1000ULL;
    uint64_t initial_x2 = TEST_MEM_GUEST_ADDR;
    uint64_t initial_x5 = TEST_MEM_GUEST_ADDR + 8;
    
    printf("Unit B: Check Atomic Capture\n");
    printf("=============================\n\n");
    
    // Clear capture buffer
    g_atomic_cmp_capture.x3_before = 0;
    g_atomic_cmp_capture.x6_before = 0;
    g_atomic_cmp_capture.nzcv_after_subs = 0;
    
    test_mem = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (test_mem == MAP_FAILED) return 1;
    *(uint64_t*)test_mem = 0xDEADBEEFCAFEBABEULL;
    
    ret = setup(&cpu, test_pc, initial_x2, initial_x5, test_mem);
    if (ret != 0) return 1;
    
    a64_gen_init(&gen_state, bytecode, 32);
    a64_gen_reset(&gen_state, test_pc);
    
    ret = a64_gen_instruction(&gen_state, 0xf800845f, test_pc);
    if (ret != 0 && ret != 1) return 1;
    
    ret = a64_gen_instruction(&gen_state, 0xeb05004f, test_pc + 4);
    if (ret != 0 && ret != 1) return 1;
    
    extern tcti_gadget_t gadget_exit;
    gen_state.gadgets[gen_state.num_gadgets++] = gadget_exit;
    
    printf("Executing...\n\n");
    tcti_entry_block(gen_state.gadgets, &cpu);
    
    printf("=== ATOMIC CAPTURE BUFFER ===\n");
    printf("x3_before = 0x%llx\n", (unsigned long long)g_atomic_cmp_capture.x3_before);
    printf("x6_before = 0x%llx\n", (unsigned long long)g_atomic_cmp_capture.x6_before);
    printf("nzcv_after_subs = 0x%llx\n", (unsigned long long)g_atomic_cmp_capture.nzcv_after_subs);
    printf("  N=%d Z=%d C=%d V=%d\n\n",
           (int)((g_atomic_cmp_capture.nzcv_after_subs >> 31) & 1),
           (int)((g_atomic_cmp_capture.nzcv_after_subs >> 30) & 1),
           (int)((g_atomic_cmp_capture.nzcv_after_subs >> 29) & 1),
           (int)((g_atomic_cmp_capture.nzcv_after_subs >> 28) & 1));
    
    printf("=== CPU STATE ===\n");
    printf("cpu.x[2] = 0x%llx\n", (unsigned long long)cpu.x[2]);
    printf("cpu.x[5] = 0x%llx\n", (unsigned long long)cpu.x[5]);
    printf("cpu.pstate = 0x%llx\n", (unsigned long long)cpu.pstate);
    
    munmap(test_mem, 4096);
    
    // Check if atomic capture shows Z=1
    if ((g_atomic_cmp_capture.nzcv_after_subs >> 30) & 1) {
        printf("\nPASS: Atomic capture shows Z=1 (equality)\n");
        return 0;
    } else {
        printf("\nFAIL: Atomic capture does not show Z=1\n");
        return 1;
    }
}
