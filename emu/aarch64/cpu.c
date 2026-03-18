/*
 * aarch64 CPU execution main loop
 *
 * Bridges the TCTI gadgets with iSH's process model.
 */

#include "emu/aarch64/cpu.h"
#include "emu/mmu.h"
#include "kernel/task.h"
#include "kernel/calls.h"
#include <string.h>
#include <setjmp.h>

// External TCTI entry point
extern void tcti_entry_block(void);
extern void tcti_exit_block(int reason);

// Block compilation
struct a64_block {
    tcti_gadget_t *gadgets;     // Array of gadget pointers
    size_t num_gadgets;         // Number of gadgets
    uint64_t start_pc;          // Starting PC
    uint64_t end_pc;            // Ending PC
    int is_branch_target;       // Can be branched to
};

#define A64_MAX_BLOCKS 4096
static struct hashmap block_cache;
static int block_cache_initialized = 0;

// Current execution state
static struct cpu_state *current_cpu = NULL;
static jmp_buf exit_jmpbuf;
static int exit_reason = 0;

// Forward declarations
static struct a64_block *a64_compile_block(uint64_t pc);
static int a64_execute_block(struct a64_block *block);

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
 * Fetch an instruction from guest memory
 * Returns 0 on success, -EFAULT on fault
 */
int a64_fetch_insn(uint64_t pc, uint32_t *insn) {
    struct mmu *mmu = current_cpu->mmu;

    // TLB lookup
    struct tlb_entry *entry = &mmu->tlb[TLB_INDEX(pc)];
    if (entry->page_addr == (pc & PAGE_MASK) &&
        (entry->flags & TLB_CODE)) {
        uint32_t *ptr = (uint32_t *)(entry->host_addr + (pc & PAGE_MASK));
        *insn = *ptr;
        return 0;
    }

    // TLB miss - use slow path
    void *ptr = mem_ptr(mmu, pc, MEM_READ);
    if (!ptr) {
        current_cpu->fault_addr = pc;
        current_cpu->fault_was_write = 0;
        return -EFAULT;
    }

    *insn = *(uint32_t *)ptr;
    return 0;
}

/*
 * Compile a basic block starting at pc
 */
static struct a64_block *a64_compile_block(uint64_t pc) {
    a64_gen_state_t gen_state;
    tcti_gadget_t buffer[A64_MAX_GADGETS_PER_BLOCK];
    struct a64_block *block;

    // Initialize generator
    a64_gen_init(&gen_state, buffer, A64_MAX_GADGETS_PER_BLOCK);
    a64_gen_reset(&gen_state, pc);

    // Translate instructions until block end
    int max_insns = 50;  // Reasonable limit
    for (int i = 0; i < max_insns; i++) {
        uint32_t insn;
        int ret = a64_fetch_insn(gen_state.guest_pc, &insn);
        if (ret < 0) {
            // Page fault during fetch
            break;
        }

        ret = a64_gen_instruction(&gen_state, insn, gen_state.guest_pc);

        if (ret < 0) {
            // Decode error - block ends here
            break;
        }

        if (ret == 1) {
            // Block should end (branch/syscall)
            break;
        }
    }

    // Finalize the block
    if (a64_gen_finalize(&gen_state) != A64_GEN_OK) {
        return NULL;
    }

    // Allocate and copy block
    block = malloc(sizeof(*block));
    if (!block) return NULL;

    block->gadgets = malloc(gen_state.num_gadgets * sizeof(tcti_gadget_t));
    if (!block->gadgets) {
        free(block);
        return NULL;
    }

    memcpy(block->gadgets, buffer, gen_state.num_gadgets * sizeof(tcti_gadget_t));
    block->num_gadgets = gen_state.num_gadgets;
    block->start_pc = gen_state.start_pc;
    block->end_pc = gen_state.end_pc;
    block->is_branch_target = 1;

    return block;
}

/*
 * Execute a compiled block using TCTI
 *
 * This is where the magic happens - we set up registers and
 * jump into the gadget chain.
 */
static int a64_execute_block(struct a64_block *block) {
    // Register setup for TCTI:
    // x1-x16  = guest x0-x15
    // x27     = link/temp
    // x28     = bytecode pointer (gadgets array)
    // x29     = CPU state pointer
    // x30     = reserved

    // Save current CPU state to locals (for after execution)
    uint64_t host_x1 = current_cpu->x[0];   // Guest x0 -> host x1
    uint64_t host_x2 = current_cpu->x[1];
    uint64_t host_x3 = current_cpu->x[2];
    uint64_t host_x4 = current_cpu->x[3];
    uint64_t host_x5 = current_cpu->x[4];
    uint64_t host_x6 = current_cpu->x[5];
    uint64_t host_x7 = current_cpu->x[6];
    uint64_t host_x8 = current_cpu->x[7];
    uint64_t host_x9 = current_cpu->x[8];
    uint64_t host_x10 = current_cpu->x[9];
    uint64_t host_x11 = current_cpu->x[10];
    uint64_t host_x12 = current_cpu->x[11];
    uint64_t host_x13 = current_cpu->x[12];
    uint64_t host_x14 = current_cpu->x[13];
    uint64_t host_x15 = current_cpu->x[14];
    uint64_t host_x16 = current_cpu->x[15];

    // Pointers for TCTI
    tcti_gadget_t *bytecode = block->gadgets;
    struct cpu_state *cpu_ptr = current_cpu;

    // Execute using inline asm
    // This is simplified - full version needs save/restore of host regs
    asm volatile(
        // Save host callee-saved registers
        "stp x19, x20, [sp, #-16]!\n\t"
        "stp x21, x22, [sp, #-16]!\n\t"
        "stp x23, x24, [sp, #-16]!\n\t"
        "stp x25, x26, [sp, #-16]!\n\t"
        "stp x27, x28, [sp, #-16]!\n\t"
        "stp x29, x30, [sp, #-16]!\n\t"

        // Load guest registers to TCTI mapping
        "ldr x1, [%[cpu], #0]\n\t"      // x[0] -> host x1
        "ldr x2, [%[cpu], #8]\n\t"      // x[1] -> host x2
        "ldr x3, [%[cpu], #16]\n\t"     // x[2] -> host x3
        "ldr x4, [%[cpu], #24]\n\t"     // x[3] -> host x4
        "ldr x5, [%[cpu], #32]\n\t"     // x[4] -> host x5
        "ldr x6, [%[cpu], #40]\n\t"     // x[5] -> host x6
        "ldr x7, [%[cpu], #48]\n\t"     // x[6] -> host x7
        "ldr x8, [%[cpu], #56]\n\t"     // x[7] -> host x8
        "ldr x9, [%[cpu], #64]\n\t"     // x[8] -> host x9
        "ldr x10, [%[cpu], #72]\n\t"    // x[9] -> host x10
        "ldr x11, [%[cpu], #80]\n\t"    // x[10] -> host x11
        "ldr x12, [%[cpu], #88]\n\t"    // x[11] -> host x12
        "ldr x13, [%[cpu], #96]\n\t"    // x[12] -> host x13
        "ldr x14, [%[cpu], #104]\n\t"   // x[13] -> host x14
        "ldr x15, [%[cpu], #112]\n\t"   // x[14] -> host x15
        "ldr x16, [%[cpu], #120]\n\t"   // x[15] -> host x16

        // Set up TCTI pointers
        "mov x28, %[bytecode]\n\t"      // Bytecode pointer
        "mov x29, %[cpu]\n\t"           // CPU state pointer

        // Load first gadget and jump
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t"

        // Exit point - restore host registers
        // (This label is referenced but we need a way to get here)
        "1:\n\t"

        // Save guest registers back
        "str x1, [%[cpu], #0]\n\t"
        "str x2, [%[cpu], #8]\n\t"
        "str x3, [%[cpu], #16]\n\t"
        "str x4, [%[cpu], #24]\n\t"
        "str x5, [%[cpu], #32]\n\t"
        "str x6, [%[cpu], #40]\n\t"
        "str x7, [%[cpu], #48]\n\t"
        "str x8, [%[cpu], #56]\n\t"
        "str x9, [%[cpu], #64]\n\t"
        "str x10, [%[cpu], #72]\n\t"
        "str x11, [%[cpu], #80]\n\t"
        "str x12, [%[cpu], #88]\n\t"
        "str x13, [%[cpu], #96]\n\t"
        "str x14, [%[cpu], #104]\n\t"
        "str x15, [%[cpu], #112]\n\t"
        "str x16, [%[cpu], #120]\n\t"

        // Restore host registers
        "ldp x29, x30, [sp], #16\n\t"
        "ldp x27, x28, [sp], #16\n\t"
        "ldp x25, x26, [sp], #16\n\t"
        "ldp x23, x24, [sp], #16\n\t"
        "ldp x21, x22, [sp], #16\n\t"
        "ldp x19, x20, [sp], #16\n\t"
        :
        : [cpu] "r" (cpu_ptr), [bytecode] "r" (bytecode)
        : "memory"
    );

    return 0;
}

/*
 * Run the CPU until interrupted
 * This is the main entry point from the kernel
 */
void a64_cpu_run(struct cpu_state *cpu) {
    current_cpu = cpu;

    // Initialize block cache if needed
    if (!block_cache_initialized) {
        // hashmap_init(&block_cache, ...);
        block_cache_initialized = 1;
    }

    while (1) {
        uint64_t pc = cpu->pc;

        // Look up block in cache
        struct a64_block *block = NULL; // hashmap_get(&block_cache, pc);

        if (!block) {
            // Compile new block
            block = a64_compile_block(pc);
            if (!block) {
                // Compilation failed (page fault, undefined insn)
                handle_interrupt(INT_GPF);
                continue;
            }
            // hashmap_put(&block_cache, pc, block);
        }

        // Execute the block
        int ret = a64_execute_block(block);

        // Check exit reason
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
int a64_cpu_step(struct cpu_state *cpu) {
    uint32_t insn;
    int ret = a64_fetch_insn(cpu->pc, &insn);
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
