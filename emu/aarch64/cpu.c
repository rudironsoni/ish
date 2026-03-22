/*
 * aarch64 CPU execution main loop
 *
 * Bridges the TCTI gadgets with iSH's process model.
 */

#include "emu/aarch64/cpu.h"
#include "emu/aarch64/block-cache.h"
#include "tcti/aarch64/gadgets_tcti.h"
#include "tcti/aarch64/gen.h"
#include "tcti/frame.h"
#include "emu/interrupt.h"
#include "emu/tlb.h"
#include "emu/mmu.h"
#include "kernel/task.h"
#include "kernel/calls.h"
#include <string.h>
#include <setjmp.h>

// External TCTI entry point (declared in gadgets_tcti.h)
extern void tcti_entry_block(void *gadgets, struct cpu_state *cpu);
extern void tcti_exit_block(int reason);

// Block cache - non-static for access from kernel/memory.c
struct a64_block_cache block_cache;
static int block_cache_initialized = 0;

// Early initialization for block cache - call this before any memory operations
void a64_cache_early_init(void) {
    if (!block_cache_initialized) {
        a64_cache_init(&block_cache);
        block_cache_initialized = 1;
    }
}

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
    printk("[a64_compile_block] ENTRY: pc=0x%llx\n", pc);
    a64_gen_state_t gen_state;
    tcti_gadget_t buffer[A64_MAX_GADGETS_PER_BLOCK];
    struct a64_block *block;

    // Initialize generator
    printk("[a64_compile_block] Initializing generator\n");
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

        // Decode to get instruction info for logging
        a64_instr_t decoded_info;
        a64_decode(insn, &decoded_info);
        
        ret = a64_gen_instruction(&gen_state, insn, gen_state.guest_pc);

        if (ret < 0) {
            // Decode error - block ends here
            printk("[TCTI] Failed: insn=%08x pc=%llx cat=%d subtype=%d rd=%d rn=%d imm=%lld\n", 
                   insn, gen_state.guest_pc, decoded_info.cat, decoded_info.subtype,
                   decoded_info.Rd, decoded_info.Rn, decoded_info.imm);
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
    // Debug: print first few gadgets
    printk("[TCTI] Executing block with %zu gadgets at PC 0x%llx\n", 
           block->num_gadgets, block->start_pc);
    if (block->num_gadgets > 0) {
        printk("[TCTI] First gadget: %p\n", block->gadgets[0]);
        if (block->num_gadgets > 1) {
            printk("[TCTI] Second gadget: %p\n", block->gadgets[1]);
        }
    }
    
    // Debug: print cpu state before entry
    printk("[TCTI] CPU state: x[0]=0x%llx, x[1]=0x%llx, x[2]=0x%llx, x[3]=0x%llx\n",
           current_cpu->x[0], current_cpu->x[1], current_cpu->x[2], current_cpu->x[3]);
    printk("[TCTI] CPU state: pc=0x%llx, sp=0x%llx\n",
           current_cpu->pc, current_cpu->sp);
    
    // Call tcti_entry_block with proper register setup
    // tcti_entry_block is defined in assembly and expects:
    //   x0 = pointer to gadget array
    //   x1 = pointer to cpu_state
    
    // Use inline asm with specific register constraints
    // "r" constraints allow compiler to choose, but we specify x0/x1 explicitly
    void *gadgets = block->gadgets;
    struct cpu_state *cpu = current_cpu;
    
    asm volatile(
        "mov x0, %0\n\t"       // x0 = gadgets
        "mov x1, %1\n\t"       // x1 = cpu_state
        "bl _tcti_entry_block\n\t"  // Call TCTI entry
        :
        : "r"(gadgets), "r"(cpu)
        : "x0", "x1", "x30", "memory"
    );
    
    return exit_reason;
}

/*
 * Run the CPU until interrupted
 * This is the main entry point from the kernel
 */
void a64_cpu_run(struct cpu_state *cpu, struct tlb *tlb) {
    printk("[a64_cpu_run] ENTRY: cpu=%p, tlb=%p, pc=0x%llx\n", cpu, tlb, cpu->pc);
    current_cpu = cpu;
    current_tlb = tlb;

    // Initialize block cache if needed
    if (!block_cache_initialized) {
        printk("[a64_cpu_run] Initializing block cache\n");
        a64_cache_init(&block_cache);
        block_cache_initialized = 1;
    }
    printk("[a64_cpu_run] Block cache ready, entering main loop\n");

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

        // Check if block has 0 gadgets (fallback to interpretation case)
        if (block->num_gadgets == 0) {
            // Execute single instruction directly
            int ret = a64_cpu_step(cpu, tlb);
            if (ret < 0) {
                handle_interrupt(INT_GPF);
            }
            continue;
        }

        // Execute the block via TCTI
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
 * Single-step one instruction (interpretation fallback)
 *
 * Executes instructions that TCTI doesn't support yet.
 * This is the "interpreter fallback" for 100% TCTI mode - when
 * TCTI can't compile a block, we execute single instructions here.
 */
int a64_cpu_step(struct cpu_state *cpu, struct tlb *tlb) {
    uint32_t insn;
    int ret = a64_fetch_insn(cpu, tlb, cpu->pc, &insn);
    if (ret < 0) return ret;

    // Decode and trace
    a64_instr_t decoded;
    ret = a64_decode(insn, &decoded);
    if (ret < 0) return ret;

    // Execute based on category
    switch (decoded.cat) {
        case A64_DP_IMM: {
            // Data Processing - Immediate
            int rd = decoded.Rd;
            int rn = decoded.Rn;
            int64_t imm = decoded.imm;
            
            if (rd == 31) {
                // Destination is SP - skip or handle specially
                cpu->pc += 4;
                return 0;
            }
            
            switch (decoded.subtype) {
                case 0: // ADR/ADRP
                    // PC-relative address calculation
                    // For now, just set rd to PC + imm (simplified)
                    cpu->x[rd] = cpu->pc + imm;
                    break;
                    
                case 1: // MOVZ
                    // Move immediate with zero extension
                    cpu->x[rd] = (uint64_t)imm;
                    break;
                    
                case 2: // MOVK
                    // Move and keep - not implemented
                    printk("[cpu] MOVK not implemented, skipping\n");
                    break;
                    
                case 3: // ADD immediate
                case 4: // ADD immediate
                case 5: // SUB immediate
                    // Add/subtract immediate
                    if (rn == 31) {
                        // Source is SP
                        cpu->x[rd] = cpu->sp + imm;
                    } else {
                        cpu->x[rd] = cpu->x[rn] + imm;
                    }
                    break;
                    
                default:
                    printk("[cpu] Unknown DP_IMM subtype %d, skipping\n", decoded.subtype);
                    break;
            }
            break;
        }
        
        case A64_DP_REG: {
            // Data Processing - Register
            int rd = decoded.Rd;
            int rn = decoded.Rn;
            int rm = decoded.Rm;
            
            if (rd == 31) {
                cpu->pc += 4;
                return 0;
            }
            
            switch (decoded.subtype) {
                case 0: // Logical
                    // AND, ORR, EOR, etc.
                    printk("[cpu] Logical operations not implemented\n");
                    break;
                    
                case 1: // Add/Subtract register
                    if (rn == 31) {
                        cpu->x[rd] = cpu->sp + (rm == 31 ? 0 : cpu->x[rm]);
                    } else {
                        cpu->x[rd] = cpu->x[rn] + (rm == 31 ? 0 : cpu->x[rm]);
                    }
                    break;
                    
                default:
                    printk("[cpu] Unknown DP_REG subtype %d\n", decoded.subtype);
                    break;
            }
            break;
        }
        
        case A64_BRANCH: {
            // Branch instructions (B, BL, SVC, etc.)
            if (decoded.subtype == A64_BRANCH_UNCOND) {
                // Unconditional branch
                // B (branch) or BL (branch with link)
                // Check if it's BL by looking at the raw instruction
                if (insn & 0x80000000) {
                    // BL - branch with link
                    cpu->x[30] = cpu->pc + 4; // LR = next instruction
                }
                cpu->pc += decoded.imm;
                return 0; // Don't add 4, PC already updated
            } else if (decoded.subtype == A64_EXCEPTION) {
                // Exception generation (SVC, HVC, SMC, BRK, etc.)
                // Check if it's SVC
                if ((insn >> 24) == 0xD4) {
                    uint8_t imm16 = (insn >> 5) & 0xFFFF;
                    if (imm16 == 0) {
                        // SVC #0 - system call
                        return 1; // Signal that syscall needs handling
                    }
                }
            }
            break;
        }
        
        default:
            printk("[cpu] Unimplemented category %d at PC 0x%llx\n", decoded.cat, cpu->pc);
            break;
    }
    
    // Advance PC
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
