/*
 * TCTI NZCV Exit Test
 * Verify that condition flags survive block exit
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
static tcti_gadget_t bytecode[32];
static struct tlb test_tlb;
static struct mmu test_mmu;

extern void tcti_entry_block(void *gadgets, struct cpu_state *cpu);

static int setup(struct cpu_state *cpu, uint64_t pc, void *mem) {
    memset(cpu, 0, sizeof(*cpu));
    memset(&test_mmu, 0, sizeof(test_mmu));
    cpu->mmu = &test_mmu;
    memset(&test_tlb, 0, sizeof(test_tlb));
    test_tlb.mmu = &test_mmu;
    cpu->tlb = &test_tlb;
    cpu->pc = pc;
    cpu->sp = 0x80000000ULL;
    cpu->pstate = 0;
    return 0;
}

int main(void) {
    struct cpu_state cpu;
    a64_gen_state_t gen;
    int ret;
    
    printf("NZCV Exit Test\n");
    printf("==============\n\n");
    
    test_mem = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (test_mem == MAP_FAILED) return 1;
    
    ret = setup(&cpu, 0x1000, test_mem);
    if (ret != 0) return 1;
    
    // Set up registers for equality compare
    cpu.x[2] = 0x1000;
    cpu.x[5] = 0x1000;
    
    a64_gen_init(&gen, bytecode, 32);
    a64_gen_reset(&gen, 0x1000);
    
    // Just CMP, then exit
    ret = a64_gen_instruction(&gen, 0xeb05004f, 0x1000);  // cmp x2, x5
    if (ret != 0 && ret != 1) {
        printf("CMP generation failed\n");
        return 1;
    }
    
    extern tcti_gadget_t gadget_exit;
    gen.gadgets[gen.num_gadgets++] = gadget_exit;
    
    printf("Executing: cmp x2, x5 (both 0x1000) -> exit\n\n");
    
    tcti_entry_block(gen.gadgets, &cpu);
    
    printf("Result:\n");
    printf("  pstate = 0x%llx\n", (unsigned long long)cpu.pstate);
    printf("  Z flag = %d\n", (int)((cpu.pstate >> 30) & 1));
    
    int pass = ((cpu.pstate >> 30) & 1) == 1;
    
    if (pass) {
        printf("\nPASS: Z flag preserved through exit\n");
    } else {
        printf("\nFAIL: Z flag lost (got %d, expected 1)\n", (int)((cpu.pstate >> 30) & 1));
    }
    
    munmap(test_mem, 4096);
    return pass ? 0 : 1;
}
