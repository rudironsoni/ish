/*
 * TCTI Unit B Diagnostic: Two-instruction sequence with capture
 * str xzr, [x2], #8
 * cmp x2, x5
 * 
 * Purpose: Capture exact live operands at CMP entry to diagnose NZCV mismatch.
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

// TCTI bytecode buffer - larger to accommodate diagnostic gadgets
static tcti_gadget_t bytecode[64];

// TLB and MMU for test
static struct tlb test_tlb;
static struct mmu test_mmu;

// Diagnostic capture buffer
struct cmp_capture {
    uint64_t live_x2;      // Host x3 (guest x2) captured right before CMP
    uint64_t live_x5;      // Host x6 (guest x5) captured right before CMP
    uint64_t mem_x2;       // cpu->x[2] from memory
    uint64_t mem_x5;       // cpu->x[5] from memory
    uint64_t nzcv_before;  // NZCV before CMP
    uint64_t nzcv_after;   // NZCV after CMP
    int captured;          // Whether capture happened
} g_capture = {0};

// External TCTI entry point
extern void tcti_entry_block(void *gadgets, struct cpu_state *cpu);

// Assembly capture function - runs in TCTI context
// Captures x3 (guest x2) and x6 (guest x5) before CMP executes
extern void capture_cmp_inputs(void);

__attribute__((naked)) void capture_cmp_inputs_impl(void) {
    // This gadget is placed before CMP to capture inputs
    // On entry: x29 = cpu_state, x3 = guest x2, x6 = guest x5
    asm volatile(
        "adrp x18, _g_capture@PAGE\n\t"
        "add x18, x18, _g_capture@PAGEOFF\n\t"
        
        // Check if already captured
        "ldr w27, [x18, #48]\n\t"  // g_capture.captured
        "cmp w27, #1\n\t"
        "b.eq 1f\n\t"
        
        // Capture live registers
        "str x3, [x18]\n\t"         // g_capture.live_x2 (host x3 = guest x2)
        "str x6, [x18, #8]\n\t"    // g_capture.live_x5 (host x6 = guest x5)
        
        // Capture NZCV before CMP
        "mrs x27, nzcv\n\t"
        "str x27, [x18, #32]\n\t"  // g_capture.nzcv_before
        
        // Mark captured
        "mov w27, #1\n\t"
        "str w27, [x18, #48]\n\t"  // g_capture.captured = 1
        
        "1:\n\t"
        // Continue to next gadget
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t"
    );
}

__attribute__((naked)) void capture_nzcv_after_impl(void) {
    asm volatile(
        "adrp x18, _g_capture@PAGE\n\t"
        "add x18, x18, _g_capture@PAGEOFF\n\t"
        "mrs x27, nzcv\n\t"
        "str x27, [x18, #40]\n\t"  // g_capture.nzcv_after
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t"
    );
}

static int setup_test_environment(struct cpu_state *cpu, uint64_t test_pc, 
                                   uint64_t guest_x2, uint64_t guest_x5,
                                   void *host_mem) {
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

static int generate_block(a64_gen_state_t *gen, uint64_t test_pc, 
                          void *bytecode_buf, size_t buf_size) {
    int ret;
    
    ret = a64_gen_init(gen, bytecode_buf, buf_size / sizeof(tcti_gadget_t));
    if (ret != 0) return -1;
    
    // Generate STR
    a64_gen_reset(gen, test_pc);
    ret = a64_gen_instruction(gen, 0xf800845f, test_pc);
    if (ret != 0 && ret != 1) return -1;
    
    // Add capture gadget before CMP
    gen->gadgets[gen->num_gadgets++] = (tcti_gadget_t)capture_cmp_inputs_impl;
    
    // Generate CMP
    ret = a64_gen_instruction(gen, 0xeb05004f, test_pc + 4);
    if (ret != 0 && ret != 1) return -1;
    
    // Add capture gadget after CMP to get NZCV
    gen->gadgets[gen->num_gadgets++] = (tcti_gadget_t)capture_nzcv_after_impl;
    
    // Add exit
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
    uint64_t initial_x5 = TEST_MEM_GUEST_ADDR + 8;
    
    printf("TCTI UNIT B DIAGNOSTIC\n");
    printf("======================\n\n");
    
    test_mem = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (test_mem == MAP_FAILED) {
        printf("FAIL: Could not allocate test memory\n");
        return 1;
    }
    
    *(uint64_t*)test_mem = 0xDEADBEEFCAFEBABEULL;
    
    ret = setup_test_environment(&cpu, test_pc, initial_x2, initial_x5, test_mem);
    if (ret != 0) {
        printf("FAIL: Test environment setup failed\n");
        return 1;
    }
    
    printf("Initial: x2=0x%llx, x5=0x%llx\n\n",
           (unsigned long long)initial_x2, (unsigned long long)initial_x5);
    
    ret = generate_block(&gen_state, test_pc, bytecode, sizeof(bytecode));
    if (ret != 0) {
        printf("FAIL: Block generation error\n");
        return 1;
    }
    
    printf("Generated %zu gadgets (includes capture gadgets)\n", gen_state.num_gadgets);
    printf("Executing...\n\n");
    fflush(stdout);
    
    tcti_entry_block(gen_state.gadgets, &cpu);
    
    // After execution, capture memory values
    g_capture.mem_x2 = cpu.x[2];
    g_capture.mem_x5 = cpu.x[5];
    
    printf("=== CAPTURED VALUES ===\n");
    printf("\n1. Live registers at CMP entry:\n");
    printf("   Host x3 (guest x2): 0x%016llx\n", (unsigned long long)g_capture.live_x2);
    printf("   Host x6 (guest x5): 0x%016llx\n", (unsigned long long)g_capture.live_x5);
    
    printf("\n2. Memory values at CMP entry:\n");
    printf("   cpu->x[2]: 0x%016llx\n", (unsigned long long)g_capture.mem_x2);
    printf("   cpu->x[5]: 0x%016llx\n", (unsigned long long)g_capture.mem_x5);
    
    printf("\n3. NZCV:\n");
    printf("   Before CMP: 0x%016llx\n", (unsigned long long)g_capture.nzcv_before);
    printf("   After CMP:  0x%016llx\n", (unsigned long long)g_capture.nzcv_after);
    printf("   N=%d Z=%d C=%d V=%d\n",
           (int)((g_capture.nzcv_after >> 31) & 1),
           (int)((g_capture.nzcv_after >> 30) & 1),
           (int)((g_capture.nzcv_after >> 29) & 1),
           (int)((g_capture.nzcv_after >> 28) & 1));
    
    printf("\n=== ANALYSIS ===\n");
    
    // Determine first failing property
    int stale_live_x2 = (g_capture.live_x2 != 0x2008);
    int stale_live_x5 = (g_capture.live_x5 != 0x2008);
    int mismatch = (g_capture.live_x2 != g_capture.live_x5);
    
    printf("\nLive x2 is %s (expected 0x2008, got 0x%llx)\n",
           stale_live_x2 ? "STALE" : "CORRECT",
           (unsigned long long)g_capture.live_x2);
    printf("Live x5 is %s (expected 0x2008, got 0x%llx)\n",
           stale_live_x5 ? "STALE" : "CORRECT",
           (unsigned long long)g_capture.live_x5);
    printf("Live values match: %s\n", mismatch ? "NO" : "YES");
    
    printf("\nFirst failing property: ");
    if (stale_live_x2) {
        printf("stale live x2 - post-STR hot register not resynced\n");
    } else if (stale_live_x5) {
        printf("stale live x5\n");
    } else if (!stale_live_x2 && !stale_live_x5 && mismatch) {
        printf("cmp itself - inputs correct but comparison wrong\n");
    } else {
        printf("NZCV flag handling in compare gadget\n");
    }
    
    printf("\n=== CONCLUSION ===\n");
    if (stale_live_x2) {
        printf("Patch target: tcti/aarch64/gadgets_memory.c\n");
        printf("Function: gadget_str_x_impl\n");
        printf("Specific: Post-helper hot-register reload/resync path\n");
    }
    
    munmap(test_mem, 4096);
    return (stale_live_x2 || stale_live_x5) ? 1 : 0;
}
