/*
 * aarch64 CPU execution main loop
 *
 * Bridges the TCTI gadgets with iSH's process model.
 */

#import <IXLandInstrumentationTracing/trace.h>
#import <IXLandLinuxRuntime/emu/aarch64/block-cache.h>
#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>
#import <IXLandLinuxRuntime/emu/aarch64/fetch.h>
#import <IXLandLinuxRuntime/emu/aarch64/memory.h>
#import <IXLandLinuxRuntime/emu/aarch64/sysreg.h>
#import <IXLandLinuxRuntime/emu/interrupt.h>
#import <IXLandLinuxRuntime/emu/mmu.h>
#import <IXLandLinuxRuntime/emu/tlb.h>
#import <IXLandLinuxRuntime/kernel/calls.h>
#import <IXLandLinuxRuntime/kernel/guest_trace_context.h>
#import <IXLandLinuxRuntime/kernel/mem_object.h>
#import <IXLandLinuxRuntime/kernel/memory.h>
#import <IXLandLinuxRuntime/kernel/page_map.h>
#import <IXLandLinuxRuntime/kernel/task.h>
#import <IXLandLinuxRuntime/tcti/aarch64/gen.h>
#import <IXLandLinuxRuntime/tcti/frame.h>
#import <IXLandLinuxRuntime/tcti/gadgets_tcti.h>
#include <dlfcn.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// External TCTI entry point (declared in gadgets_tcti.h)
extern void tcti_entry_block(void *gadgets, struct cpu_state *cpu);

// Execution state is now passed via parameters, not globals
static jmp_buf exit_jmpbuf __attribute__((unused));

// Fault containment state for guest execution
static __thread sigjmp_buf guest_fault_jmpbuf;
static __thread volatile int guest_fault_active = 0;
static __thread volatile int guest_fault_signal = 0;
static __thread volatile uintptr_t guest_fault_host_addr = 0;

static bool a64_conservative_mode_enabled(void)
{
    static int initialized = 0;
    static bool enabled = false;
    if (!initialized) {
        const char *value = getenv("ISH_A64_CONSERVATIVE_MODE");
        enabled = value && strcmp(value, "0") != 0;
        initialized = 1;
    }
    return enabled;
}

static bool a64_verbose_block_trace_enabled(void)
{
    static int initialized = 0;
    static bool enabled = false;
    if (!initialized) {
        const char *value = getenv("ISH_A64_VERBOSE_BLOCK_TRACE");
        enabled = value && strcmp(value, "0") != 0;
        initialized = 1;
    }
    return enabled;
}

static bool a64_hot_ldso_pc(uint64_t pc)
{
    return (pc >= 0x79600 && pc < 0x7c600) ||
           (pc >= 0x23570 && pc < 0x23590) ||
           (pc >= 0x2f898 && pc < 0x2f8e0) ||
           (pc >= 0x386a0 && pc < 0x38724) ||
           (pc >= 0xdc700 && pc < 0xdc800) ||
           (pc >= 0x85818 && pc < 0x859e0) ||
           (pc >= 0x8e6c0 && pc < 0x8e940);
}

static int g_insn64_trace_budget = 128;
static int g_guest_first_user_pc_emitted = 0;

static uint64_t trace_ldst_reg_or_zr(struct cpu_state *cpu, int reg);
static uint64_t a64_read_reg_or_sp(struct cpu_state *cpu, int reg, bool is_64bit);
static uint64_t a64_extend_index(uint64_t value, int extend_type);
static bool a64_ldst_uses_register_offset(uint32_t raw, const a64_instr_t *instr);
static const char *trace_ldst_mnemonic(int is_load, int size, int is_signed);

static bool a64_trace_enabled(const char *event_name)
{
    return trace_should_emit_event(event_name);
}

static void a64_trace_event(const char *event_name, const char *format, ...)
{
    if (!a64_trace_enabled(event_name))
        return;

    char event[512];
    va_list args;
    va_start(args, format);
    vsnprintf(event, sizeof(event), format, args);
    va_end(args);

    trace_record_event(TRACE_ORIGIN_EXEC, event);
}

static void a64_trace_block_registers(const char *phase, const struct cpu_state *cpu,
                                      const struct a64_block *block)
{
    if (!phase || !cpu || !block || !a64_verbose_block_trace_enabled() ||
        !trace_should_emit_event("tcti.block.regs"))
        return;

    char event[900];
    snprintf(
        event, sizeof(event),
        "tcti.block.regs=phase:%s,pc:0x%llx,block_start:0x%llx,block_end:0x%llx,"
        "x0:0x%llx,x1:0x%llx,x2:0x%llx,x3:0x%llx,x4:0x%llx,x5:0x%llx,"
        "x6:0x%llx,x7:0x%llx,x8:0x%llx,x9:0x%llx,x10:0x%llx,x11:0x%llx,"
        "x12:0x%llx,x13:0x%llx,x14:0x%llx,x15:0x%llx",
        phase, (unsigned long long)cpu->pc, (unsigned long long)block->start_pc,
        (unsigned long long)block->end_pc, (unsigned long long)cpu->x[0],
        (unsigned long long)cpu->x[1], (unsigned long long)cpu->x[2], (unsigned long long)cpu->x[3],
        (unsigned long long)cpu->x[4], (unsigned long long)cpu->x[5], (unsigned long long)cpu->x[6],
        (unsigned long long)cpu->x[7], (unsigned long long)cpu->x[8], (unsigned long long)cpu->x[9],
        (unsigned long long)cpu->x[10], (unsigned long long)cpu->x[11],
        (unsigned long long)cpu->x[12], (unsigned long long)cpu->x[13],
        (unsigned long long)cpu->x[14], (unsigned long long)cpu->x[15]);
    trace_record_event(TRACE_ORIGIN_EXEC, event);

    snprintf(event, sizeof(event),
             "tcti.block.regs.high=phase:%s,pc:0x%llx,block_start:0x%llx,block_end:0x%llx,"
             "x16:0x%llx,x17:0x%llx,x18:0x%llx,x19:0x%llx,x20:0x%llx,x21:0x%llx,"
             "x22:0x%llx,x23:0x%llx,x24:0x%llx,x25:0x%llx,x26:0x%llx,x27:0x%llx,"
             "x28:0x%llx,x29:0x%llx,x30:0x%llx,sp:0x%llx,pstate:0x%llx,exit:%d",
             phase, (unsigned long long)cpu->pc, (unsigned long long)block->start_pc,
             (unsigned long long)block->end_pc, (unsigned long long)cpu->x[16],
             (unsigned long long)cpu->x[17], (unsigned long long)cpu->x[18],
             (unsigned long long)cpu->x[19], (unsigned long long)cpu->x[20],
             (unsigned long long)cpu->x[21], (unsigned long long)cpu->x[22],
             (unsigned long long)cpu->x[23], (unsigned long long)cpu->x[24],
             (unsigned long long)cpu->x[25], (unsigned long long)cpu->x[26],
             (unsigned long long)cpu->x[27], (unsigned long long)cpu->x[28],
             (unsigned long long)cpu->x[29], (unsigned long long)cpu->x[30],
             (unsigned long long)cpu->sp, (unsigned long long)cpu->pstate, cpu->tcti_exit_reason);
    trace_record_event(TRACE_ORIGIN_EXEC, event);
}

static uint64_t a64_trace_read_reg_or_sp_const(const struct cpu_state *cpu, int reg, bool is_64bit)
{
    if (!cpu)
        return 0;
    if (reg == 31)
        return cpu->sp;
    if (reg < 0 || reg > 30)
        return 0;
    return is_64bit ? cpu->x[reg] : (uint32_t)cpu->x[reg];
}

static uint64_t a64_trace_read_reg_or_zr_const(const struct cpu_state *cpu, int reg)
{
    if (!cpu || reg == 31 || reg < 0 || reg > 30)
        return 0;
    return cpu->x[reg];
}

#define A64_TRACE_BLOCK_HISTORY_SIZE 16

typedef struct {
    uint64_t before_pc;
    uint64_t after_pc;
    uint64_t block_start;
    uint64_t block_end;
    uint64_t branch_target;
    uint64_t rd_value;
    uint64_t rn_value;
    uint64_t rm_value;
    uint64_t sp;
    uint64_t x21;
    uint64_t x26;
    uint64_t x30;
    uint32_t raw;
    int cat;
    int subtype;
    int rd;
    int rn;
    int rm;
    int exit_reason;
    int explicit_pc;
    int branch_reg;
} a64_trace_block_history_entry_t;

static a64_trace_block_history_entry_t g_a64_trace_block_history[A64_TRACE_BLOCK_HISTORY_SIZE];
static unsigned g_a64_trace_block_history_next = 0;
static unsigned g_a64_trace_block_history_count = 0;
static uint64_t g_a64_last_compile_failure_pc = UINT64_MAX;
static unsigned g_a64_compile_failure_repeat_count = 0;

static void a64_trace_record_block_transition(uint64_t before_pc, uint64_t after_pc,
                                              const struct a64_block *block, uint32_t raw,
                                              const a64_instr_t *decoded, int exit_reason,
                                              int branch_reg, uint64_t branch_target,
                                              const struct cpu_state *cpu)
{
    a64_trace_block_history_entry_t *entry =
        &g_a64_trace_block_history[g_a64_trace_block_history_next];

    memset(entry, 0, sizeof(*entry));
    entry->before_pc = before_pc;
    entry->after_pc = after_pc;
    entry->block_start = block ? block->start_pc : 0;
    entry->block_end = block ? block->end_pc : 0;
    entry->raw = raw;
    entry->exit_reason = exit_reason;
    entry->explicit_pc = block && block->explicit_pc_on_exit ? 1 : 0;
    entry->branch_reg = branch_reg;
    entry->branch_target = branch_target;
    entry->sp = cpu ? cpu->sp : 0;
    entry->x21 = cpu ? cpu->x[21] : 0;
    entry->x26 = cpu ? cpu->x[26] : 0;
    entry->x30 = cpu ? cpu->x[30] : 0;

    if (decoded) {
        entry->cat = decoded->cat;
        entry->subtype = decoded->subtype;
        entry->rd = decoded->Rd;
        entry->rn = decoded->Rn;
        entry->rm = decoded->Rm;
        entry->rd_value = a64_trace_read_reg_or_zr_const(cpu, decoded->Rd);
        entry->rn_value = a64_trace_read_reg_or_sp_const(cpu, decoded->Rn, true);
        entry->rm_value = a64_trace_read_reg_or_zr_const(cpu, decoded->Rm);
    } else {
        entry->cat = -1;
        entry->subtype = -1;
        entry->rd = -1;
        entry->rn = -1;
        entry->rm = -1;
    }

    g_a64_trace_block_history_next =
        (g_a64_trace_block_history_next + 1) % A64_TRACE_BLOCK_HISTORY_SIZE;
    if (g_a64_trace_block_history_count < A64_TRACE_BLOCK_HISTORY_SIZE)
        g_a64_trace_block_history_count++;
}

static bool a64_trace_should_emit_compile_failure(uint64_t pc)
{
    if (g_a64_last_compile_failure_pc != pc) {
        g_a64_last_compile_failure_pc = pc;
        g_a64_compile_failure_repeat_count = 0;
        return true;
    }

    g_a64_compile_failure_repeat_count++;
    return g_a64_compile_failure_repeat_count < 4 ||
           (g_a64_compile_failure_repeat_count & (g_a64_compile_failure_repeat_count - 1)) == 0;
}

static void a64_trace_emit_block_history(uint64_t failure_pc)
{
    if (!a64_trace_enabled("tcti.block.history"))
        return;

    for (unsigned i = 0; i < g_a64_trace_block_history_count; i++) {
        unsigned index = (g_a64_trace_block_history_next + A64_TRACE_BLOCK_HISTORY_SIZE -
                          g_a64_trace_block_history_count + i) %
                         A64_TRACE_BLOCK_HISTORY_SIZE;
        const a64_trace_block_history_entry_t *entry = &g_a64_trace_block_history[index];

        a64_trace_event(
            "tcti.block.history",
            "tcti.block.history=failure_pc:0x%llx,slot:%u,before:0x%llx,after:0x%llx,"
            "reason:%d,start:0x%llx,end:0x%llx,explicit:%d,raw:0x%08x,cat:%d,sub:%d,"
            "rd:%d,rn:%d,rm:%d,rd_val:0x%llx,rn_val:0x%llx,rm_val:0x%llx,"
            "branch_reg:%d,branch_target:0x%llx,sp:0x%llx,x21:0x%llx,x26:0x%llx,x30:0x%llx",
            (unsigned long long)failure_pc, i, (unsigned long long)entry->before_pc,
            (unsigned long long)entry->after_pc, entry->exit_reason,
            (unsigned long long)entry->block_start, (unsigned long long)entry->block_end,
            entry->explicit_pc, entry->raw, entry->cat, entry->subtype, entry->rd, entry->rn,
            entry->rm, (unsigned long long)entry->rd_value, (unsigned long long)entry->rn_value,
            (unsigned long long)entry->rm_value, entry->branch_reg,
            (unsigned long long)entry->branch_target, (unsigned long long)entry->sp,
            (unsigned long long)entry->x21, (unsigned long long)entry->x26,
            (unsigned long long)entry->x30);
    }
}

typedef struct {
    int seen;
    uint64_t pc;
    uint32_t raw;
    int cat;
    int subtype;
    int rd;
    int rn;
    int rm;
    int idx_mode;
    int size;
    int is_load;
    int is_signed;
    uint64_t rt_val;
    uint64_t rn_val;
    uint64_t rm_val;
    uint64_t guest_ea;
    uint64_t host_ptr_probe;
    int translation_fault;
    int host_signal;
    uint64_t host_fault_addr;
} guest_first_fault_info_t;

static guest_first_fault_info_t g_guest_first_fault = { 0 };

static void trace_guest_first_fault_capture(struct cpu_state *cpu, int host_signal)
{
    if (!cpu || !cpu->tlb || g_guest_first_fault.seen)
        return;

    uint32_t raw = 0;
    a64_instr_t decoded = { 0 };
    if (a64_fetch_insn(cpu, cpu->tlb, cpu->pc, &raw) != 0 || a64_decode(raw, &decoded) != 0)
        return;

    g_guest_first_fault.seen = 1;
    g_guest_first_fault.pc = cpu->pc;
    g_guest_first_fault.raw = raw;
    g_guest_first_fault.cat = decoded.cat;
    g_guest_first_fault.subtype = decoded.subtype;
    g_guest_first_fault.rd = decoded.Rd;
    g_guest_first_fault.rn = decoded.Rn;
    g_guest_first_fault.rm = decoded.Rm;
    g_guest_first_fault.idx_mode = decoded.idx_mode;
    g_guest_first_fault.size = decoded.size;
    g_guest_first_fault.is_load = bit(raw, 22) ? 1 : 0;
    g_guest_first_fault.is_signed = decoded.is_signed ? 1 : 0;
    g_guest_first_fault.host_signal = host_signal;
    g_guest_first_fault.host_fault_addr = (uint64_t)guest_fault_host_addr;

    if (decoded.Rd >= 0 && decoded.Rd <= 31)
        g_guest_first_fault.rt_val = trace_ldst_reg_or_zr(cpu, decoded.Rd);
    if (decoded.Rn >= 0 && decoded.Rn <= 31)
        g_guest_first_fault.rn_val = a64_read_reg_or_sp(cpu, decoded.Rn, true);
    if (decoded.Rm >= 0 && decoded.Rm <= 31)
        g_guest_first_fault.rm_val = trace_ldst_reg_or_zr(cpu, decoded.Rm);

    if (decoded.cat == A64_LD_ST) {
        if (decoded.subtype == A64_LDST_SINGLE) {
            int is_reg_offset = a64_ldst_uses_register_offset(raw, &decoded) ? 1 : 0;
            if (is_reg_offset && decoded.Rm >= 0 && decoded.Rm <= 31) {
                uint64_t off = a64_extend_index(g_guest_first_fault.rm_val, decoded.extend_type);
                g_guest_first_fault.guest_ea =
                    g_guest_first_fault.rn_val + (off << decoded.imm_shift);
            } else {
                switch (decoded.idx_mode) {
                case A64_PRE_INDEX:
                    g_guest_first_fault.guest_ea = g_guest_first_fault.rn_val + decoded.imm;
                    break;
                case A64_POST_INDEX:
                    g_guest_first_fault.guest_ea = g_guest_first_fault.rn_val;
                    break;
                case A64_INDEX_OFFSET:
                default:
                    g_guest_first_fault.guest_ea = g_guest_first_fault.rn_val + decoded.imm;
                    break;
                }
            }
        } else {
            g_guest_first_fault.guest_ea = cpu->fault_addr;
        }
    } else {
        g_guest_first_fault.guest_ea = cpu->fault_addr;
    }

    void *probe = a64_guest_to_host(cpu, cpu->tlb, g_guest_first_fault.guest_ea,
                                    g_guest_first_fault.is_load ? 0 : 1);
    g_guest_first_fault.host_ptr_probe = (uint64_t)(uintptr_t)probe;
    g_guest_first_fault.translation_fault = probe ? 0 : 1;
}

static const char *trace_guest_mnemonic(const guest_first_fault_info_t *fi)
{
    if (!fi)
        return "unknown";
    if (fi->cat == A64_LD_ST)
        return trace_ldst_mnemonic(fi->is_load, fi->size, fi->is_signed);
    if (fi->cat == A64_BRANCH || fi->cat == A64_BRANCH2)
        return "branch";
    if (fi->cat == A64_DP_IMM || fi->cat == A64_DP_IMM2 || fi->cat == A64_DP_REG ||
        fi->cat == A64_DP_REG2 || fi->cat == A64_DP_REG3 || fi->cat == A64_DP_REG4)
        return "alu";
    if (fi->cat == A64_SIMD || fi->cat == A64_SIMD2 || fi->cat == A64_SIMD0)
        return "simd";
    return "unknown";
}

static void trace_guest_first_fault_events(void)
{
    if (!g_guest_first_fault.seen)
        return;

    char pc_buf[32];
    char raw_buf[32];
    char cat_buf[16];
    char sub_buf[16];
    char rd_buf[16];
    char rn_buf[16];
    char rm_buf[16];
    char idx_buf[16];
    char load_buf[8];
    char ea_buf[32];
    char rt_buf[32];
    char rnv_buf[32];
    char rmv_buf[32];
    char host_ptr_buf[32];
    char host_fault_buf[32];
    char trans_buf[8];
    char host_sig_buf[16];
    const char *mnemonic = trace_guest_mnemonic(&g_guest_first_fault);

    snprintf(pc_buf, sizeof(pc_buf), "0x%llx", (unsigned long long)g_guest_first_fault.pc);
    snprintf(raw_buf, sizeof(raw_buf), "0x%08x", g_guest_first_fault.raw);
    snprintf(cat_buf, sizeof(cat_buf), "%d", g_guest_first_fault.cat);
    snprintf(sub_buf, sizeof(sub_buf), "%d", g_guest_first_fault.subtype);
    snprintf(rd_buf, sizeof(rd_buf), "%d", g_guest_first_fault.rd);
    snprintf(rn_buf, sizeof(rn_buf), "%d", g_guest_first_fault.rn);
    snprintf(rm_buf, sizeof(rm_buf), "%d", g_guest_first_fault.rm);
    snprintf(idx_buf, sizeof(idx_buf), "%d", g_guest_first_fault.idx_mode);
    snprintf(load_buf, sizeof(load_buf), "%d", g_guest_first_fault.is_load);
    snprintf(ea_buf, sizeof(ea_buf), "0x%llx", (unsigned long long)g_guest_first_fault.guest_ea);
    snprintf(rt_buf, sizeof(rt_buf), "0x%llx", (unsigned long long)g_guest_first_fault.rt_val);
    snprintf(rnv_buf, sizeof(rnv_buf), "0x%llx", (unsigned long long)g_guest_first_fault.rn_val);
    snprintf(rmv_buf, sizeof(rmv_buf), "0x%llx", (unsigned long long)g_guest_first_fault.rm_val);
    snprintf(host_ptr_buf, sizeof(host_ptr_buf), "0x%llx",
             (unsigned long long)g_guest_first_fault.host_ptr_probe);
    snprintf(host_fault_buf, sizeof(host_fault_buf), "0x%llx",
             (unsigned long long)g_guest_first_fault.host_fault_addr);
    snprintf(trans_buf, sizeof(trans_buf), "%d", g_guest_first_fault.translation_fault);
    snprintf(host_sig_buf, sizeof(host_sig_buf), "%d", g_guest_first_fault.host_signal);

    ixland_instrumentation_attribute_t pc_attrs[] = {
        { .key = "guest_pc", .value = pc_buf },
    };
    ixland_guest_trace_emit_attrs(IXLAND_INSTRUMENTATION_ORIGIN_EMULATOR, "guest.first_fault.pc",
                                  pc_attrs, sizeof(pc_attrs) / sizeof(pc_attrs[0]));

    ixland_instrumentation_attribute_t decode_attrs[] = {
        { .key = "guest_pc", .value = pc_buf },   { .key = "raw_opcode", .value = raw_buf },
        { .key = "mnemonic", .value = mnemonic }, { .key = "cat", .value = cat_buf },
        { .key = "subtype", .value = sub_buf },   { .key = "rd", .value = rd_buf },
        { .key = "rn", .value = rn_buf },         { .key = "rm", .value = rm_buf },
        { .key = "idx_mode", .value = idx_buf },  { .key = "is_load", .value = load_buf },
    };
    ixland_guest_trace_emit_attrs(IXLAND_INSTRUMENTATION_ORIGIN_EMULATOR,
                                  "guest.first_fault.decode", decode_attrs,
                                  sizeof(decode_attrs) / sizeof(decode_attrs[0]));

    ixland_instrumentation_attribute_t regs_attrs[] = {
        { .key = "rt_val", .value = rt_buf },
        { .key = "rn_val", .value = rnv_buf },
        { .key = "rm_val", .value = rmv_buf },
    };
    ixland_guest_trace_emit_attrs(IXLAND_INSTRUMENTATION_ORIGIN_EMULATOR, "guest.first_fault.regs",
                                  regs_attrs, sizeof(regs_attrs) / sizeof(regs_attrs[0]));

    ixland_instrumentation_attribute_t trans_attrs[] = {
        { .key = "guest_ea", .value = ea_buf },
        { .key = "host_ptr_probe", .value = host_ptr_buf },
        { .key = "host_fault_addr", .value = host_fault_buf },
        { .key = "translation_fault", .value = trans_buf },
    };
    ixland_guest_trace_emit_attrs(IXLAND_INSTRUMENTATION_ORIGIN_EMULATOR,
                                  "guest.first_fault.translation", trans_attrs,
                                  sizeof(trans_attrs) / sizeof(trans_attrs[0]));

    ixland_instrumentation_attribute_t exit_attrs[] = {
        { .key = "host_signal", .value = host_sig_buf },
        { .key = "host_fault_addr", .value = host_fault_buf },
        { .key = "translation_fault", .value = trans_buf },
    };
    ixland_guest_trace_emit_attrs(IXLAND_INSTRUMENTATION_ORIGIN_EMULATOR, "guest.first_fault.exit",
                                  exit_attrs, sizeof(exit_attrs) / sizeof(exit_attrs[0]));
}

static void __attribute__((unused)) trace_insn64_event(const char *name, const char *payload)
{
    if (!name || !payload || g_insn64_trace_budget <= 0)
        return;

    ixland_instrumentation_attribute_t attrs[] = {
        { .key = "payload", .value = payload },
    };
    ixland_guest_trace_emit_attrs(IXLAND_INSTRUMENTATION_ORIGIN_EMULATOR, name, attrs,
                                  sizeof(attrs) / sizeof(attrs[0]));
    g_insn64_trace_budget--;
}

#define TRACE_FIELD_STR(key_, value_)                                                              \
    { .key = (key_), .kind = IXLAND_GUEST_TRACE_FIELD_STRING, .string_value = (value_) }
#define TRACE_FIELD_I64(key_, value_)                                                              \
    { .key = (key_), .kind = IXLAND_GUEST_TRACE_FIELD_I64_DEC, .i64_value = (int64_t)(value_) }
#define TRACE_FIELD_U64_DEC(key_, value_)                                                          \
    { .key = (key_), .kind = IXLAND_GUEST_TRACE_FIELD_U64_DEC, .u64_value = (uint64_t)(value_) }
#define TRACE_FIELD_U64_HEX(key_, value_)                                                          \
    { .key = (key_), .kind = IXLAND_GUEST_TRACE_FIELD_U64_HEX, .u64_value = (uint64_t)(value_) }

#define A64_TRACE_MEM_HISTORY_SIZE 2048

typedef struct {
    uint64_t pc;
    uint64_t addr;
    uint64_t value;
    uint64_t rn_value;
    uint64_t rm_value;
    uint32_t raw;
    uint16_t sequence;
    uint8_t width;
    uint8_t is_load;
    int rt;
    int rn;
    int rm;
    int idx_mode;
} a64_trace_mem_history_entry_t;

static a64_trace_mem_history_entry_t g_a64_trace_mem_history[A64_TRACE_MEM_HISTORY_SIZE];
static uint16_t g_a64_trace_mem_history_next = 0;
static uint16_t g_a64_trace_mem_history_count = 0;
static uint16_t g_a64_trace_mem_history_sequence = 0;

static void a64_trace_record_mem_access(const struct cpu_state *cpu, const a64_instr_t *instr,
                                        uint64_t addr, uint64_t value, uint8_t width, bool is_load)
{
    if (!cpu || !instr)
        return;

    a64_trace_mem_history_entry_t *entry = &g_a64_trace_mem_history[g_a64_trace_mem_history_next];
    memset(entry, 0, sizeof(*entry));
    entry->pc = cpu->pc;
    entry->addr = addr;
    entry->value = value;
    entry->raw = instr->raw;
    entry->sequence = g_a64_trace_mem_history_sequence++;
    entry->width = width;
    entry->is_load = is_load ? 1 : 0;
    entry->rt = instr->Rd;
    entry->rn = instr->Rn;
    entry->rm = instr->Rm;
    entry->idx_mode = instr->idx_mode;
    entry->rn_value = a64_trace_read_reg_or_sp_const(cpu, instr->Rn, true);
    entry->rm_value = a64_trace_read_reg_or_zr_const(cpu, instr->Rm);

    g_a64_trace_mem_history_next =
        (uint16_t)((g_a64_trace_mem_history_next + 1) % A64_TRACE_MEM_HISTORY_SIZE);
    if (g_a64_trace_mem_history_count < A64_TRACE_MEM_HISTORY_SIZE)
        g_a64_trace_mem_history_count++;
}

static void a64_trace_emit_mem_history_entry(const char *event_name, uint32_t slot,
                                             const a64_trace_mem_history_entry_t *entry,
                                             int value_match_reg)
{
    if (!event_name || !entry)
        return;

    trace_field_t fields[] = {
        { .key = "slot", .kind = TRACE_FIELD_U64_DEC, .u64_value = slot },
        { .key = "sequence", .kind = TRACE_FIELD_U64_DEC, .u64_value = entry->sequence },
        { .key = "guest_pc", .kind = TRACE_FIELD_U64_HEX, .u64_value = entry->pc },
        { .key = "raw_opcode", .kind = TRACE_FIELD_U64_HEX, .u64_value = entry->raw },
        { .key = "addr", .kind = TRACE_FIELD_U64_HEX, .u64_value = entry->addr },
        { .key = "value", .kind = TRACE_FIELD_U64_HEX, .u64_value = entry->value },
        { .key = "width", .kind = TRACE_FIELD_U64_DEC, .u64_value = entry->width },
        { .key = "is_load", .kind = TRACE_FIELD_I64_DEC, .i64_value = entry->is_load },
        { .key = "rt", .kind = TRACE_FIELD_I64_DEC, .i64_value = entry->rt },
        { .key = "rn", .kind = TRACE_FIELD_I64_DEC, .i64_value = entry->rn },
        { .key = "rm", .kind = TRACE_FIELD_I64_DEC, .i64_value = entry->rm },
        { .key = "idx_mode", .kind = TRACE_FIELD_I64_DEC, .i64_value = entry->idx_mode },
        { .key = "rn_value", .kind = TRACE_FIELD_U64_HEX, .u64_value = entry->rn_value },
        { .key = "rm_value", .kind = TRACE_FIELD_U64_HEX, .u64_value = entry->rm_value },
        { .key = "value_match_reg", .kind = TRACE_FIELD_I64_DEC, .i64_value = value_match_reg },
    };
    trace_record_event_fields(TRACE_ORIGIN_EMULATOR, event_name, fields,
                              sizeof(fields) / sizeof(fields[0]));
}

static int a64_trace_find_matching_reg_value(const struct cpu_state *cpu, uint64_t value)
{
    if (!cpu || value == 0)
        return -1;

    for (int reg = 0; reg <= 30; reg++) {
        if (cpu->x[reg] == value)
            return reg;
    }
    if (cpu->sp == value)
        return 31;
    return -1;
}

static void a64_trace_emit_mem_history_on_fault(const struct cpu_state *cpu, uint64_t fault_addr)
{
    if (!trace_should_emit_event("tcti.mem.history"))
        return;

    const uint32_t recent_limit = 96;
    uint32_t emitted_recent = 0;
    uint32_t emitted_matches = 0;

    for (uint32_t i = 0; i < g_a64_trace_mem_history_count; i++) {
        uint32_t index = (uint32_t)((g_a64_trace_mem_history_next + A64_TRACE_MEM_HISTORY_SIZE -
                                     g_a64_trace_mem_history_count + i) %
                                    A64_TRACE_MEM_HISTORY_SIZE);
        const a64_trace_mem_history_entry_t *entry = &g_a64_trace_mem_history[index];
        uint32_t remaining = g_a64_trace_mem_history_count - i;
        int value_match_reg = a64_trace_find_matching_reg_value(cpu, entry->value);
        bool addr_match = entry->addr == fault_addr;

        if ((addr_match || value_match_reg >= 0) && emitted_matches < 96) {
            a64_trace_emit_mem_history_entry("tcti.mem.history.match", i, entry, value_match_reg);
            emitted_matches++;
        }

        if (remaining <= recent_limit && emitted_recent < recent_limit) {
            a64_trace_emit_mem_history_entry("tcti.mem.history.recent", emitted_recent, entry,
                                             value_match_reg);
            emitted_recent++;
        }
    }
}

static void trace_insn64_event_fields(const char *name, const ixland_guest_trace_field_t *fields,
                                      uint32_t field_count)
{
    if (!name || !fields || field_count == 0 || g_insn64_trace_budget <= 0)
        return;

    ixland_guest_trace_emit_structured(IXLAND_INSTRUMENTATION_ORIGIN_EMULATOR, name, fields,
                                       field_count);
    g_insn64_trace_budget--;
}

static void trace_block_bytecode(struct a64_block *block)
{
    if (!block || !block->gadgets || !trace_should_emit_event("tcti.block.bytecode"))
        return;

    ixland_guest_trace_field_t start_fields[] = {
        TRACE_FIELD_U64_HEX("guest_pc", block->start_pc),
        TRACE_FIELD_U64_HEX("block_start", block->start_pc),
        TRACE_FIELD_U64_HEX("block_end", block->end_pc),
        TRACE_FIELD_U64_DEC("num_gadgets", block->num_gadgets),
    };
    trace_insn64_event_fields("tcti.block.bytecode.start", start_fields,
                              sizeof(start_fields) / sizeof(start_fields[0]));

    size_t max_items = block->num_gadgets < 16 ? block->num_gadgets : 16;
    for (size_t i = 0; i < max_items; i++) {
        void *entry = (void *)block->gadgets[i];
        Dl_info info;
        const char *symbol = "unknown";
        if (entry && dladdr(entry, &info) != 0 && info.dli_sname)
            symbol = info.dli_sname;

        if (strstr(symbol, "gadget_mov_imm") != NULL && (i + 1) < block->num_gadgets) {
            uint64_t imm = (uint64_t)(uintptr_t)block->gadgets[i + 1];
            ixland_guest_trace_field_t imm_fields[] = {
                TRACE_FIELD_U64_HEX("guest_pc", block->start_pc),
                TRACE_FIELD_U64_HEX("block_start", block->start_pc),
                TRACE_FIELD_U64_DEC("index", i),
                TRACE_FIELD_STR("symbol", symbol),
                TRACE_FIELD_U64_HEX("inline_imm", imm),
            };
            trace_insn64_event_fields("tcti.block.bytecode.imm", imm_fields,
                                      sizeof(imm_fields) / sizeof(imm_fields[0]));
        } else {
            ixland_guest_trace_field_t gadget_fields[] = {
                TRACE_FIELD_U64_HEX("guest_pc", block->start_pc),
                TRACE_FIELD_U64_HEX("block_start", block->start_pc),
                TRACE_FIELD_U64_DEC("index", i),
                TRACE_FIELD_STR("symbol", symbol),
                TRACE_FIELD_U64_HEX("addr", (uint64_t)(uintptr_t)entry),
            };
            trace_insn64_event_fields("tcti.block.bytecode.gadget", gadget_fields,
                                      sizeof(gadget_fields) / sizeof(gadget_fields[0]));
        }
    }

    ixland_guest_trace_field_t end_fields[] = {
        TRACE_FIELD_U64_HEX("guest_pc", block->start_pc),
        TRACE_FIELD_U64_HEX("block_start", block->start_pc),
        TRACE_FIELD_U64_HEX("block_end", block->end_pc),
        TRACE_FIELD_U64_DEC("emitted_items", max_items),
    };
    trace_insn64_event_fields("tcti.block.bytecode.end", end_fields,
                              sizeof(end_fields) / sizeof(end_fields[0]));
}

static bool a64_ldst_uses_register_offset(uint32_t raw, const a64_instr_t *instr)
{
    if (!instr || instr->subtype != A64_LDST_SINGLE)
        return false;

    return bit(raw, 24) == 0 && bits(raw, 11, 10) == 2;
}

static const char *trace_page0_obj_kind_name(enum mem_object_kind kind)
{
    switch (kind) {
    case MEM_OBJ_RAM:
        return "ram";
    case MEM_OBJ_FILE:
        return "file";
    case MEM_OBJ_VDSO:
        return "vdso";
    case MEM_OBJ_SPECIAL:
        return "special";
    default:
        return "unknown";
    }
}

static void trace_mm_page0_state_runtime(const char *name, struct cpu_state *cpu, struct tlb *tlb,
                                         addr_t guest_ea)
{
    if (!name || !cpu || !tlb)
        return;

    struct page_desc *desc = NULL;
    struct mem_object *obj = NULL;
    void *host_ptr_from_map = NULL;
    void *host_ptr_probe = a64_guest_to_host(cpu, tlb, guest_ea, 0);

    if (current && current->mem) {
        desc = page_map_lookup(&current->mem->pages, PAGE(guest_ea));
        if (desc)
            obj = desc->obj;
    }

    if (obj && obj->host_base != NULL && desc) {
        size_t host_off = desc->offset + PGOFFSET(guest_ea);
        if (host_off < obj->host_size)
            host_ptr_from_map = (void *)((uint8_t *)obj->host_base + host_off);
    }

    char ev[768];
    snprintf(
        ev, sizeof(ev),
        "%s=mapped:%s,guest_page:0x%llx,guest_ea:0x%llx,backing:%s,host_ptr_probe:%p,"
        "host_ptr_map:%p,guest_range:[0x%llx..0x%llx],task:%p,mm:%p,mem:%p,cpu:%p,tlb:%p,"
        "page_desc:%p,obj:%p,cpu_mmu:%p,tlb_mmu:%p,cpu_mmu_gen:%llu,tlb_mmu_gen:%llu,"
        "page_desc_off:0x%zx,page_flags:0x%x",
        name, desc ? "yes" : "no", (unsigned long long)PAGE(guest_ea), (unsigned long long)guest_ea,
        (obj && obj->name) ? obj->name : (obj ? trace_page0_obj_kind_name(obj->kind) : "none"),
        host_ptr_probe, host_ptr_from_map, (unsigned long long)(PAGE(guest_ea) << PAGE_BITS),
        (unsigned long long)(((PAGE(guest_ea) + 1) << PAGE_BITS) - 1), (void *)current,
        current ? (void *)current->mm : NULL, current ? (void *)current->mem : NULL, (void *)cpu,
        (void *)tlb, (void *)desc, (void *)obj, (void *)cpu->mmu, tlb ? (void *)tlb->mmu : NULL,
        (unsigned long long)(cpu->mmu ? cpu->mmu->generation : 0),
        (unsigned long long)(tlb && tlb->mmu ? tlb->mmu->generation : 0), desc ? desc->offset : 0,
        desc ? desc->flags : 0);
    trace_record_event(TRACE_ORIGIN_EXEC, ev);
}

// Signal handler that converts host signal to controlled exit
static void guest_fault_handler(int sig, siginfo_t *info, void *context)
{
    (void)context;
    guest_fault_signal = sig;
    guest_fault_host_addr = info ? (uintptr_t)info->si_addr : 0;
    if (guest_fault_active)
        siglongjmp(guest_fault_jmpbuf, 1);
}

// Forward declarations
struct a64_block *a64_compile_block(struct cpu_state *cpu, uint64_t pc, struct tlb *tlb);
int a64_execute_block(struct cpu_state *cpu, struct a64_block *block);

// Load/store helper functions used by execute_ldst
static uint64_t a64_read_reg_or_sp(struct cpu_state *cpu, int reg, bool is_64bit);
static void a64_write_reg_or_sp(struct cpu_state *cpu, int reg, uint64_t value, bool is_64bit);
static uint64_t a64_extend_index(uint64_t value, int extend_type);
static void trace_first_live_ldst_fault(struct cpu_state *cpu, int host_signal);

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

static void __attribute__((unused)) trace_cpu_layout_checkpoint(const char *name, struct task *task,
                                                                struct cpu_state *cpu, int err)
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
    if (!a64_verbose_block_trace_enabled() || !trace_should_emit_event(name))
        return;

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

static void trace_fault_origin_checkpoint(const char *name, uint64_t fault_pc, int base_reg,
                                          uint64_t base_reg_value, int index_reg,
                                          uint64_t index_reg_value, int64_t imm_offset,
                                          uint64_t computed_addr)
{
    char fault_pc_buf[32];
    char base_reg_buf[32];
    char base_reg_value_buf[32];
    char index_reg_buf[32];
    char index_reg_value_buf[32];
    char imm_buf[32];
    char computed_buf[32];

    snprintf(fault_pc_buf, sizeof(fault_pc_buf), "0x%llx", (unsigned long long)fault_pc);
    snprintf(base_reg_buf, sizeof(base_reg_buf), "%d", base_reg);
    snprintf(base_reg_value_buf, sizeof(base_reg_value_buf), "0x%llx",
             (unsigned long long)base_reg_value);
    snprintf(index_reg_buf, sizeof(index_reg_buf), "%d", index_reg);
    snprintf(index_reg_value_buf, sizeof(index_reg_value_buf), "0x%llx",
             (unsigned long long)index_reg_value);
    snprintf(imm_buf, sizeof(imm_buf), "%lld", (long long)imm_offset);
    snprintf(computed_buf, sizeof(computed_buf), "0x%llx", (unsigned long long)computed_addr);

    trace_attribute_t attrs[] = {
        { "fault_pc", fault_pc_buf },
        { "base_reg", base_reg_buf },
        { "base_reg_value", base_reg_value_buf },
        { "index_reg", index_reg_buf },
        { "index_reg_value", index_reg_value_buf },
        { "imm_offset", imm_buf },
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
    trace_cpu_init_checkpoint("task.proof.a64_cpu_init_probe.entry", task, cpu, err);
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

    a64_trace_event("tcti.compile.entry",
                    "tcti.compile.entry=pc:0x%llx,tlb:%d,mmu_gen:%llu,fault:0x%llx",
                    (unsigned long long)pc, tlb ? 1 : 0,
                    (tlb && tlb->mmu) ? (unsigned long long)tlb->mmu->generation : 0ULL,
                    cpu ? (unsigned long long)cpu->fault_addr : 0ULL);

    // Trace: Block compilation start
    trace_emit_block_compile_start(pc);

    // Create sidecar if tracing is active at level >= BLOCK
    trace_block_sidecar_t *sidecar = NULL;
    if (trace_sidecar_enabled()) {
        sidecar = trace_sidecar_create(pc, pc);
    }

    int ret = a64_gen_init(&gen_state, buffer, A64_MAX_GADGETS_PER_BLOCK);
    if (ret != A64_GEN_OK) {
        a64_trace_event("tcti.compile.init_fail", "tcti.compile.init_fail=pc:0x%llx,ret:%d",
                        (unsigned long long)pc, ret);
        return NULL;
    }
    a64_gen_reset(&gen_state, pc);

    bool conservative_mode = a64_conservative_mode_enabled();
    a64_gen_set_conservative_mode(&gen_state, conservative_mode ? 1 : 0);

    // Translate instructions until block end
    int max_insns = conservative_mode ? 1 : 50;
    int insns_decoded = 0;
    for (int i = 0; i < max_insns; i++) {
        uint32_t insn;
        int ret = a64_fetch_insn(cpu, tlb, gen_state.guest_pc, &insn);
        if (ret < 0) {
            void *direct = NULL;
            if (current && current->mem) {
                read_wrlock(&current->mem->lock);
                direct = mem_ptr(current->mem, gen_state.guest_pc, MEM_READ);
                read_wrunlock(&current->mem->lock);
            }
            a64_trace_event("tcti.compile.fetch_fail",
                            "tcti.compile.fetch_fail=start:0x%llx,pc:0x%llx,ret:%d,"
                            "fault:0x%llx,was_write:%d,tlb_gen:%llu,mmu_gen:%llu,direct:%d",
                            (unsigned long long)pc, (unsigned long long)gen_state.guest_pc, ret,
                            (unsigned long long)cpu->fault_addr, cpu->fault_was_write ? 1 : 0,
                            tlb ? (unsigned long long)tlb->generation : 0ULL,
                            (tlb && tlb->mmu) ? (unsigned long long)tlb->mmu->generation : 0ULL,
                            direct ? 1 : 0);
            // Page fault during fetch
            break;
        }

        // Decode to get instruction info
        a64_instr_t decoded_info;
        int decode_ret = a64_decode(insn, &decoded_info);
        (void)decode_ret;

        if (a64_hot_ldso_pc(gen_state.guest_pc)) {
            char event[512];
            if (gen_state.guest_pc >= 0x7a2bc && gen_state.guest_pc <= 0x7a2d8) {
                snprintf(event, sizeof(event),
                         "hot.ldso=pc:0x%llx,raw:0x%08x,cat:%d,sub:%d,rd:%d,rn:%d,rm:%d,imm:%lld,"
                         "x0:0x%llx,x1:0x%llx,x2:0x%llx,x17:0x%llx,x25:0x%llx,pstate:0x%llx",
                         (unsigned long long)gen_state.guest_pc, insn,
                         decode_ret == 0 ? decoded_info.cat : -1,
                         decode_ret == 0 ? decoded_info.subtype : -1,
                         decode_ret == 0 ? decoded_info.Rd : -1,
                         decode_ret == 0 ? decoded_info.Rn : -1,
                         decode_ret == 0 ? decoded_info.Rm : -1,
                         decode_ret == 0 ? (long long)decoded_info.imm : 0LL,
                         (unsigned long long)cpu->x[0], (unsigned long long)cpu->x[1],
                         (unsigned long long)cpu->x[2], (unsigned long long)cpu->x[17],
                         (unsigned long long)cpu->x[25], (unsigned long long)cpu->pstate);
            } else if (gen_state.guest_pc >= 0x23570 && gen_state.guest_pc <= 0x2358c) {
                uint64_t malloc_slot = 0;
                uint64_t calloc_slot = 0;
                uint32_t malloc_target_raw = 0;
                uint32_t calloc_target_raw = 0;
                bool have_malloc_slot =
                    a64_guest_read64(cpu, tlb, 0xcff30, &malloc_slot) == A64_MEM_OK;
                bool have_calloc_slot =
                    a64_guest_read64(cpu, tlb, 0xcff38, &calloc_slot) == A64_MEM_OK;
                bool have_malloc_target_raw = have_malloc_slot && malloc_slot != 0 &&
                    a64_guest_read32(cpu, tlb, malloc_slot, &malloc_target_raw) == A64_MEM_OK;
                bool have_calloc_target_raw = have_calloc_slot && calloc_slot != 0 &&
                    a64_guest_read32(cpu, tlb, calloc_slot, &calloc_target_raw) == A64_MEM_OK;
                snprintf(event, sizeof(event),
                         "hot.ldso=pc:0x%llx,raw:0x%08x,cat:%d,sub:%d,rd:%d,rn:%d,rm:%d,imm:%lld,"
                         "x0:0x%llx,x1:0x%llx,x16:0x%llx,x17:0x%llx,malloc_slot:0x%llx,"
                         "malloc_raw:0x%08x,calloc_slot:0x%llx,calloc_raw:0x%08x,sp:0x%llx,"
                         "pstate:0x%llx",
                         (unsigned long long)gen_state.guest_pc, insn,
                         decode_ret == 0 ? decoded_info.cat : -1,
                         decode_ret == 0 ? decoded_info.subtype : -1,
                         decode_ret == 0 ? decoded_info.Rd : -1,
                         decode_ret == 0 ? decoded_info.Rn : -1,
                         decode_ret == 0 ? decoded_info.Rm : -1,
                         decode_ret == 0 ? (long long)decoded_info.imm : 0LL,
                         (unsigned long long)cpu->x[0], (unsigned long long)cpu->x[1],
                         (unsigned long long)cpu->x[16], (unsigned long long)cpu->x[17],
                         (unsigned long long)(have_malloc_slot ? malloc_slot : 0),
                         have_malloc_target_raw ? malloc_target_raw : 0U,
                         (unsigned long long)(have_calloc_slot ? calloc_slot : 0),
                         have_calloc_target_raw ? calloc_target_raw : 0U,
                         (unsigned long long)cpu->sp, (unsigned long long)cpu->pstate);
            } else if (gen_state.guest_pc >= 0x2f898 && gen_state.guest_pc <= 0x2f8dc) {
                snprintf(event, sizeof(event),
                         "hot.ldso=pc:0x%llx,raw:0x%08x,cat:%d,sub:%d,rd:%d,rn:%d,rm:%d,imm:%lld,"
                         "x0:0x%llx,x1:0x%llx,x2:0x%llx,x29:0x%llx,sp:0x%llx,pstate:0x%llx",
                         (unsigned long long)gen_state.guest_pc, insn,
                         decode_ret == 0 ? decoded_info.cat : -1,
                         decode_ret == 0 ? decoded_info.subtype : -1,
                         decode_ret == 0 ? decoded_info.Rd : -1,
                         decode_ret == 0 ? decoded_info.Rn : -1,
                         decode_ret == 0 ? decoded_info.Rm : -1,
                         decode_ret == 0 ? (long long)decoded_info.imm : 0LL,
                         (unsigned long long)cpu->x[0], (unsigned long long)cpu->x[1],
                         (unsigned long long)cpu->x[2], (unsigned long long)cpu->x[29],
                         (unsigned long long)cpu->sp, (unsigned long long)cpu->pstate);
            } else if (gen_state.guest_pc >= 0x386a0 && gen_state.guest_pc <= 0x38720) {
                snprintf(event, sizeof(event),
                         "hot.ldso=pc:0x%llx,raw:0x%08x,cat:%d,sub:%d,rd:%d,rn:%d,rm:%d,imm:%lld,"
                         "x0:0x%llx,x1:0x%llx,x2:0x%llx,x19:0x%llx,x20:0x%llx,x21:0x%llx,"
                         "x29:0x%llx,sp:0x%llx,pstate:0x%llx",
                         (unsigned long long)gen_state.guest_pc, insn,
                         decode_ret == 0 ? decoded_info.cat : -1,
                         decode_ret == 0 ? decoded_info.subtype : -1,
                         decode_ret == 0 ? decoded_info.Rd : -1,
                         decode_ret == 0 ? decoded_info.Rn : -1,
                         decode_ret == 0 ? decoded_info.Rm : -1,
                         decode_ret == 0 ? (long long)decoded_info.imm : 0LL,
                         (unsigned long long)cpu->x[0], (unsigned long long)cpu->x[1],
                         (unsigned long long)cpu->x[2], (unsigned long long)cpu->x[19],
                         (unsigned long long)cpu->x[20], (unsigned long long)cpu->x[21],
                         (unsigned long long)cpu->x[29], (unsigned long long)cpu->sp,
                         (unsigned long long)cpu->pstate);
            } else if (gen_state.guest_pc >= 0x7a1c0 && gen_state.guest_pc <= 0x7a1e4) {
                snprintf(event, sizeof(event),
                         "hot.ldso=pc:0x%llx,raw:0x%08x,cat:%d,sub:%d,rd:%d,rn:%d,rm:%d,imm:%lld,"
                         "x0:0x%llx,x1:0x%llx,x7:0x%llx,x14:0x%llx,x20:0x%llx,x21:0x%llx,"
                         "x25:0x%llx,x28:0x%llx,sp:0x%llx,pstate:0x%llx",
                         (unsigned long long)gen_state.guest_pc, insn,
                         decode_ret == 0 ? decoded_info.cat : -1,
                         decode_ret == 0 ? decoded_info.subtype : -1,
                         decode_ret == 0 ? decoded_info.Rd : -1,
                         decode_ret == 0 ? decoded_info.Rn : -1,
                         decode_ret == 0 ? decoded_info.Rm : -1,
                         decode_ret == 0 ? (long long)decoded_info.imm : 0LL,
                         (unsigned long long)cpu->x[0], (unsigned long long)cpu->x[1],
                         (unsigned long long)cpu->x[7], (unsigned long long)cpu->x[14],
                         (unsigned long long)cpu->x[20], (unsigned long long)cpu->x[21],
                         (unsigned long long)cpu->x[25], (unsigned long long)cpu->x[28],
                         (unsigned long long)cpu->sp, (unsigned long long)cpu->pstate);
            } else if (gen_state.guest_pc >= 0x7a1f4 && gen_state.guest_pc <= 0x7a220) {
                snprintf(event, sizeof(event),
                         "hot.ldso=pc:0x%llx,raw:0x%08x,cat:%d,sub:%d,rd:%d,rn:%d,rm:%d,imm:%lld,"
                         "x0:0x%llx,x1:0x%llx,x2:0x%llx,x14:0x%llx,x18:0x%llx,x25:0x%llx,"
                         "x26:0x%llx,x28:0x%llx,sp:0x%llx,pstate:0x%llx",
                         (unsigned long long)gen_state.guest_pc, insn,
                         decode_ret == 0 ? decoded_info.cat : -1,
                         decode_ret == 0 ? decoded_info.subtype : -1,
                         decode_ret == 0 ? decoded_info.Rd : -1,
                         decode_ret == 0 ? decoded_info.Rn : -1,
                         decode_ret == 0 ? decoded_info.Rm : -1,
                         decode_ret == 0 ? (long long)decoded_info.imm : 0LL,
                         (unsigned long long)cpu->x[0], (unsigned long long)cpu->x[1],
                         (unsigned long long)cpu->x[2], (unsigned long long)cpu->x[14],
                         (unsigned long long)cpu->x[18], (unsigned long long)cpu->x[25],
                         (unsigned long long)cpu->x[26], (unsigned long long)cpu->x[28],
                         (unsigned long long)cpu->sp, (unsigned long long)cpu->pstate);
            } else if (gen_state.guest_pc >= 0x7a27c && gen_state.guest_pc <= 0x7a29c) {
                uint64_t symtab = 0;
                uint64_t hashtab = 0;
                uint64_t ghashtab = 0;
                uint64_t strtab = 0;
                if (gen_state.guest_pc == 0x7a29c && cpu->x[0] != 0) {
                    (void)a64_guest_read64(cpu, tlb, cpu->x[0] + 0x40, &symtab);
                    (void)a64_guest_read64(cpu, tlb, cpu->x[0] + 0x50, &ghashtab);
                    (void)a64_guest_read64(cpu, tlb, cpu->x[0] + 0x58, &hashtab);
                    (void)a64_guest_read64(cpu, tlb, cpu->x[0] + 0x60, &strtab);
                }
                snprintf(event, sizeof(event),
                         "hot.ldso=pc:0x%llx,raw:0x%08x,cat:%d,sub:%d,rd:%d,rn:%d,rm:%d,imm:%lld,"
                         "x0:0x%llx,x1:0x%llx,x2:0x%llx,x14:0x%llx,x18:0x%llx,x25:0x%llx,"
                         "x26:0x%llx,x28:0x%llx,symtab:0x%llx,ghashtab:0x%llx,hashtab:0x%llx,"
                         "strtab:0x%llx,sp:0x%llx,pstate:0x%llx",
                         (unsigned long long)gen_state.guest_pc, insn,
                         decode_ret == 0 ? decoded_info.cat : -1,
                         decode_ret == 0 ? decoded_info.subtype : -1,
                         decode_ret == 0 ? decoded_info.Rd : -1,
                         decode_ret == 0 ? decoded_info.Rn : -1,
                         decode_ret == 0 ? decoded_info.Rm : -1,
                         decode_ret == 0 ? (long long)decoded_info.imm : 0LL,
                         (unsigned long long)cpu->x[0], (unsigned long long)cpu->x[1],
                         (unsigned long long)cpu->x[2], (unsigned long long)cpu->x[14],
                         (unsigned long long)cpu->x[18], (unsigned long long)cpu->x[25],
                         (unsigned long long)cpu->x[26], (unsigned long long)cpu->x[28],
                         (unsigned long long)symtab, (unsigned long long)ghashtab,
                         (unsigned long long)hashtab, (unsigned long long)strtab,
                         (unsigned long long)cpu->sp, (unsigned long long)cpu->pstate);
            } else if (gen_state.guest_pc >= 0x7a3fc && gen_state.guest_pc <= 0x7a408) {
                snprintf(event, sizeof(event),
                         "hot.ldso=pc:0x%llx,raw:0x%08x,cat:%d,sub:%d,rd:%d,rn:%d,rm:%d,imm:%lld,"
                         "x0:0x%llx,x14:0x%llx,x15:0x%llx,pstate:0x%llx",
                         (unsigned long long)gen_state.guest_pc, insn,
                         decode_ret == 0 ? decoded_info.cat : -1,
                         decode_ret == 0 ? decoded_info.subtype : -1,
                         decode_ret == 0 ? decoded_info.Rd : -1,
                         decode_ret == 0 ? decoded_info.Rn : -1,
                         decode_ret == 0 ? decoded_info.Rm : -1,
                         decode_ret == 0 ? (long long)decoded_info.imm : 0LL,
                         (unsigned long long)cpu->x[0], (unsigned long long)cpu->x[14],
                         (unsigned long long)cpu->x[15], (unsigned long long)cpu->pstate);
            } else if (gen_state.guest_pc >= 0x79f28 && gen_state.guest_pc <= 0x79ffc) {
                snprintf(event, sizeof(event),
                         "hot.ldso=pc:0x%llx,raw:0x%08x,cat:%d,sub:%d,rd:%d,rn:%d,rm:%d,imm:%lld,"
                         "x0:0x%llx,x1:0x%llx,x2:0x%llx,x3:0x%llx,x13:0x%llx,x15:0x%llx,"
                         "x18:0x%llx,x19:0x%llx,x21:0x%llx,sp:0x%llx,pstate:0x%llx",
                         (unsigned long long)gen_state.guest_pc, insn,
                         decode_ret == 0 ? decoded_info.cat : -1,
                         decode_ret == 0 ? decoded_info.subtype : -1,
                         decode_ret == 0 ? decoded_info.Rd : -1,
                         decode_ret == 0 ? decoded_info.Rn : -1,
                         decode_ret == 0 ? decoded_info.Rm : -1,
                         decode_ret == 0 ? (long long)decoded_info.imm : 0LL,
                         (unsigned long long)cpu->x[0], (unsigned long long)cpu->x[1],
                         (unsigned long long)cpu->x[2], (unsigned long long)cpu->x[3],
                         (unsigned long long)cpu->x[13], (unsigned long long)cpu->x[15],
                         (unsigned long long)cpu->x[18], (unsigned long long)cpu->x[19],
                         (unsigned long long)cpu->x[21], (unsigned long long)cpu->sp,
                         (unsigned long long)cpu->pstate);
            } else if (gen_state.guest_pc >= 0x79884 && gen_state.guest_pc <= 0x798a4) {
                uint32_t nbuckets = 0;
                uint32_t symoffset = 0;
                uint32_t bloom_size = 0;
                uint32_t bloom_shift = 0;
                uint64_t bloom_word = 0;
                if (cpu->x[1] != 0) {
                    (void)a64_guest_read32(cpu, tlb, cpu->x[1] + 0x0, &nbuckets);
                    (void)a64_guest_read32(cpu, tlb, cpu->x[1] + 0x4, &symoffset);
                    (void)a64_guest_read32(cpu, tlb, cpu->x[1] + 0x8, &bloom_size);
                    (void)a64_guest_read32(cpu, tlb, cpu->x[1] + 0xc, &bloom_shift);
                    if (bloom_size != 0) {
                        uint64_t bloom_index = ((uint32_t)cpu->x[4]) & (uint64_t)(bloom_size - 1);
                        (void)a64_guest_read64(cpu, tlb, cpu->x[1] + 0x10 + (bloom_index << 3),
                                               &bloom_word);
                    }
                }
                snprintf(event, sizeof(event),
                         "hot.ldso=pc:0x%llx,raw:0x%08x,cat:%d,sub:%d,rd:%d,rn:%d,rm:%d,imm:%lld,"
                         "x0:0x%llx,x1:0x%llx,x4:0x%llx,x5:0x%llx,x10:0x%llx,nb:%u,so:%u,bs:%u,"
                         "bsh:%u,bw:0x%llx,pstate:0x%llx",
                         (unsigned long long)gen_state.guest_pc, insn,
                         decode_ret == 0 ? decoded_info.cat : -1,
                         decode_ret == 0 ? decoded_info.subtype : -1,
                         decode_ret == 0 ? decoded_info.Rd : -1,
                         decode_ret == 0 ? decoded_info.Rn : -1,
                         decode_ret == 0 ? decoded_info.Rm : -1,
                         decode_ret == 0 ? (long long)decoded_info.imm : 0LL,
                         (unsigned long long)cpu->x[0], (unsigned long long)cpu->x[1],
                         (unsigned long long)cpu->x[4], (unsigned long long)cpu->x[5],
                         (unsigned long long)cpu->x[10], nbuckets, symoffset, bloom_size,
                         bloom_shift, (unsigned long long)bloom_word,
                         (unsigned long long)cpu->pstate);
            } else if (gen_state.guest_pc >= 0x7a010 && gen_state.guest_pc <= 0x7a030) {
                uint64_t dso_ghashtab = 0;
                uint64_t dso_strtab = 0;
                uint64_t dso_next = 0;
                uint64_t dso_deps = 0;
                uint64_t dso_dep0 = 0;
                uint64_t dso_dep1 = 0;
                if (cpu->x[15] != 0) {
                    (void)a64_guest_read64(cpu, tlb, cpu->x[15] + 0x50, &dso_ghashtab);
                    (void)a64_guest_read64(cpu, tlb, cpu->x[15] + 0x60, &dso_strtab);
                    (void)a64_guest_read64(cpu, tlb, cpu->x[15] + 0x68, &dso_next);
                    (void)a64_guest_read64(cpu, tlb, cpu->x[15] + 0xb0, &dso_deps);
                    if (dso_deps != 0) {
                        (void)a64_guest_read64(cpu, tlb, dso_deps + 0x0, &dso_dep0);
                        (void)a64_guest_read64(cpu, tlb, dso_deps + 0x8, &dso_dep1);
                    }
                }
                snprintf(event, sizeof(event),
                         "hot.ldso=pc:0x%llx,raw:0x%08x,cat:%d,sub:%d,rd:%d,rn:%d,rm:%d,imm:%lld,"
                         "x0:0x%llx,x1:0x%llx,x2:0x%llx,x13:0x%llx,x15:0x%llx,x18:0x%llx,"
                         "x19:0x%llx,x21:0x%llx,dso_ghashtab:0x%llx,dso_strtab:0x%llx,"
                         "dso_next:0x%llx,dso_deps:0x%llx,dso_dep0:0x%llx,dso_dep1:0x%llx,"
                         "sp:0x%llx,pstate:0x%llx",
                         (unsigned long long)gen_state.guest_pc, insn,
                         decode_ret == 0 ? decoded_info.cat : -1,
                         decode_ret == 0 ? decoded_info.subtype : -1,
                         decode_ret == 0 ? decoded_info.Rd : -1,
                         decode_ret == 0 ? decoded_info.Rn : -1,
                         decode_ret == 0 ? decoded_info.Rm : -1,
                         decode_ret == 0 ? (long long)decoded_info.imm : 0LL,
                         (unsigned long long)cpu->x[0], (unsigned long long)cpu->x[1],
                         (unsigned long long)cpu->x[2], (unsigned long long)cpu->x[13],
                         (unsigned long long)cpu->x[15], (unsigned long long)cpu->x[18],
                         (unsigned long long)cpu->x[19], (unsigned long long)cpu->x[21],
                         (unsigned long long)dso_ghashtab, (unsigned long long)dso_strtab,
                         (unsigned long long)dso_next, (unsigned long long)dso_deps,
                         (unsigned long long)dso_dep0, (unsigned long long)dso_dep1,
                         (unsigned long long)cpu->sp, (unsigned long long)cpu->pstate);
            } else if (gen_state.guest_pc >= 0x6b5f0 && gen_state.guest_pc <= 0x6b6b8) {
                uint32_t reserved_mask = 0;
                uint32_t reserved_flags = 0;
                uint64_t loader_head = 0;
                uint64_t loader_tail = 0;
                uint64_t x0_u64 = 0;
                uint64_t x1_u64 = 0;
                uint64_t x2_u64 = 0;
                uint64_t x22_next = 0;
                if (cpu->x[0] != 0)
                    (void)a64_guest_read64(cpu, tlb, cpu->x[0] + 0x20, &x0_u64);
                if (cpu->x[1] != 0)
                    (void)a64_guest_read64(cpu, tlb, cpu->x[1], &x1_u64);
                if (cpu->x[2] != 0)
                    (void)a64_guest_read64(cpu, tlb, cpu->x[2], &x2_u64);
                if (cpu->x[22] != 0)
                    (void)a64_guest_read64(cpu, tlb, cpu->x[22] + 0x18, &x22_next);
                (void)a64_guest_read32(cpu, tlb, 0xc2f08, &reserved_mask);
                (void)a64_guest_read32(cpu, tlb, 0xc2ef8, &reserved_flags);
                (void)a64_guest_read64(cpu, tlb, 0xc2b90, &loader_tail);
                (void)a64_guest_read64(cpu, tlb, 0xc2b98, &loader_head);
                snprintf(event, sizeof(event),
                         "hot.ldso=pc:0x%llx,raw:0x%08x,cat:%d,sub:%d,rd:%d,rn:%d,rm:%d,imm:%lld,"
                         "x0:0x%llx,x1:0x%llx,x2:0x%llx,x3:0x%llx,x4:0x%llx,x21:0x%llx,"
                         "x22:0x%llx,x24:0x%llx,x25:0x%llx,x28:0x%llx,loader_head:0x%llx,"
                         "loader_tail:0x%llx,reserved_mask:0x%x,reserved_flags:0x%x,"
                         "x0_link:0x%llx,x1_qword:0x%llx,x2_qword:0x%llx,x22_next:0x%llx,"
                         "sp:0x%llx,pstate:0x%llx",
                         (unsigned long long)gen_state.guest_pc, insn,
                         decode_ret == 0 ? decoded_info.cat : -1,
                         decode_ret == 0 ? decoded_info.subtype : -1,
                         decode_ret == 0 ? decoded_info.Rd : -1,
                         decode_ret == 0 ? decoded_info.Rn : -1,
                         decode_ret == 0 ? decoded_info.Rm : -1,
                         decode_ret == 0 ? (long long)decoded_info.imm : 0LL,
                         (unsigned long long)cpu->x[0], (unsigned long long)cpu->x[1],
                         (unsigned long long)cpu->x[2], (unsigned long long)cpu->x[3],
                         (unsigned long long)cpu->x[4], (unsigned long long)cpu->x[21],
                         (unsigned long long)cpu->x[22], (unsigned long long)cpu->x[24],
                         (unsigned long long)cpu->x[25], (unsigned long long)cpu->x[28],
                         (unsigned long long)loader_head, (unsigned long long)loader_tail,
                         reserved_mask, reserved_flags, (unsigned long long)x0_u64,
                         (unsigned long long)x1_u64, (unsigned long long)x2_u64,
                         (unsigned long long)x22_next, (unsigned long long)cpu->sp,
                         (unsigned long long)cpu->pstate);
            } else if (gen_state.guest_pc >= 0x6ca60 && gen_state.guest_pc <= 0x6cac0) {
                uint64_t x22_next = 0;
                uint64_t x23_tail = 0;
                uint64_t x24_byte = 0;
                uint64_t x26_qword = 0;
                if (cpu->x[22] != 0)
                    (void)a64_guest_read64(cpu, tlb, cpu->x[22] + 0x18, &x22_next);
                if (cpu->x[23] != 0)
                    (void)a64_guest_read64(cpu, tlb, cpu->x[23] + 0xb80, &x23_tail);
                if (cpu->x[24] != 0)
                    (void)a64_guest_read8(cpu, tlb, cpu->x[24], (uint8_t *)&x24_byte);
                if (cpu->x[26] != 0)
                    (void)a64_guest_read64(cpu, tlb, cpu->x[26], &x26_qword);
                snprintf(event, sizeof(event),
                         "hot.ldso=pc:0x%llx,raw:0x%08x,cat:%d,sub:%d,rd:%d,rn:%d,rm:%d,imm:%lld,"
                         "x0:0x%llx,x1:0x%llx,x2:0x%llx,x3:0x%llx,x22:0x%llx,x23:0x%llx,"
                         "x24:0x%llx,x25:0x%llx,x26:0x%llx,x28:0x%llx,x22_next:0x%llx,"
                         "x23_tail:0x%llx,x24_byte:0x%llx,x26_qword:0x%llx,sp:0x%llx,"
                         "pstate:0x%llx",
                         (unsigned long long)gen_state.guest_pc, insn,
                         decode_ret == 0 ? decoded_info.cat : -1,
                         decode_ret == 0 ? decoded_info.subtype : -1,
                         decode_ret == 0 ? decoded_info.Rd : -1,
                         decode_ret == 0 ? decoded_info.Rn : -1,
                         decode_ret == 0 ? decoded_info.Rm : -1,
                         decode_ret == 0 ? (long long)decoded_info.imm : 0LL,
                         (unsigned long long)cpu->x[0], (unsigned long long)cpu->x[1],
                         (unsigned long long)cpu->x[2], (unsigned long long)cpu->x[3],
                         (unsigned long long)cpu->x[22], (unsigned long long)cpu->x[23],
                         (unsigned long long)cpu->x[24], (unsigned long long)cpu->x[25],
                         (unsigned long long)cpu->x[26], (unsigned long long)cpu->x[28],
                         (unsigned long long)x22_next, (unsigned long long)x23_tail,
                         (unsigned long long)x24_byte, (unsigned long long)x26_qword,
                         (unsigned long long)cpu->sp, (unsigned long long)cpu->pstate);
            } else if (gen_state.guest_pc >= 0x6cac4 && gen_state.guest_pc <= 0x6caf4) {
                uint64_t dep_list_next = 0;
                uint64_t current_next = 0;
                uint64_t tail_next = 0;
                uint64_t global_tail = 0;
                if (cpu->x[0] != 0) {
                    (void)a64_guest_read64(cpu, tlb, cpu->x[0] + 0x18, &dep_list_next);
                    (void)a64_guest_read64(cpu, tlb, cpu->x[0] + 0x68, &current_next);
                }
                if (cpu->x[2] != 0)
                    (void)a64_guest_read64(cpu, tlb, cpu->x[2] + 0x68, &tail_next);
                if (cpu->x[23] != 0)
                    (void)a64_guest_read64(cpu, tlb, cpu->x[23] + 0xb80, &global_tail);
                snprintf(event, sizeof(event),
                         "hot.ldso=pc:0x%llx,raw:0x%08x,cat:%d,sub:%d,rd:%d,rn:%d,rm:%d,imm:%lld,"
                         "x0:0x%llx,x1:0x%llx,x2:0x%llx,x3:0x%llx,x23:0x%llx,x26:0x%llx,"
                         "dep_list_next:0x%llx,current_next:0x%llx,tail_next:0x%llx,"
                         "global_tail:0x%llx,sp:0x%llx,pstate:0x%llx",
                         (unsigned long long)gen_state.guest_pc, insn,
                         decode_ret == 0 ? decoded_info.cat : -1,
                         decode_ret == 0 ? decoded_info.subtype : -1,
                         decode_ret == 0 ? decoded_info.Rd : -1,
                         decode_ret == 0 ? decoded_info.Rn : -1,
                         decode_ret == 0 ? decoded_info.Rm : -1,
                         decode_ret == 0 ? (long long)decoded_info.imm : 0LL,
                         (unsigned long long)cpu->x[0], (unsigned long long)cpu->x[1],
                         (unsigned long long)cpu->x[2], (unsigned long long)cpu->x[3],
                         (unsigned long long)cpu->x[23], (unsigned long long)cpu->x[26],
                         (unsigned long long)dep_list_next, (unsigned long long)current_next,
                         (unsigned long long)tail_next, (unsigned long long)global_tail,
                         (unsigned long long)cpu->sp, (unsigned long long)cpu->pstate);
            } else if (gen_state.guest_pc >= 0x6ade0 && gen_state.guest_pc <= 0x6af18) {
                uint64_t x14_ghashtab = 0;
                uint64_t x14_next = 0;
                uint64_t x14_deps = 0;
                uint64_t x15_slot = 0;
                if (cpu->x[14] != 0) {
                    (void)a64_guest_read64(cpu, tlb, cpu->x[14] + 0x50, &x14_ghashtab);
                    (void)a64_guest_read64(cpu, tlb, cpu->x[14] + 0x68, &x14_next);
                    (void)a64_guest_read64(cpu, tlb, cpu->x[14] + 0xb0, &x14_deps);
                }
                if (cpu->x[15] != 0)
                    (void)a64_guest_read64(cpu, tlb, cpu->x[15], &x15_slot);
                snprintf(event, sizeof(event),
                         "hot.ldso=pc:0x%llx,raw:0x%08x,cat:%d,sub:%d,rd:%d,rn:%d,rm:%d,imm:%lld,"
                         "x0:0x%llx,x1:0x%llx,x14:0x%llx,x15:0x%llx,x20:0x%llx,x21:0x%llx,"
                         "x22:0x%llx,x14_ghashtab:0x%llx,x14_next:0x%llx,x14_deps:0x%llx,"
                         "x15_slot:0x%llx,sp:0x%llx,pstate:0x%llx",
                         (unsigned long long)gen_state.guest_pc, insn,
                         decode_ret == 0 ? decoded_info.cat : -1,
                         decode_ret == 0 ? decoded_info.subtype : -1,
                         decode_ret == 0 ? decoded_info.Rd : -1,
                         decode_ret == 0 ? decoded_info.Rn : -1,
                         decode_ret == 0 ? decoded_info.Rm : -1,
                         decode_ret == 0 ? (long long)decoded_info.imm : 0LL,
                         (unsigned long long)cpu->x[0], (unsigned long long)cpu->x[1],
                         (unsigned long long)cpu->x[14], (unsigned long long)cpu->x[15],
                         (unsigned long long)cpu->x[20], (unsigned long long)cpu->x[21],
                         (unsigned long long)cpu->x[22], (unsigned long long)x14_ghashtab,
                         (unsigned long long)x14_next, (unsigned long long)x14_deps,
                         (unsigned long long)x15_slot, (unsigned long long)cpu->sp,
                         (unsigned long long)cpu->pstate);
            } else if (gen_state.guest_pc >= 0x7b1f0 && gen_state.guest_pc <= 0x7b29c) {
                snprintf(event, sizeof(event),
                         "hot.ldso=pc:0x%llx,raw:0x%08x,cat:%d,sub:%d,rd:%d,rn:%d,rm:%d,imm:%lld,"
                         "x0:0x%llx,x1:0x%llx,x2:0x%llx,x4:0x%llx,x19:0x%llx,x21:0x%llx,"
                         "x25:0x%llx,x26:0x%llx,sp:0x%llx,pstate:0x%llx",
                         (unsigned long long)gen_state.guest_pc, insn,
                         decode_ret == 0 ? decoded_info.cat : -1,
                         decode_ret == 0 ? decoded_info.subtype : -1,
                         decode_ret == 0 ? decoded_info.Rd : -1,
                         decode_ret == 0 ? decoded_info.Rn : -1,
                         decode_ret == 0 ? decoded_info.Rm : -1,
                         decode_ret == 0 ? (long long)decoded_info.imm : 0LL,
                         (unsigned long long)cpu->x[0], (unsigned long long)cpu->x[1],
                         (unsigned long long)cpu->x[2], (unsigned long long)cpu->x[4],
                         (unsigned long long)cpu->x[19], (unsigned long long)cpu->x[21],
                         (unsigned long long)cpu->x[25], (unsigned long long)cpu->x[26],
                         (unsigned long long)cpu->sp, (unsigned long long)cpu->pstate);
            } else if (gen_state.guest_pc >= 0xdc700 && gen_state.guest_pc <= 0xdc780) {
                snprintf(event, sizeof(event),
                         "hot.ldso=pc:0x%llx,raw:0x%08x,cat:%d,sub:%d,rd:%d,rn:%d,rm:%d,imm:%lld,"
                         "x0:0x%llx,x1:0x%llx,x2:0x%llx,x3:0x%llx,x4:0x%llx,x5:0x%llx,"
                         "sp:0x%llx,pstate:0x%llx",
                         (unsigned long long)gen_state.guest_pc, insn,
                         decode_ret == 0 ? decoded_info.cat : -1,
                         decode_ret == 0 ? decoded_info.subtype : -1,
                         decode_ret == 0 ? decoded_info.Rd : -1,
                         decode_ret == 0 ? decoded_info.Rn : -1,
                         decode_ret == 0 ? decoded_info.Rm : -1,
                         decode_ret == 0 ? (long long)decoded_info.imm : 0LL,
                         (unsigned long long)cpu->x[0], (unsigned long long)cpu->x[1],
                         (unsigned long long)cpu->x[2], (unsigned long long)cpu->x[3],
                         (unsigned long long)cpu->x[4], (unsigned long long)cpu->x[5],
                         (unsigned long long)cpu->sp, (unsigned long long)cpu->pstate);
            } else if (gen_state.guest_pc >= 0x7b018 && gen_state.guest_pc <= 0x7b12c) {
                snprintf(event, sizeof(event),
                         "hot.ldso=pc:0x%llx,raw:0x%08x,cat:%d,sub:%d,rd:%d,rn:%d,rm:%d,imm:%lld,"
                         "x0:0x%llx,x1:0x%llx,x2:0x%llx,x8:0x%llx,x21:0x%llx,pstate:0x%llx",
                         (unsigned long long)gen_state.guest_pc, insn,
                         decode_ret == 0 ? decoded_info.cat : -1,
                         decode_ret == 0 ? decoded_info.subtype : -1,
                         decode_ret == 0 ? decoded_info.Rd : -1,
                         decode_ret == 0 ? decoded_info.Rn : -1,
                         decode_ret == 0 ? decoded_info.Rm : -1,
                         decode_ret == 0 ? (long long)decoded_info.imm : 0LL,
                         (unsigned long long)cpu->x[0], (unsigned long long)cpu->x[1],
                         (unsigned long long)cpu->x[2], (unsigned long long)cpu->x[8],
                         (unsigned long long)cpu->x[21], (unsigned long long)cpu->pstate);
            } else if (gen_state.guest_pc >= 0x7c2bc && gen_state.guest_pc <= 0x7c2fc) {
                snprintf(event, sizeof(event),
                         "hot.ldso=pc:0x%llx,raw:0x%08x,cat:%d,sub:%d,rd:%d,rn:%d,rm:%d,imm:%lld,"
                         "x0:0x%llx,x1:0x%llx,x2:0x%llx,x19:0x%llx,x21:0x%llx,x22:0x%llx,"
                         "x29:0x%llx,sp:0x%llx,pstate:0x%llx",
                         (unsigned long long)gen_state.guest_pc, insn,
                         decode_ret == 0 ? decoded_info.cat : -1,
                         decode_ret == 0 ? decoded_info.subtype : -1,
                         decode_ret == 0 ? decoded_info.Rd : -1,
                         decode_ret == 0 ? decoded_info.Rn : -1,
                         decode_ret == 0 ? decoded_info.Rm : -1,
                         decode_ret == 0 ? (long long)decoded_info.imm : 0LL,
                         (unsigned long long)cpu->x[0], (unsigned long long)cpu->x[1],
                         (unsigned long long)cpu->x[2], (unsigned long long)cpu->x[19],
                         (unsigned long long)cpu->x[21], (unsigned long long)cpu->x[22],
                         (unsigned long long)cpu->x[29], (unsigned long long)cpu->sp,
                         (unsigned long long)cpu->pstate);
            } else if (gen_state.guest_pc >= 0x7bf44 && gen_state.guest_pc <= 0x7bf60) {
                snprintf(event, sizeof(event),
                         "hot.ldso=pc:0x%llx,raw:0x%08x,cat:%d,sub:%d,rd:%d,rn:%d,rm:%d,imm:%lld,"
                         "x0:0x%llx,x1:0x%llx,x2:0x%llx,x3:0x%llx,x21:0x%llx,x23:0x%llx,"
                         "x25:0x%llx,x26:0x%llx,sp:0x%llx,pstate:0x%llx",
                         (unsigned long long)gen_state.guest_pc, insn,
                         decode_ret == 0 ? decoded_info.cat : -1,
                         decode_ret == 0 ? decoded_info.subtype : -1,
                         decode_ret == 0 ? decoded_info.Rd : -1,
                         decode_ret == 0 ? decoded_info.Rn : -1,
                         decode_ret == 0 ? decoded_info.Rm : -1,
                         decode_ret == 0 ? (long long)decoded_info.imm : 0LL,
                         (unsigned long long)cpu->x[0], (unsigned long long)cpu->x[1],
                         (unsigned long long)cpu->x[2], (unsigned long long)cpu->x[3],
                         (unsigned long long)cpu->x[21], (unsigned long long)cpu->x[23],
                         (unsigned long long)cpu->x[25], (unsigned long long)cpu->x[26],
                         (unsigned long long)cpu->sp, (unsigned long long)cpu->pstate);
            } else if (gen_state.guest_pc >= 0x7c4e0 && gen_state.guest_pc <= 0x7c500) {
                snprintf(event, sizeof(event),
                         "hot.ldso=pc:0x%llx,raw:0x%08x,cat:%d,sub:%d,rd:%d,rn:%d,rm:%d,imm:%lld,"
                         "x0:0x%llx,x1:0x%llx,x2:0x%llx,x19:0x%llx,x20:0x%llx,x21:0x%llx,"
                         "x23:0x%llx,sp:0x%llx,pstate:0x%llx",
                         (unsigned long long)gen_state.guest_pc, insn,
                         decode_ret == 0 ? decoded_info.cat : -1,
                         decode_ret == 0 ? decoded_info.subtype : -1,
                         decode_ret == 0 ? decoded_info.Rd : -1,
                         decode_ret == 0 ? decoded_info.Rn : -1,
                         decode_ret == 0 ? decoded_info.Rm : -1,
                         decode_ret == 0 ? (long long)decoded_info.imm : 0LL,
                         (unsigned long long)cpu->x[0], (unsigned long long)cpu->x[1],
                         (unsigned long long)cpu->x[2], (unsigned long long)cpu->x[19],
                         (unsigned long long)cpu->x[20], (unsigned long long)cpu->x[21],
                         (unsigned long long)cpu->x[23], (unsigned long long)cpu->sp,
                         (unsigned long long)cpu->pstate);
            } else {
                snprintf(event, sizeof(event),
                         "hot.ldso=pc:0x%llx,raw:0x%08x,cat:%d,sub:%d,rd:%d,rn:%d,rm:%d,imm:%lld",
                         (unsigned long long)gen_state.guest_pc, insn,
                         decode_ret == 0 ? decoded_info.cat : -1,
                         decode_ret == 0 ? decoded_info.subtype : -1,
                         decode_ret == 0 ? decoded_info.Rd : -1,
                         decode_ret == 0 ? decoded_info.Rn : -1,
                         decode_ret == 0 ? decoded_info.Rm : -1,
                         decode_ret == 0 ? (long long)decoded_info.imm : 0LL);
            }
            trace_record_event(TRACE_ORIGIN_EXEC, event);
        }

        // Generate TCTI instruction - load/store and bitfield now have inline TCTI support
        ret = a64_gen_instruction(&gen_state, insn, gen_state.guest_pc);

        a64_trace_event(
            "tcti.compile.instruction",
            "tcti.compile.instruction=start:0x%llx,pc:0x%llx,raw:0x%08x,ret:%d,"
            "cat:%d,sub:%d,rd:%d,rn:%d,rm:%d,imm:%lld,is_complete:%d,count:%d",
            (unsigned long long)pc, (unsigned long long)gen_state.guest_pc, insn, ret,
            decode_ret == 0 ? decoded_info.cat : -1, decode_ret == 0 ? decoded_info.subtype : -1,
            decode_ret == 0 ? decoded_info.Rd : -1, decode_ret == 0 ? decoded_info.Rn : -1,
            decode_ret == 0 ? decoded_info.Rm : -1,
            decode_ret == 0 ? (long long)decoded_info.imm : 0LL, gen_state.is_complete,
            insns_decoded);

        if (ret < 0) {
            if (insns_decoded > 0) {
                char ev[192];
                snprintf(ev, sizeof(ev),
                         "task.proof.tcti.partial_unsupported=start:0x%llx,pc:0x%llx,raw:0x%08x,"
                         "cat:%d,sub:%d,decoded:%d,decoded_count:%d",
                         (unsigned long long)pc, (unsigned long long)gen_state.guest_pc, insn,
                         decode_ret == 0 ? decoded_info.cat : -1,
                         decode_ret == 0 ? decoded_info.subtype : -1, decode_ret, insns_decoded);
                trace_record_event(TRACE_ORIGIN_EXEC, ev);
            }
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

        if (ret == 1 || gen_state.is_complete) {
            // Block should end when generator marks terminal semantics.
            break;
        }
    }

    // Check if we decoded any instructions
    if (insns_decoded == 0) {
        // No instructions could be decoded - this is a fatal error in 100% TCTI mode
        uint32_t failing_insn = 0;
        int fetch_ret = a64_fetch_insn(cpu, tlb, pc, &failing_insn);
        if (a64_trace_should_emit_compile_failure(pc)) {
            a64_trace_event("tcti.compile.no_insns",
                            "tcti.compile.no_insns=pc:0x%llx,raw:0x%08x,fetch_ret:%d,"
                            "aligned:%d,fault:0x%llx,repeat:%u",
                            (unsigned long long)pc, failing_insn, fetch_ret, (pc & 3) == 0 ? 1 : 0,
                            (unsigned long long)cpu->fault_addr,
                            g_a64_compile_failure_repeat_count);
            a64_trace_emit_block_history(pc);
        }
        trace_emit_u32(TRACE_EVENT_UNSUPPORTED_INSTRUCTION, pc, failing_insn);
        return NULL;
    }

    // Finalize the block
    int finalize_ret = a64_gen_finalize(&gen_state);
    if (finalize_ret != A64_GEN_OK) {
        a64_trace_event("tcti.compile.finalize_fail",
                        "tcti.compile.finalize_fail=pc:0x%llx,ret:%d,gadgets:%zu",
                        (unsigned long long)pc, finalize_ret, gen_state.num_gadgets);
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
    block->compile_generation = cpu->mmu->generation;
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
    a64_trace_event("tcti.compile.exit",
                    "tcti.compile.exit=start:0x%llx,end:0x%llx,insns:%d,gadgets:%zu,explicit:%d",
                    (unsigned long long)block->start_pc, (unsigned long long)block->end_pc,
                    insns_decoded, block->num_gadgets, block->explicit_pc_on_exit ? 1 : 0);

    trace_block_bytecode(block);

    return block;
}

/*
 * Execute a compiled block using TCTI
 *
 * Uses tcti_entry_block to set up register mapping and execute
 * the entire gadget chain. Gadgets use epilogue to chain together.
 */
__attribute__((no_stack_protector)) int a64_execute_block(struct cpu_state *cpu,
                                                          struct a64_block *block)
{
    if (!cpu || !block) {
        if (cpu)
            cpu->tcti_exit_reason = TCTI_EXIT_FAULT;
        return TCTI_EXIT_FAULT;
    }

    struct cpu_state *volatile fault_cpu = cpu;
    struct a64_block *volatile fault_block = block;

    // Trace: Register snapshot if at block level
    if (trace_should_emit_event("tcti.block.register_snapshot")) {
        uint64_t regs[6] = { cpu->x[0], cpu->x[1], cpu->x[2], cpu->x[3], cpu->x[4], cpu->x[5] };
        trace_emit_register_snapshot(block->start_pc, regs, 0x3F);
    }

    trace_emit_block_entry(block->start_pc, (uint32_t)block->num_gadgets);

    // Validate pointers before calling
    if (!block->gadgets) {
        cpu->tcti_exit_reason = TCTI_EXIT_FAULT;
        return TCTI_EXIT_FAULT;
    }

    // Verify first gadget is not NULL
    if (block->num_gadgets > 0 && !block->gadgets[0]) {
        cpu->tcti_exit_reason = TCTI_EXIT_FAULT;
        return TCTI_EXIT_FAULT;
    }

    // Set up fault containment for guest execution
    // Host signals (SIGSEGV, SIGBUS, etc.) during guest execution will be
    // caught and converted to TCTI_EXIT_FAULT instead of killing the process
    struct sigaction old_segv, old_bus, old_ill, old_fpe;
    guest_fault_signal = 0;
    guest_fault_host_addr = 0;

    // Install fault containment handlers
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = guest_fault_handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGSEGV, &sa, &old_segv);
    sigaction(SIGBUS, &sa, &old_bus);
    sigaction(SIGILL, &sa, &old_ill);
    sigaction(SIGFPE, &sa, &old_fpe);

    if (sigsetjmp(guest_fault_jmpbuf, 1) == 0) {
        // Normal execution path
        guest_fault_active = 1;
        a64_trace_block_registers("entry", cpu, block);
        tcti_entry_block(block->gadgets, cpu);
        a64_trace_block_registers("exit", cpu, block);
    } else {
        // Fault containment path - signal was caught
        struct cpu_state *faulted_cpu = fault_cpu;
        if (faulted_cpu) {
            faulted_cpu->tcti_exit_reason = TCTI_EXIT_FAULT;
            faulted_cpu->fault_was_write = false;
            trace_first_live_ldst_fault(faulted_cpu, guest_fault_signal);
        }
    }
    guest_fault_active = 0;

    cpu = fault_cpu;
    block = fault_block;

    // Restore original signal handlers
    sigaction(SIGSEGV, &old_segv, NULL);
    sigaction(SIGBUS, &old_bus, NULL);
    sigaction(SIGILL, &old_ill, NULL);
    sigaction(SIGFPE, &old_fpe, NULL);

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
    case A64_EXT_UXTB:
        return (uint8_t)value;
    case A64_EXT_UXTH:
        return (uint16_t)value;
    case A64_EXT_UXTW:
        return (uint32_t)value;
    case A64_EXT_SXTB:
        return (uint64_t)(int64_t)(int8_t)value;
    case A64_EXT_SXTH:
        return (uint64_t)(int64_t)(int16_t)value;
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

/*
 * Stage 3A.6: Pre-syscall userspace initialization tracing
 * Tracks first userspace entry and execution progression
 */

#import <IXLandLinuxRuntime/kernel/memory.h>
#import <IXLandLinuxRuntime/kernel/mm.h>
#import <IXLandLinuxRuntime/kernel/page_map.h>

static uint64_t trace_ldst_reg_or_zr(struct cpu_state *cpu, int reg)
{
    if (reg == 31)
        return 0;
    if (reg < 0 || reg > 30)
        return 0;
    return cpu->x[reg];
}

static const char *trace_ldst_mnemonic(int is_load, int size, int is_signed)
{
    if (is_load) {
        switch (size) {
        case A64_SIZE_B:
            return is_signed ? "ldrsb" : "ldrb";
        case A64_SIZE_H:
            return is_signed ? "ldrsh" : "ldrh";
        case A64_SIZE_W:
            return is_signed ? "ldrsw" : "ldr";
        case A64_SIZE_X:
            return "ldr";
        default:
            return "ldr?";
        }
    }

    switch (size) {
    case A64_SIZE_B:
        return "strb";
    case A64_SIZE_H:
        return "strh";
    case A64_SIZE_W:
        return "str";
    case A64_SIZE_X:
        return "str";
    default:
        return "str?";
    }
}

static void trace_first_live_ldst_fault(struct cpu_state *cpu, int host_signal)
{
    static int captured = 0;
    if (!cpu || !cpu->tlb || captured)
        return;

    uint32_t raw = 0;
    a64_instr_t decoded = { 0 };
    if (a64_fetch_insn(cpu, cpu->tlb, cpu->pc, &raw) != 0 || a64_decode(raw, &decoded) != 0)
        return;

    if (decoded.cat != A64_LD_ST || decoded.subtype != A64_LDST_SINGLE)
        return;

    captured = 1;

    int is_load = bit(raw, 22) ? 1 : 0;
    int is_reg_offset = a64_ldst_uses_register_offset(raw, &decoded) ? 1 : 0;
    int rm = is_reg_offset ? decoded.Rm : -1;

    uint64_t rt_val = trace_ldst_reg_or_zr(cpu, decoded.Rd);
    uint64_t rn_val = a64_read_reg_or_sp(cpu, decoded.Rn, true);
    uint64_t rm_val = is_reg_offset ? trace_ldst_reg_or_zr(cpu, rm) : 0;

    uint64_t offset_before_shift =
        is_reg_offset ? a64_extend_index(rm_val, decoded.extend_type) : (uint64_t)decoded.imm;
    uint64_t computed_offset =
        is_reg_offset ? (offset_before_shift << decoded.imm_shift) : (uint64_t)decoded.imm;

    uint64_t guest_ea = rn_val;
    uint64_t writeback_val = rn_val;
    if (is_reg_offset) {
        guest_ea = rn_val + computed_offset;
    } else {
        switch (decoded.idx_mode) {
        case A64_PRE_INDEX:
            guest_ea = rn_val + decoded.imm;
            writeback_val = guest_ea;
            break;
        case A64_POST_INDEX:
            guest_ea = rn_val;
            writeback_val = rn_val + decoded.imm;
            break;
        case A64_INDEX_OFFSET:
        default:
            guest_ea = rn_val + decoded.imm;
            break;
        }
    }

    int writeback_expected = (!is_reg_offset && (decoded.idx_mode == A64_PRE_INDEX ||
                                                 decoded.idx_mode == A64_POST_INDEX))
                                 ? 1
                                 : 0;

    struct mem *mem = cpu->mmu ? container_of(cpu->mmu, struct mem, mmu) : NULL;
    page_t page = PAGE(guest_ea);
    struct page_desc *desc = mem ? page_map_lookup(&mem->pages, page) : NULL;
    uint64_t host_ptr_page = 0;
    if (desc && desc->obj) {
        host_ptr_page =
            (uint64_t)((char *)desc->obj->host_base + desc->offset + (unsigned)PGOFFSET(guest_ea));
    }

    void *host_ptr_probe = a64_guest_to_host(cpu, cpu->tlb, guest_ea, is_load ? 0 : 1);
    int mem_probe_ret = host_ptr_probe ? A64_MEM_OK : A64_MEM_FAULT;

    trace_field_t fields[] = {
        { .key = "guest_pc", .kind = TRACE_FIELD_U64_HEX, .u64_value = cpu->pc },
        { .key = "raw_opcode", .kind = TRACE_FIELD_U64_HEX, .u64_value = raw },
        { .key = "mnemonic",
          .kind = TRACE_FIELD_STRING,
          .string_value = trace_ldst_mnemonic(is_load, decoded.size, decoded.is_signed ? 1 : 0) },
        { .key = "cat", .kind = TRACE_FIELD_I64_DEC, .i64_value = decoded.cat },
        { .key = "subtype", .kind = TRACE_FIELD_I64_DEC, .i64_value = decoded.subtype },
        { .key = "is_load", .kind = TRACE_FIELD_I64_DEC, .i64_value = is_load },
        { .key = "rt", .kind = TRACE_FIELD_I64_DEC, .i64_value = decoded.Rd },
        { .key = "rn", .kind = TRACE_FIELD_I64_DEC, .i64_value = decoded.Rn },
        { .key = "rm", .kind = TRACE_FIELD_I64_DEC, .i64_value = rm },
        { .key = "size", .kind = TRACE_FIELD_I64_DEC, .i64_value = decoded.size },
        { .key = "idx_mode", .kind = TRACE_FIELD_I64_DEC, .i64_value = decoded.idx_mode },
        { .key = "extend_type", .kind = TRACE_FIELD_I64_DEC, .i64_value = decoded.extend_type },
        { .key = "imm_shift", .kind = TRACE_FIELD_I64_DEC, .i64_value = decoded.imm_shift },
        { .key = "imm", .kind = TRACE_FIELD_I64_DEC, .i64_value = decoded.imm },
        { .key = "is_reg_offset", .kind = TRACE_FIELD_I64_DEC, .i64_value = is_reg_offset },
        { .key = "rt_val", .kind = TRACE_FIELD_U64_HEX, .u64_value = rt_val },
        { .key = "rn_val", .kind = TRACE_FIELD_U64_HEX, .u64_value = rn_val },
        { .key = "rm_val", .kind = TRACE_FIELD_U64_HEX, .u64_value = rm_val },
        { .key = "offset_pre_shift",
          .kind = TRACE_FIELD_U64_HEX,
          .u64_value = offset_before_shift },
        { .key = "computed_offset", .kind = TRACE_FIELD_U64_HEX, .u64_value = computed_offset },
        { .key = "guest_ea", .kind = TRACE_FIELD_U64_HEX, .u64_value = guest_ea },
        { .key = "page_lookup", .kind = TRACE_FIELD_STRING, .string_value = desc ? "hit" : "miss" },
        { .key = "host_ptr_page", .kind = TRACE_FIELD_U64_HEX, .u64_value = host_ptr_page },
        { .key = "host_ptr_probe",
          .kind = TRACE_FIELD_U64_HEX,
          .u64_value = (uint64_t)(uintptr_t)host_ptr_probe },
        { .key = "mem_ret", .kind = TRACE_FIELD_I64_DEC, .i64_value = mem_probe_ret },
        { .key = "writeback_expected",
          .kind = TRACE_FIELD_I64_DEC,
          .i64_value = writeback_expected },
        { .key = "writeback_val", .kind = TRACE_FIELD_U64_HEX, .u64_value = writeback_val },
        { .key = "exit_reason", .kind = TRACE_FIELD_I64_DEC, .i64_value = TCTI_EXIT_FAULT },
        { .key = "interrupt", .kind = TRACE_FIELD_I64_DEC, .i64_value = INT_GPF },
        { .key = "host_signal", .kind = TRACE_FIELD_I64_DEC, .i64_value = host_signal },
    };
    trace_record_event_fields(TRACE_ORIGIN_EMULATOR, "guest.first_fault.ldst", fields,
                              sizeof(fields) / sizeof(fields[0]));
}

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

    // Look up mapping for this PC using VMA tree
    uint64_t pc_addr = pc;
    struct vm_area *vma = vma_tree_find(&current->mem->vmas, pc_addr);
    if (vma) {
        snprintf(page_buf, sizeof(page_buf), "0x%llx", (unsigned long long)(vma->start));
        snprintf(flags_buf, sizeof(flags_buf), "0x%x", vma->flags);

        snprintf(map_start_buf, sizeof(map_start_buf), "0x%llx", (unsigned long long)vma->start);
        snprintf(map_end_buf, sizeof(map_end_buf), "0x%llx", (unsigned long long)(vma->end - 1));

        // Calculate file offset for this PC
        size_t offset_in_vma = pc_addr - vma->start;
        addr_t file_offset = vma->obj->file_offset + offset_in_vma + PGOFFSET(pc);
        snprintf(file_offset_buf, sizeof(file_offset_buf), "0x%llx",
                 (unsigned long long)file_offset);

        if (vma->obj->name) {
            strncpy(name_buf, vma->obj->name, sizeof(name_buf) - 1);
            name_buf[sizeof(name_buf) - 1] = '\0';

            if (strstr(name_buf, "ld-musl") || strstr(name_buf, "ld-linux")) {
                snprintf(is_interp_buf, sizeof(is_interp_buf), "yes");
            } else {
                snprintf(is_interp_buf, sizeof(is_interp_buf), "no");
            }

            if (current->mm && current->mm->exefile && vma->obj->fd == current->mm->exefile) {
                snprintf(is_exe_buf, sizeof(is_exe_buf), "yes");
            } else {
                snprintf(is_exe_buf, sizeof(is_exe_buf), "no");
            }
        }

        if (vma->obj->fd) {
            snprintf(fd_buf, sizeof(fd_buf), "%p", (void *)vma->obj->fd);
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

static void trace_startup_progress_event(const char *name, struct cpu_state *cpu,
                                         struct a64_block *block, int exit_reason, int loop_count,
                                         int block_count)
{
    uint32_t raw = 0;
    a64_instr_t decoded;
    memset(&decoded, 0, sizeof(decoded));
    int fetched = cpu && cpu->tlb ? a64_fetch_insn(cpu, cpu->tlb, cpu->pc, &raw) : -1;
    int decoded_ok = fetched == 0 ? a64_decode(raw, &decoded) : -1;

    char ev[2048];
    snprintf(
        ev, sizeof(ev),
        "%s=pc:0x%llx,block_start:0x%llx,block_end:0x%llx,explicit:%d,exit:%d,"
        "repeat:%d,blocks:%d,raw:0x%08x,fetch:%d,decode:%d,cat:%d,sub:%d,rd:%d,rn:%d,"
        "rm:%d,imm:%lld,sp:0x%llx,"
        "x0:0x%llx,x1:0x%llx,x2:0x%llx,x3:0x%llx,x4:0x%llx,x5:0x%llx,x6:0x%llx,"
        "x7:0x%llx,x8:0x%llx,x9:0x%llx,x10:0x%llx,x11:0x%llx,x12:0x%llx,x13:0x%llx,"
        "x14:0x%llx,x15:0x%llx,x16:0x%llx,x17:0x%llx,x18:0x%llx,x19:0x%llx,"
        "x20:0x%llx,x21:0x%llx,x22:0x%llx,x23:0x%llx,x24:0x%llx,x25:0x%llx,"
        "x26:0x%llx,x27:0x%llx,x28:0x%llx,x29:0x%llx,x30:0x%llx",
        name, cpu ? (unsigned long long)cpu->pc : 0ULL,
        block ? (unsigned long long)block->start_pc : 0ULL,
        block ? (unsigned long long)block->end_pc : 0ULL,
        block && block->explicit_pc_on_exit ? 1 : 0, exit_reason, loop_count, block_count, raw,
        fetched, decoded_ok, decoded.cat, decoded.subtype, decoded.Rd, decoded.Rn, decoded.Rm,
        (long long)decoded.imm, cpu ? (unsigned long long)cpu->sp : 0ULL,
        cpu ? (unsigned long long)cpu->x[0] : 0ULL, cpu ? (unsigned long long)cpu->x[1] : 0ULL,
        cpu ? (unsigned long long)cpu->x[2] : 0ULL, cpu ? (unsigned long long)cpu->x[3] : 0ULL,
        cpu ? (unsigned long long)cpu->x[4] : 0ULL, cpu ? (unsigned long long)cpu->x[5] : 0ULL,
        cpu ? (unsigned long long)cpu->x[6] : 0ULL, cpu ? (unsigned long long)cpu->x[7] : 0ULL,
        cpu ? (unsigned long long)cpu->x[8] : 0ULL, cpu ? (unsigned long long)cpu->x[9] : 0ULL,
        cpu ? (unsigned long long)cpu->x[10] : 0ULL, cpu ? (unsigned long long)cpu->x[11] : 0ULL,
        cpu ? (unsigned long long)cpu->x[12] : 0ULL, cpu ? (unsigned long long)cpu->x[13] : 0ULL,
        cpu ? (unsigned long long)cpu->x[14] : 0ULL, cpu ? (unsigned long long)cpu->x[15] : 0ULL,
        cpu ? (unsigned long long)cpu->x[16] : 0ULL, cpu ? (unsigned long long)cpu->x[17] : 0ULL,
        cpu ? (unsigned long long)cpu->x[18] : 0ULL, cpu ? (unsigned long long)cpu->x[19] : 0ULL,
        cpu ? (unsigned long long)cpu->x[20] : 0ULL, cpu ? (unsigned long long)cpu->x[21] : 0ULL,
        cpu ? (unsigned long long)cpu->x[22] : 0ULL, cpu ? (unsigned long long)cpu->x[23] : 0ULL,
        cpu ? (unsigned long long)cpu->x[24] : 0ULL, cpu ? (unsigned long long)cpu->x[25] : 0ULL,
        cpu ? (unsigned long long)cpu->x[26] : 0ULL, cpu ? (unsigned long long)cpu->x[27] : 0ULL,
        cpu ? (unsigned long long)cpu->x[28] : 0ULL, cpu ? (unsigned long long)cpu->x[29] : 0ULL,
        cpu ? (unsigned long long)cpu->x[30] : 0ULL);
    trace_record_event(TRACE_ORIGIN_EXEC, ev);
}

static void trace_interpreter_edge_checkpoint(const char *name, struct cpu_state *cpu,
                                              uint64_t block_start, uint64_t block_end,
                                              int repeat_count)
{
    char pc_buf[32];
    char block_start_buf[32];
    char block_end_buf[32];
    char repeat_count_buf[16];
    char sp_buf[32];
    char x0_buf[32];
    char x1_buf[32];
    char tpidr_buf[32];

    snprintf(pc_buf, sizeof(pc_buf), "0x%llx", (unsigned long long)cpu->pc);
    snprintf(block_start_buf, sizeof(block_start_buf), "0x%llx", (unsigned long long)block_start);
    snprintf(block_end_buf, sizeof(block_end_buf), "0x%llx", (unsigned long long)block_end);
    snprintf(repeat_count_buf, sizeof(repeat_count_buf), "%d", repeat_count);
    snprintf(sp_buf, sizeof(sp_buf), "0x%llx", (unsigned long long)cpu->sp);
    snprintf(x0_buf, sizeof(x0_buf), "0x%llx", (unsigned long long)cpu->x[0]);
    snprintf(x1_buf, sizeof(x1_buf), "0x%llx", (unsigned long long)cpu->x[1]);
    snprintf(tpidr_buf, sizeof(tpidr_buf), "0x%llx", (unsigned long long)cpu->tpidr_el0);

    trace_attribute_t attrs[] = {
        { "pc", pc_buf },
        { "block_start", block_start_buf },
        { "block_end", block_end_buf },
        { "repeat_count", repeat_count_buf },
        { "sp", sp_buf },
        { "x0", x0_buf },
        { "x1", x1_buf },
        { "tpidr_el0", tpidr_buf },
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
    a64_cpu_run_limited(cpu, tlb, 0); // 0 = unlimited
}

/*
 * Run the CPU with optional iteration limit
 * max_iterations: 0 = unlimited, N = return after N blocks
 * This is used for testing/proof scenarios
 */
void a64_cpu_run_limited(struct cpu_state *cpu, struct tlb *tlb, int max_iterations)
{
    trace_cpu_run_checkpoint("task.proof.a64_cpu_run.entry", current, cpu, 0);
    const char *stop_reason = "iteration_limit";

    if (!cpu || !tlb || !cpu->mmu) {
        stop_reason = "invalid_state";
        ixland_guest_trace_emit(IXLAND_INSTRUMENTATION_ORIGIN_EMULATOR, "guest.a64_cpu_run.stop");
        return;
    }

    // Initialize tracing from environment
    trace_config_t trace_config;
    trace_config_from_env(&trace_config);
    if (trace_init(&trace_config) == 0 && trace_should_emit_event("guest.process.entry")) {
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
    trace_mm_page0_state_runtime("mm.page0.state.before_first_user_insn", cpu, tlb, 0);

    bool conservative_mode = a64_conservative_mode_enabled();

    // Stage 3A.6: Pre-syscall initialization tracing state
    bool first_block_lookup = true;
    bool first_compile = true;
    bool first_execute = true;
    bool first_user_entry = true;
    uint64_t last_block_start_pc = 0;
    int same_block_repeat_count = 0;
    int total_blocks_executed = 0;
    bool interpreter_loop_active = false;

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

    if (!g_guest_first_user_pc_emitted) {
        char pc_buf[32];
        char sp_buf[32];
        snprintf(pc_buf, sizeof(pc_buf), "0x%llx", (unsigned long long)cpu->pc);
        snprintf(sp_buf, sizeof(sp_buf), "0x%llx", (unsigned long long)cpu->sp);
        ixland_instrumentation_attribute_t attrs[] = {
            { .key = "guest_pc", .value = pc_buf },
            { .key = "sp", .value = sp_buf },
        };
        ixland_guest_trace_emit_attrs(IXLAND_INSTRUMENTATION_ORIGIN_EMULATOR, "guest.first_user_pc",
                                      attrs, sizeof(attrs) / sizeof(attrs[0]));
        g_guest_first_user_pc_emitted = 1;
    }

    int iteration_count = 0;
    while (max_iterations == 0 || iteration_count < max_iterations) {
        iteration_count++;

        // Reacquire context if it was marked inactive (e.g., after interrupt return)
        if (!ctx->active) {
            ctx = fiber_exec_ctx_get(cpu);
            if (!ctx) {
                trace_emit(TRACE_EVENT_FAULT, cpu->pc);
                handle_interrupt(INT_GPF);
                stop_reason = "ctx_reacquire_failed";
                break;
            }
        }

        if (cpu->mmu == NULL) {
            trace_emit(TRACE_EVENT_FAULT, cpu->pc);
            handle_interrupt(INT_GPF);
            stop_reason = "missing_mmu";
            break;
        }
        tlb_refresh(tlb, cpu->mmu);
        cpu->tlb = tlb;

        uint64_t pc = cpu->pc;
        if (first_block_lookup) {
            trace_cpu_run_checkpoint("task.proof.a64_cpu_run.before_first_block_lookup", current,
                                     cpu, 0);
        }

        struct a64_block *block = NULL;
        if (conservative_mode) {
            if (first_compile) {
                trace_cpu_run_checkpoint("task.proof.a64_cpu_run.before_first_compile", current,
                                         cpu, 0);
            }
            block = a64_compile_block(cpu, pc, tlb);
            if (!block) {
                trace_emit(TRACE_EVENT_FAULT, pc);
                trace_cpu_run_checkpoint("task.proof.a64_cpu_run.exit_fault", current, cpu,
                                         TCTI_EXIT_FAULT);
                trace_cpu_run_checkpoint("task.proof.a64_cpu_run.before_handle_interrupt", current,
                                         cpu, TCTI_EXIT_FAULT);
                handle_interrupt(INT_GPF);
                trace_cpu_run_checkpoint("task.proof.a64_cpu_run.after_handle_interrupt_call",
                                         current, cpu, TCTI_EXIT_FAULT);
                trace_cpu_run_checkpoint("task.proof.a64_cpu_run.after_handle_interrupt", current,
                                         cpu, TCTI_EXIT_FAULT);
                continue;
            }
            if (first_compile) {
                trace_cpu_run_checkpoint("task.proof.a64_cpu_run.after_first_compile", current, cpu,
                                         0);
                first_compile = false;
            }
            fiber_stat_inc(ctx, STAT_TB_COMPILES);
        } else {
            // L0 cache lookup (fast path via fiber_exec_ctx)
            size_t l0_idx = ((pc ^ (pc >> 12)) & FIBER_EXEC_CTX_CACHE_MASK);
            block = ctx->l0_cache[l0_idx];

            // Validate L0 cache hit against the same generation contract as L1.
            if (block && block->start_pc == pc && !block->is_jetsam &&
                block->compile_generation == cpu->mmu->generation) {
                fiber_stat_inc(ctx, STAT_TB_L0_HITS);
            } else {
                // L0 miss - fall back to MMU cache (L1)
                ctx->l0_cache[l0_idx] = NULL;
                block = NULL;
                if (cpu->mmu->block_cache) {
                    block = a64_cache_lookup(cpu->mmu->block_cache, pc, cpu->mmu->generation);
                    if (block) {
                        fiber_stat_inc(ctx, STAT_TB_L1_HITS);
                    }
                }

                if (!block) {
                    // Compile new block
                    if (first_compile) {
                        trace_cpu_run_checkpoint("task.proof.a64_cpu_run.before_first_compile",
                                                 current, cpu, 0);
                    }
                    a64_trace_event("tcti.dispatch.compile.call",
                                    "tcti.dispatch.compile.call=pc:0x%llx,total:%d,mmu_gen:%llu",
                                    (unsigned long long)pc, total_blocks_executed,
                                    cpu && cpu->mmu ? (unsigned long long)cpu->mmu->generation
                                                    : 0ULL);
                    block = a64_compile_block(cpu, pc, tlb);
                    a64_trace_event("tcti.dispatch.compile.return",
                                    "tcti.dispatch.compile.return=pc:0x%llx,block:%d,fault:0x%llx,"
                                    "was_write:%d,total:%d",
                                    (unsigned long long)pc, block ? 1 : 0,
                                    (unsigned long long)cpu->fault_addr,
                                    cpu->fault_was_write ? 1 : 0, total_blocks_executed);
                    if (!block) {
                        trace_emit(TRACE_EVENT_FAULT, pc);
                        trace_cpu_run_checkpoint("task.proof.a64_cpu_run.exit_fault", current, cpu,
                                                 TCTI_EXIT_FAULT);
                        trace_cpu_run_checkpoint("task.proof.a64_cpu_run.before_handle_interrupt",
                                                 current, cpu, TCTI_EXIT_FAULT);
                        handle_interrupt(INT_GPF);
                        trace_cpu_run_checkpoint(
                            "task.proof.a64_cpu_run.after_handle_interrupt_call", current, cpu,
                            TCTI_EXIT_FAULT);
                        trace_cpu_run_checkpoint("task.proof.a64_cpu_run.after_handle_interrupt",
                                                 current, cpu, TCTI_EXIT_FAULT);
                        continue;
                    }
                    if (first_compile) {
                        trace_cpu_run_checkpoint("task.proof.a64_cpu_run.after_first_compile",
                                                 current, cpu, 0);
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
        }

        if (first_block_lookup) {
            trace_cpu_run_checkpoint("task.proof.a64_cpu_run.after_first_block_lookup", current,
                                     cpu, 0);
            first_block_lookup = false;
        }

        a64_trace_event("tcti.dispatch.lookup",
                        "tcti.dispatch.lookup=pc:0x%llx,block:%d,start:0x%llx,end:0x%llx,"
                        "gadgets:%zu,explicit:%d,total_before:%d",
                        (unsigned long long)pc, block ? 1 : 0,
                        block ? (unsigned long long)block->start_pc : 0ULL,
                        block ? (unsigned long long)block->end_pc : 0ULL,
                        block ? block->num_gadgets : 0, block && block->explicit_pc_on_exit ? 1 : 0,
                        total_blocks_executed);

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
            if (first_user_entry) {
                if (trace_is_active()) {
                    trace_pre_syscall_checkpoint("task.proof.user.first_block", cpu->pc,
                                                 block->start_pc, block->end_pc, 0, 1);
                    // Capture mapping info for first userspace PC
                    trace_pc_mapping_info("task.proof.user.pc_mapping.entry", cpu->pc);
                }
                first_user_entry = false;
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
        uint64_t pc_before_execute = cpu->pc;
        uint32_t writer_raw = 0;
        a64_instr_t writer_decoded;
        memset(&writer_decoded, 0, sizeof(writer_decoded));
        bool writer_decoded_valid = false;
        int branch_reg = -1;
        uint64_t branch_target_before_execute = 0;

        if (a64_fetch_insn(cpu, cpu->tlb, pc_before_execute, &writer_raw) == 0 &&
            a64_decode(writer_raw, &writer_decoded) == 0) {
            writer_decoded_valid = true;
            if ((writer_decoded.cat == A64_BRANCH || writer_decoded.cat == A64_BRANCH2) &&
                writer_decoded.subtype == A64_BRANCH_REG) {
                branch_reg = writer_decoded.Rn;
                branch_target_before_execute = a64_read_reg_or_sp(cpu, branch_reg, true);
            }
        }
        if (a64_verbose_block_trace_enabled()) {
            a64_trace_event("tcti.block.entry",
                            "tcti.block.entry=pc:0x%llx,raw:0x%08x,cat:%d,sub:%d,rd:%d,rn:%d,rm:%d,"
                            "imm:%lld,start:0x%llx,end:0x%llx,gadgets:%zu,explicit:%d,sp:0x%llx,"
                            "x0:0x%llx,x1:0x%llx,x2:0x%llx,x3:0x%llx,x4:0x%llx,x5:0x%llx,"
                            "x6:0x%llx,x7:0x%llx,x8:0x%llx,x9:0x%llx,x10:0x%llx,x11:0x%llx,"
                            "x12:0x%llx,x13:0x%llx,x14:0x%llx,x15:0x%llx,x16:0x%llx,x17:0x%llx,"
                            "x18:0x%llx,x19:0x%llx,x20:0x%llx,x21:0x%llx,x22:0x%llx,x23:0x%llx,"
                            "x24:0x%llx,x25:0x%llx,x26:0x%llx,x27:0x%llx,x28:0x%llx,x29:0x%llx,"
                            "x30:0x%llx",
                            (unsigned long long)pc_before_execute, writer_raw, writer_decoded.cat,
                            writer_decoded.subtype, writer_decoded.Rd, writer_decoded.Rn,
                            writer_decoded.Rm, (long long)writer_decoded.imm,
                            (unsigned long long)block->start_pc, (unsigned long long)block->end_pc,
                            block->num_gadgets, block->explicit_pc_on_exit ? 1 : 0,
                            (unsigned long long)cpu->sp, (unsigned long long)cpu->x[0],
                            (unsigned long long)cpu->x[1], (unsigned long long)cpu->x[2],
                            (unsigned long long)cpu->x[3], (unsigned long long)cpu->x[4],
                            (unsigned long long)cpu->x[5], (unsigned long long)cpu->x[6],
                            (unsigned long long)cpu->x[7], (unsigned long long)cpu->x[8],
                            (unsigned long long)cpu->x[9], (unsigned long long)cpu->x[10],
                            (unsigned long long)cpu->x[11], (unsigned long long)cpu->x[12],
                            (unsigned long long)cpu->x[13], (unsigned long long)cpu->x[14],
                            (unsigned long long)cpu->x[15], (unsigned long long)cpu->x[16],
                            (unsigned long long)cpu->x[17], (unsigned long long)cpu->x[18],
                            (unsigned long long)cpu->x[19], (unsigned long long)cpu->x[20],
                            (unsigned long long)cpu->x[21], (unsigned long long)cpu->x[22],
                            (unsigned long long)cpu->x[23], (unsigned long long)cpu->x[24],
                            (unsigned long long)cpu->x[25], (unsigned long long)cpu->x[26],
                            (unsigned long long)cpu->x[27], (unsigned long long)cpu->x[28],
                            (unsigned long long)cpu->x[29], (unsigned long long)cpu->x[30]);
        }
        int exit_reason = a64_execute_block(cpu, block);
        a64_trace_record_block_transition(pc_before_execute, cpu->pc, block, writer_raw,
                                          writer_decoded_valid ? &writer_decoded : NULL,
                                          exit_reason, branch_reg, branch_target_before_execute,
                                          cpu);
        if (branch_reg >= 0 && a64_verbose_block_trace_enabled()) {
            a64_trace_event(
                "tcti.branch.reg",
                "tcti.branch.reg=before:0x%llx,after:0x%llx,reason:%d,raw:0x%08x,"
                "rn:%d,target_before:0x%llx,aligned:%d,link_x30:0x%llx",
                (unsigned long long)pc_before_execute, (unsigned long long)cpu->pc, exit_reason,
                writer_raw, branch_reg, (unsigned long long)branch_target_before_execute,
                (branch_target_before_execute & 3) == 0 ? 1 : 0, (unsigned long long)cpu->x[30]);
        }
        if (a64_verbose_block_trace_enabled()) {
            a64_trace_event(
                "tcti.block.exit",
                "tcti.block.exit=before:0x%llx,after:0x%llx,reason:%d,start:0x%llx,end:0x%llx,"
                "explicit:%d,sp:0x%llx,pstate:0x%llx,x0:0x%llx,x1:0x%llx,x2:0x%llx,"
                "x3:0x%llx,x4:0x%llx,x5:0x%llx,x6:0x%llx,x7:0x%llx,x8:0x%llx,"
                "x9:0x%llx,x10:0x%llx,x11:0x%llx,x12:0x%llx,x13:0x%llx,x14:0x%llx,"
                "x15:0x%llx,x16:0x%llx,x17:0x%llx,x18:0x%llx,x19:0x%llx,x20:0x%llx,"
                "x21:0x%llx,x22:0x%llx,x23:0x%llx,x24:0x%llx,x25:0x%llx,x26:0x%llx,"
                "x27:0x%llx,x28:0x%llx,x29:0x%llx,x30:0x%llx",
                (unsigned long long)pc_before_execute, (unsigned long long)cpu->pc, exit_reason,
                (unsigned long long)block->start_pc, (unsigned long long)block->end_pc,
                block->explicit_pc_on_exit ? 1 : 0, (unsigned long long)cpu->sp,
                (unsigned long long)cpu->pstate, (unsigned long long)cpu->x[0],
                (unsigned long long)cpu->x[1], (unsigned long long)cpu->x[2],
                (unsigned long long)cpu->x[3], (unsigned long long)cpu->x[4],
                (unsigned long long)cpu->x[5], (unsigned long long)cpu->x[6],
                (unsigned long long)cpu->x[7], (unsigned long long)cpu->x[8],
                (unsigned long long)cpu->x[9], (unsigned long long)cpu->x[10],
                (unsigned long long)cpu->x[11], (unsigned long long)cpu->x[12],
                (unsigned long long)cpu->x[13], (unsigned long long)cpu->x[14],
                (unsigned long long)cpu->x[15], (unsigned long long)cpu->x[16],
                (unsigned long long)cpu->x[17], (unsigned long long)cpu->x[18],
                (unsigned long long)cpu->x[19], (unsigned long long)cpu->x[20],
                (unsigned long long)cpu->x[21], (unsigned long long)cpu->x[22],
                (unsigned long long)cpu->x[23], (unsigned long long)cpu->x[24],
                (unsigned long long)cpu->x[25], (unsigned long long)cpu->x[26],
                (unsigned long long)cpu->x[27], (unsigned long long)cpu->x[28],
                (unsigned long long)cpu->x[29], (unsigned long long)cpu->x[30]);
        }
        trace_cpu_run_checkpoint("task.proof.a64_cpu_run.exit_reason_set", current, cpu,
                                 exit_reason);

        if (first_execute) {
            trace_cpu_run_checkpoint("task.proof.a64_cpu_run.after_first_execute", current, cpu,
                                     exit_reason);
            first_execute = false;
        }

        // Check exit reason and handle
        if (exit_reason == TCTI_EXIT_SYSCALL) {
            if (trace_is_active() && interpreter_loop_active) {
                trace_interpreter_edge_checkpoint("task.proof.interpreter.syscall.before", cpu,
                                                  block->start_pc, block->end_pc,
                                                  same_block_repeat_count);
            }
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
            if (trace_is_active() && interpreter_loop_active) {
                trace_interpreter_edge_checkpoint("task.proof.interpreter.syscall.after", cpu,
                                                  block->start_pc, block->end_pc,
                                                  same_block_repeat_count);
            }
        } else if (exit_reason == TCTI_EXIT_FAULT) {
            trace_cpu_run_checkpoint("task.proof.a64_cpu_run.exit_fault", current, cpu,
                                     exit_reason);
            trace_startup_progress_event("task.proof.tcti.fault.full_regs", cpu, block, exit_reason,
                                         same_block_repeat_count, total_blocks_executed);

            trace_guest_first_fault_capture(cpu, guest_fault_signal);

            // Decode and trace the faulting instruction
            uint32_t raw_insn = 0;
            a64_instr_t decoded;
            int fault_rn = -1;
            int fault_rm = -1;
            int64_t fault_imm = 0;
            uint64_t fault_rn_value = 0;
            uint64_t fault_rm_value = 0;
            if (a64_fetch_insn(cpu, cpu->tlb, cpu->pc, &raw_insn) == 0 &&
                a64_decode(raw_insn, &decoded) == 0) {
                trace_insn_decode_checkpoint("task.proof.faulting_insn.decode", cpu->pc, raw_insn,
                                             decoded.cat, decoded.subtype, decoded.Rn, decoded.Rm,
                                             (int)decoded.imm);
                fault_rn = decoded.Rn;
                fault_rm = decoded.Rm;
                fault_imm = decoded.imm;
                if (fault_rn >= 0 && fault_rn < 31)
                    fault_rn_value = cpu->x[fault_rn];
                if (fault_rm >= 0 && fault_rm < 31)
                    fault_rm_value = cpu->x[fault_rm];
                if (a64_hot_ldso_pc(cpu->pc)) {
                    a64_trace_event(
                        "tcti.hot_fault",
                        "tcti.hot_fault=pc:0x%llx,raw:0x%08x,cat:%d,sub:%d,rd:%d,rn:%d,rm:%d,"
                        "imm:%lld,fault_addr:0x%llx,x0:0x%llx,x1:0x%llx,x7:0x%llx,x14:0x%llx,"
                        "x20:0x%llx,x21:0x%llx,x25:0x%llx,x28:0x%llx,sp:0x%llx,pstate:0x%llx",
                        (unsigned long long)cpu->pc, raw_insn, decoded.cat, decoded.subtype,
                        decoded.Rd, decoded.Rn, decoded.Rm, (long long)decoded.imm,
                        (unsigned long long)cpu->fault_addr, (unsigned long long)cpu->x[0],
                        (unsigned long long)cpu->x[1], (unsigned long long)cpu->x[7],
                        (unsigned long long)cpu->x[14], (unsigned long long)cpu->x[20],
                        (unsigned long long)cpu->x[21], (unsigned long long)cpu->x[25],
                        (unsigned long long)cpu->x[28], (unsigned long long)cpu->sp,
                        (unsigned long long)cpu->pstate);
                }
            }

            // Trace fault event with full context for first fault analysis
            trace_fault_origin_checkpoint("task.proof.first_fault.details", cpu->pc, fault_rn,
                                          fault_rn_value, fault_rm, fault_rm_value, fault_imm,
                                          cpu->fault_addr);
            trace_emit_fault(cpu->pc, cpu->fault_addr, cpu->fault_was_write, 0);

            // Dump sidecar and ring if configured
            trace_dump_on_fault(cpu->pc, cpu->fault_addr, cpu->fault_was_write);

            // Emit first-fault proof before INT_GPF handling can terminate the task.
            trace_guest_first_fault_events();
            a64_trace_emit_mem_history_on_fault(cpu, cpu->fault_addr);

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
                    if (decoded.cat == A64_BRANCH &&
                        (decoded.subtype == 2 || decoded.subtype == 4)) {
                        if (a64_sysreg_handle_complex(cpu, &decoded) != 0) {
                            if (decoded.subtype == 2)
                                trace_emit_unhandled_mrs(cpu->pc, decoded.sysreg);
                            else
                                trace_emit_unhandled_msr(cpu->pc, decoded.sysreg);
                            handle_interrupt(INT_GPF);
                        }
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
        } else if (exit_reason == TCTI_EXIT_UNSUPPORTED_SYSREG) {
            handle_interrupt(INT_GPF);
        } else {
            // Fallthrough blocks advance to end_pc. Control-transfer blocks preserve
            // the guest PC written by their terminal gadget.
            if (!block->explicit_pc_on_exit)
                cpu->pc = block->end_pc;
        }

        if (exit_reason == TCTI_EXIT_NORMAL) {
            static int normal_exit_flow_budget = 16;
            if (normal_exit_flow_budget > 0) {
                char ev[192];
                snprintf(ev, sizeof(ev),
                         "task.proof.a64_cpu_run.normal_exit_flow=before:0x%llx,start:0x%llx,end:"
                         "0x%llx,explicit:%d,after:0x%llx",
                         (unsigned long long)pc_before_execute, (unsigned long long)block->start_pc,
                         (unsigned long long)block->end_pc, block->explicit_pc_on_exit ? 1 : 0,
                         (unsigned long long)cpu->pc);
                trace_record_event(TRACE_ORIGIN_EXEC, ev);
                normal_exit_flow_budget--;
            }
        }

        if (exit_reason == TCTI_EXIT_NORMAL &&
            ((trace_should_emit_event("task.proof.a64_cpu_run.startup_progress") &&
              total_blocks_executed % 10 == 0) ||
             total_blocks_executed == 1000 || total_blocks_executed == 10000 ||
             total_blocks_executed == 100000 || same_block_repeat_count == 1000 ||
             same_block_repeat_count == 10000)) {
            trace_startup_progress_event("task.proof.a64_cpu_run.startup_progress", cpu, block,
                                         exit_reason, same_block_repeat_count,
                                         total_blocks_executed);
        }

        // Stage 3A.6: Track PC progression and detect loops
        // If we've executed many blocks without a syscall, we're in pre-syscall init
        if (total_blocks_executed >= 1000 && !first_user_entry && trace_is_active()) {
            trace_pre_syscall_checkpoint("task.proof.user.pre_syscall.loop_suspected", cpu->pc,
                                         last_block_start_pc, block->start_pc,
                                         same_block_repeat_count, total_blocks_executed);
            if ((total_blocks_executed % 1000) == 0) {
                static int progress_budget = 12;
                if (progress_budget > 0) {
                    uint32_t raw_insn = 0;
                    (void)a64_fetch_insn(cpu, cpu->tlb, cpu->pc, &raw_insn);
                    char ev[512];
                    snprintf(ev, sizeof(ev),
                             "task.proof.user.pre_syscall.progress=pc:0x%llx,raw:0x%08x,total:%d,"
                             "repeat:%d,block:0x%llx,x0:0x%llx,x1:0x%llx,x2:0x%llx,x3:0x%llx,"
                             "x4:0x%llx,x5:0x%llx,x8:0x%llx,pstate:0x%llx",
                             (unsigned long long)cpu->pc, raw_insn, total_blocks_executed,
                             same_block_repeat_count, (unsigned long long)block->start_pc,
                             (unsigned long long)cpu->x[0], (unsigned long long)cpu->x[1],
                             (unsigned long long)cpu->x[2], (unsigned long long)cpu->x[3],
                             (unsigned long long)cpu->x[4], (unsigned long long)cpu->x[5],
                             (unsigned long long)cpu->x[8], (unsigned long long)cpu->pstate);
                    trace_record_event(TRACE_ORIGIN_EXEC, ev);
                    progress_budget--;
                }
            }
            if (total_blocks_executed == 1000) {
                char ev[128];
                snprintf(ev, sizeof(ev), "task.proof.user.pre_syscall.first_stuck_pc=0x%llx",
                         (unsigned long long)cpu->pc);
                trace_record_event(TRACE_ORIGIN_EXEC, ev);

                char block_ev[192];
                snprintf(block_ev, sizeof(block_ev),
                         "task.proof.user.pre_syscall.first_stuck_block=start:0x%llx,end:0x%llx,"
                         "explicit:%d,repeat:%d",
                         (unsigned long long)block->start_pc, (unsigned long long)block->end_pc,
                         block->explicit_pc_on_exit ? 1 : 0, same_block_repeat_count);
                trace_record_event(TRACE_ORIGIN_EXEC, block_ev);

                uint32_t raw_insn = 0;
                if (a64_fetch_insn(cpu, cpu->tlb, cpu->pc, &raw_insn) == 0) {
                    char raw_ev[96];
                    snprintf(raw_ev, sizeof(raw_ev),
                             "task.proof.user.pre_syscall.first_stuck_raw=0x%08x", raw_insn);
                    trace_record_event(TRACE_ORIGIN_EXEC, raw_ev);

                    a64_instr_t decoded;
                    if (a64_decode(raw_insn, &decoded) == 0) {
                        char dec_ev[160];
                        snprintf(dec_ev, sizeof(dec_ev),
                                 "task.proof.user.pre_syscall.first_stuck_decode=cat:%d,sub:%d,rn:%"
                                 "d,rm:%d,imm:%d",
                                 decoded.cat, decoded.subtype, decoded.Rn, decoded.Rm,
                                 (int)decoded.imm);
                        trace_record_event(TRACE_ORIGIN_EXEC, dec_ev);
                    }
                }
            }
            // Capture mapping info for stuck PC
            trace_pc_mapping_info("task.proof.user.pc_mapping.stuck", cpu->pc);
        }

        // Normal exit - PC already advanced, continue to next block
    }

    {
        char pc_buf[32];
        char iterations_buf[32];
        char total_buf[32];
        snprintf(pc_buf, sizeof(pc_buf), "0x%llx", cpu ? (unsigned long long)cpu->pc : 0ULL);
        snprintf(iterations_buf, sizeof(iterations_buf), "%d", iteration_count);
        snprintf(total_buf, sizeof(total_buf), "%d", total_blocks_executed);
        ixland_instrumentation_attribute_t attrs[] = {
            { .key = "reason", .value = stop_reason },
            { .key = "guest_pc", .value = pc_buf },
            { .key = "iterations", .value = iterations_buf },
            { .key = "total_blocks", .value = total_buf },
        };
        ixland_guest_trace_emit_attrs(IXLAND_INSTRUMENTATION_ORIGIN_EMULATOR,
                                      "guest.a64_cpu_run.stop", attrs,
                                      sizeof(attrs) / sizeof(attrs[0]));
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
    bool is_pair = instr->subtype == A64_LDST_PAIR;
    bool is_literal = instr->subtype == A64_LDST_LITERAL;
    bool is_load = is_pair ? bit(raw, 22) : a64_ldst_raw_is_load(raw);
    bool is_reg_offset = a64_ldst_uses_register_offset(raw, instr);
    bool writeback = instr->idx_mode == A64_PRE_INDEX || instr->idx_mode == A64_POST_INDEX;
    int access = is_load ? MEM_READ : MEM_WRITE;
    uint64_t base = 0;
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
            a64_trace_record_mem_access(cpu, instr, addr, first, (uint8_t)width, true);
            a64_trace_record_mem_access(cpu, instr, addr + width, second, (uint8_t)width, true);
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
            a64_trace_record_mem_access(cpu, instr, addr, first, (uint8_t)width, false);
            a64_trace_record_mem_access(cpu, instr, addr + width, second, (uint8_t)width, false);
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
        a64_write_reg_or_sp(cpu, instr->Rd, value,
                            instr->size == A64_SIZE_X || (instr->is_signed && instr->is_64bit));
        a64_trace_record_mem_access(cpu, instr, addr, value, (uint8_t)(1u << instr->size), true);
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
        a64_trace_record_mem_access(cpu, instr, addr, value, (uint8_t)(1u << instr->size), false);
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
