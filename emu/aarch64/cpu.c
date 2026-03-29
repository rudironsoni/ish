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
#include "trace/trace.h"
#include <string.h>
#include <setjmp.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>

// External TCTI entry point (declared in gadgets_tcti.h)
extern void tcti_entry_block(void *gadgets, struct cpu_state *cpu);

// Execution state is now passed via parameters, not globals
static jmp_buf exit_jmpbuf __attribute__((unused));

// Forward declarations
struct a64_block *a64_compile_block(struct cpu_state *cpu, uint64_t pc, struct tlb *tlb);
int a64_execute_block(struct cpu_state *cpu, struct a64_block *block);

// Load/store helper functions used by execute_ldst
static uint64_t a64_read_reg_or_sp(struct cpu_state *cpu, int reg, bool is_64bit);
static void a64_write_reg_or_sp(struct cpu_state *cpu, int reg, uint64_t value, bool is_64bit);
static uint64_t a64_extend_index(uint64_t value, int extend_type);
static uint64_t a64_apply_shift(uint64_t value, int shift_type, int amount, bool is_64bit);

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

    printk("[A64_COMPILE_BLOCK] START: PC=0x%016llx cpu=%p tlb=%p\n", pc, cpu, tlb);

    // Trace: Block compilation start
    trace_emit_block_compile_start(pc);

    // Create sidecar if tracing is active at level >= BLOCK
    trace_block_sidecar_t *sidecar = NULL;
    if (trace_sidecar_enabled()) {
        sidecar = trace_sidecar_create(pc, pc);
    }

    int ret = a64_gen_init(&gen_state, buffer, A64_MAX_GADGETS_PER_BLOCK);
    if (ret != A64_GEN_OK) {
        printk("[A64_COMPILE_BLOCK] ERROR: a64_gen_init failed: %d\n", ret);
        return NULL;
    }
    a64_gen_reset(&gen_state, pc);
    printk("[A64_COMPILE_BLOCK] Generator initialized, starting at PC=0x%016llx\n", pc);

    // Translate instructions until block end
    int max_insns = 50;  // Reasonable limit
    int insns_decoded = 0;
    for (int i = 0; i < max_insns; i++) {
        uint32_t insn;
        printk("[A64_COMPILE_BLOCK] Fetching instruction at PC=0x%016llx\n", gen_state.guest_pc);
        int ret = a64_fetch_insn(cpu, tlb, gen_state.guest_pc, &insn);
        if (ret < 0) {
            // Page fault during fetch
            printk("[A64_COMPILE_BLOCK] Fetch failed at PC=0x%016llx: ret=%d\n",
                   gen_state.guest_pc, ret);
            break;
        }
        printk("[A64_COMPILE_BLOCK] Fetched: insn=0x%08x at PC=0x%016llx\n", insn, gen_state.guest_pc);

        // Decode to get instruction info
        a64_instr_t decoded_info;
        int decode_ret = a64_decode(insn, &decoded_info);
        if (decode_ret < 0) {
            printk("[A64_COMPILE_BLOCK] Decode failed for insn=0x%08x: ret=%d\n", insn, decode_ret);
        } else {
            printk("[A64_COMPILE_BLOCK] Decoded: cat=%d subtype=%d\n", decoded_info.cat, decoded_info.subtype);
        }

        // Generate TCTI instruction - load/store and bitfield now have inline TCTI support
        ret = a64_gen_instruction(&gen_state, insn, gen_state.guest_pc);
        printk("[A64_COMPILE_BLOCK] a64_gen_instruction returned: %d\n", ret);

        if (ret < 0) {
            // Decode error - block ends here
            printk("[A64_COMPILE_BLOCK] Generation error at PC=0x%016llx: ret=%d\n",
                   gen_state.guest_pc, ret);
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
            printk("[A64_COMPILE_BLOCK] Block end signaled at PC=0x%016llx\n", gen_state.guest_pc);
            break;
        }
    }
    printk("[A64_COMPILE_BLOCK] Translation complete: insns_decoded=%d\n", insns_decoded);

    // Check if we decoded any instructions
    if (insns_decoded == 0) {
        // No instructions could be decoded - this is a fatal error in 100% TCTI mode
        uint32_t failing_insn = 0;
        a64_fetch_insn(cpu, tlb, pc, &failing_insn);
        printk("[A64_COMPILE_BLOCK] FAILED: No instructions decoded at PC=0x%016llx, insn=0x%08x\n",
               pc, failing_insn);
        trace_emit_u32(TRACE_EVENT_UNSUPPORTED_INSTRUCTION, pc, failing_insn);
        return NULL;
    }

    // Finalize the block
    printk("[A64_COMPILE_BLOCK] Finalizing block with %zu gadgets...\n", gen_state.num_gadgets);
    if (a64_gen_finalize(&gen_state) != A64_GEN_OK) {
        printk("[A64_COMPILE_BLOCK] FAILED: a64_gen_finalize failed\n");
        return NULL;
    }

    // Allocate block
    block = malloc(sizeof(*block));
    if (!block) {
        printk("[A64_COMPILE_BLOCK] FAILED: malloc failed for block struct\n");
        return NULL;
    }
    printk("[A64_COMPILE_BLOCK] Block struct allocated at %p\n", block);

    // Allocate gadget array
    block->gadgets = malloc(gen_state.num_gadgets * sizeof(void *));
    if (!block->gadgets) {
        printk("[A64_COMPILE_BLOCK] FAILED: malloc failed for gadget array (%zu gadgets)\n",
               gen_state.num_gadgets);
        free(block);
        return NULL;
    }
    printk("[A64_COMPILE_BLOCK] Gadget array allocated at %p (%zu gadgets)\n",
           block->gadgets, gen_state.num_gadgets);

    // Copy gadgets
    memcpy(block->gadgets, buffer, gen_state.num_gadgets * sizeof(void *));
    block->num_gadgets = gen_state.num_gadgets;
    block->start_pc = gen_state.start_pc;
    block->end_pc = gen_state.end_pc;
    block->explicit_pc_on_exit = explicit_pc_on_exit;
    block->is_jetsam = false;
    block->trace_sidecar = NULL;

    // Update and attach sidecar if present
    if (sidecar) {
        sidecar->end_pc = gen_state.end_pc;
        sidecar->explicit_pc_on_exit = explicit_pc_on_exit;
        sidecar->insn_count = insns_decoded;
        trace_sidecar_set_gadget_count(sidecar, (uint32_t)gen_state.num_gadgets);
        block->trace_sidecar = sidecar;
    }

    // Initialize list links
    list_init(&block->chain);
    list_init(&block->jetsam);

    // Trace: Block compilation end
    trace_emit_block_compile_end(pc, gen_state.end_pc, (uint32_t)insns_decoded);

    return block;
}

/*
 * Execute a compiled block using TCTI
 *
 * Uses tcti_entry_block to set up register mapping and execute
 * the entire gadget chain. Gadgets use epilogue to chain together.
 */
int a64_execute_block(struct cpu_state *cpu, struct a64_block *block) {
    // Trace: Register snapshot if at block level
    if (trace_get_level() >= TRACE_LEVEL_BLOCK) {
        uint64_t regs[6] = {cpu->x[0], cpu->x[1], cpu->x[2], cpu->x[3], cpu->x[4], cpu->x[5]};
        trace_emit_register_snapshot(block->start_pc, regs, 0x3F);
    }

    trace_emit_block_entry(block->start_pc, (uint32_t)block->num_gadgets);

    printk("[A64_EXECUTE_BLOCK] About to call tcti_entry_block:\n");
    printk("[A64_EXECUTE_BLOCK]   block->gadgets=%p\n", (void*)block->gadgets);
    printk("[A64_EXECUTE_BLOCK]   block->num_gadgets=%zu\n", block->num_gadgets);
    printk("[A64_EXECUTE_BLOCK]   cpu=%p\n", (void*)cpu);
    printk("[A64_EXECUTE_BLOCK]   cpu->pc=0x%016llx\n", cpu->pc);
    printk("[A64_EXECUTE_BLOCK]   First gadget=%p\n",
           block->gadgets ? (void*)block->gadgets[0] : NULL);

    // Validate pointers before calling
    if (!block->gadgets) {
        printk("[A64_EXECUTE_BLOCK] ERROR: block->gadgets is NULL!\n");
        cpu->tcti_exit_reason = TCTI_EXIT_FAULT;
        return TCTI_EXIT_FAULT;
    }
    if (!cpu) {
        printk("[A64_EXECUTE_BLOCK] ERROR: cpu is NULL!\n");
        return TCTI_EXIT_FAULT;
    }

    // Verify first gadget is not NULL
    if (block->num_gadgets > 0 && !block->gadgets[0]) {
        printk("[A64_EXECUTE_BLOCK] ERROR: First gadget is NULL!\n");
        cpu->tcti_exit_reason = TCTI_EXIT_FAULT;
        return TCTI_EXIT_FAULT;
    }

    printk("[A64_EXECUTE_BLOCK] Calling tcti_entry_block now...\n");

    tcti_entry_block(block->gadgets, cpu);
    int exit_reason = cpu->tcti_exit_reason;

    printk("[A64_EXECUTE_BLOCK] Returned from tcti_entry_block, exit_reason=%d\n", exit_reason);

    trace_emit_block_exit(block->start_pc, exit_reason, cpu->pc);

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
    printk("[A64_CPU_RUN] ENTER: cpu=%p tlb=%p mmu=%p\n", cpu, tlb, cpu ? cpu->mmu : NULL);

    if (!cpu || !tlb || !cpu->mmu) {
        printk("[A64_CPU_RUN] EARLY RETURN: missing required pointer (cpu=%p tlb=%p mmu=%p)\n",
               cpu, tlb, cpu ? cpu->mmu : NULL);
        return;
    }

    printk("[A64_CPU_RUN] Initial PC=0x%016llx SP=0x%016llx x0=0x%016llx\n",
           cpu->pc, cpu->sp, cpu->x[0]);

    // Initialize tracing from environment
    trace_config_t trace_config;
    trace_config_from_env(&trace_config);
    if (trace_init(&trace_config) == 0 && trace_get_level() >= TRACE_LEVEL_SUMMARY) {
        trace_emit_process_entry(cpu->pc, cpu->sp, cpu->x[0], cpu->x[1]);
    }

    cpu->tlb = tlb;  // Store TLB pointer in cpu_state for inline TLB access

    // Get or create persistent execution context for this CPU
    printk("[A64_CPU_RUN] Getting fiber_exec_ctx...\n");
    struct fiber_exec_ctx *ctx = fiber_exec_ctx_get(cpu);
    if (!ctx) {
        printk("[A64_CPU_RUN] ERROR: fiber_exec_ctx_get returned NULL!\n");
        trace_emit(TRACE_EVENT_FAULT, cpu->pc);
        handle_interrupt(INT_GPF);
        trace_shutdown();
        return;
    }
    printk("[A64_CPU_RUN] Got fiber_exec_ctx: %p\n", ctx);

    // Reset frame state for new execution run
    fiber_exec_ctx_reset(ctx, cpu);

    // Initialize per-MMU block cache if needed
    printk("[A64_CPU_RUN] Checking block_cache: mmu->block_cache=%p\n",
           cpu->mmu->block_cache);
    if (!cpu->mmu->block_cache) {
        printk("[A64_CPU_RUN] Allocating block_cache...\n");
        cpu->mmu->block_cache = malloc(sizeof(struct a64_block_cache));
        if (cpu->mmu->block_cache) {
            printk("[A64_CPU_RUN] Initializing block_cache at %p\n", cpu->mmu->block_cache);
            a64_cache_init(cpu->mmu->block_cache);
        } else {
            printk("[A64_CPU_RUN] ERROR: malloc failed for block_cache!\n");
        }
    }

    // Set up TLB for this CPU
    printk("[A64_CPU_RUN] Calling tlb_refresh...\n");
    tlb_refresh(tlb, cpu->mmu);
    printk("[A64_CPU_RUN] Entering main execution loop...\n");

    while (1) {
        // Reacquire context if it was marked inactive (e.g., after interrupt return)
        if (!ctx->active) {
            printk("[A64_CPU_RUN] Context inactive, reacquiring...\n");
            ctx = fiber_exec_ctx_get(cpu);
            if (!ctx) {
                printk("[A64_CPU_RUN] ERROR: Failed to reacquire context!\n");
                trace_emit(TRACE_EVENT_FAULT, cpu->pc);
                handle_interrupt(INT_GPF);
                break;
            }
        }

        uint64_t pc = cpu->pc;
        printk("[A64_CPU_RUN] LOOP: PC=0x%016llx\n", pc);

        // L0 cache lookup (fast path via fiber_exec_ctx)
        size_t l0_idx = ((pc ^ (pc >> 12)) & FIBER_EXEC_CTX_CACHE_MASK);
        struct a64_block *block = ctx->l0_cache[l0_idx];
        printk("[A64_CPU_RUN] L0 cache lookup: idx=%zu block=%p\n", l0_idx, block);

        // Validate L0 cache hit (check PC matches)
        if (block && block->start_pc == pc) {
            printk("[A64_CPU_RUN] L0 HIT: block=%p start_pc=0x%016llx\n",
                   block, block->start_pc);
            fiber_stat_inc(ctx, STAT_TB_L0_HITS);
        } else {
            // L0 miss - fall back to MMU cache (L1)
            block = NULL;
            if (cpu->mmu->block_cache) {
                block = a64_cache_lookup(cpu->mmu->block_cache, pc);
                if (block) {
                    printk("[A64_CPU_RUN] L1 HIT: block=%p start_pc=0x%016llx\n",
                           block, block->start_pc);
                    fiber_stat_inc(ctx, STAT_TB_L1_HITS);
                }
            }

            if (!block) {
                // Compile new block
                printk("[A64_CPU_RUN] CACHE MISS: Compiling block at PC=0x%016llx...\n", pc);
                block = a64_compile_block(cpu, pc, tlb);
                if (!block) {
                    printk("[A64_CPU_RUN] ERROR: a64_compile_block returned NULL for PC=0x%016llx!\n", pc);
                    trace_emit(TRACE_EVENT_FAULT, pc);
                    handle_interrupt(INT_GPF);
                    continue;
                }
                printk("[A64_CPU_RUN] Block compiled: block=%p start=0x%016llx end=0x%016llx gadgets=%zu\n",
                       block, block->start_pc, block->end_pc, block->num_gadgets);
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
        printk("[A64_CPU_RUN] Executing block: block=%p num_gadgets=%zu\n",
               block, block->num_gadgets);
        int exit_reason = a64_execute_block(cpu, block);
        printk("[A64_CPU_RUN] Block executed: exit_reason=%d (NORMAL=0,SYSCALL=1,SIGNAL=2,FAULT=3,COMPLEX=4)\n",
               exit_reason);
        
        // Check exit reason and handle
        if (exit_reason == TCTI_EXIT_SYSCALL) {
            printk("[A64_CPU_RUN] EXIT: SYSCALL at PC=0x%016llx\n", cpu->pc);
            if (!block->explicit_pc_on_exit)
                cpu->pc = block->end_pc;
            // Mark context inactive before handing control to kernel
            fiber_exec_ctx_put(ctx);
            handle_interrupt(INT_SYSCALL);
        } else if (exit_reason == TCTI_EXIT_FAULT) {
            printk("[A64_CPU_RUN] EXIT: FAULT at PC=0x%016llx fault_addr=0x%016llx is_write=%d\n",
                   cpu->pc, cpu->fault_addr, cpu->fault_was_write);
            // Trace fault event
            trace_emit_fault(cpu->pc, cpu->fault_addr, cpu->fault_was_write, 0);

            // Dump sidecar and ring if configured
            trace_dump_on_fault(cpu->pc, cpu->fault_addr, cpu->fault_was_write);

            // Mark context inactive before handling fault
            fiber_exec_ctx_put(ctx);
            handle_interrupt(INT_GPF);
        } else if (exit_reason == TCTI_EXIT_SIGNAL) {
            printk("[A64_CPU_RUN] EXIT: SIGNAL at PC=0x%016llx\n", cpu->pc);
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
                            // Unhandled MRS system register - this is an emulation limitation
                            // Not a guest error, so we just set the register to 0 and continue
                            trace_emit_unhandled_mrs(cpu->pc, decoded.sysreg);
                            cpu->x[decoded.Rd] = 0;
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
                            // Unhandled MSR system register - this is an emulation limitation
                            // Not a guest error, so we just ignore the write and continue
                            trace_emit_unhandled_msr(cpu->pc, decoded.sysreg);
                        }
                        cpu->pc += 4;  // Advance past MSR instruction
                    } else {
                        trace_emit_complex_unknown(cpu->pc, decoded.cat, decoded.subtype);
                        handle_interrupt(INT_GPF);
                    }
                } else {
                    trace_emit_complex_decode_fail(cpu->pc, insn);
                    handle_interrupt(INT_GPF);
                }
            } else {
                trace_emit_complex_fetch_fail(cpu->pc, -EFAULT);
                handle_interrupt(INT_GPF);
            }
        } else {
            printk("[A64_CPU_RUN] EXIT: NORMAL (fallthrough) next_PC=0x%016llx\n", block->end_pc);
            // Fallthrough blocks advance to end_pc. Control-transfer blocks preserve
            // the guest PC written by their terminal gadget.
            if (!block->explicit_pc_on_exit)
                cpu->pc = block->end_pc;
        }

        // Normal exit - PC already advanced, continue to next block
        printk("[A64_CPU_RUN] Continuing to next block...\n");
    }

    printk("[A64_CPU_RUN] EXITING LOOP (should not reach here in normal execution)\n");
    trace_shutdown();
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
