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
#include "emu/aarch64/memory.h"
#include "emu/tlb.h"
#include "emu/mmu.h"
#include "kernel/task.h"
#include "kernel/calls.h"
#include <string.h>
#include <setjmp.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>

// External TCTI entry point (declared in gadgets_tcti.h)
extern void tcti_entry_block(void *gadgets, struct cpu_state *cpu);

// External diagnostic dump functions
extern void dump_cmp_bcond_diag(void);
extern void dump_cmp_capture(void);
extern void dump_str_wb_diag(void);
extern void dump_runtime_diag(void);
extern void dump_gen_emit_diag(void);
extern void tcti_exit_block(int reason);

// Execution state is now passed via parameters, not globals
static jmp_buf exit_jmpbuf __attribute__((unused));

// Forward declarations
struct a64_block *a64_compile_block(struct cpu_state *cpu, uint64_t pc, struct tlb *tlb);
// Forward declaration - made non-static for use by asbestos.c
int a64_execute_block(struct cpu_state *cpu, struct a64_block *block);

// Low-frequency progress sampler state
static time_t last_sample_time = 0;
static int sample_count = 0;
static uint64_t last_pc = 0;
static uint64_t last_sp = 0;
static uint64_t last_ldr_fast = 0;
static uint64_t last_str_fast = 0;
static uint64_t last_ldr_fallback = 0;
static uint64_t last_str_fallback = 0;

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
struct a64_block *a64_compile_block(struct cpu_state *cpu, uint64_t pc, struct tlb *tlb) {
    a64_gen_state_t gen_state;
    tcti_gadget_t buffer[A64_MAX_GADGETS_PER_BLOCK];
    struct a64_block *block;
    bool explicit_pc_on_exit = false;

    a64_gen_init(&gen_state, buffer, A64_MAX_GADGETS_PER_BLOCK);
    a64_gen_reset(&gen_state, pc);

    // Translate instructions until block end
    int max_insns = 50;  // Reasonable limit
    int insns_decoded = 0;
    for (int i = 0; i < max_insns; i++) {
        uint32_t insn;
        int ret = a64_fetch_insn(cpu, tlb, gen_state.guest_pc, &insn);
        if (ret < 0) {
            // Page fault during fetch
            break;
        }

        // Decode to get instruction info
        a64_instr_t decoded_info;
        int decode_ret = a64_decode(insn, &decoded_info);
        
        // Generate TCTI instruction - load/store and bitfield now have inline TCTI support
        ret = a64_gen_instruction(&gen_state, insn, gen_state.guest_pc);

        if (ret < 0) {
            // Decode error - block ends here
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
        uint32_t failing_insn = 0;
        a64_fetch_insn(cpu, tlb, pc, &failing_insn);
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
int a64_execute_block(struct cpu_state *cpu, struct a64_block *block) {
    tcti_entry_block(block->gadgets, cpu);
    int exit_reason = cpu->tcti_exit_reason;
    // DIAGNOSTIC: Log block execution result
    if (exit_reason == TCTI_EXIT_FAULT) {
        printk("[EXEC-DIAG] Block exit: reason=%d pc=0x%llx fault_addr=0x%llx is_write=%d tcti_reason=%d\n",
               exit_reason,
               (unsigned long long) cpu->pc,
               (unsigned long long) cpu->fault_addr,
               cpu->fault_was_write,
               cpu->tcti_exit_reason);
    }
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
    fprintf(stderr, "[RUN-TRACE] a64_cpu_run entry pc=0x%llx sp=0x%llx tlb=%p\n",
           (unsigned long long) cpu->pc,
           (unsigned long long) cpu->sp,
           tlb);
    fprintf(stderr, "[RUN-TRACE] INITIAL REGS: x0=0x%llx x1=0x%llx x2=0x%llx x3=0x%llx\n",
           (unsigned long long) cpu->x[0],
           (unsigned long long) cpu->x[1],
           (unsigned long long) cpu->x[2],
           (unsigned long long) cpu->x[3]);
    
    cpu->tlb = tlb;  // Store TLB pointer in cpu_state for inline TLB access

    // Get or create persistent execution context for this CPU
    struct fiber_exec_ctx *ctx = fiber_exec_ctx_get(cpu);
    if (!ctx) {
        printk("[TCTI] FATAL: Cannot get execution context\n");
        handle_interrupt(INT_GPF);
        return;
    }
    
    // Reset frame state for new execution run
    fiber_exec_ctx_reset(ctx, cpu);

    // Initialize per-MMU block cache if needed
    if (!cpu->mmu->block_cache) {
        cpu->mmu->block_cache = malloc(sizeof(struct a64_block_cache));
        if (cpu->mmu->block_cache) {
            a64_cache_init(cpu->mmu->block_cache);
        }
    }

    // Set up TLB for this CPU
    tlb_refresh(tlb, cpu->mmu);

    bool logged_loop_entry = false;
    bool logged_sampler_point = false;

    while (1) {
        if (!logged_loop_entry) {
            printk("[RUN-TRACE] a64_cpu_run loop entry pc=0x%llx sp=0x%llx\n",
                   (unsigned long long) cpu->pc,
                   (unsigned long long) cpu->sp);
            logged_loop_entry = true;
        }
        // Reacquire context if it was marked inactive (e.g., after interrupt return)
        if (!ctx->active) {
            ctx = fiber_exec_ctx_get(cpu);
            if (!ctx) {
                printk("[TCTI] FATAL: Cannot reacquire execution context\n");
                return;
            }
        }
        
        uint64_t pc = cpu->pc;
        
        // Simple trace: dump x0-x7 at start of each block execution
        static int trace_count = 0;
        if (trace_count++ < 200) {
            fprintf(stderr, "[TRACE %d] pc=0x%llx x0=%llx x1=%llx x2=%llx x3=%llx x4=%llx x5=%llx x6=%llx x7=%llx sp=%llx\n",
                   trace_count,
                   (unsigned long long)pc,
                   (unsigned long long)cpu->x[0],
                   (unsigned long long)cpu->x[1],
                   (unsigned long long)cpu->x[2],
                   (unsigned long long)cpu->x[3],
                   (unsigned long long)cpu->x[4],
                   (unsigned long long)cpu->x[5],
                   (unsigned long long)cpu->x[6],
                   (unsigned long long)cpu->x[7],
                   (unsigned long long)cpu->sp);
        }
        
        // TEMPORARY: Debug CBZ branch at PC 0xf7fb012c
        // The CBZ branches to 0xf7fb0154 if x2 != 0
        if (pc == 0xf7fb0128ULL) {
            fprintf(stderr, "\n[CBZ-DEBUG] At PC 0xf7fb0128 block entry\n");
            fprintf(stderr, "  x2=0x%016llx (CBZ condition)\n", (unsigned long long)cpu->x[2]);
            fprintf(stderr, "  x4=0x%016llx (guest x4, should be 0xfffffd10)\n", (unsigned long long)cpu->x[4]);
        }
        if (pc == 0xf7fb0154ULL) {
            fprintf(stderr, "\n[CBZ-DEBUG] Reached PC 0xf7fb0154 (branch target!)\n");
            fprintf(stderr, "  x4=0x%016llx (guest x4)\n", (unsigned long long)cpu->x[4]);
            fprintf(stderr, "  Expected: 0xfffffd10\n");
        }
        
        // DIAGNOSTIC: Track zeroing loop progression
        // First loop: x2 goes from x7 to x5 (sp+8 to sp+0x108)
        // Second loop: x2 goes from x5 to x3
        static uint64_t prev_x2 = 0, prev_x3 = 0;
        static int loop_iter = 0;
        
        // Detect when x2 == x5 (first loop completion point)
        if (cpu->x[2] == cpu->x[5] && trace_count < 250) {
            fprintf(stderr, "[LOOP-TRANSITION] x2 == x5 at PC=0x%llx, x2=x5=0x%llx, x3=0x%llx\n",
                   (unsigned long long)pc, (unsigned long long)cpu->x[2], (unsigned long long)cpu->x[3]);
        }
        
        // Detect when x2 == x3 (second loop completion point)
        if (cpu->x[2] == cpu->x[3] && cpu->x[2] != 0 && trace_count < 250) {
            fprintf(stderr, "[LOOP-TRANSITION] x2 == x3 at PC=0x%llx, x2=x3=0x%llx, x5=0x%llx\n",
                   (unsigned long long)pc, (unsigned long long)cpu->x[2], (unsigned long long)cpu->x[5]);
        }
        
        // Track x3 stability in second loop (when x2 > x5)
        if (cpu->x[2] > cpu->x[5] && cpu->x[3] != 0 && trace_count < 100) {
            if (prev_x3 != 0 && cpu->x[3] != prev_x3) {
                fprintf(stderr, "[X3-CHANGE] x3 changed from 0x%llx to 0x%llx at PC=0x%llx, x2=0x%llx\n",
                       (unsigned long long)prev_x3, (unsigned long long)cpu->x[3],
                       (unsigned long long)pc, (unsigned long long)cpu->x[2]);
            }
            prev_x3 = cpu->x[3];
        }
        
        prev_x2 = cpu->x[2];
        
        // LOW-FREQUENCY PROGRESS SAMPLER (max once per second)
        if (!logged_sampler_point) {
            printk("[RUN-TRACE] a64_cpu_run sampler point pc=0x%llx sp=0x%llx\n",
                   (unsigned long long) pc,
                   (unsigned long long) cpu->sp);
            logged_sampler_point = true;
        }
        time_t now = time(NULL);
        if (now != last_sample_time) {
            last_sample_time = now;
            sample_count++;
            
            uint64_t ldr_fast = cpu->stat_ldr_fast_hits;
            uint64_t str_fast = cpu->stat_str_fast_hits;
            uint64_t ldr_fb = cpu->stat_ldr_fallback;
            uint64_t str_fb = cpu->stat_str_fallback;
            
            // Calculate deltas since last sample
            uint64_t delta_ldr_fast = ldr_fast - last_ldr_fast;
            uint64_t delta_str_fast = str_fast - last_str_fast;
            uint64_t delta_ldr_fb = ldr_fb - last_ldr_fallback;
            uint64_t delta_str_fb = str_fb - last_str_fallback;
            uint64_t pc_delta = pc - last_pc;
            
            printk("[SAMPLE-%d] pc=0x%llx sp=0x%llx exit_reason=%d\n",
                   sample_count,
                   (unsigned long long)pc,
                   (unsigned long long)cpu->sp,
                   cpu->tcti_exit_reason);
            printk("[SAMPLE-%d] mem_stats: ldr_fast=+%llu str_fast=+%llu ldr_fb=+%llu str_fb=+%llu\n",
                   sample_count,
                   (unsigned long long)delta_ldr_fast,
                   (unsigned long long)delta_str_fast,
                   (unsigned long long)delta_ldr_fb,
                   (unsigned long long)delta_str_fb);
            printk("[SAMPLE-%d] fallback_reasons: nonhot=%llu size=%llu idx=%llu meta=%llu align=%llu cross=%llu tlbmiss=%llu notlb=%llu\n",
                   sample_count,
                   (unsigned long long)(cpu->stat_ldr_fallback_nonhot + cpu->stat_str_fallback_nonhot),
                   (unsigned long long)(cpu->stat_ldr_fallback_size + cpu->stat_str_fallback_size),
                   (unsigned long long)(cpu->stat_ldr_fallback_idxmode + cpu->stat_str_fallback_idxmode),
                   (unsigned long long)(cpu->stat_ldr_fallback_meta + cpu->stat_str_fallback_meta),
                   (unsigned long long)(cpu->stat_ldr_fallback_align + cpu->stat_str_fallback_align),
                   (unsigned long long)(cpu->stat_ldr_fallback_crosspg + cpu->stat_str_fallback_crosspg),
                   (unsigned long long)(cpu->stat_ldr_fallback_tlbmiss + cpu->stat_str_fallback_tlbmiss),
                   (unsigned long long)(cpu->stat_ldr_fallback_notlb + cpu->stat_str_fallback_notlb));
            printk("[SAMPLE-%d] pc_delta=+%lld\n", sample_count, (long long)pc_delta);
            
            last_pc = pc;
            last_sp = cpu->sp;
            last_ldr_fast = ldr_fast;
            last_str_fast = str_fast;
            last_ldr_fallback = ldr_fb;
            last_str_fallback = str_fb;
        }
        
        // L0 cache lookup (fast path via fiber_exec_ctx)
        size_t l0_idx = ((pc ^ (pc >> 12)) & FIBER_EXEC_CTX_CACHE_MASK);
        struct a64_block *block = ctx->l0_cache[l0_idx];
        
        // Validate L0 cache hit (check PC matches)
        if (block && block->start_pc == pc) {
            fiber_stat_inc(ctx, STAT_TB_L0_HITS);
        } else {
            // L0 miss - fall back to MMU cache (L1)
            block = NULL;
            if (cpu->mmu->block_cache) {
                block = a64_cache_lookup(cpu->mmu->block_cache, pc);
                if (block) {
                    fiber_stat_inc(ctx, STAT_TB_L1_HITS);
                }
            }
            
            if (!block) {
                // Compile new block
                block = a64_compile_block(cpu, pc, tlb);
                if (!block) {
                    printk("[TCTI] FATAL: Cannot compile block at PC=0x%llx\n", pc);
                    handle_interrupt(INT_GPF);
                    continue;
                }
                fiber_stat_inc(ctx, STAT_TB_COMPILES);
                
                // Insert into MMU cache (L1)
                if (cpu->mmu->block_cache) {
                    a64_cache_insert(cpu->mmu->block_cache, block);
                }
            }
            
            // Populate L0 cache for next access
            ctx->l0_cache[l0_idx] = block;
        }

        // Execute the block via TCTI
        // NOTE: Execution runs directly on cpu_state (authoritative state owner)
        // ctx->frame.cpu is RESERVED for future fiber work, not used today
        int exit_reason = a64_execute_block(cpu, block);
        
        // Check exit reason and handle
        if (exit_reason == TCTI_EXIT_SYSCALL) {
            if (!block->explicit_pc_on_exit)
                cpu->pc = block->end_pc;
            // Mark context inactive before handing control to kernel
            fiber_exec_ctx_put(ctx);
            handle_interrupt(INT_SYSCALL);
        } else if (exit_reason == TCTI_EXIT_FAULT) {
            fprintf(stderr, "[RUN-DIAG] TCTI_EXIT_FAULT reached, calling handle_interrupt(INT_GPF)\n");
            
            // DIAGNOSTIC: Dump CMP-to-B.NE diagnostic if captured
            dump_cmp_bcond_diag();
            
            // DIAGNOSTIC: Capture fault details
            fprintf(stderr, "[FAULT-DIAG] pc=0x%llx fault_addr=0x%llx is_write=%d sp=0x%llx\n",
                   (unsigned long long)cpu->pc,
                   (unsigned long long)cpu->fault_addr,
                   cpu->fault_was_write,
                   (unsigned long long)cpu->sp);
            fprintf(stderr, "[FAULT-DIAG] regs x1=0x%llx x2=0x%llx x3=0x%llx x5=0x%llx x6=0x%llx x7=0x%llx\n",
                   (unsigned long long)cpu->x[1],
                   (unsigned long long)cpu->x[2],
                   (unsigned long long)cpu->x[3],
                   (unsigned long long)cpu->x[5],
                   (unsigned long long)cpu->x[6],
                   (unsigned long long)cpu->x[7]);

            if (cpu->pc == 0xf7fa4720ULL) {
                uint64_t rel_ptr = 0;
                uint64_t rel_sz = 0;
                int rel_ptr_ok = a64_guest_read64(cpu, cpu->tlb, cpu->sp + 0x190, &rel_ptr);
                int rel_sz_ok = a64_guest_read64(cpu, cpu->tlb, cpu->sp + 0x198, &rel_sz);

                printk("[FAULT-DIAG] rel slots [sp+0x190]=0x%llx (%d) [sp+0x198]=0x%llx (%d)\n",
                       (unsigned long long) rel_ptr, rel_ptr_ok,
                       (unsigned long long) rel_sz, rel_sz_ok);
            }
            
            // Fetch and decode instruction at fault PC
            uint32_t insn;
            if (a64_fetch_insn(cpu, cpu->tlb, cpu->pc, &insn) == 0) {
                fprintf(stderr, "[FAULT-DIAG] Instruction at PC: 0x%08x\n", insn);
                
                a64_instr_t decoded;
                if (a64_decode(insn, &decoded) == 0) {
                    fprintf(stderr, "[FAULT-DIAG] Decoded: cat=%d subtype=%d Rd=%d Rn=%d Rm=%d\n",
                           decoded.cat, decoded.subtype, decoded.Rd, decoded.Rn, decoded.Rm);
                    fprintf(stderr, "[FAULT-DIAG] imm=%lld imm_shift=%d set_flags=%d is_64bit=%d\n",
                           (long long)decoded.imm, decoded.imm_shift, 
                           decoded.set_flags, decoded.is_64bit);
                } else {
                    fprintf(stderr, "[FAULT-DIAG] Failed to decode instruction\n");
                }
            } else {
                fprintf(stderr, "[FAULT-DIAG] Failed to fetch instruction at PC\n");
            }
            
            // Dump CMP-to-B.NE diagnostic if captured
            dump_cmp_bcond_diag();
            
            // Dump CMP capture diagnostic (one-shot)
            dump_cmp_capture();
            
            // Dump STR writeback diagnostic
            dump_str_wb_diag();
            
            // Dump runtime diagnostic for pc=0xf7fa4650
            dump_runtime_diag();
            
            // Dump emission diagnostic for pc=0xf7fa4650
            dump_gen_emit_diag();
            
            // Mark context inactive before handling fault
            fiber_exec_ctx_put(ctx);
            handle_interrupt(INT_GPF);
            fprintf(stderr, "[RUN-DIAG] handle_interrupt(INT_GPF) returned\n");
        } else if (exit_reason == TCTI_EXIT_SIGNAL) {
            if (!block->explicit_pc_on_exit)
                cpu->pc = block->end_pc;
            // Mark context inactive before handling signal
            fiber_exec_ctx_put(ctx);
            // Check for pending signals
            // deliver_signal(...)
        } else if (exit_reason == TCTI_EXIT_COMPLEX) {
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
                        } else if (op1 == 3 && crn == 13 && crm == 0 && op2 == 3) {
                            // TPIDRRO_EL0 read (same value on Linux)
                            cpu->x[decoded.Rd] = cpu->tpidr_el0;
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
        
        // DIAGNOSTIC: Capture NZCV and branch decision for zeroing loop
        if (trace_count < 250 && cpu->pc >= 0xf7fa4650ULL && cpu->pc <= 0xf7fa4660ULL) {
            fprintf(stderr, "[NZCV-DIAG] Block exit PC=0x%llx, pstate=0x%llx, x2=0x%llx, x5=0x%llx\n",
                   (unsigned long long)cpu->pc,
                   (unsigned long long)cpu->pstate,
                   (unsigned long long)cpu->x[2],
                   (unsigned long long)cpu->x[5]);
            // Check Z flag (bit 30)
            int z_flag = (cpu->pstate >> 30) & 1;
            fprintf(stderr, "[NZCV-DIAG] Z=%d, x2==x5=%d (should exit loop when Z=1)\n",
                   z_flag, cpu->x[2] == cpu->x[5]);
        }
        
        // Normal exit - PC already advanced, continue to next block
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

/*
 * Dump Phase 1B statistics for data-driven optimization
 * Reports fast-path hits and fallback reasons
 */
void a64_cpu_dump_stats(struct cpu_state *cpu) {
    printk("=== Phase 1B Memory Access Statistics ===\n");
    
    printk("LDR:\n");
    printk("  Fast-path hits: %llu\n", (unsigned long long)cpu->stat_ldr_fast_hits);
    printk("  Total fallbacks: %llu\n", (unsigned long long)cpu->stat_ldr_fallback);
    if (cpu->stat_ldr_fallback > 0) {
        printk("  Fallback reasons:\n");
        printk("    Non-hot registers: %llu\n", (unsigned long long)cpu->stat_ldr_fallback_nonhot);
        printk("    Non-64-bit size: %llu\n", (unsigned long long)cpu->stat_ldr_fallback_size);
        printk("    Non-offset mode: %llu\n", (unsigned long long)cpu->stat_ldr_fallback_idxmode);
        printk("    Non-zero meta: %llu\n", (unsigned long long)cpu->stat_ldr_fallback_meta);
        printk("    Unaligned access: %llu\n", (unsigned long long)cpu->stat_ldr_fallback_align);
        printk("    Cross-page access: %llu\n", (unsigned long long)cpu->stat_ldr_fallback_crosspg);
        printk("    TLB miss: %llu\n", (unsigned long long)cpu->stat_ldr_fallback_tlbmiss);
        printk("    No TLB: %llu\n", (unsigned long long)cpu->stat_ldr_fallback_notlb);
    }
    
    printk("STR:\n");
    printk("  Fast-path hits: %llu\n", (unsigned long long)cpu->stat_str_fast_hits);
    printk("  Total fallbacks: %llu\n", (unsigned long long)cpu->stat_str_fallback);
    if (cpu->stat_str_fallback > 0) {
        printk("  Fallback reasons:\n");
        printk("    Non-hot registers: %llu\n", (unsigned long long)cpu->stat_str_fallback_nonhot);
        printk("    Non-64-bit size: %llu\n", (unsigned long long)cpu->stat_str_fallback_size);
        printk("    Non-offset mode: %llu\n", (unsigned long long)cpu->stat_str_fallback_idxmode);
        printk("    Non-zero meta: %llu\n", (unsigned long long)cpu->stat_str_fallback_meta);
        printk("    Unaligned access: %llu\n", (unsigned long long)cpu->stat_str_fallback_align);
        printk("    Cross-page access: %llu\n", (unsigned long long)cpu->stat_str_fallback_crosspg);
        printk("    TLB miss: %llu\n", (unsigned long long)cpu->stat_str_fallback_tlbmiss);
        printk("    No TLB: %llu\n", (unsigned long long)cpu->stat_str_fallback_notlb);
    }
    
    // Calculate totals
    uint64_t ldr_total = cpu->stat_ldr_fast_hits + cpu->stat_ldr_fallback;
    uint64_t str_total = cpu->stat_str_fast_hits + cpu->stat_str_fallback;
    
    if (ldr_total > 0) {
        printk("LDR hit rate: %.2f%%\n", 
               100.0 * cpu->stat_ldr_fast_hits / ldr_total);
    }
    if (str_total > 0) {
        printk("STR hit rate: %.2f%%\n", 
               100.0 * cpu->stat_str_fast_hits / str_total);
    }
    printk("========================================\n");
}
