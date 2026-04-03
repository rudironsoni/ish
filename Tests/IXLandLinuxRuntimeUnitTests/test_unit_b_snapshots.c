/*
 * TCTI Unit B with Entry and Post-STR Snapshots
 * Flag-neutral capture to localize where x3/x6 become zero
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

// Test memory
static uint8_t *test_mem = NULL;
#define TEST_MEM_GUEST_ADDR 0x2000ULL
static tcti_gadget_t bytecode[64];
static struct tlb test_tlb;
static struct mmu test_mmu;

// Snapshot buffers
struct entry_snapshot {
    uint64_t x3;
    uint64_t x6;
} g_entry_snap = {0};

struct post_str_snapshot {
    uint64_t x3;
    uint64_t x6;
} g_post_str_snap = {0};

extern void tcti_entry_block(void *gadgets, struct cpu_state *cpu);

// Flag-neutral snapshot gadget - just stores and chains
__attribute__((naked)) void entry_snapshot_gadget(void) {
    asm volatile(
        "adrp x26, _g_entry_snap@PAGE\n\t"
        "add x26, x26, _g_entry_snap@PAGEOFF\n\t"
        "str x3, [x26]\n\t"          // Store x3 to g_entry_snap.x3
        "str x6, [x26, #8]\n\t"      // Store x6 to g_entry_snap.x6
        "ldr x27, [x28], #8\n\t"     // Load next gadget
        "br x27\n\t"                 // Branch to next
    );
}

__attribute__((naked)) void post_str_snapshot_gadget(void) {
    asm volatile(
        "adrp x26, _g_post_str_snap@PAGE\n\t"
        "add x26, x26, _g_post_str_snap@PAGEOFF\n\t"
        "str x3, [x26]\n\t"          // Store x3 to g_post_str_snap.x3
        "str x6, [x26, #8]\n\t"      // Store x6 to g_post_str_snap.x6
        "ldr x27, [x28], #8\n\t"     // Load next gadget
        "br x27\n\t"                 // Branch to next
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
    uint64_t initial_x2 = TEST_MEM_GUEST_ADDR;
    uint64_t initial_x5 = TEST_MEM_GUEST_ADDR + 8;
    
    printf("TCTI Unit B: Flag-Neutral Snapshot Test\n");
    printf("========================================\n\n");
    
    // Clear snapshots
    g_entry_snap.x3 = 0;
    g_entry_snap.x6 = 0;
    g_post_str_snap.x3 = 0;
    g_post_str_snap.x6 = 0;
    
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
    
    // Generate: snapshot_entry, STR, snapshot_after_str, CMP, exit
    a64_gen_init(&gen, bytecode, 64);
    a64_gen_reset(&gen, test_pc);
    
    // Add entry snapshot gadget
    gen.gadgets[gen.num_gadgets++] = (tcti_gadget_t)entry_snapshot_gadget;
    
    // Generate STR
    ret = a64_gen_instruction(&gen, 0xf800845f, test_pc);
    if (ret != 0 && ret != 1) {
        printf("FAIL: STR generation error\n");
        return 1;
    }
    
    // Add post-STR snapshot gadget
    gen.gadgets[gen.num_gadgets++] = (tcti_gadget_t)post_str_snapshot_gadget;
    
    // Generate CMP
    ret = a64_gen_instruction(&gen, 0xeb05004f, test_pc + 4);
    if (ret != 0 && ret != 1) {
        printf("FAIL: CMP generation error\n");
        return 1;
    }
    
    // Add exit
    extern tcti_gadget_t gadget_exit;
    gen.gadgets[gen.num_gadgets++] = gadget_exit;
    
    printf("Executing sequence:\n");
    printf("  1. snapshot_entry\n");
    printf("  2. str xzr, [x2], #8\n");
    printf("  3. snapshot_after_str\n");
    printf("  4. cmp x2, x5\n");
    printf("  5. exit\n\n");
    
    tcti_entry_block(gen.gadgets, &cpu);
    
    // Report results
    printf("=== SNAPSHOT RESULTS ===\n\n");
    
    printf("1. Entry snapshot:\n");
    printf("   x3 = 0x%016llx (expected: 0x0000000000002008)\n",
           (unsigned long long)g_entry_snap.x3);
    printf("   x6 = 0x%016llx (expected: 0x0000000000002008)\n",
           (unsigned long long)g_entry_snap.x6);
    printf("   Status: %s\n",
           (g_entry_snap.x3 == 0x2008 && g_entry_snap.x6 == 0x2008) ? "CORRECT" : "WRONG");
    
    printf("\n2. After-STR snapshot:\n");
    printf("   x3 = 0x%016llx (expected: 0x0000000000002008)\n",
           (unsigned long long)g_post_str_snap.x3);
    printf("   x6 = 0x%016llx (expected: 0x0000000000002008)\n",
           (unsigned long long)g_post_str_snap.x6);
    printf("   Status: %s\n",
           (g_post_str_snap.x3 == 0x2008 && g_post_str_snap.x6 == 0x2008) ? "CORRECT" : "WRONG");
    
    printf("\n3. Architectural state:\n");
    printf("   cpu->x[2] = 0x%llx\n", (unsigned long long)cpu.x[2]);
    printf("   cpu->x[5] = 0x%llx\n", (unsigned long long)cpu.x[5]);
    printf("   pstate    = 0x%llx\n", (unsigned long long)cpu.pstate);
    printf("   exit      = %d\n", cpu.tcti_exit_reason);
    
    printf("\n=== ANALYSIS ===\n");
    
    int entry_ok = (g_entry_snap.x3 == 0x2008 && g_entry_snap.x6 == 0x2008);
    int post_str_ok = (g_post_str_snap.x3 == 0x2008 && g_post_str_snap.x6 == 0x2008);
    
    if (!entry_ok) {
        printf("\nFirst failing point: harness entry state\n");
        printf("Entry snapshot already shows wrong values.\n");
        printf("The TCTI entry is not loading cpu->x[2] and cpu->x[5] correctly.\n");
    } else if (!post_str_ok) {
        printf("\nFirst failing point: STR helper save/restore\n");
        printf("Entry was correct, but after-STR is wrong.\n");
        printf("The save/restore in gadget_str_x_impl corrupted x3 or x6.\n");
    } else {
        printf("\nFirst failing point: compare capture still invalid\n");
        printf("Both snapshots correct, but compare still sees wrong values.\n");
        printf("The atomic compare capture mechanism has a bug.\n");
    }
    
    munmap(test_mem, 4096);
    return (entry_ok && post_str_ok) ? 0 : 1;
}
