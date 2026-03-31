/*
 * aarch64 CPU execution main loop
 *
 * Bridges the TCTI gadgets with iSH's process model.
 */

#include "emu/aarch64/cpu.h"

#include "gadgets_tcti.h"

#include "emu/aarch64/block-cache.h"
#include "emu/aarch64/memory.h"
#include "emu/interrupt.h"
#include "emu/mmu.h"
#include "emu/tlb.h"
#include "kernel/calls.h"
#include "kernel/task.h"
#include "tcti/aarch64/gen.h"
#include "tcti/frame.h"
#include "trace/trace.h"

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// External TCTI entry point (declared in gadgets_tcti.h)
extern void tcti_entry_block(void *gadgets, struct cpu_state *cpu);

// External diagnostic dump function from gen.c
extern void dump_gen_emit_diag(void);
extern struct emit_diag {
    int captured;
    uint64_t fault_pc;
    uint64_t rt;
    uint64_t rn;
    int64_t imm;
    uint64_t size;
    uint64_t idx_mode;
    uint64_t meta;
    uint64_t is_load;
} g_emit_diag;

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
static void trace_cpu_init_checkpoint(const char *name, struct task *task, struct cpu_state *cpu,
                                      int err)
{
    char pid_buf[32];
    char task_buf[32];
    char cpu_buf[32];
    char mm_buf[32];
    char mem_buf[32];
    char cpu_mmu_buf[32];
    char expected_mem_mmu_buf[32];
    char err_buf[32];

    snprintf(pid_buf, sizeof(pid_buf), "%u", (unsigned)(task ? task->pid : 0));
    snprintf(task_buf, sizeof(task_buf), "%p", (void *)task);
    snprintf(cpu_buf, sizeof(cpu_buf), "%p", (void *)cpu);
    snprintf(mm_buf, sizeof(mm_buf), "%p", task ? (void *)task->mm : NULL);
    snprintf(mem_buf, sizeof(mem_buf), "%p", task ? (void *)task->mem : NULL);
    snprintf(cpu_mmu_buf, sizeof(cpu_mmu_buf), "%p", cpu ? (void *)cpu->mmu : NULL);
    snprintf(expected_mem_mmu_buf, sizeof(expected_mem_mmu_buf), "%p",
             task && task->mem ? (void *)&task->mem->mmu : NULL);
    snprintf(err_buf, sizeof(err_buf), "%d", err);

    trace_attribute_t attrs[] = {
        { "pid", pid_buf },
        { "task", task_buf },
        { "cpu", cpu_buf },
        { "mm", mm_buf },
        { "mem", mem_buf },
        { "cpu.mmu", cpu_mmu_buf },
        { "expected.mem.mmu", expected_mem_mmu_buf },
        { "err", err_buf },
    };

    (void)trace_begin_interval(TRACE_ORIGIN_EXEC, name, attrs, sizeof(attrs) / sizeof(attrs[0]));
}

static void trace_cpu_layout_checkpoint(const char *name, struct task *task, struct cpu_state *cpu,
                                        int err)
{
    char task_buf[32];
    char cpu_buf[32];
    char pid_field_buf[32];
    char mm_field_buf[32];
    char mem_field_buf[32];
    char pid_value_buf[32];
    char mm_value_buf[32];
    char mem_value_buf[32];
    char cpu_mmu_value_buf[32];
    char expected_mem_mmu_buf[32];
    char sizeof_task_buf[32];
    char sizeof_cpu_buf[32];
    char off_cpu_buf[32];
    char off_pid_buf[32];
    char off_mm_buf[32];
    char off_mem_buf[32];
    char err_buf[32];

    snprintf(task_buf, sizeof(task_buf), "%p", (void *)task);
    snprintf(cpu_buf, sizeof(cpu_buf), "%p", (void *)cpu);
    snprintf(pid_field_buf, sizeof(pid_field_buf), "%p", task ? (void *)&task->pid : NULL);
    snprintf(mm_field_buf, sizeof(mm_field_buf), "%p", task ? (void *)&task->mm : NULL);
    snprintf(mem_field_buf, sizeof(mem_field_buf), "%p", task ? (void *)&task->mem : NULL);
    snprintf(pid_value_buf, sizeof(pid_value_buf), "%u", (unsigned)(task ? task->pid : 0));
    snprintf(mm_value_buf, sizeof(mm_value_buf), "%p", task ? (void *)task->mm : NULL);
    snprintf(mem_value_buf, sizeof(mem_value_buf), "%p", task ? (void *)task->mem : NULL);
    snprintf(cpu_mmu_value_buf, sizeof(cpu_mmu_value_buf), "%p", cpu ? (void *)cpu->mmu : NULL);
    snprintf(expected_mem_mmu_buf, sizeof(expected_mem_mmu_buf), "%p",
             task && task->mem ? (void *)&task->mem->mmu : NULL);
    snprintf(sizeof_task_buf, sizeof(sizeof_task_buf), "%zu", sizeof(struct task));
    snprintf(sizeof_cpu_buf, sizeof(sizeof_cpu_buf), "%zu", sizeof(struct cpu_state));
    snprintf(off_cpu_buf, sizeof(off_cpu_buf), "%zu", __builtin_offsetof(struct task, cpu));
    snprintf(off_pid_buf, sizeof(off_pid_buf), "%zu", __builtin_offsetof(struct task, pid));
    snprintf(off_mm_buf, sizeof(off_mm_buf), "%zu", __builtin_offsetof(struct task, mm));
    snprintf(off_mem_buf, sizeof(off_mem_buf), "%zu", __builtin_offsetof(struct task, mem));
    snprintf(err_buf, sizeof(err_buf), "%d", err);

    trace_attribute_t attrs[] = {
        { "task", task_buf },
        { "addr.cpu", cpu_buf },
        { "addr.pid", pid_field_buf },
        { "addr.mm", mm_field_buf },
        { "addr.mem", mem_field_buf },
        { "pid", pid_value_buf },
        { "mm", mm_value_buf },
        { "mem", mem_value_buf },
        { "cpu.mmu", cpu_mmu_value_buf },
        { "expected.mem.mmu", expected_mem_mmu_buf },
        { "sizeof.task", sizeof_task_buf },
        { "sizeof.cpu", sizeof_cpu_buf },
        { "offsetof.cpu", off_cpu_buf },
        { "offsetof.pid", off_pid_buf },
        { "offsetof.mm", off_mm_buf },
        { "offsetof.mem", off_mem_buf },
        { "err", err_buf },
    };

    (void)trace_begin_interval(TRACE_ORIGIN_EXEC, name, attrs, sizeof(attrs) / sizeof(attrs[0]));
}

static void trace_cpu_run_checkpoint(const char *name, struct task *task, struct cpu_state *cpu,
                                     int exit_reason)
{
    char task_buf[32];
    char pid_buf[32];
    char mm_buf[32];
    char mem_buf[32];
    char cpu_mmu_buf[32];
    char expected_mem_mmu_buf[32];
    char pc_buf[32];
    char exit_reason_buf[32];

    snprintf(task_buf, sizeof(task_buf), "%p", (void *)task);
    snprintf(pid_buf, sizeof(pid_buf), "%u", (unsigned)(task ? task->pid : 0));
    snprintf(mm_buf, sizeof(mm_buf), "%p", task ? (void *)task->mm : NULL);
    snprintf(mem_buf, sizeof(mem_buf), "%p", task ? (void *)task->mem : NULL);
    snprintf(cpu_mmu_buf, sizeof(cpu_mmu_buf), "%p", cpu ? (void *)cpu->mmu : NULL);
    snprintf(expected_mem_mmu_buf, sizeof(expected_mem_mmu_buf), "%p",
             task && task->mem ? (void *)&task->mem->mmu : NULL);
    snprintf(pc_buf, sizeof(pc_buf), "0x%llx", cpu ? (unsigned long long)cpu->pc : 0ULL);
    snprintf(exit_reason_buf, sizeof(exit_reason_buf), "%d", exit_reason);

    trace_attribute_t attrs[] = {
        { "task", task_buf },       { "pid", pid_buf },
        { "mm", mm_buf },           { "mem", mem_buf },
        { "cpu.mmu", cpu_mmu_buf }, { "expected.mem.mmu", expected_mem_mmu_buf },
        { "pc", pc_buf },           { "exit_reason", exit_reason_buf },
    };

    (void)trace_begin_interval(TRACE_ORIGIN_EXEC, name, attrs, sizeof(attrs) / sizeof(attrs[0]));
}

static void trace_fault_origin_checkpoint(const char *name, uint64_t fault_pc, uint64_t base_reg,
                                          uint64_t index_reg, int64_t imm_offset,
                                          uint64_t computed_addr)
{
    char fault_pc_buf[32];
    char base_reg_buf[32];
    char index_reg_buf[32];
    char imm_buf[32];
    char computed_buf[32];

    snprintf(fault_pc_buf, sizeof(fault_pc_buf), "0x%llx", (unsigned long long)fault_pc);
    snprintf(base_reg_buf, sizeof(base_reg_buf), "0x%llx", (unsigned long long)base_reg);
    snprintf(index_reg_buf, sizeof(index_reg_buf), "0x%llx", (unsigned long long)index_reg);
    snprintf(imm_buf, sizeof(imm_buf), "%lld", (long long)imm_offset);
    snprintf(computed_buf, sizeof(computed_buf), "0x%llx", (unsigned long long)computed_addr);

    trace_attribute_t attrs[] = {
        { "fault_pc", fault_pc_buf },      { "base_reg", base_reg_buf },
        { "index_reg", index_reg_buf },    { "imm_offset", imm_buf },
        { "computed_addr", computed_buf },
    };

    (void)trace_begin_interval(TRACE_ORIGIN_EXEC, name, attrs, sizeof(attrs) / sizeof(attrs[0]));
}

static void trace_insn_decode_checkpoint(const char *name, uint64_t pc, uint32_t raw_insn, int cat,
                                         int subtype, int rn, int rm, int imm)
{
    char pc_buf[24];
    char raw_buf[16];
    char cat_buf[8];
    char subtype_buf[8];
    char rn_buf[8];
    char rm_buf[8];
    char imm_buf[16];

    snprintf(pc_buf, sizeof(pc_buf), "0x%llx", (unsigned long long)pc);
    snprintf(raw_buf, sizeof(raw_buf), "0x%08x", raw_insn);
    snprintf(cat_buf, sizeof(cat_buf), "%d", cat);
    snprintf(subtype_buf, sizeof(subtype_buf), "%d", subtype);
    snprintf(rn_buf, sizeof(rn_buf), "%d", rn);
    snprintf(rm_buf, sizeof(rm_buf), "%d", rm);
    snprintf(imm_buf, sizeof(imm_buf), "%d", imm);

    trace_attribute_t attrs[] = {
        { "pc", pc_buf }, { "raw_insn", raw_buf }, { "cat", cat_buf }, { "subtype", subtype_buf },
        { "rn", rn_buf }, { "rm", rm_buf },        { "imm", imm_buf },
    };

    (void)trace_begin_interval(TRACE_ORIGIN_EXEC, name, attrs, sizeof(attrs) / sizeof(attrs[0]));
}

void a64_cpu_init(struct task *task, struct cpu_state *cpu, int err)
{
    trace_cpu_init_checkpoint("task.proof.a64_cpu_init.entry", task, cpu, err);
    memset(cpu, 0, sizeof(*cpu));
    trace_cpu_init_checkpoint("task.proof.a64_cpu_init.after_memset", task, cpu, err);

    // Initialize vector registers (optional - clear to known state)
    for (int i = 0; i < 32; i++) {
        cpu->vregs[i].q = 0;
    }
    trace_cpu_init_checkpoint("task.proof.a64_cpu_init.after_vregs_clear", task, cpu, err);

    // PSTATE initial state: no flags set, EL0
    cpu->pstate = 0;
    trace_cpu_init_checkpoint("task.proof.a64_cpu_init.after_pstate_init", task, cpu, err);

    // TLS starts at 0 (set by libc)
    cpu->tpidr_el0 = 0;
    trace_cpu_init_checkpoint("task.proof.a64_cpu_init.after_tpidr_init", task, cpu, err);
    trace_cpu_init_checkpoint("task.proof.a64_cpu_init.before_return", task, cpu, err);
}

void a64_cpu_init_probe(struct task *task, struct cpu_state *cpu, int err)
{
    trace_cpu_layout_checkpoint("task.proof.cpu.layout_at_probe_entry", task, cpu, err);
    trace_cpu_init_checkpoint("task.proof.a64_cpu_init_probe.entry", task, cpu, err);
}

/*
 * Fetch an instruction from guest memory using TLB
 * Returns 0 on success, -EFAULT on fault
 */
int a64_fetch_insn(struct cpu_state *cpu, struct tlb *tlb, uint64_t pc, uint32_t *insn)
{
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
struct a64_block *a64_compile_block(struct cpu_state *cpu, uint64_t pc, struct tlb *tlb)
{
    a64_gen_state_t gen_state;
    tcti_gadget_t buffer[A64_MAX_GADGETS_PER_BLOCK];
    struct a64_block *block;
    bool explicit_pc_on_exit = false;

    // Trace: Block compilation start
    trace_emit_block_compile_start(pc);

    // Create sidecar if tracing is active at level >= BLOCK
    trace_block_sidecar_t *sidecar = NULL;
    if (trace_sidecar_enabled()) {
        sidecar = trace_sidecar_create(pc, pc);
    }

    int ret = a64_gen_init(&gen_state, buffer, A64_MAX_GADGETS_PER_BLOCK);
    if (ret != A64_GEN_OK) {
        return NULL;
    }
    a64_gen_reset(&gen_state, pc);

    // Translate instructions until block end
    int max_insns = 50; // Reasonable limit
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
        (void)decode_ret;

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
        gen_state.guest_pc += 4; // Advance to next instruction

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
        trace_emit_u32(TRACE_EVENT_UNSUPPORTED_INSTRUCTION, pc, failing_insn);
        return NULL;
    }

    // Finalize the block
    if (a64_gen_finalize(&gen_state) != A64_GEN_OK) {
        return NULL;
    }

    // Allocate block
    block = malloc(sizeof(*block));
    if (!block) {
        return NULL;
    }

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
int a64_execute_block(struct cpu_state *cpu, struct a64_block *block)
{
    // Trace: Register snapshot if at block level
    if (trace_get_level() >= TRACE_LEVEL_BLOCK) {
        uint64_t regs[6] = { cpu->x[0], cpu->x[1], cpu->x[2], cpu->x[3], cpu->x[4], cpu->x[5] };
        trace_emit_register_snapshot(block->start_pc, regs, 0x3F);
    }

    trace_emit_block_entry(block->start_pc, (uint32_t)block->num_gadgets);

    // Validate pointers before calling
    if (!block->gadgets) {
        cpu->tcti_exit_reason = TCTI_EXIT_FAULT;
        return TCTI_EXIT_FAULT;
    }
    if (!cpu) {
        return TCTI_EXIT_FAULT;
    }

    // Verify first gadget is not NULL
    if (block->num_gadgets > 0 && !block->gadgets[0]) {
        cpu->tcti_exit_reason = TCTI_EXIT_FAULT;
        return TCTI_EXIT_FAULT;
    }

    tcti_entry_block(block->gadgets, cpu);
    int exit_reason = cpu->tcti_exit_reason;

    trace_emit_block_exit(block->start_pc, exit_reason, cpu->pc);

    return exit_reason;
}

static uint64_t a64_read_reg_or_sp(struct cpu_state *cpu, int reg, bool is_64bit)
{
    if (reg == 31)
        return cpu->sp;
    if (reg < 0 || reg > 30)
        return 0;
    return is_64bit ? cpu->x[reg] : (uint32_t)cpu->x[reg];
}

static void a64_write_reg_or_sp(struct cpu_state *cpu, int reg, uint64_t value, bool is_64bit)
{
    if (reg == 31) {
        cpu->sp = is_64bit ? value : (uint32_t)value;
        return;
    }
    if (reg < 0 || reg > 30)
        return;
    cpu->x[reg] = is_64bit ? value : (uint32_t)value;
}

static uint64_t a64_extend_index(uint64_t value, int extend_type)
{
    switch (extend_type) {
    case A64_EXT_UXTW:
        return (uint32_t)value;
    case A64_EXT_SXTW:
        return (uint64_t)(int64_t)(int32_t)value;
    case A64_EXT_SXTX:
        return (uint64_t)(int64_t)value;
    case A64_EXT_UXTX:
    case A64_EXT_LSL:
    default:
        return value;
    }
}

static uint64_t a64_apply_shift(uint64_t value, int shift_type, int amount, bool is_64bit)
{
    amount &= is_64bit ? 63 : 31;
    if (!is_64bit)
        value = (uint32_t)value;
    switch (shift_type) {
    case A64_SHIFT_LSR:
        return is_64bit ? (value >> amount) : (uint32_t)value >> amount;
    case A64_SHIFT_ASR:
        return is_64bit ? (uint64_t)((int64_t)value >> amount)
                        : (uint32_t)((int32_t)value >> amount);
    case A64_SHIFT_ROR:
        if (amount == 0)
            return is_64bit ? value : (uint32_t)value;
        return is_64bit
                   ? ((value >> amount) | (value << (64 - amount)))
                   : (uint32_t)(((uint32_t)value >> amount) | ((uint32_t)value << (32 - amount)));
    case A64_SHIFT_LSL:
    default:
        return is_64bit ? (value << amount) : (uint32_t)value << amount;
    }
}

/*
 * Stage 3A.6: Pre-syscall userspace initialization tracing
 * Tracks first userspace entry and execution progression
 */

#include "kernel/memory.h"
#include "kernel/mm.h"

/*
 * Look up memory mapping information for a given PC address
 * Used to determine which mapping owns the stuck PC (interpreter vs main executable)
 */
static void trace_pc_mapping_info(const char *name, uint64_t pc)
{
    if (!current || !current->mem) {
        return;
    }

    char pc_buf[32];
    char page_buf[32];
    char flags_buf[32];
    char name_buf[128];
    char fd_buf[32];
    char is_interp_buf[8];
    char is_exe_buf[8];
    char map_start_buf[32];
    char map_end_buf[32];
    char file_offset_buf[32];

    snprintf(pc_buf, sizeof(pc_buf), "0x%llx", (unsigned long long)pc);
    snprintf(is_interp_buf, sizeof(is_interp_buf), "unknown");
    snprintf(is_exe_buf, sizeof(is_exe_buf), "unknown");
    name_buf[0] = '\0';
    fd_buf[0] = '\0';
    flags_buf[0] = '\0';
    map_start_buf[0] = '\0';
    map_end_buf[0] = '\0';
    file_offset_buf[0] = '\0';

    // Look up page table entry for this PC
    page_t page = PAGE(pc);
    read_wrlock(&current->mem->lock);
    struct pt_entry *entry = mem_pt(current->mem, page);
    if (entry && entry->data) {
        snprintf(page_buf, sizeof(page_buf), "0x%lx", (unsigned long)page << PAGE_BITS);
        snprintf(flags_buf, sizeof(flags_buf), "0x%x", entry->flags);

        // Scan backward to find mapping start (while data and flags match)
        page_t map_start_page = page;
        struct data *ref_data = entry->data;
        unsigned ref_flags = entry->flags;

        // Scan backward
        for (page_t pg = page; pg > 0; pg--) {
            struct pt_entry *e = mem_pt(current->mem, pg - 1);
            if (!e || e->data != ref_data || e->flags != ref_flags) {
                break;
            }
            map_start_page = pg - 1;
        }

        // Scan forward to find mapping end (while data and flags match)
        page_t map_end_page = page;
        // Scan forward up to reasonable limit
        for (page_t pg = page; pg < page + 10000 && pg < 0xFFFFFFFF; pg++) {
            struct pt_entry *e = mem_pt(current->mem, pg + 1);
            if (!e || e->data != ref_data || e->flags != ref_flags) {
                break;
            }
            map_end_page = pg + 1;
        }

        snprintf(map_start_buf, sizeof(map_start_buf), "0x%lx",
                 (unsigned long)map_start_page << PAGE_BITS);
        snprintf(map_end_buf, sizeof(map_end_buf), "0x%lx",
                 ((unsigned long)(map_end_page + 1) << PAGE_BITS) - 1);

        // Calculate file offset for this PC
        size_t page_offset_in_mapping = (page - map_start_page) * PAGE_SIZE;
        addr_t file_offset = entry->data->file_offset + page_offset_in_mapping + PGOFFSET(pc);
        snprintf(file_offset_buf, sizeof(file_offset_buf), "0x%lx", (unsigned long)file_offset);

        if (entry->data->name) {
            strncpy(name_buf, entry->data->name, sizeof(name_buf) - 1);
            name_buf[sizeof(name_buf) - 1] = '\0';

            // Check if this is interpreter mapping
            if (strstr(name_buf, "ld-musl") || strstr(name_buf, "ld-linux")) {
                snprintf(is_interp_buf, sizeof(is_interp_buf), "yes");
            } else {
                snprintf(is_interp_buf, sizeof(is_interp_buf), "no");
            }

            // Check if this is the main executable
            if (current->mm && current->mm->exefile && entry->data->fd == current->mm->exefile) {
                snprintf(is_exe_buf, sizeof(is_exe_buf), "yes");
            } else {
                snprintf(is_exe_buf, sizeof(is_exe_buf), "no");
            }
        }

        if (entry->data->fd) {
            snprintf(fd_buf, sizeof(fd_buf), "%p", (void *)entry->data->fd);
        }
    } else {
        snprintf(page_buf, sizeof(page_buf), "unmapped");
        snprintf(flags_buf, sizeof(flags_buf), "none");
        snprintf(name_buf, sizeof(name_buf), "[no mapping]");
        snprintf(fd_buf, sizeof(fd_buf), "none");
        snprintf(is_interp_buf, sizeof(is_interp_buf), "no");
        snprintf(is_exe_buf, sizeof(is_exe_buf), "no");
        snprintf(map_start_buf, sizeof(map_start_buf), "unmapped");
        snprintf(map_end_buf, sizeof(map_end_buf), "unmapped");
        snprintf(file_offset_buf, sizeof(file_offset_buf), "none");
    }
    read_wrunlock(&current->mem->lock);

    trace_attribute_t attrs[] = {
        { "pc", pc_buf },
        { "page", page_buf },
        { "map_start", map_start_buf },
        { "map_end", map_end_buf },
        { "file_offset", file_offset_buf },
        { "flags", flags_buf },
        { "name", name_buf },
        { "fd", fd_buf },
        { "is_interpreter", is_interp_buf },
        { "is_executable", is_exe_buf },
    };

    (void)trace_begin_interval(TRACE_ORIGIN_EMULATOR, name, attrs,
                               sizeof(attrs) / sizeof(attrs[0]));
}

static void trace_pre_syscall_checkpoint(const char *name, uint64_t pc, uint64_t block_start,
                                         uint64_t block_end, int loop_count, int block_count)
{
    char pc_buf[32];
    char block_start_buf[32];
    char block_end_buf[32];
    char loop_count_buf[16];
    char block_count_buf[16];

    snprintf(pc_buf, sizeof(pc_buf), "0x%llx", (unsigned long long)pc);
    snprintf(block_start_buf, sizeof(block_start_buf), "0x%llx", (unsigned long long)block_start);
    snprintf(block_end_buf, sizeof(block_end_buf), "0x%llx", (unsigned long long)block_end);
    snprintf(loop_count_buf, sizeof(loop_count_buf), "%d", loop_count);
    snprintf(block_count_buf, sizeof(block_count_buf), "%d", block_count);

    trace_attribute_t attrs[] = {
        { "pc", pc_buf },
        { "block_start", block_start_buf },
        { "block_end", block_end_buf },
        { "loop_count", loop_count_buf },
        { "block_count", block_count_buf },
    };

    (void)trace_begin_interval(TRACE_ORIGIN_EMULATOR, name, attrs,
                               sizeof(attrs) / sizeof(attrs[0]));
}

/*
 * Run the CPU until interrupted
 * This is the main entry point from the kernel
 */
void a64_cpu_run(struct cpu_state *cpu, struct tlb *tlb)
{
    trace_cpu_run_checkpoint("task.proof.a64_cpu_run.entry", current, cpu, 0);

    if (!cpu || !tlb || !cpu->mmu) {
        return;
    }

    // Initialize tracing from environment
    trace_config_t trace_config;
    trace_config_from_env(&trace_config);
    if (trace_init(&trace_config) == 0 && trace_get_level() >= TRACE_LEVEL_SUMMARY) {
        trace_emit_process_entry(cpu->pc, cpu->sp, cpu->x[0], cpu->x[1]);
    }

    cpu->tlb = tlb; // Store TLB pointer in cpu_state for inline TLB access

    // Get or create persistent execution context for this CPU
    struct fiber_exec_ctx *ctx = fiber_exec_ctx_get(cpu);
    if (!ctx) {
        trace_emit(TRACE_EVENT_FAULT, cpu->pc);
        trace_cpu_run_checkpoint("task.proof.a64_cpu_run.before_handle_interrupt_ctx_null", current,
                                 cpu, TCTI_EXIT_FAULT);
        handle_interrupt(INT_GPF);
        trace_cpu_run_checkpoint("task.proof.a64_cpu_run.after_handle_interrupt_call_ctx_null",
                                 current, cpu, TCTI_EXIT_FAULT);
        trace_cpu_run_checkpoint("task.proof.a64_cpu_run.before_exit_ctx_null", current, cpu,
                                 TCTI_EXIT_FAULT);
        trace_shutdown();
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
    trace_cpu_run_checkpoint("task.proof.a64_cpu_run.after_tlb_refresh", current, cpu, 0);

    // Stage 3A.6: Pre-syscall initialization tracing state
    bool first_block_lookup = true;
    bool first_compile = true;
    bool first_execute = true;
    bool first_user_entry = true;
    int block_execution_count = 0;
    uint64_t last_block_start_pc = 0;
    int same_block_repeat_count = 0;
    int total_blocks_executed = 0;

    // Stage 3A.6: Track first userspace PC entry
    if (trace_is_active()) {
        char pc_buf[32];
        char sp_buf[32];
        char x0_buf[32];
        char x1_buf[32];

        snprintf(pc_buf, sizeof(pc_buf), "0x%llx", (unsigned long long)cpu->pc);
        snprintf(sp_buf, sizeof(sp_buf), "0x%llx", (unsigned long long)cpu->sp);
        snprintf(x0_buf, sizeof(x0_buf), "0x%llx", (unsigned long long)cpu->x[0]);
        snprintf(x1_buf, sizeof(x1_buf), "0x%llx", (unsigned long long)cpu->x[1]);

        trace_attribute_t entry_attrs[] = {
            { "entry_pc", pc_buf },
            { "sp", sp_buf },
            { "x0", x0_buf },
            { "x1", x1_buf },
        };

        trace_begin_interval(TRACE_ORIGIN_EMULATOR, "task.proof.user.entry.pc", entry_attrs,
                             sizeof(entry_attrs) / sizeof(entry_attrs[0]));
    }

    while (1) {
        // Reacquire context if it was marked inactive (e.g., after interrupt return)
        if (!ctx->active) {
            ctx = fiber_exec_ctx_get(cpu);
            if (!ctx) {
                trace_emit(TRACE_EVENT_FAULT, cpu->pc);
                handle_interrupt(INT_GPF);
                break;
            }
        }

        uint64_t pc = cpu->pc;
        if (first_block_lookup) {
            trace_cpu_run_checkpoint("task.proof.a64_cpu_run.before_first_block_lookup", current,
                                     cpu, 0);
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
                if (first_compile) {
                    trace_cpu_run_checkpoint("task.proof.a64_cpu_run.before_first_compile", current,
                                             cpu, 0);
                }
                block = a64_compile_block(cpu, pc, tlb);
                if (!block) {
                    trace_emit(TRACE_EVENT_FAULT, pc);
                    trace_cpu_run_checkpoint("task.proof.a64_cpu_run.exit_fault", current, cpu,
                                             TCTI_EXIT_FAULT);
                    trace_cpu_run_checkpoint("task.proof.a64_cpu_run.before_handle_interrupt",
                                             current, cpu, TCTI_EXIT_FAULT);
                    handle_interrupt(INT_GPF);
                    trace_cpu_run_checkpoint("task.proof.a64_cpu_run.after_handle_interrupt_call",
                                             current, cpu, TCTI_EXIT_FAULT);
                    trace_cpu_run_checkpoint("task.proof.a64_cpu_run.after_handle_interrupt",
                                             current, cpu, TCTI_EXIT_FAULT);
                    continue;
                }
                if (first_compile) {
                    trace_cpu_run_checkpoint("task.proof.a64_cpu_run.after_first_compile", current,
                                             cpu, 0);
                    first_compile = false;
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

        if (first_block_lookup) {
            trace_cpu_run_checkpoint("task.proof.a64_cpu_run.after_first_block_lookup", current,
                                     cpu, 0);
            first_block_lookup = false;
        }

        // Stage 3A.6: Track block execution progression
        total_blocks_executed++;

        // Stage 3A.6: Detect block repetition (potential loop)
        if (block->start_pc == last_block_start_pc) {
            same_block_repeat_count++;
        } else {
            // Block changed - record the transition
            if (same_block_repeat_count > 0 && trace_is_active()) {
                trace_pre_syscall_checkpoint("task.proof.user.block.repeat_detected", cpu->pc,
                                             last_block_start_pc, block->start_pc,
                                             same_block_repeat_count, total_blocks_executed);
            }
            same_block_repeat_count = 0;
            last_block_start_pc = block->start_pc;

            // Record first block entry
            if (first_user_entry && trace_is_active()) {
                trace_pre_syscall_checkpoint("task.proof.user.first_block", cpu->pc,
                                             block->start_pc, block->end_pc, 0, 1);
                first_user_entry = false;
                // Capture mapping info for first userspace PC
                trace_pc_mapping_info("task.proof.user.pc_mapping.entry", cpu->pc);
            }
        }

        // Stage 3A.6: Periodic checkpoint every 100 blocks to detect slow progress
        if (total_blocks_executed % 100 == 0 && trace_is_active()) {
            trace_pre_syscall_checkpoint("task.proof.user.progress.checkpoint", cpu->pc,
                                         block->start_pc, block->end_pc, same_block_repeat_count,
                                         total_blocks_executed);
        }

        // Execute the block via TCTI
        // NOTE: Execution runs directly on cpu_state (authoritative state owner)
        // ctx->frame.cpu is RESERVED for future fiber work, not used today
        trace_cpu_run_checkpoint("task.proof.a64_cpu_run.before_block_execute", current, cpu,
                                 (int)block->start_pc);
        if (first_execute) {
            trace_cpu_run_checkpoint("task.proof.a64_cpu_run.before_first_execute", current, cpu,
                                     0);
        }
        int exit_reason = a64_execute_block(cpu, block);
        trace_cpu_run_checkpoint("task.proof.a64_cpu_run.exit_reason_set", current, cpu,
                                 exit_reason);
        if (first_execute) {
            trace_cpu_run_checkpoint("task.proof.a64_cpu_run.after_first_execute", current, cpu,
                                     exit_reason);
            first_execute = false;
        }

        // Check exit reason and handle
        if (exit_reason == TCTI_EXIT_SYSCALL) {
            trace_cpu_run_checkpoint("task.proof.a64_cpu_run.exit_syscall", current, cpu,
                                     exit_reason);
            if (!block->explicit_pc_on_exit)
                cpu->pc = block->end_pc;
            // Mark context inactive before handing control to kernel
            fiber_exec_ctx_put(ctx);
            trace_cpu_run_checkpoint("task.proof.a64_cpu_run.before_handle_interrupt", current, cpu,
                                     exit_reason);
            handle_interrupt(INT_SYSCALL);
            trace_cpu_run_checkpoint("task.proof.a64_cpu_run.after_handle_interrupt_call", current,
                                     cpu, exit_reason);
            trace_cpu_run_checkpoint("task.proof.a64_cpu_run.after_handle_interrupt", current, cpu,
                                     exit_reason);
        } else if (exit_reason == TCTI_EXIT_FAULT) {
            trace_cpu_run_checkpoint("task.proof.a64_cpu_run.exit_fault", current, cpu,
                                     exit_reason);

            // Decode and trace the faulting instruction
            uint32_t raw_insn = 0;
            a64_instr_t decoded;
            if (a64_fetch_insn(cpu, cpu->tlb, cpu->pc, &raw_insn) == 0 &&
                a64_decode(raw_insn, &decoded) == 0) {
                trace_insn_decode_checkpoint("task.proof.faulting_insn.decode", cpu->pc, raw_insn,
                                             decoded.cat, decoded.subtype, decoded.Rn, decoded.Rm,
                                             decoded.imm);
            }

            // Trace fault event with full context for first fault analysis
            trace_fault_origin_checkpoint("task.proof.first_fault.details", cpu->pc, cpu->x[2], 0,
                                          0, cpu->fault_addr);
            trace_emit_fault(cpu->pc, cpu->fault_addr, cpu->fault_was_write, 0);

            // Dump sidecar and ring if configured
            trace_dump_on_fault(cpu->pc, cpu->fault_addr, cpu->fault_was_write);

            // Mark context inactive before handling fault
            fiber_exec_ctx_put(ctx);
            trace_cpu_run_checkpoint("task.proof.a64_cpu_run.before_handle_interrupt", current, cpu,
                                     exit_reason);
            handle_interrupt(INT_GPF);
            trace_cpu_run_checkpoint("task.proof.a64_cpu_run.after_handle_interrupt_call", current,
                                     cpu, exit_reason);
            trace_cpu_run_checkpoint("task.proof.a64_cpu_run.after_handle_interrupt", current, cpu,
                                     exit_reason);
        } else if (exit_reason == TCTI_EXIT_SIGNAL) {
            trace_cpu_run_checkpoint("task.proof.a64_cpu_run.exit_signal", current, cpu,
                                     exit_reason);
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
                        cpu->pc += 4; // Advance past MRS instruction
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
                        cpu->pc += 4; // Advance past MSR instruction
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
            // Fallthrough blocks advance to end_pc. Control-transfer blocks preserve
            // the guest PC written by their terminal gadget.
            if (!block->explicit_pc_on_exit)
                cpu->pc = block->end_pc;
        }

        // Stage 3A.6: Track PC progression and detect loops
        // If we've executed many blocks without a syscall, we're in pre-syscall init
        if (total_blocks_executed >= 1000 && !first_user_entry && trace_is_active()) {
            trace_pre_syscall_checkpoint("task.proof.user.pre_syscall.loop_suspected", cpu->pc,
                                         last_block_start_pc, block->start_pc,
                                         same_block_repeat_count, total_blocks_executed);
            // Capture mapping info for stuck PC
            trace_pc_mapping_info("task.proof.user.pc_mapping.stuck", cpu->pc);
        }

        // Normal exit - PC already advanced, continue to next block
    }

    trace_shutdown();
}

/*
 * Execute load/store instruction in C with full TLB translation
 * This is called when TCTI encounters a load/store and needs C assistance
 */
int a64_execute_ldst(struct cpu_state *cpu, struct tlb *tlb, const a64_instr_t *instr)
{
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
            uint64_t first = instr->is_64bit ? *(uint64_t *)ptr : *(uint32_t *)ptr;
            uint64_t second =
                instr->is_64bit ? *(uint64_t *)(ptr + width) : *(uint32_t *)(ptr + width);
            a64_write_reg_or_sp(cpu, instr->Rd, first, instr->is_64bit);
            a64_write_reg_or_sp(cpu, instr->Rm, second, instr->is_64bit);
        } else {
            uint64_t first = a64_read_reg_or_sp(cpu, instr->Rd, instr->is_64bit);
            uint64_t second = a64_read_reg_or_sp(cpu, instr->Rm, instr->is_64bit);
            if (instr->is_64bit) {
                *(uint64_t *)ptr = first;
                *(uint64_t *)(ptr + width) = second;
            } else {
                *(uint32_t *)ptr = (uint32_t)first;
                *(uint32_t *)(ptr + width) = (uint32_t)second;
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
        case A64_SIZE_B:
            value = instr->is_signed ? (int8_t)*(uint8_t *)ptr : *(uint8_t *)ptr;
            break;
        case A64_SIZE_H:
            value = instr->is_signed ? (int16_t)*(uint16_t *)ptr : *(uint16_t *)ptr;
            break;
        case A64_SIZE_W:
            value = instr->is_signed ? (int32_t)*(uint32_t *)ptr : *(uint32_t *)ptr;
            break;
        case A64_SIZE_X:
            value = *(uint64_t *)ptr;
            break;
        default:
            return -1;
        }
        a64_write_reg_or_sp(cpu, instr->Rd, value, instr->size == A64_SIZE_X);
    } else {
        uint64_t value = a64_read_reg_or_sp(cpu, instr->Rd, instr->size == A64_SIZE_X);
        switch (instr->size) {
        case A64_SIZE_B:
            *(uint8_t *)ptr = (uint8_t)value;
            break;
        case A64_SIZE_H:
            *(uint16_t *)ptr = (uint16_t)value;
            break;
        case A64_SIZE_W:
            *(uint32_t *)ptr = (uint32_t)value;
            break;
        case A64_SIZE_X:
            *(uint64_t *)ptr = value;
            break;
        default:
            return -1;
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
void a64_cpu_dump(struct cpu_state *cpu)
{
    // Dormant debug helper - no active printk per tracing rules
    (void)cpu;
}

/*
 * Dump Phase 1B statistics for data-driven optimization
 * Reports fast-path hits and fallback reasons
 */
void a64_cpu_dump_stats(struct cpu_state *cpu)
{
    // Dormant debug helper - no active printk per tracing rules
    (void)cpu;
}
