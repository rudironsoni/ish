/*
 * aarch64 CPU execution main loop
 *
 * Bridges the TCTI gadgets with iSH's process model.
 */

#include "emu/aarch64/cpu.h"
#include "emu/aarch64/block-cache.h"
#include "gadgets_tcti.h"
#include "tcti/aarch64/gen.h"
#include "tcti/frame.h"
#include "emu/interrupt.h"
#include "emu/tlb.h"
#include "emu/mmu.h"
#include "kernel/task.h"
#include "kernel/calls.h"
#include <string.h>
#include <setjmp.h>
#include <unistd.h>

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
    bool explicit_pc_on_exit = false;

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
        int decode_ret = a64_decode(insn, &decoded_info);
        
        // Debug: print decoded info
        if (decode_ret == 0) {
            printk("[TCTI] Decoded insn=%08x cat=%d subtype=%d Rd=%d Rn=%d imm=%lld\n",
                   insn, decoded_info.cat, decoded_info.subtype, decoded_info.Rd, 
                   decoded_info.Rn, decoded_info.imm);
        } else {
            printk("[TCTI] Failed to decode insn=%08x\n", insn);
        }
        
        // Generate TCTI instruction - load/store and bitfield now have inline TCTI support
        ret = a64_gen_instruction(&gen_state, insn, gen_state.guest_pc);

        if (ret < 0) {
            // Decode error - block ends here
            printk("[TCTI] Failed: insn=%08x pc=%llx cat=%d subtype=%d rd=%d rn=%d imm=%lld\n", 
                   insn, gen_state.guest_pc, decoded_info.cat, decoded_info.subtype,
                   decoded_info.Rd, decoded_info.Rn, decoded_info.imm);
            break;
        }

        if (gen_state.is_complete &&
                (decoded_info.cat == A64_BRANCH || decoded_info.cat == A64_BRANCH2) &&
                decoded_info.subtype != A64_EXCEPTION && decoded_info.subtype != 6) {
            explicit_pc_on_exit = true;
        }

        insns_decoded++;
        gen_state.guest_pc += 4;  // Advance to next instruction

        if (ret == 1) {
            // Block should end (branch/syscall)
            break;
        }
    }

    // Check if we decoded any instructions
    if (insns_decoded == 0) {
        // No instructions could be decoded - this is a fatal error in 100% TCTI mode
        // Log the failing instruction
        uint32_t failing_insn = 0;
        a64_fetch_insn(current_cpu, tlb, pc, &failing_insn);
        printk("[TCTI] FATAL: Cannot compile block at 0x%llx - unsupported instruction\n", pc);
        printk("[TCTI] FATAL: Instruction bytes: 0x%08x\n", failing_insn);
        return NULL;
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
    block->explicit_pc_on_exit = explicit_pc_on_exit;
    block->is_jetsam = false;
    
    // Debug: print first 20 bytecode entries after compilation
    printk("[TCTI] Block compiled: start_pc=0x%llx, num_gadgets=%zu\n", 
           block->start_pc, block->num_gadgets);
    printk("[TCTI] Bytecode after compilation (first 20):\n");
    for (size_t i = 0; i < block->num_gadgets && i < 20; i++) {
        printk("[TCTI]   [%zu] = %p\n", i, block->gadgets[i]);
    }

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
    
    // Debug: dump first 20 bytecode entries to see pattern
    printk("[TCTI] Bytecode dump (first 20 entries):\n");
    for (size_t i = 0; i < block->num_gadgets && i < 20; i++) {
        printk("[TCTI]   [%zu] = %p\n", i, block->gadgets[i]);
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
    
    void *gadgets = block->gadgets;
    struct cpu_state *cpu = current_cpu;
    
    printk("[TCTI] About to enter TCTI...\n");
    
    tcti_entry_block(gadgets, cpu);
    
    printk("[TCTI] Returned from TCTI! exit_reason=%d\n", cpu->tcti_exit_reason);
    
    // Debug: Check if we can safely read from cpu and block
    printk("[TCTI] Debug check 1: cpu=%p, block=%p\n", (void*)cpu, (void*)block);
    printk("[TCTI] Debug check 2: cpu->tcti_exit_reason=%d\n", cpu->tcti_exit_reason);
    printk("[TCTI] Debug check 3: block->start_pc=0x%llx\n", block->start_pc);
    
    // Exit reason is stored in cpu->tcti_exit_reason by assembly code
    int exit_reason = cpu->tcti_exit_reason;
    printk("[TCTI] Debug check 4: exit_reason=%d\n", exit_reason);
    return exit_reason;
}

static uint64_t a64_read_reg_or_sp(struct cpu_state *cpu, int reg, bool is_64bit) {
    if (reg == 31)
        return cpu->sp;
    if (reg < 0 || reg > 30)
        return 0;
    return is_64bit ? cpu->x[reg] : (uint32_t) cpu->x[reg];
}

static void a64_write_reg_or_sp(struct cpu_state *cpu, int reg, uint64_t value,
        bool is_64bit) {
    if (reg == 31) {
        cpu->sp = is_64bit ? value : (uint32_t) value;
        return;
    }
    if (reg < 0 || reg > 30)
        return;
    cpu->x[reg] = is_64bit ? value : (uint32_t) value;
}

static uint64_t a64_extend_index(uint64_t value, int extend_type) {
    switch (extend_type) {
        case A64_EXT_UXTW:
            return (uint32_t) value;
        case A64_EXT_SXTW:
            return (uint64_t) (int64_t) (int32_t) value;
        case A64_EXT_SXTX:
            return (uint64_t) (int64_t) value;
        case A64_EXT_UXTX:
        case A64_EXT_LSL:
        default:
            return value;
    }
}

static uint64_t a64_apply_shift(uint64_t value, int shift_type, int amount, bool is_64bit) {
    amount &= is_64bit ? 63 : 31;
    if (!is_64bit)
        value = (uint32_t) value;
    switch (shift_type) {
        case A64_SHIFT_LSR:
            return is_64bit ? (value >> amount) : (uint32_t) value >> amount;
        case A64_SHIFT_ASR:
            return is_64bit ? (uint64_t) ((int64_t) value >> amount)
                : (uint32_t) ((int32_t) value >> amount);
        case A64_SHIFT_ROR:
            if (amount == 0)
                return is_64bit ? value : (uint32_t) value;
            return is_64bit
                ? ((value >> amount) | (value << (64 - amount)))
                : (uint32_t) (((uint32_t) value >> amount) | ((uint32_t) value << (32 - amount)));
        case A64_SHIFT_LSL:
        default:
            return is_64bit ? (value << amount) : (uint32_t) value << amount;
    }
}

/*
 * Run the CPU until interrupted
 * This is the main entry point from the kernel
 */
void a64_cpu_run(struct cpu_state *cpu, struct tlb *tlb) {
    printk("[a64_cpu_run] ENTRY: cpu=%p, tlb=%p, pc=0x%llx\n", cpu, tlb, cpu->pc);
    current_cpu = cpu;
    current_tlb = tlb;
    cpu->tlb = tlb;  // Store TLB pointer in cpu_state for inline TLB access

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
                printk("[TCTI] FATAL: Cannot compile block at PC=0x%llx\n", pc);
                handle_interrupt(INT_GPF);
                continue;
            }
            // Insert into cache
            a64_cache_insert(&block_cache, block);
        }

        // Execute the block via TCTI
        printk("[TCTI] About to execute block at %p, end_pc=0x%llx, current pc=0x%llx\n", block, block->end_pc, cpu->pc);
        printk("[TCTI] Block contents: start_pc=0x%llx, end_pc=0x%llx, num_gadgets=%zu, gadgets=%p\n",
               block->start_pc, block->end_pc, block->num_gadgets, block->gadgets);
        
        // Compute checksum of first 10 gadgets before execution
        uint64_t checksum_before = 0;
        for (size_t i = 0; i < 10 && i < block->num_gadgets; i++) {
            checksum_before ^= (uint64_t)block->gadgets[i];
        }
        printk("[TCTI] Checksum before: 0x%llx\n", checksum_before);
        
        int exit_reason = a64_execute_block(block);
        
        // Compute checksum after execution
        uint64_t checksum_after = 0;
        for (size_t i = 0; i < 10 && i < block->num_gadgets; i++) {
            checksum_after ^= (uint64_t)block->gadgets[i];
        }
        printk("[TCTI] Checksum after: 0x%llx\n", checksum_after);
        
        if (checksum_before != checksum_after) {
            printk("[TCTI] ERROR: Block bytecode corrupted during execution!\n");
            printk("[TCTI] First 10 gadgets after corruption:\n");
            for (size_t i = 0; i < 10 && i < block->num_gadgets; i++) {
                printk("[TCTI]   [%zu] = %p\n", i, block->gadgets[i]);
            }
        }
        
        printk("[TCTI] Block executed, exit_reason=%d\n", exit_reason);
        printk("[TCTI] Block contents after: start_pc=0x%llx, end_pc=0x%llx, num_gadgets=%zu, gadgets=%p\n",
               block->start_pc, block->end_pc, block->num_gadgets, block->gadgets);
        
        // Check exit reason and handle
        if (exit_reason == TCTI_EXIT_SYSCALL) {
            if (!block->explicit_pc_on_exit)
                cpu->pc = block->end_pc;
            handle_interrupt(INT_SYSCALL);
        } else if (exit_reason == TCTI_EXIT_FAULT) {
            handle_interrupt(INT_GPF);
        } else if (exit_reason == TCTI_EXIT_SIGNAL) {
            if (!block->explicit_pc_on_exit)
                cpu->pc = block->end_pc;
            // Check for pending signals
            // deliver_signal(...)
        } else if (exit_reason == TCTI_EXIT_COMPLEX) {
            printk("[TCTI] COMPLEX exit at PC=0x%llx, handling MRS/MSR\n", cpu->pc);
            // Decode instruction at current PC to determine if it's MRS or MSR
            uint32_t insn;
            if (a64_fetch_insn(cpu, cpu->tlb, cpu->pc, &insn) == 0) {
                a64_instr_t decoded;
                if (a64_decode(insn, &decoded) == 0) {
                    if (decoded.cat == A64_BRANCH && decoded.subtype == 2) {
                        // MRS - Move to register from system register
                        // TPIDR_EL0: op1=3, CRn=13, CRm=0, op2=2
                        int op1 = (decoded.sysreg >> 14) & 0x7;
                        int crn = (decoded.sysreg >> 10) & 0xF;
                        int crm = (decoded.sysreg >> 6) & 0xF;
                        int op2 = (decoded.sysreg >> 3) & 0x7;
                        
                        if (op1 == 3 && crn == 13 && crm == 0 && op2 == 2) {
                            // TPIDR_EL0 read
                            cpu->x[decoded.Rd] = cpu->tpidr_el0;
                            printk("[TCTI] MRS TPIDR_EL0 -> x[%d] = 0x%llx\n", 
                                   decoded.Rd, (unsigned long long)cpu->tpidr_el0);
                        } else if (op1 == 3 && crn == 13 && crm == 0 && op2 == 3) {
                            // TPIDRRO_EL0 read (same value on Linux)
                            cpu->x[decoded.Rd] = cpu->tpidr_el0;
                            printk("[TCTI] MRS TPIDRRO_EL0 -> x[%d] = 0x%llx\n", 
                                   decoded.Rd, (unsigned long long)cpu->tpidr_el0);
                        } else {
                            printk("[TCTI] Warning: Unhandled MRS sysreg 0x%x\n", decoded.sysreg);
                        }
                        cpu->pc += 4;  // Advance past MRS instruction
                    } else if (decoded.cat == A64_BRANCH && decoded.subtype == 4) {
                        // MSR (reg) - Move from register to system register
                        int op1 = (decoded.sysreg >> 14) & 0x7;
                        int crn = (decoded.sysreg >> 10) & 0xF;
                        int crm = (decoded.sysreg >> 6) & 0xF;
                        int op2 = (decoded.sysreg >> 3) & 0x7;
                        
                        if (op1 == 3 && crn == 13 && crm == 0 && op2 == 2) {
                            // TPIDR_EL0 write
                            if (decoded.Rd < 31) {
                                cpu->tpidr_el0 = cpu->x[decoded.Rd];
                            } else {
                                cpu->tpidr_el0 = 0;
                            }
                            printk("[TCTI] MSR x[%d] -> TPIDR_EL0 = 0x%llx\n", 
                                   decoded.Rd, (unsigned long long)cpu->tpidr_el0);
                        } else {
                            printk("[TCTI] Warning: Unhandled MSR sysreg 0x%x\n", decoded.sysreg);
                        }
                        cpu->pc += 4;  // Advance past MSR instruction
                    } else {
                        printk("[TCTI] FATAL: Unknown COMPLEX exit, decoded cat=%d subtype=%d\n",
                               decoded.cat, decoded.subtype);
                        handle_interrupt(INT_GPF);
                    }
                } else {
                    printk("[TCTI] FATAL: Failed to decode instruction at PC=0x%llx\n", cpu->pc);
                    handle_interrupt(INT_GPF);
                }
            } else {
                printk("[TCTI] FATAL: Failed to fetch instruction at PC=0x%llx\n", cpu->pc);
                handle_interrupt(INT_GPF);
            }
        } else {
            // Fallthrough blocks advance to end_pc. Control-transfer blocks preserve
            // the guest PC written by their terminal gadget.
            if (!block->explicit_pc_on_exit)
                cpu->pc = block->end_pc;
        }
        // Normal exit - PC already advanced, continue to next block
        
        // Continue to next block
    }
}

/*
 * Execute load/store instruction in C with full TLB translation
 * This is called when TCTI encounters a load/store and needs C assistance
 */
int a64_execute_ldst(struct cpu_state *cpu, struct tlb *tlb, const a64_instr_t *instr) {
    uint32_t raw = instr->raw;
    bool is_load = bit(raw, 22);
    bool is_pair = instr->subtype == A64_LDST_PAIR;
    bool is_literal = instr->subtype == A64_LDST_LITERAL;
    bool is_reg_offset = bits(raw, 11, 10) == 2;
    bool writeback = instr->idx_mode == A64_PRE_INDEX || instr->idx_mode == A64_POST_INDEX;
    int access = is_load ? MEM_READ : MEM_WRITE;
    uint64_t base;
    uint64_t addr;

    if (is_literal) {
        addr = cpu->pc + instr->imm;
    } else {
        base = a64_read_reg_or_sp(cpu, instr->Rn, true);
        if (is_reg_offset) {
            uint64_t index = a64_read_reg_or_sp(cpu, instr->Rm, true);
            index = a64_extend_index(index, instr->extend_type);
            addr = base + (index << instr->imm_shift);
        } else if (instr->idx_mode == A64_PRE_INDEX) {
            base += instr->imm;
            addr = base;
        } else {
            addr = base + instr->imm;
        }
    }

    if (is_pair) {
        size_t width = instr->is_64bit ? sizeof(uint64_t) : sizeof(uint32_t);
        char *ptr = is_load ? __tlb_read_ptr(tlb, addr) : __tlb_write_ptr(tlb, addr);
        if (ptr == NULL) {
            ptr = tlb_handle_miss(tlb, addr, access);
            if (ptr == NULL) {
                cpu->fault_addr = tlb->segfault_addr;
                cpu->fault_was_write = !is_load;
                return -EFAULT;
            }
        }

        if (is_load) {
            uint64_t first = instr->is_64bit ? *(uint64_t *) ptr : *(uint32_t *) ptr;
            uint64_t second = instr->is_64bit
                ? *(uint64_t *) (ptr + width)
                : *(uint32_t *) (ptr + width);
            a64_write_reg_or_sp(cpu, instr->Rd, first, instr->is_64bit);
            a64_write_reg_or_sp(cpu, instr->Rm, second, instr->is_64bit);
        } else {
            uint64_t first = a64_read_reg_or_sp(cpu, instr->Rd, instr->is_64bit);
            uint64_t second = a64_read_reg_or_sp(cpu, instr->Rm, instr->is_64bit);
            if (instr->is_64bit) {
                *(uint64_t *) ptr = first;
                *(uint64_t *) (ptr + width) = second;
            } else {
                *(uint32_t *) ptr = (uint32_t) first;
                *(uint32_t *) (ptr + width) = (uint32_t) second;
            }
        }

        if (writeback) {
            uint64_t updated = (instr->idx_mode == A64_POST_INDEX)
                ? a64_read_reg_or_sp(cpu, instr->Rn, true) + instr->pair_offset
                : base;
            a64_write_reg_or_sp(cpu, instr->Rn, updated, true);
        }
        return 0;
    }

    char *ptr = is_load ? __tlb_read_ptr(tlb, addr) : __tlb_write_ptr(tlb, addr);
    if (ptr == NULL) {
        ptr = tlb_handle_miss(tlb, addr, access);
        if (ptr == NULL) {
            cpu->fault_addr = tlb->segfault_addr;
            cpu->fault_was_write = !is_load;
            return -EFAULT;
        }
    }

    if (is_load) {
        uint64_t value;
        switch (instr->size) {
            case A64_SIZE_B: value = instr->is_signed ? (int8_t) *(uint8_t *) ptr : *(uint8_t *) ptr; break;
            case A64_SIZE_H: value = instr->is_signed ? (int16_t) *(uint16_t *) ptr : *(uint16_t *) ptr; break;
            case A64_SIZE_W: value = instr->is_signed ? (int32_t) *(uint32_t *) ptr : *(uint32_t *) ptr; break;
            case A64_SIZE_X: value = *(uint64_t *) ptr; break;
            default: return -1;
        }
        a64_write_reg_or_sp(cpu, instr->Rd, value, instr->size == A64_SIZE_X);
    } else {
        uint64_t value = a64_read_reg_or_sp(cpu, instr->Rd, instr->size == A64_SIZE_X);
        switch (instr->size) {
            case A64_SIZE_B: *(uint8_t *) ptr = (uint8_t) value; break;
            case A64_SIZE_H: *(uint16_t *) ptr = (uint16_t) value; break;
            case A64_SIZE_W: *(uint32_t *) ptr = (uint32_t) value; break;
            case A64_SIZE_X: *(uint64_t *) ptr = value; break;
            default: return -1;
        }
    }

    if (!is_literal && writeback) {
        uint64_t updated = (instr->idx_mode == A64_POST_INDEX)
            ? a64_read_reg_or_sp(cpu, instr->Rn, true) + instr->imm
            : base;
        a64_write_reg_or_sp(cpu, instr->Rn, updated, true);
    }

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
