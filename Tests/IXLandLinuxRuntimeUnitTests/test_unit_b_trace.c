/*
 * TCTI Unit B with NZCV tracing
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>

#import <IXLandLinuxRuntime/util/misc.h>
#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>
#import <IXLandLinuxRuntime/emu/mmu.h>
#import <IXLandLinuxRuntime/emu/tlb.h>
#import <IXLandLinuxRuntime/tcti/aarch64/gen.h>

static uint8_t *test_mem = NULL;
static tcti_gadget_t bytecode[32];
static struct tlb test_tlb;
static struct mmu test_mmu;

// Global trace buffer
struct trace_entry {
    uint64_t x3;
    uint64_t x6;
    uint64_t nzcv;
} g_trace[8];
int g_trace_count = 0;

extern void tcti_entry_block(void *gadgets, struct cpu_state *cpu);

__attribute__((naked)) void trace_gadget(void) {
    asm volatile(
        // Save regs we'll use - use x19/x20 (callee-saved)
        "stp x19, x20, [sp, #-16]!\n\t"
        
        // Load current index into x19
        "adrp x20, _g_trace_count@PAGE\n\t"
        "add x20, x20, _g_trace_count@PAGEOFF\n\t"
        "ldr w19, [x20]\n\t"
        
        // Check if index >= 8 using subtraction (cmp clobbers flags)
        // Instead: just use cbz or always write (wrap around)
        // Simple approach: always write, let it wrap or overflow
        
        // Calculate offset = index * 24
        "mov x20, #24\n\t"
        "mul x19, x19, x20\n\t"
        "adrp x20, _g_trace@PAGE\n\t"
        "add x20, x20, _g_trace@PAGEOFF\n\t"
        "add x20, x20, x19\n\t"
        
        // Store values - CAPTURE NZCV FIRST before any flag-setting instruction
        "mrs x19, nzcv\n\t"           // Capture NZCV immediately!
        "str x19, [x20, #16]\n\t"     // Store NZCV
        "str x3, [x20]\n\t"           // Store x3
        "str x6, [x20, #8]\n\t"       // Store x6
        
        // Increment counter - this uses add which sets flags, but we've already captured
        "adrp x20, _g_trace_count@PAGE\n\t"
        "add x20, x20, _g_trace_count@PAGEOFF\n\t"
        "ldr w19, [x20]\n\t"
        "add w19, w19, #1\n\t"
        "str w19, [x20]\n\t"
        
        // Restore regs
        "ldp x19, x20, [sp], #16\n\t"
        
        // Chain to next
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t"
    );
}

int main(void) {
    struct cpu_state cpu;
    a64_gen_state_t gen;
    
    memset(g_trace, 0, sizeof(g_trace));
    g_trace_count = 0;
    
    test_mem = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (test_mem == MAP_FAILED) return 1;
    
    memset(&cpu, 0, sizeof(cpu));
    memset(&test_mmu, 0, sizeof(test_mmu));
    cpu.mmu = &test_mmu;
    memset(&test_tlb, 0, sizeof(test_tlb));
    test_tlb.mmu = &test_mmu;
    cpu.tlb = &test_tlb;
    
    cpu.pc = 0x1000;
    cpu.x[2] = 0x2000;
    cpu.x[5] = 0x2008;
    cpu.sp = 0x80000000ULL;
    cpu.pstate = 0;
    
    *(uint64_t*)test_mem = 0xDEADBEEFCAFEBABEULL;
    
    uint64_t page = 0x2000 & ~0xFFFULL;
    int idx = TLB_INDEX(0x2000);
    cpu.tlb->entries[idx].page = page;
    cpu.tlb->entries[idx].page_if_writable = page;
    cpu.tlb->entries[idx].data_minus_addr = (uintptr_t)test_mem - (uintptr_t)page;
    
    a64_gen_init(&gen, bytecode, 64);
    a64_gen_reset(&gen, 0x1000);
    
    // trace0 -> STR -> trace1 -> CMP -> trace2 -> exit -> trace3
    gen.gadgets[gen.num_gadgets++] = (tcti_gadget_t)trace_gadget;  // [0]
    
    int ret = a64_gen_instruction(&gen, 0xf800845f, 0x1000);  // STR
    if (ret != 0 && ret != 1) return 1;
    
    gen.gadgets[gen.num_gadgets++] = (tcti_gadget_t)trace_gadget;  // [1]
    
    ret = a64_gen_instruction(&gen, 0xeb05004f, 0x1004);  // CMP
    if (ret != 0 && ret != 1) return 1;
    
    gen.gadgets[gen.num_gadgets++] = (tcti_gadget_t)trace_gadget;  // [2]
    
    extern tcti_gadget_t gadget_exit;
    gen.gadgets[gen.num_gadgets++] = gadget_exit;
    gen.gadgets[gen.num_gadgets++] = (tcti_gadget_t)trace_gadget;  // [3]
    
    printf("Trace: [0]entry [1]after-STR [2]after-CMP [3]after-exit\n\n");
    
    tcti_entry_block(gen.gadgets, &cpu);
    
    printf("Bytecode layout (%zu gadgets):\n", gen.num_gadgets);
    for (size_t i = 0; i < gen.num_gadgets; i++) {
        printf("  [%zu] = %p\n", i, (void*)gen.gadgets[i]);
    }
    printf("\n");
    
    printf("Trace results (%d entries):\n\n", g_trace_count);
    
    const char* labels[] = {"Entry", "After STR", "After CMP", "After exit"};
    for (int i = 0; i < g_trace_count && i < 8; i++) {
        printf("[%d] %s:\n", i, i < 4 ? labels[i] : "?");
        printf("  x3   = 0x%016llx\n", (unsigned long long)g_trace[i].x3);
        printf("  x6   = 0x%016llx\n", (unsigned long long)g_trace[i].x6);
        printf("  NZCV = 0x%016llx (N=%d Z=%d C=%d V=%d)\n",
               (unsigned long long)g_trace[i].nzcv,
               (int)((g_trace[i].nzcv >> 31) & 1),
               (int)((g_trace[i].nzcv >> 30) & 1),
               (int)((g_trace[i].nzcv >> 29) & 1),
               (int)((g_trace[i].nzcv >> 28) & 1));
        printf("\n");
    }
    
    printf("Final cpu->pstate = 0x%llx\n", (unsigned long long)cpu.pstate);
    printf("Final cpu->x[2]   = 0x%llx\n", (unsigned long long)cpu.x[2]);
    printf("Final cpu->x[5]   = 0x%llx\n", (unsigned long long)cpu.x[5]);
    
    munmap(test_mem, 4096);
    return 0;
}
