/*
 * aarch64 CPU execution main loop
 *
 * Bridges the TCTI gadgets with iSH's process model.
 */

#include "emu/aarch64/cpu.h"
#include "emu/aarch64/block-cache.h"
#include "asbestos/aarch64/gadgets_tcti.h"
#include "asbestos/aarch64/gen.h"
#include "asbestos/frame.h"
#include "emu/interrupt.h"
#include "emu/tlb.h"
#include "emu/mmu.h"
#include "kernel/task.h"
#include "kernel/calls.h"
#include <string.h>
#include <setjmp.h>

// External TCTI entry point
extern void tcti_entry_block(void);
extern void tcti_exit_block(int reason);

// Block cache
static struct a64_block_cache block_cache;
static int block_cache_initialized = 0;

// Current execution state - non-static so asbestos.c can access it
struct cpu_state *current_cpu = NULL;
static struct tlb *current_tlb = NULL;
static jmp_buf exit_jmpbuf __attribute__((unused));
static int exit_reason = 0;

// Forward declarations
struct a64_block *a64_compile_block(uint64_t pc, struct tlb *tlb);
// Forward declaration - made non-static for use by asbestos.c
int a64_execute_block(struct a64_block *block);

/*
 * Initialize aarch64 CPU for a task
 */
void a64_cpu_init(struct cpu_state *cpu) {
    memset(cpu, 0, sizeof(*cpu));

    // Initialize vector registers (optional - clear to known state)
    for (int i = 0; i < 32; i++) {
        cpu->vregs[i].q = 0;
    }

    // PSTATE initial state: no flags set, EL0
    cpu->pstate = 0;

    // TLS starts at 0 (set by libc)
    cpu->tpidr_el0 = 0;
}

/*
 * Fetch an instruction from guest memory using TLB
 * Returns 0 on success, -EFAULT on fault
 */
int a64_fetch_insn(struct cpu_state *cpu, struct tlb *tlb, uint64_t pc, uint32_t *insn) {
    // Use iSH's TLB for fast lookup
    void *ptr = __tlb_read_ptr(tlb, pc);
    if (ptr == NULL) {
        // TLB miss - use slow path
        ptr = tlb_handle_miss(tlb, pc, MEM_READ);
        if (ptr == NULL) {
            cpu->fault_addr = tlb->segfault_addr;
            cpu->fault_was_write = 0;
            return -EFAULT;
        }
    }

    *insn = *(uint32_t *)ptr;
    return 0;
}

/*
 * Compile a basic block starting at pc
 */
struct a64_block *a64_compile_block(uint64_t pc, struct tlb *tlb) {
    a64_gen_state_t gen_state;
    tcti_gadget_t buffer[A64_MAX_GADGETS_PER_BLOCK];
    struct a64_block *block;

    // Initialize generator
    a64_gen_init(&gen_state, buffer, A64_MAX_GADGETS_PER_BLOCK);
    a64_gen_reset(&gen_state, pc);

    // Translate instructions until block end
    int max_insns = 50;  // Reasonable limit
    int insns_decoded = 0;
    for (int i = 0; i < max_insns; i++) {
        uint32_t insn;
        int ret = a64_fetch_insn(current_cpu, tlb, gen_state.guest_pc, &insn);
        if (ret < 0) {
            // Page fault during fetch
            printk("[TCTI] Page fault fetching instruction at %llx\n", gen_state.guest_pc);
            break;
        }

        ret = a64_gen_instruction(&gen_state, insn, gen_state.guest_pc);

        if (ret < 0) {
            // Decode error - block ends here
            printk("[TCTI] Failed to generate gadget for instruction %08x at %llx (cat=%d)\n", 
                   insn, gen_state.guest_pc, (insn >> 25) & 0xF);
            break;
        }

        insns_decoded++;

        if (ret == 1) {
            // Block should end (branch/syscall)
            break;
        }
    }

    // Check if we decoded any instructions
    if (insns_decoded == 0) {
        // No instructions could be decoded - page fault or unsupported
        // Fall back to single-instruction interpretation
        printk("[TCTI] Falling back to interpretation for %llx\n", pc);
        
        // Create a minimal block that will trigger interpretation
        block = malloc(sizeof(*block));
        if (!block) return NULL;
        
        block->start_pc = pc;
        block->end_pc = pc;  // Will be updated after interpretation
        block->num_gadgets = 0;
        block->gadgets = NULL;
        block->is_jetsam = false;
        list_init(&block->chain);
        list_init(&block->jetsam);
        
        return block;
    }

    // Finalize the block
    if (a64_gen_finalize(&gen_state) != A64_GEN_OK) {
        return NULL;
    }

    // Allocate block
    block = malloc(sizeof(*block));
    if (!block) return NULL;

    // Allocate gadget array
    block->gadgets = malloc(gen_state.num_gadgets * sizeof(void *));
    if (!block->gadgets) {
        free(block);
        return NULL;
    }

    // Copy gadgets
    memcpy(block->gadgets, buffer, gen_state.num_gadgets * sizeof(void *));
    block->num_gadgets = gen_state.num_gadgets;
    block->start_pc = gen_state.start_pc;
    block->end_pc = gen_state.end_pc;
    block->is_jetsam = false;

    // Initialize list links
    list_init(&block->chain);
    list_init(&block->jetsam);

    return block;
}

/*
 * Execute a compiled block using TCTI
 *
 * Uses tcti_entry_block to set up register mapping and execute
 * the entire gadget chain. Gadgets use epilogue to chain together.
 */
int a64_execute_block(struct a64_block *block) {
    // Set up globals for TCTI entry
    // x28 needs to point to gadget array
    // x29 needs to point to cpu_state
    // Then call tcti_entry_block which loads regs and starts execution
    
    // Use inline asm to set up TCTI environment
    asm volatile(
        "mov x28, %0\n\t"      // x28 = gadget array
        "mov x29, %1\n\t"      // x29 = cpu_state
        "b tcti_entry_block\n\t"  // Branch to TCTI entry (doesn't return)
        :
        : "r"(block->gadgets), "r"(current_cpu)
        : "x28", "x29", "memory"
    );
    
    // Should never reach here - tcti_exit_block jumps back via longjmp
    return exit_reason;
}

/*
 * Run the CPU until interrupted
 * This is the main entry point from the kernel
 */
void a64_cpu_run(struct cpu_state *cpu, struct tlb *tlb) {
    current_cpu = cpu;
    current_tlb = tlb;

    // Initialize block cache if needed
    if (!block_cache_initialized) {
        a64_cache_init(&block_cache);
        block_cache_initialized = 1;
    }

    // Set up TLB for this CPU
    tlb_refresh(tlb, cpu->mmu);

    while (1) {
        uint64_t pc = cpu->pc;

        // Look up block in cache
        struct a64_block *block = a64_cache_lookup(&block_cache, pc);

        if (!block) {
            // Compile new block
            block = a64_compile_block(pc, tlb);
            if (!block) {
                // Compilation failed (page fault, undefined insn)
                handle_interrupt(INT_GPF);
                continue;
            }
            // Insert into cache
            a64_cache_insert(&block_cache, block);
        }

        // Execute the block
        a64_execute_block(block);

        // Check exit reason and handle
        if (exit_reason == TCTI_EXIT_SYSCALL) {
            handle_interrupt(INT_SYSCALL);
        } else if (exit_reason == TCTI_EXIT_FAULT) {
            handle_interrupt(INT_GPF);
        } else if (exit_reason == TCTI_EXIT_SIGNAL) {
            // Check for pending signals
            // deliver_signal(...)
        }

        // Continue to next block
    }
}

/*
 * Single-step one instruction (for debugging)
 */
int a64_cpu_step(struct cpu_state *cpu, struct tlb *tlb) {
    uint32_t insn;
    int ret = a64_fetch_insn(cpu, tlb, cpu->pc, &insn);
    if (ret < 0) return ret;

    // Decode and trace
    a64_instr_t decoded;
    ret = a64_decode(insn, &decoded);
    if (ret < 0) return ret;

    // TODO: Execute single instruction
    // For now, just advance PC
    cpu->pc += 4;

    return 0;
}

/*
 * Dump CPU state for debugging
 */
void a64_cpu_dump(struct cpu_state *cpu) {
    printk("aarch64 CPU state:\n");
    printk("  PC: 0x%016llx  SP: 0x%016llx\n", cpu->pc, cpu->sp);

    for (int i = 0; i < 31; i += 4) {
        printk("  x%-2d: 0x%016llx  x%-2d: 0x%016llx  x%-2d: 0x%016llx  x%-2d: 0x%016llx\n",
               i, cpu->x[i],
               i+1, cpu->x[i+1],
               i+2, cpu->x[i+2],
               i+3, cpu->x[i+3]);
    }

    printk("  PSTATE: 0x%016llx (N=%d Z=%d C=%d V=%d)\n",
           cpu->pstate, cpu->n, cpu->z, cpu->c, cpu->v);
}
