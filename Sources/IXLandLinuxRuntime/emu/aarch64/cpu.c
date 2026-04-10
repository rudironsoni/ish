/*
 * aarch64 CPU execution main loop
 *
 * Bridges the TCTI gadgets with iSH's process model.
 */

#import <IXLandInstrumentationTracing/trace.h>
#import <IXLandLinuxRuntime/emu/aarch64/block-cache.h>
#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>
#import <IXLandLinuxRuntime/emu/aarch64/memory.h>
#import <IXLandLinuxRuntime/emu/aarch64/sysreg.h>
#import <IXLandLinuxRuntime/emu/interrupt.h>
#import <IXLandLinuxRuntime/emu/mmu.h>
#import <IXLandLinuxRuntime/emu/tlb.h>
#import <IXLandLinuxRuntime/kernel/calls.h>
#import <IXLandLinuxRuntime/kernel/guest_trace_context.h>
#import <IXLandLinuxRuntime/kernel/mem_object.h>
#import <IXLandLinuxRuntime/kernel/page_map.h>
#import <IXLandLinuxRuntime/kernel/task.h>
#import <IXLandLinuxRuntime/tcti/aarch64/gen.h>
#import <IXLandLinuxRuntime/tcti/frame.h>
#import <IXLandLinuxRuntime/tcti/gadgets_tcti.h>
#include <dlfcn.h>
#include <setjmp.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// External TCTI entry point (declared in gadgets_tcti.h)
extern void tcti_entry_block(void *gadgets, struct cpu_state *cpu);

// Execution state is now passed via parameters, not globals
static jmp_buf exit_jmpbuf __attribute__((unused));

// Fault containment state for guest execution
static jmp_buf guest_fault_jmpbuf;
static volatile int guest_fault_signal = 0;

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

typedef struct {
    int valid;
    uint64_t writer_pc;
    uint32_t writer_raw;
    char writer_mnemonic[128];
    int writer_rd;
    int writer_rn;
    uint64_t x3_before;
    uint64_t x3_after;
    uint64_t block_start;
    uint64_t block_end;
} base6a628_last_writer_t;

typedef struct {
    int valid;
    uint64_t writer_pc;
    uint32_t writer_raw;
    char writer_mnemonic[128];
    int writer_rd;
    int writer_rn;
    uint64_t x0_before;
    uint64_t x0_after;
    uint64_t block_start;
    uint64_t block_end;
} x0chain_last_writer_t;

typedef struct {
    int valid;
    uint64_t writer_pc;
    uint32_t writer_raw;
    char writer_mnemonic[128];
    int writer_rd;
    int writer_rn;
    int writer_rm;
    int writer_idx_mode;
    int64_t writer_imm;
    uint64_t x2_before;
    uint64_t x2_after;
    uint64_t block_start;
    uint64_t block_end;
} str6a650_last_writer_t;

typedef struct {
    int valid;
    uint64_t writer_pc;
    uint32_t writer_raw;
    char writer_mnemonic[128];
    int writer_rd;
    int writer_rn;
    int writer_rm;
    int writer_idx_mode;
    int64_t writer_imm;
    uint64_t x7_before;
    uint64_t x7_after;
    uint64_t block_start;
    uint64_t block_end;
} x7chain_last_writer_t;

static base6a628_last_writer_t g_base6a628_last_writer;
static int g_base6a628_trace_budget = 32;
static x0chain_last_writer_t g_x0chain_last_writer;
static int g_x0chain_trace_budget = 32;
static str6a650_last_writer_t g_str6a650_last_writer;
static int g_str6a650_trace_budget = 64;
static x7chain_last_writer_t g_x7chain_last_writer;
static int g_x7chain_trace_budget = 96;
static int g_insn64_trace_budget = 128;
static int g_guest_first_user_pc_emitted = 0;

static uint64_t trace_ldst_reg_or_zr(struct cpu_state *cpu, int reg);
static uint64_t a64_read_reg_or_sp(struct cpu_state *cpu, int reg, bool is_64bit);
static uint64_t a64_extend_index(uint64_t value, int extend_type);
static const char *trace_ldst_mnemonic(int is_load, int size, int is_signed);

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

    if (decoded.Rd >= 0 && decoded.Rd <= 31)
        g_guest_first_fault.rt_val = trace_ldst_reg_or_zr(cpu, decoded.Rd);
    if (decoded.Rn >= 0 && decoded.Rn <= 31)
        g_guest_first_fault.rn_val = a64_read_reg_or_sp(cpu, decoded.Rn, true);
    if (decoded.Rm >= 0 && decoded.Rm <= 31)
        g_guest_first_fault.rm_val = trace_ldst_reg_or_zr(cpu, decoded.Rm);

    if (decoded.cat == A64_LD_ST) {
        if (decoded.subtype == A64_LDST_SINGLE) {
            int is_reg_offset = bits(raw, 11, 10) == 2 ? 1 : 0;
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
        { .key = "translation_fault", .value = trans_buf },
    };
    ixland_guest_trace_emit_attrs(IXLAND_INSTRUMENTATION_ORIGIN_EMULATOR,
                                  "guest.first_fault.translation", trans_attrs,
                                  sizeof(trans_attrs) / sizeof(trans_attrs[0]));

    ixland_instrumentation_attribute_t exit_attrs[] = {
        { .key = "host_signal", .value = host_sig_buf },
        { .key = "translation_fault", .value = trans_buf },
    };
    ixland_guest_trace_emit_attrs(IXLAND_INSTRUMENTATION_ORIGIN_EMULATOR, "guest.first_fault.exit",
                                  exit_attrs, sizeof(exit_attrs) / sizeof(exit_attrs[0]));
}

static void trace_base6a628_event(const char *name, const char *payload)
{
    if (!name || !payload || g_base6a628_trace_budget <= 0)
        return;

    char ev[768];
    snprintf(ev, sizeof(ev), "%s=%s", name, payload);
    trace_record_event(TRACE_ORIGIN_EXEC, ev);
    g_base6a628_trace_budget--;
}

static void trace_x0chain_event(const char *name, const char *payload)
{
    if (!name || !payload || g_x0chain_trace_budget <= 0)
        return;

    char ev[768];
    snprintf(ev, sizeof(ev), "%s=%s", name, payload);
    trace_record_event(TRACE_ORIGIN_EXEC, ev);
    g_x0chain_trace_budget--;
}

static void trace_str6a650_event(const char *name, const char *payload)
{
    if (!name || !payload || g_str6a650_trace_budget <= 0)
        return;

    ixland_instrumentation_attribute_t attrs[] = {
        { .key = "payload", .value = payload },
    };
    ixland_guest_trace_emit_attrs(IXLAND_INSTRUMENTATION_ORIGIN_EMULATOR, name, attrs,
                                  sizeof(attrs) / sizeof(attrs[0]));
    g_str6a650_trace_budget--;
}

static void trace_str6a650_event_fields(const char *name, const ixland_guest_trace_field_t *fields,
                                        uint32_t field_count)
{
    if (!name || !fields || field_count == 0 || g_str6a650_trace_budget <= 0)
        return;

    ixland_guest_trace_emit_structured(IXLAND_INSTRUMENTATION_ORIGIN_EMULATOR, name, fields,
                                       field_count);
    g_str6a650_trace_budget--;
}

static void trace_x7chain_event(const char *name, const char *payload)
{
    if (!name || !payload || g_x7chain_trace_budget <= 0)
        return;

    ixland_instrumentation_attribute_t attrs[] = {
        { .key = "payload", .value = payload },
    };
    ixland_guest_trace_emit_attrs(IXLAND_INSTRUMENTATION_ORIGIN_EMULATOR, name, attrs,
                                  sizeof(attrs) / sizeof(attrs[0]));
    g_x7chain_trace_budget--;
}

static void trace_x7chain_event_fields(const char *name, const ixland_guest_trace_field_t *fields,
                                       uint32_t field_count)
{
    if (!name || !fields || field_count == 0 || g_x7chain_trace_budget <= 0)
        return;

    ixland_guest_trace_emit_structured(IXLAND_INSTRUMENTATION_ORIGIN_EMULATOR, name, fields,
                                       field_count);
    g_x7chain_trace_budget--;
}

static void trace_insn64_event(const char *name, const char *payload)
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

static void trace_insn64_event_fields(const char *name, const ixland_guest_trace_field_t *fields,
                                      uint32_t field_count)
{
    if (!name || !fields || field_count == 0 || g_insn64_trace_budget <= 0)
        return;

    ixland_guest_trace_emit_structured(IXLAND_INSTRUMENTATION_ORIGIN_EMULATOR, name, fields,
                                       field_count);
    g_insn64_trace_budget--;
}

static void trace_block6a640_bytecode(struct a64_block *block)
{
    if (!block || block->start_pc != 0x6a640ULL || !block->gadgets)
        return;

    ixland_guest_trace_field_t start_fields[] = {
        TRACE_FIELD_U64_HEX("guest_pc", block->start_pc),
        TRACE_FIELD_U64_HEX("block_start", block->start_pc),
        TRACE_FIELD_U64_HEX("block_end", block->end_pc),
        TRACE_FIELD_U64_DEC("num_gadgets", block->num_gadgets),
    };
    trace_insn64_event_fields("block6a640.bytecode.start", start_fields,
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
            trace_insn64_event_fields("block6a640.bytecode.imm", imm_fields,
                                      sizeof(imm_fields) / sizeof(imm_fields[0]));
        } else {
            ixland_guest_trace_field_t gadget_fields[] = {
                TRACE_FIELD_U64_HEX("guest_pc", block->start_pc),
                TRACE_FIELD_U64_HEX("block_start", block->start_pc),
                TRACE_FIELD_U64_DEC("index", i),
                TRACE_FIELD_STR("symbol", symbol),
                TRACE_FIELD_U64_HEX("addr", (uint64_t)(uintptr_t)entry),
            };
            trace_insn64_event_fields("block6a640.bytecode.gadget", gadget_fields,
                                      sizeof(gadget_fields) / sizeof(gadget_fields[0]));
        }
    }

    ixland_guest_trace_field_t end_fields[] = {
        TRACE_FIELD_U64_HEX("guest_pc", block->start_pc),
        TRACE_FIELD_U64_HEX("block_start", block->start_pc),
        TRACE_FIELD_U64_HEX("block_end", block->end_pc),
        TRACE_FIELD_U64_DEC("emitted_items", max_items),
    };
    trace_insn64_event_fields("block6a640.bytecode.end", end_fields,
                              sizeof(end_fields) / sizeof(end_fields[0]));
}

static uint64_t trace_ldst_effective_address(struct cpu_state *cpu, uint32_t raw,
                                             const a64_instr_t *instr, uint64_t rn_val,
                                             uint64_t rm_val)
{
    if (!cpu || !instr)
        return 0;

    if (instr->subtype != A64_LDST_SINGLE)
        return cpu->fault_addr;

    if (bits(raw, 11, 10) == 2) {
        uint64_t off = a64_extend_index(rm_val, instr->extend_type);
        return rn_val + (off << instr->imm_shift);
    }

    switch (instr->idx_mode) {
    case A64_PRE_INDEX:
        return rn_val + instr->imm;
    case A64_POST_INDEX:
        return rn_val;
    case A64_INDEX_OFFSET:
    default:
        return rn_val + instr->imm;
    }
}

static bool trace_instr_writes_rd(const a64_instr_t *instr, uint32_t raw)
{
    if (!instr)
        return false;

    switch (instr->cat) {
    case A64_BRANCH:
    case A64_BRANCH2:
        return false;

    case A64_DP_REG:
    case A64_DP_REG2:
    case A64_DP_REG3:
    case A64_DP_REG4:
        // CCMN/CCMP update flags only; they do not write Rd.
        if (instr->subtype == 4 || instr->subtype == 5)
            return false;
        return instr->Rd >= 0 && instr->Rd <= 31;

    case A64_LD_ST:
        // Single/pair forms share bit 22 as load/store selector.
        if (instr->subtype == A64_LDST_SINGLE || instr->subtype == A64_LDST_PAIR)
            return bit(raw, 22) != 0;
        if (instr->subtype == A64_LDST_LITERAL)
            return true;
        return false;

    case A64_SIMD:
    case A64_SIMD2:
    case A64_SIMD0:
    case A64_DP_IMM:
    case A64_DP_IMM2:
        return instr->Rd >= 0 && instr->Rd <= 31;

    default:
        return false;
    }
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
            host_ptr_from_map = (void *)((byte_t *)obj->host_base + host_off);
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
static void guest_fault_handler(int sig)
{
    guest_fault_signal = sig;
    longjmp(guest_fault_jmpbuf, 1);
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

static void trace_69650_block_window(struct cpu_state *cpu, struct tlb *tlb,
                                     struct a64_block *block)
{
    static int captured = 0;
    static int captured_69634 = 0;
    if (captured || !cpu || !tlb || !block)
        return;
    int has_69650 = (block->start_pc <= 0x69650ULL && 0x69650ULL < block->end_pc);
    int has_69634 = (block->start_pc <= 0x69634ULL && 0x69634ULL < block->end_pc);
    if (!has_69650 && !has_69634)
        return;

    if (has_69650)
        captured = 1;

    char block_start_buf[24];
    char block_end_buf[24];
    char x0_buf[24];
    char x1_buf[24];
    char x2_buf[24];
    char x3_buf[24];
    char x4_buf[24];
    char x5_buf[24];
    char sp_buf[24];
    char pc_buf[24];

    snprintf(block_start_buf, sizeof(block_start_buf), "0x%llx",
             (unsigned long long)block->start_pc);
    snprintf(block_end_buf, sizeof(block_end_buf), "0x%llx", (unsigned long long)block->end_pc);
    snprintf(x0_buf, sizeof(x0_buf), "0x%llx", (unsigned long long)cpu->x[0]);
    snprintf(x1_buf, sizeof(x1_buf), "0x%llx", (unsigned long long)cpu->x[1]);
    snprintf(x2_buf, sizeof(x2_buf), "0x%llx", (unsigned long long)cpu->x[2]);
    snprintf(x3_buf, sizeof(x3_buf), "0x%llx", (unsigned long long)cpu->x[3]);
    snprintf(x4_buf, sizeof(x4_buf), "0x%llx", (unsigned long long)cpu->x[4]);
    snprintf(x5_buf, sizeof(x5_buf), "0x%llx", (unsigned long long)cpu->x[5]);
    snprintf(sp_buf, sizeof(sp_buf), "0x%llx", (unsigned long long)cpu->sp);
    snprintf(pc_buf, sizeof(pc_buf), "0x%llx", (unsigned long long)cpu->pc);

    {
        char ev[320];
        snprintf(ev, sizeof(ev),
                 "task.proof.69650.block_entry=block_start:%s,block_end:%s,x0:%s,x1:%s,x2:%s,x3:%s,"
                 "x4:%s,x5:%s,x7:0x%llx,sp:%s,pc:%s",
                 block_start_buf, block_end_buf, x0_buf, x1_buf, x2_buf, x3_buf, x4_buf, x5_buf,
                 (unsigned long long)cpu->x[7], sp_buf, pc_buf);
        trace_record_event(TRACE_ORIGIN_EXEC, ev);
    }

    if (has_69650) {
        uint64_t prev_pc = 0;
        uint64_t next_pc = 0x69654ULL;
        uint64_t last_x2_write_pc = 0;
        uint32_t last_x2_write_raw = 0;
        a64_instr_t last_x2_write_decoded = { 0 };
        int have_last_x2_write = 0;

        uint64_t start_pc = block->start_pc;
        if (start_pc + 8 * 4 < 0x69650ULL)
            start_pc = 0x69650ULL - 8 * 4;

        for (uint64_t insn_pc = start_pc; insn_pc <= 0x69650ULL; insn_pc += 4) {
            uint32_t raw = 0;
            a64_instr_t decoded = { 0 };
            if (a64_fetch_insn(cpu, tlb, insn_pc, &raw) != 0 || a64_decode(raw, &decoded) != 0)
                continue;

            if (insn_pc == 0x69650ULL && insn_pc >= 4)
                prev_pc = insn_pc - 4;

            if (decoded.Rd == 2) {
                last_x2_write_pc = insn_pc;
                last_x2_write_raw = raw;
                last_x2_write_decoded = decoded;
                have_last_x2_write = 1;
            }

            char ev[256];
            snprintf(
                ev, sizeof(ev),
                "task.proof.69650.window.insn=pc:0x%llx,raw:0x%08x,cat:%d,sub:%d,rd:%d,rn:%d,rm:%"
                "d,idx:%d,size:%d,imm:%lld",
                (unsigned long long)insn_pc, raw, decoded.cat, decoded.subtype, decoded.Rd,
                decoded.Rn, decoded.Rm, decoded.idx_mode, decoded.size, (long long)decoded.imm);
            trace_record_event(TRACE_ORIGIN_EXEC, ev);
        }

        {
            char ev[224];
            snprintf(
                ev, sizeof(ev),
                "task.proof.69650.window.flow=prev_pc:0x%llx,current_pc:0x69650,next_pc:0x%llx,"
                "block_start:0x%llx,block_end:0x%llx,x2_block_entry:0x%llx,x7_block_entry:0x%llx",
                (unsigned long long)prev_pc, (unsigned long long)next_pc,
                (unsigned long long)block->start_pc, (unsigned long long)block->end_pc,
                (unsigned long long)cpu->x[2], (unsigned long long)cpu->x[7]);
            trace_record_event(TRACE_ORIGIN_EXEC, ev);
        }

        if (have_last_x2_write) {
            char ev[256];
            snprintf(
                ev, sizeof(ev),
                "task.proof.69650.window.last_x2_write=pc:0x%llx,raw:0x%08x,cat:%d,sub:%d,rd:%d,"
                "rn:%d,rm:%d,idx:%d,size:%d,imm:%lld",
                (unsigned long long)last_x2_write_pc, last_x2_write_raw, last_x2_write_decoded.cat,
                last_x2_write_decoded.subtype, last_x2_write_decoded.Rd, last_x2_write_decoded.Rn,
                last_x2_write_decoded.Rm, last_x2_write_decoded.idx_mode,
                last_x2_write_decoded.size, (long long)last_x2_write_decoded.imm);
            trace_record_event(TRACE_ORIGIN_EXEC, ev);
        }
    }

    if (!captured_69634 && block->start_pc <= 0x69634ULL && 0x69634ULL < block->end_pc) {
        captured_69634 = 1;

        int guest_pid = current ? current->pid : -1;
        int attempt_id = -1;

        char ev[320];
        snprintf(ev, sizeof(ev),
                 "ldr69634.block_entry=attempt:%d,guest_pid:%d,block_start:0x%llx,block_end:0x%llx,"
                 "x2:0x%llx,w2:0x%x,x3:0x%llx,x7:0x%llx,sp:0x%llx,pc:0x%llx",
                 attempt_id, guest_pid, (unsigned long long)block->start_pc,
                 (unsigned long long)block->end_pc, (unsigned long long)cpu->x[2],
                 (unsigned)((uint32_t)cpu->x[2]), (unsigned long long)cpu->x[3],
                 (unsigned long long)cpu->x[7], (unsigned long long)cpu->sp,
                 (unsigned long long)cpu->pc);
        trace_record_event(TRACE_ORIGIN_EXEC, ev);

        uint64_t last_x2_pc = 0;
        uint32_t last_x2_raw = 0;
        a64_instr_t last_x2_decoded = { 0 };
        int have_last_69634_x2 = 0;

        uint64_t last_x3_pc = 0;
        uint32_t last_x3_raw = 0;
        a64_instr_t last_x3_decoded = { 0 };
        int have_last_69634_x3 = 0;

        uint64_t last_w2_pc_before_69630 = 0;
        uint32_t last_w2_raw_before_69630 = 0;
        a64_instr_t last_w2_decoded_before_69630 = { 0 };
        int have_last_w2_before_69630 = 0;

        for (uint64_t insn_pc = block->start_pc; insn_pc <= 0x69634ULL; insn_pc += 4) {
            uint32_t raw = 0;
            a64_instr_t decoded = { 0 };
            if (a64_fetch_insn(cpu, tlb, insn_pc, &raw) != 0 || a64_decode(raw, &decoded) != 0)
                continue;
            if (decoded.Rd == 2) {
                last_x2_pc = insn_pc;
                last_x2_raw = raw;
                last_x2_decoded = decoded;
                have_last_69634_x2 = 1;
                if (insn_pc < 0x69630ULL) {
                    last_w2_pc_before_69630 = insn_pc;
                    last_w2_raw_before_69630 = raw;
                    last_w2_decoded_before_69630 = decoded;
                    have_last_w2_before_69630 = 1;
                }
            }
            if (decoded.Rd == 3) {
                last_x3_pc = insn_pc;
                last_x3_raw = raw;
                last_x3_decoded = decoded;
                have_last_69634_x3 = 1;
            }
        }

        if (have_last_69634_x2) {
            snprintf(ev, sizeof(ev),
                     "ldr69634.last_x2_writer=pc:0x%llx,raw:0x%08x,cat:%d,sub:%d,rd:%d,rn:%d,"
                     "rm:%d,idx:%d,size:%d,imm:%lld",
                     (unsigned long long)last_x2_pc, last_x2_raw, last_x2_decoded.cat,
                     last_x2_decoded.subtype, last_x2_decoded.Rd, last_x2_decoded.Rn,
                     last_x2_decoded.Rm, last_x2_decoded.idx_mode, last_x2_decoded.size,
                     (long long)last_x2_decoded.imm);
            trace_record_event(TRACE_ORIGIN_EXEC, ev);

            snprintf(
                ev, sizeof(ev),
                "x2.provenance.last_writer=attempt:%d,guest_pid:%d,pc:0x%llx,raw:0x%08x,"
                "cat:%d,sub:%d,rd:%d,rn:%d,rm:%d,idx:%d,size:%d,imm:%lld,x2_block_entry:0x%llx,"
                "w2_block_entry:0x%x,x3_block_entry:0x%llx",
                attempt_id, guest_pid, (unsigned long long)last_x2_pc, last_x2_raw,
                last_x2_decoded.cat, last_x2_decoded.subtype, last_x2_decoded.Rd,
                last_x2_decoded.Rn, last_x2_decoded.Rm, last_x2_decoded.idx_mode,
                last_x2_decoded.size, (long long)last_x2_decoded.imm, (unsigned long long)cpu->x[2],
                (unsigned)((uint32_t)cpu->x[2]), (unsigned long long)cpu->x[3]);
            trace_record_event(TRACE_ORIGIN_EXEC, ev);
        }

        if (have_last_69634_x3) {
            snprintf(
                ev, sizeof(ev),
                "x3.provenance.last_writer=attempt:%d,guest_pid:%d,pc:0x%llx,raw:0x%08x,"
                "cat:%d,sub:%d,rd:%d,rn:%d,rm:%d,idx:%d,size:%d,imm:%lld,x3_block_entry:0x%llx",
                attempt_id, guest_pid, (unsigned long long)last_x3_pc, last_x3_raw,
                last_x3_decoded.cat, last_x3_decoded.subtype, last_x3_decoded.Rd,
                last_x3_decoded.Rn, last_x3_decoded.Rm, last_x3_decoded.idx_mode,
                last_x3_decoded.size, (long long)last_x3_decoded.imm,
                (unsigned long long)cpu->x[3]);
            trace_record_event(TRACE_ORIGIN_EXEC, ev);
        }

        {
            uint32_t raw_69630 = 0;
            a64_instr_t dec_69630 = { 0 };
            int have_69630 = (a64_fetch_insn(cpu, tlb, 0x69630ULL, &raw_69630) == 0 &&
                              a64_decode(raw_69630, &dec_69630) == 0);
            uint32_t w2_pre = (uint32_t)cpu->x[2];
            uint64_t x2_post = (uint64_t)(int64_t)(int32_t)w2_pre;

            snprintf(
                ev, sizeof(ev),
                "w2.provenance.pre_69630=attempt:%d,guest_pid:%d,pc:0x69630,raw:0x%08x,"
                "canonical:sbfm x2,x2,#0,#31,alias:sxtw x2,w2,w2_pre:0x%x,x2_pre:0x%llx,"
                "last_w2_writer_pc:0x%llx,last_w2_writer_raw:0x%08x,last_w2_writer_cat:%d,"
                "last_w2_writer_sub:%d,last_w2_writer_rd:%d,last_w2_writer_rn:%d,last_w2_writer_rm:"
                "%d",
                attempt_id, guest_pid, raw_69630, (unsigned)w2_pre, (unsigned long long)cpu->x[2],
                (unsigned long long)(have_last_w2_before_69630 ? last_w2_pc_before_69630 : 0ULL),
                have_last_w2_before_69630 ? last_w2_raw_before_69630 : 0U,
                have_last_w2_before_69630 ? last_w2_decoded_before_69630.cat : -1,
                have_last_w2_before_69630 ? last_w2_decoded_before_69630.subtype : -1,
                have_last_w2_before_69630 ? last_w2_decoded_before_69630.Rd : -1,
                have_last_w2_before_69630 ? last_w2_decoded_before_69630.Rn : -1,
                have_last_w2_before_69630 ? last_w2_decoded_before_69630.Rm : -1);
            trace_record_event(TRACE_ORIGIN_EXEC, ev);

            snprintf(ev, sizeof(ev),
                     "w2.provenance.post_69630=attempt:%d,guest_pid:%d,pc:0x69630,raw:0x%08x,"
                     "canonical:sbfm x2,x2,#0,#31,alias:sxtw x2,w2,w2_post:0x%x,x2_post:0x%llx,"
                     "decode_cat:%d,decode_sub:%d,decode_rd:%d,decode_rn:%d,decode_rm:%d,decode_"
                     "imm:%lld",
                     attempt_id, guest_pid, raw_69630, (unsigned)w2_pre,
                     (unsigned long long)x2_post, have_69630 ? dec_69630.cat : -1,
                     have_69630 ? dec_69630.subtype : -1, have_69630 ? dec_69630.Rd : -1,
                     have_69630 ? dec_69630.Rn : -1, have_69630 ? dec_69630.Rm : -1,
                     (long long)(have_69630 ? dec_69630.imm : 0));
            trace_record_event(TRACE_ORIGIN_EXEC, ev);
        }
    }
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

    bool conservative_mode = a64_conservative_mode_enabled();
    a64_gen_set_conservative_mode(&gen_state, conservative_mode ? 1 : 0);

    // Translate instructions until block end
    int max_insns = conservative_mode ? 1 : 50;
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

        if (ret == 1 || gen_state.is_complete) {
            // Block should end when generator marks terminal semantics.
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

    trace_block6a640_bytecode(block);

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

    // Set up fault containment for guest execution
    // Host signals (SIGSEGV, SIGBUS, etc.) during guest execution will be
    // caught and converted to TCTI_EXIT_FAULT instead of killing the process
    struct sigaction old_segv, old_bus, old_ill, old_fpe;
    guest_fault_signal = 0;

    // Install fault containment handlers
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = guest_fault_handler;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGSEGV, &sa, &old_segv);
    sigaction(SIGBUS, &sa, &old_bus);
    sigaction(SIGILL, &sa, &old_ill);
    sigaction(SIGFPE, &sa, &old_fpe);

    if (setjmp(guest_fault_jmpbuf) == 0) {
        // Normal execution path
        if (block->start_pc == 0x6a628ULL) {
            char payload[512];
            snprintf(payload, sizeof(payload),
                     "attempt:-1,guest_pid:%d,guest_pc:0x%llx,block_start:0x%llx,block_end:0x%llx,"
                     "arch_base_reg:3,arch_base_val:0x%llx,carrier_val:0x%llx,cpu_slot_val:0x%llx,"
                     "is_zero:%d,helper_path_reached:0,signal_immediate:0",
                     current ? current->pid : -1, (unsigned long long)cpu->pc,
                     (unsigned long long)block->start_pc, (unsigned long long)block->end_pc,
                     (unsigned long long)cpu->x[3], (unsigned long long)cpu->x[3],
                     (unsigned long long)cpu->x[3], cpu->x[3] == 0 ? 1 : 0);
            trace_base6a628_event("base6a628.pre_sync", payload);
        }
        if (block->start_pc == 0x6a620ULL) {
            char payload[512];
            snprintf(payload, sizeof(payload),
                     "attempt:-1,guest_pid:%d,guest_pc:0x%llx,block_start:0x%llx,block_end:0x%llx,"
                     "x0_val:0x%llx,carrier_val:0x%llx,cpu_slot_val:0x%llx,is_zero:%d",
                     current ? current->pid : -1, (unsigned long long)cpu->pc,
                     (unsigned long long)block->start_pc, (unsigned long long)block->end_pc,
                     (unsigned long long)cpu->x[0], (unsigned long long)cpu->x[0],
                     (unsigned long long)cpu->x[0], cpu->x[0] == 0 ? 1 : 0);
            trace_x0chain_event("x0chain.pre_sync", payload);
        }
        if (block->start_pc == 0x6a650ULL) {
            ixland_guest_trace_field_t fields[] = {
                TRACE_FIELD_U64_HEX("guest_pc", cpu->pc),
                TRACE_FIELD_U64_HEX("raw_opcode", cpu->pc == 0x6a650ULL ? 0xf800845fU : 0),
                TRACE_FIELD_STR("mnemonic", "str"),
                TRACE_FIELD_I64("base_reg", 2),
                TRACE_FIELD_U64_HEX("base_val", cpu->x[2]),
                TRACE_FIELD_I64("src_reg", 31),
                TRACE_FIELD_U64_HEX("src_val", 0),
                TRACE_FIELD_I64("signal_follow_immediate", 0),
            };
            trace_str6a650_event_fields("str6a650.pre_sync", fields,
                                        sizeof(fields) / sizeof(fields[0]));
        }
        if (block->start_pc == 0x6a640ULL) {
            ixland_guest_trace_field_t fields[] = {
                TRACE_FIELD_U64_HEX("guest_pc", 0x6a640ULL),
                TRACE_FIELD_U64_HEX("raw_opcode", 0x910023e7ULL),
                TRACE_FIELD_STR("mnemonic", "add x7,sp,#8"),
                TRACE_FIELD_U64_HEX("sp", cpu->sp),
                TRACE_FIELD_U64_HEX("x7", cpu->x[7]),
                TRACE_FIELD_U64_HEX("x2", cpu->x[2]),
                TRACE_FIELD_U64_HEX("block_start", block->start_pc),
                TRACE_FIELD_U64_HEX("block_end", block->end_pc),
            };
            trace_insn64_event_fields("insn6a640.pre_sync", fields,
                                      sizeof(fields) / sizeof(fields[0]));
        }
        if (block->start_pc == 0x6a64cULL) {
            ixland_guest_trace_field_t fields[] = {
                TRACE_FIELD_U64_HEX("guest_pc", cpu->pc),
                TRACE_FIELD_U64_HEX("raw_opcode", 0xaa0703e2U),
                TRACE_FIELD_STR("mnemonic", "mov_x2_x7"),
                TRACE_FIELD_U64_HEX("x7", cpu->x[7]),
                TRACE_FIELD_U64_HEX("x2", cpu->x[2]),
                TRACE_FIELD_U64_HEX("block_start", block->start_pc),
                TRACE_FIELD_U64_HEX("block_end", block->end_pc),
            };
            trace_x7chain_event_fields("mov6a64c.pre_sync", fields,
                                       sizeof(fields) / sizeof(fields[0]));
        }
        tcti_entry_block(block->gadgets, cpu);
        if (block->start_pc == 0x6a628ULL) {
            char payload[512];
            snprintf(payload, sizeof(payload),
                     "attempt:-1,guest_pid:%d,guest_pc:0x%llx,block_start:0x%llx,block_end:0x%llx,"
                     "arch_base_reg:3,arch_base_val:0x%llx,carrier_val:0x%llx,cpu_slot_val:0x%llx,"
                     "is_zero:%d,helper_path_reached:1,signal_immediate:%d",
                     current ? current->pid : -1, (unsigned long long)cpu->pc,
                     (unsigned long long)block->start_pc, (unsigned long long)block->end_pc,
                     (unsigned long long)cpu->x[3], (unsigned long long)cpu->x[3],
                     (unsigned long long)cpu->x[3], cpu->x[3] == 0 ? 1 : 0,
                     cpu->tcti_exit_reason == TCTI_EXIT_FAULT ? 1 : 0);
            trace_base6a628_event("base6a628.post_sync", payload);
        }
        if (block->start_pc == 0x6a620ULL) {
            char payload[512];
            snprintf(payload, sizeof(payload),
                     "attempt:-1,guest_pid:%d,guest_pc:0x%llx,block_start:0x%llx,block_end:0x%llx,"
                     "x0_val:0x%llx,carrier_val:0x%llx,cpu_slot_val:0x%llx,is_zero:%d",
                     current ? current->pid : -1, (unsigned long long)cpu->pc,
                     (unsigned long long)block->start_pc, (unsigned long long)block->end_pc,
                     (unsigned long long)cpu->x[0], (unsigned long long)cpu->x[0],
                     (unsigned long long)cpu->x[0], cpu->x[0] == 0 ? 1 : 0);
            trace_x0chain_event("x0chain.post_sync", payload);
        }
        if (block->start_pc == 0x6a650ULL) {
            ixland_guest_trace_field_t fields[] = {
                TRACE_FIELD_U64_HEX("guest_pc", cpu->pc),
                TRACE_FIELD_U64_HEX("raw_opcode", cpu->pc == 0x6a650ULL ? 0xf800845fU : 0),
                TRACE_FIELD_STR("mnemonic", "str"),
                TRACE_FIELD_I64("base_reg", 2),
                TRACE_FIELD_U64_HEX("base_val", cpu->x[2]),
                TRACE_FIELD_I64("src_reg", 31),
                TRACE_FIELD_U64_HEX("src_val", 0),
                TRACE_FIELD_I64("translation_fault", cpu->tcti_exit_reason == TCTI_EXIT_FAULT),
                TRACE_FIELD_I64("signal_follow_immediate",
                                cpu->tcti_exit_reason == TCTI_EXIT_FAULT),
            };
            trace_str6a650_event_fields("str6a650.post_sync", fields,
                                        sizeof(fields) / sizeof(fields[0]));
        }
        if (block->start_pc == 0x6a640ULL) {
            ixland_guest_trace_field_t fields[] = {
                TRACE_FIELD_U64_HEX("guest_pc", 0x6a640ULL),
                TRACE_FIELD_U64_HEX("raw_opcode", 0x910023e7ULL),
                TRACE_FIELD_STR("mnemonic", "add x7,sp,#8"),
                TRACE_FIELD_U64_HEX("sp", cpu->sp),
                TRACE_FIELD_U64_HEX("x7", cpu->x[7]),
                TRACE_FIELD_U64_HEX("x2", cpu->x[2]),
                TRACE_FIELD_U64_HEX("block_start", block->start_pc),
                TRACE_FIELD_U64_HEX("block_end", block->end_pc),
            };
            trace_insn64_event_fields("insn6a640.post_sync", fields,
                                      sizeof(fields) / sizeof(fields[0]));
        }
        if (block->start_pc == 0x6a64cULL) {
            ixland_guest_trace_field_t fields[] = {
                TRACE_FIELD_U64_HEX("guest_pc", cpu->pc),
                TRACE_FIELD_U64_HEX("raw_opcode", 0xaa0703e2U),
                TRACE_FIELD_STR("mnemonic", "mov_x2_x7"),
                TRACE_FIELD_U64_HEX("x7", cpu->x[7]),
                TRACE_FIELD_U64_HEX("x2", cpu->x[2]),
                TRACE_FIELD_U64_HEX("block_start", block->start_pc),
                TRACE_FIELD_U64_HEX("block_end", block->end_pc),
            };
            trace_x7chain_event_fields("mov6a64c.post_sync", fields,
                                       sizeof(fields) / sizeof(fields[0]));
        }
    } else {
        // Fault containment path - signal was caught
        cpu->tcti_exit_reason = TCTI_EXIT_FAULT;
        cpu->fault_addr = cpu->pc; // Best guess at fault location
        cpu->fault_was_write = false;
        trace_first_live_ldst_fault(cpu, guest_fault_signal);
    }

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

static const char *trace_ldst_extend_name(int extend_type)
{
    switch (extend_type) {
    case A64_EXT_UXTW:
        return "uxtw";
    case A64_EXT_UXTX:
        return "uxtx";
    case A64_EXT_SXTW:
        return "sxtw";
    case A64_EXT_SXTX:
        return "sxtx";
    case A64_EXT_LSL:
        return "lsl";
    default:
        return "unknown";
    }
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
    int is_reg_offset = bits(raw, 11, 10) == 2 ? 1 : 0;
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

    char ev[320];
    snprintf(ev, sizeof(ev), "task.proof.ldr_first_fault.pc=0x%llx", (unsigned long long)cpu->pc);
    trace_record_event(TRACE_ORIGIN_EXEC, ev);

    snprintf(ev, sizeof(ev), "task.proof.ldr_first_fault.raw=0x%08x", raw);
    trace_record_event(TRACE_ORIGIN_EXEC, ev);

    snprintf(ev, sizeof(ev),
             "task.proof.ldr_first_fault.decode=cat:%d,sub:%d,is_load:%d,rt:%d,rn:%d,rm:%d,size:%d,"
             "idx:%d,ext:%d,shift:%d,imm:%lld,regoff:%d",
             decoded.cat, decoded.subtype, is_load, decoded.Rd, rm >= 0 ? decoded.Rn : decoded.Rn,
             rm, decoded.size, decoded.idx_mode, decoded.extend_type, decoded.imm_shift,
             (long long)decoded.imm, is_reg_offset);
    trace_record_event(TRACE_ORIGIN_EXEC, ev);

    if (is_reg_offset) {
        snprintf(ev, sizeof(ev), "task.proof.ldr_first_fault.human=%s x%d,[x%d,x%d,%s #%d]",
                 trace_ldst_mnemonic(is_load, decoded.size, decoded.is_signed ? 1 : 0), decoded.Rd,
                 decoded.Rn, decoded.Rm, trace_ldst_extend_name(decoded.extend_type),
                 decoded.imm_shift);
    } else {
        snprintf(ev, sizeof(ev), "task.proof.ldr_first_fault.human=%s x%d,[x%d,#%lld],idx:%d",
                 trace_ldst_mnemonic(is_load, decoded.size, decoded.is_signed ? 1 : 0), decoded.Rd,
                 decoded.Rn, (long long)decoded.imm, decoded.idx_mode);
    }
    trace_record_event(TRACE_ORIGIN_EXEC, ev);

    snprintf(ev, sizeof(ev),
             "task.proof.ldr_first_fault.regs=rt_val:0x%llx,rn_val:0x%llx,rm_val:0x%llx",
             (unsigned long long)rt_val, (unsigned long long)rn_val, (unsigned long long)rm_val);
    trace_record_event(TRACE_ORIGIN_EXEC, ev);

    snprintf(ev, sizeof(ev),
             "task.proof.ldr_first_fault.address=offset_pre_shift:0x%llx,computed_offset:0x%llx,"
             "guest_ea:0x%llx",
             (unsigned long long)offset_before_shift, (unsigned long long)computed_offset,
             (unsigned long long)guest_ea);
    trace_record_event(TRACE_ORIGIN_EXEC, ev);

    snprintf(ev, sizeof(ev),
             "task.proof.ldr_first_fault.translation=page_lookup:%s,host_ptr_page:0x%llx,"
             "host_ptr_probe:0x%llx,mem_ret:%d",
             desc ? "hit" : "miss", (unsigned long long)host_ptr_page,
             (unsigned long long)(uintptr_t)host_ptr_probe, mem_probe_ret);
    trace_record_event(TRACE_ORIGIN_EXEC, ev);

    snprintf(ev, sizeof(ev), "task.proof.ldr_first_fault.writeback=expected:%d,val:0x%llx",
             writeback_expected, (unsigned long long)writeback_val);
    trace_record_event(TRACE_ORIGIN_EXEC, ev);

    snprintf(ev, sizeof(ev),
             "task.proof.ldr_first_fault.exit=exit_reason:%d,interrupt:%d,host_signal:%d",
             TCTI_EXIT_FAULT, INT_GPF, host_signal);
    trace_record_event(TRACE_ORIGIN_EXEC, ev);
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

static void trace_interpreter_store_edge_checkpoint(const char *name, struct cpu_state *cpu,
                                                    int repeat_count)
{
    char pc_buf[32];
    char x2_buf[32];
    char computed_addr_buf[32];
    char sp_buf[32];
    char x0_buf[32];
    char x1_buf[32];
    char tpidr_buf[32];
    char repeat_count_buf[16];

    uint64_t computed_addr = cpu->x[2];

    snprintf(pc_buf, sizeof(pc_buf), "0x%llx", (unsigned long long)cpu->pc);
    snprintf(x2_buf, sizeof(x2_buf), "0x%llx", (unsigned long long)cpu->x[2]);
    snprintf(computed_addr_buf, sizeof(computed_addr_buf), "0x%llx",
             (unsigned long long)computed_addr);
    snprintf(sp_buf, sizeof(sp_buf), "0x%llx", (unsigned long long)cpu->sp);
    snprintf(x0_buf, sizeof(x0_buf), "0x%llx", (unsigned long long)cpu->x[0]);
    snprintf(x1_buf, sizeof(x1_buf), "0x%llx", (unsigned long long)cpu->x[1]);
    snprintf(tpidr_buf, sizeof(tpidr_buf), "0x%llx", (unsigned long long)cpu->tpidr_el0);
    snprintf(repeat_count_buf, sizeof(repeat_count_buf), "%d", repeat_count);

    trace_attribute_t attrs[] = {
        { "pc", pc_buf },
        { "x2", x2_buf },
        { "computed_addr", computed_addr_buf },
        { "sp", sp_buf },
        { "x0", x0_buf },
        { "x1", x1_buf },
        { "tpidr_el0", tpidr_buf },
        { "repeat_count", repeat_count_buf },
    };

    (void)trace_begin_interval(TRACE_ORIGIN_EMULATOR, name, attrs,
                               sizeof(attrs) / sizeof(attrs[0]));
}

static uint64_t trace_cpu_reg_value(struct cpu_state *cpu, int reg)
{
    if (reg == 31)
        return cpu->sp;
    if (reg >= 0 && reg < 31)
        return cpu->x[reg];
    return 0;
}

static void trace_interpreter_loop_predicate_checkpoint(const char *name, struct cpu_state *cpu,
                                                        struct tlb *tlb, uint64_t block_start,
                                                        uint64_t block_end, int repeat_count)
{
    uint64_t compare_pc = 0;
    uint64_t branch_pc = 0;
    uint32_t compare_raw = 0;
    uint32_t branch_raw = 0;
    a64_instr_t compare_decoded = { 0 };
    uint64_t branch_target = 0;
    uint64_t compare_lhs = 0;
    uint64_t compare_rhs = 0;
    uint64_t bound_value = 0;
    int branch_taken = 0;

    for (uint64_t insn_pc = block_start; insn_pc < block_end; insn_pc += 4) {
        uint32_t raw = 0;
        a64_instr_t decoded = { 0 };
        if (a64_fetch_insn(cpu, tlb, insn_pc, &raw) != 0 || a64_decode(raw, &decoded) != 0)
            continue;
        if (decoded.cat == A64_BRANCH) {
            uint64_t target = insn_pc + decoded.imm;
            if (target == block_start) {
                branch_pc = insn_pc;
                branch_raw = raw;
                branch_target = target;
                branch_taken = (cpu->pc == target);
                break;
            }
        }
    }

    if (branch_pc != 0 && branch_pc >= 4) {
        compare_pc = branch_pc - 4;
        if (a64_fetch_insn(cpu, tlb, compare_pc, &compare_raw) == 0 &&
            a64_decode(compare_raw, &compare_decoded) == 0) {
            compare_lhs = trace_cpu_reg_value(cpu, compare_decoded.Rn);
            if (compare_decoded.cat == A64_DP_IMM || compare_decoded.cat == A64_DP_IMM2) {
                compare_rhs = (uint64_t)compare_decoded.imm;
                bound_value = compare_rhs;
            } else {
                compare_rhs = trace_cpu_reg_value(cpu, compare_decoded.Rm);
                bound_value = compare_rhs;
            }
        }
    }

    char pc_buf[32];
    char x2_buf[32];
    char compare_pc_buf[32];
    char compare_raw_buf[32];
    char branch_pc_buf[32];
    char branch_raw_buf[32];
    char compare_lhs_buf[32];
    char compare_rhs_buf[32];
    char bound_buf[32];
    char branch_target_buf[32];
    char branch_taken_buf[8];
    char repeat_count_buf[16];

    snprintf(pc_buf, sizeof(pc_buf), "0x%llx", (unsigned long long)cpu->pc);
    snprintf(x2_buf, sizeof(x2_buf), "0x%llx", (unsigned long long)cpu->x[2]);
    snprintf(compare_pc_buf, sizeof(compare_pc_buf), "0x%llx", (unsigned long long)compare_pc);
    snprintf(compare_raw_buf, sizeof(compare_raw_buf), "0x%08x", compare_raw);
    snprintf(branch_pc_buf, sizeof(branch_pc_buf), "0x%llx", (unsigned long long)branch_pc);
    snprintf(branch_raw_buf, sizeof(branch_raw_buf), "0x%08x", branch_raw);
    snprintf(compare_lhs_buf, sizeof(compare_lhs_buf), "0x%llx", (unsigned long long)compare_lhs);
    snprintf(compare_rhs_buf, sizeof(compare_rhs_buf), "0x%llx", (unsigned long long)compare_rhs);
    snprintf(bound_buf, sizeof(bound_buf), "0x%llx", (unsigned long long)bound_value);
    snprintf(branch_target_buf, sizeof(branch_target_buf), "0x%llx",
             (unsigned long long)branch_target);
    snprintf(branch_taken_buf, sizeof(branch_taken_buf), "%d", branch_taken);
    snprintf(repeat_count_buf, sizeof(repeat_count_buf), "%d", repeat_count);

    trace_attribute_t attrs[] = {
        { "pc", pc_buf },
        { "x2", x2_buf },
        { "compare_pc", compare_pc_buf },
        { "compare_raw", compare_raw_buf },
        { "branch_pc", branch_pc_buf },
        { "branch_raw", branch_raw_buf },
        { "compare_lhs", compare_lhs_buf },
        { "compare_rhs", compare_rhs_buf },
        { "bound_value", bound_buf },
        { "branch_target", branch_target_buf },
        { "branch_taken", branch_taken_buf },
        { "repeat_count", repeat_count_buf },
    };

    (void)trace_begin_interval(TRACE_ORIGIN_EMULATOR, name, attrs,
                               sizeof(attrs) / sizeof(attrs[0]));
}

struct tcti_asm_probe {
    uint64_t pc_value;
    uint64_t x29_value;
    uint64_t host_x3;
    uint64_t host_x6;
    uint64_t nzcv_value;
    uint8_t captured;
};

struct tcti_bcond_ne_probe {
    uint64_t branch_site_pc;
    uint64_t target_pc;
    uint64_t fallthrough_pc;
    uint64_t x15_loaded;
    uint64_t x25_after_mrs;
    uint64_t x14_after_and;
    uint64_t x14_after_and_mirror;
    uint64_t path_marker;
    uint64_t path_pc;
    uint64_t branch_path_result;
    uint8_t captured;
};

extern struct tcti_asm_probe tcti_asm_probe;
extern struct tcti_bcond_ne_probe tcti_bcond_ne_probe;

static void trace_interpreter_cmp_entry_asm_checkpoint(const char *name, uint64_t block_start,
                                                       uint64_t block_end, int repeat_count)
{
    char pc_buf[32];
    char block_start_buf[32];
    char block_end_buf[32];
    char host_x3_buf[32];
    char host_x6_buf[32];
    char x29_buf[32];
    char nzcv_buf[32];
    char repeat_count_buf[16];

    snprintf(pc_buf, sizeof(pc_buf), "0x%llx", (unsigned long long)tcti_asm_probe.pc_value);
    snprintf(block_start_buf, sizeof(block_start_buf), "0x%llx", (unsigned long long)block_start);
    snprintf(block_end_buf, sizeof(block_end_buf), "0x%llx", (unsigned long long)block_end);
    snprintf(host_x3_buf, sizeof(host_x3_buf), "0x%llx",
             (unsigned long long)tcti_asm_probe.host_x3);
    snprintf(host_x6_buf, sizeof(host_x6_buf), "0x%llx",
             (unsigned long long)tcti_asm_probe.host_x6);
    snprintf(x29_buf, sizeof(x29_buf), "0x%llx", (unsigned long long)tcti_asm_probe.x29_value);
    snprintf(nzcv_buf, sizeof(nzcv_buf), "0x%llx", (unsigned long long)tcti_asm_probe.nzcv_value);
    snprintf(repeat_count_buf, sizeof(repeat_count_buf), "%d", repeat_count);

    trace_attribute_t attrs[] = {
        { "pc", pc_buf },
        { "block_start", block_start_buf },
        { "block_end", block_end_buf },
        { "host_x3", host_x3_buf },
        { "host_x6", host_x6_buf },
        { "x29_value", x29_buf },
        { "raw_nzcv", nzcv_buf },
        { "repeat_count", repeat_count_buf },
    };

    (void)trace_begin_interval(TRACE_ORIGIN_EMULATOR, name, attrs,
                               sizeof(attrs) / sizeof(attrs[0]));
}

static void trace_interpreter_nzcv_chain_checkpoint(const char *name, uint64_t block_start,
                                                    uint64_t block_end, int repeat_count)
{
    char block_start_buf[32];
    char block_end_buf[32];
    char branch_site_buf[32];
    char target_pc_buf[32];
    char fallthrough_pc_buf[32];
    char raw_nzcv_buf[32];
    char x15_loaded_buf[32];
    char x25_after_mrs_buf[32];
    char x14_after_and_buf[32];
    char x14_after_and_mirror_buf[32];
    char path_marker_buf[32];
    char path_pc_buf[32];
    char branch_taken_buf[16];
    char repeat_count_buf[16];

    snprintf(block_start_buf, sizeof(block_start_buf), "0x%llx", (unsigned long long)block_start);
    snprintf(block_end_buf, sizeof(block_end_buf), "0x%llx", (unsigned long long)block_end);
    snprintf(branch_site_buf, sizeof(branch_site_buf), "0x%llx",
             (unsigned long long)tcti_bcond_ne_probe.branch_site_pc);
    snprintf(target_pc_buf, sizeof(target_pc_buf), "0x%llx",
             (unsigned long long)tcti_bcond_ne_probe.target_pc);
    snprintf(fallthrough_pc_buf, sizeof(fallthrough_pc_buf), "0x%llx",
             (unsigned long long)tcti_bcond_ne_probe.fallthrough_pc);
    snprintf(raw_nzcv_buf, sizeof(raw_nzcv_buf), "0x%llx",
             (unsigned long long)tcti_asm_probe.nzcv_value);
    snprintf(x15_loaded_buf, sizeof(x15_loaded_buf), "0x%llx",
             (unsigned long long)tcti_bcond_ne_probe.x15_loaded);
    snprintf(x25_after_mrs_buf, sizeof(x25_after_mrs_buf), "0x%llx",
             (unsigned long long)tcti_bcond_ne_probe.x25_after_mrs);
    snprintf(x14_after_and_buf, sizeof(x14_after_and_buf), "0x%llx",
             (unsigned long long)tcti_bcond_ne_probe.x14_after_and);
    snprintf(x14_after_and_mirror_buf, sizeof(x14_after_and_mirror_buf), "0x%llx",
             (unsigned long long)tcti_bcond_ne_probe.x14_after_and_mirror);
    snprintf(path_marker_buf, sizeof(path_marker_buf), "0x%llx",
             (unsigned long long)tcti_bcond_ne_probe.path_marker);
    snprintf(path_pc_buf, sizeof(path_pc_buf), "0x%llx",
             (unsigned long long)tcti_bcond_ne_probe.path_pc);
    snprintf(branch_taken_buf, sizeof(branch_taken_buf), "%llu",
             (unsigned long long)tcti_bcond_ne_probe.branch_path_result);
    snprintf(repeat_count_buf, sizeof(repeat_count_buf), "%d", repeat_count);

    trace_attribute_t attrs[] = {
        { "block_start", block_start_buf },
        { "block_end", block_end_buf },
        { "branch_site_pc", branch_site_buf },
        { "target_pc", target_pc_buf },
        { "fallthrough_pc", fallthrough_pc_buf },
        { "raw_nzcv", raw_nzcv_buf },
        { "x15_loaded", x15_loaded_buf },
        { "x25_after_mrs", x25_after_mrs_buf },
        { "x14_after_and", x14_after_and_buf },
        { "x14_after_and_mirror", x14_after_and_mirror_buf },
        { "path_marker", path_marker_buf },
        { "path_pc", path_pc_buf },
        { "branch_taken", branch_taken_buf },
        { "repeat_count", repeat_count_buf },
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

            // Validate L0 cache hit (check PC matches)
            if (block && block->start_pc == pc) {
                fiber_stat_inc(ctx, STAT_TB_L0_HITS);
            } else {
                // L0 miss - fall back to MMU cache (L1)
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
                    block = a64_compile_block(cpu, pc, tlb);
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
        trace_69650_block_window(cpu, tlb, block);
        trace_cpu_run_checkpoint("task.proof.a64_cpu_run.before_block_execute", current, cpu,
                                 (int)block->start_pc);
        if (first_execute) {
            trace_cpu_run_checkpoint("task.proof.a64_cpu_run.before_first_execute", current, cpu,
                                     0);
        }
        uint64_t pc_before_execute = cpu->pc;
        uint64_t sp_before_execute = cpu->sp;
        uint64_t x3_before_execute = cpu->x[3];
        uint64_t x0_before_execute = cpu->x[0];
        uint64_t x2_before_execute = cpu->x[2];
        uint64_t x7_before_execute = cpu->x[7];
        bool writer_candidate = false;
        bool x0_writer_candidate = false;
        bool x2_writer_candidate = false;
        bool x7_writer_candidate = false;
        uint32_t writer_raw = 0;
        a64_instr_t writer_decoded;
        memset(&writer_decoded, 0, sizeof(writer_decoded));

        if (a64_fetch_insn(cpu, cpu->tlb, pc_before_execute, &writer_raw) == 0 &&
            a64_decode(writer_raw, &writer_decoded) == 0) {
            if (pc_before_execute == 0x6a640ULL || pc_before_execute == 0x6a64cULL) {
                const char *mnemonic =
                    pc_before_execute == 0x6a640ULL ? "add x7,sp,#8" : "orr x2,xzr,x7 (mov x2,x7)";
                const char *expected = pc_before_execute == 0x6a640ULL ? "x7=sp+imm" : "x2=x7";
                const char *rn31_interp =
                    (pc_before_execute == 0x6a640ULL && writer_decoded.Rn == 31) ? "SP" : "XZR";

                ixland_guest_trace_field_t entry_fields[] = {
                    TRACE_FIELD_U64_HEX("guest_pc", pc_before_execute),
                    TRACE_FIELD_U64_HEX("raw_opcode", writer_raw),
                    TRACE_FIELD_STR("mnemonic", mnemonic),
                    TRACE_FIELD_U64_HEX("sp", sp_before_execute),
                    TRACE_FIELD_U64_HEX("x7", x7_before_execute),
                    TRACE_FIELD_U64_HEX("x2", x2_before_execute),
                    TRACE_FIELD_U64_HEX("block_start", block->start_pc),
                    TRACE_FIELD_U64_HEX("block_end", block->end_pc),
                };
                trace_insn64_event_fields(pc_before_execute == 0x6a640ULL ? "insn6a640.block_entry"
                                                                          : "insn6a64c.block_entry",
                                          entry_fields,
                                          sizeof(entry_fields) / sizeof(entry_fields[0]));

                ixland_guest_trace_field_t decode_fields[] = {
                    TRACE_FIELD_U64_HEX("guest_pc", pc_before_execute),
                    TRACE_FIELD_U64_HEX("raw_opcode", writer_raw),
                    TRACE_FIELD_STR("mnemonic", mnemonic),
                    TRACE_FIELD_I64("cat", writer_decoded.cat),
                    TRACE_FIELD_I64("subtype", writer_decoded.subtype),
                    TRACE_FIELD_I64("rd", writer_decoded.Rd),
                    TRACE_FIELD_I64("rn", writer_decoded.Rn),
                    TRACE_FIELD_I64("rm", writer_decoded.Rm),
                    TRACE_FIELD_I64("imm", writer_decoded.imm),
                    TRACE_FIELD_STR("rn31_interp", rn31_interp),
                    TRACE_FIELD_STR("expected", expected),
                };
                trace_insn64_event_fields(
                    pc_before_execute == 0x6a640ULL ? "insn6a640.decode" : "insn6a64c.decode",
                    decode_fields, sizeof(decode_fields) / sizeof(decode_fields[0]));

                ixland_guest_trace_field_t pre_exec_fields[] = {
                    TRACE_FIELD_U64_HEX("guest_pc", pc_before_execute),
                    TRACE_FIELD_U64_HEX("raw_opcode", writer_raw),
                    TRACE_FIELD_STR("mnemonic", mnemonic),
                    TRACE_FIELD_U64_HEX("sp", sp_before_execute),
                    TRACE_FIELD_U64_HEX("x7", x7_before_execute),
                    TRACE_FIELD_U64_HEX("x2", x2_before_execute),
                };
                trace_insn64_event_fields(
                    pc_before_execute == 0x6a640ULL ? "insn6a640.pre_exec" : "insn6a64c.pre_exec",
                    pre_exec_fields, sizeof(pre_exec_fields) / sizeof(pre_exec_fields[0]));

                if (pc_before_execute == 0x6a640ULL && block && block->gadgets &&
                    block->num_gadgets >= 4) {
                    uint64_t inline_imm = 0;
                    Dl_info info;
                    const char *sym = "unknown";
                    const char *load_sp_sym = "unknown";
                    if (dladdr((void *)block->gadgets[0], &info) != 0 && info.dli_sname)
                        load_sp_sym = info.dli_sname;
                    if (dladdr((void *)block->gadgets[1], &info) != 0 && info.dli_sname)
                        sym = info.dli_sname;
                    if (strstr(sym, "gadget_mov_imm_14") != NULL)
                        inline_imm = (uint64_t)(uintptr_t)block->gadgets[2];

                    uint64_t temp_x14_before = cpu->x[13];
                    uint64_t temp_x15_before = cpu->x[14];
                    ixland_guest_trace_field_t mov_pre_fields[] = {
                        TRACE_FIELD_U64_HEX("guest_pc", 0x6a640ULL),
                        TRACE_FIELD_STR("gadget", "gadget_mov_imm_14"),
                        TRACE_FIELD_U64_HEX("temp_x15_before", temp_x15_before),
                        TRACE_FIELD_U64_HEX("consumed_imm", inline_imm),
                        TRACE_FIELD_U64_HEX("sp", cpu->sp),
                        TRACE_FIELD_U64_HEX("x7", cpu->x[7]),
                        TRACE_FIELD_U64_HEX("x2", cpu->x[2]),
                    };
                    trace_insn64_event_fields("gadget6a640.mov_imm.pre", mov_pre_fields,
                                              sizeof(mov_pre_fields) / sizeof(mov_pre_fields[0]));

                    ixland_guest_trace_field_t load_sp_post_fields[] = {
                        TRACE_FIELD_U64_HEX("guest_pc", 0x6a640ULL),
                        TRACE_FIELD_STR("gadget", load_sp_sym),
                        TRACE_FIELD_U64_HEX("x14", temp_x14_before),
                        TRACE_FIELD_U64_HEX("x15", temp_x15_before),
                        TRACE_FIELD_U64_HEX("x8", x7_before_execute),
                        TRACE_FIELD_U64_HEX("x7", cpu->x[7]),
                        TRACE_FIELD_U64_HEX("x2", cpu->x[2]),
                        TRACE_FIELD_U64_HEX("sp", cpu->sp),
                        TRACE_FIELD_U64_HEX("cpu_state_x7", cpu->x[7]),
                        TRACE_FIELD_U64_HEX("cpu_state_x2", cpu->x[2]),
                        TRACE_FIELD_U64_HEX("cpu_state_sp", cpu->sp),
                    };
                    trace_insn64_event_fields("gadget6a640.load_sp.post", load_sp_post_fields,
                                              sizeof(load_sp_post_fields) /
                                                  sizeof(load_sp_post_fields[0]));

                    ixland_guest_trace_field_t add_pre_fields[] = {
                        TRACE_FIELD_U64_HEX("guest_pc", 0x6a640ULL),
                        TRACE_FIELD_STR("gadget", "gadget_add_reg_7_13_14"),
                        TRACE_FIELD_U64_HEX("src_x14", temp_x14_before),
                        TRACE_FIELD_U64_HEX("src_x15", temp_x15_before),
                        TRACE_FIELD_U64_HEX("dst_x8_before", x7_before_execute),
                        TRACE_FIELD_U64_HEX("sp", cpu->sp),
                        TRACE_FIELD_U64_HEX("x7", cpu->x[7]),
                        TRACE_FIELD_U64_HEX("x2", cpu->x[2]),
                    };
                    trace_insn64_event_fields("gadget6a640.add_reg.pre", add_pre_fields,
                                              sizeof(add_pre_fields) / sizeof(add_pre_fields[0]));
                }
            }
            if (pc_before_execute == 0x6a628ULL) {
                char pretty[192] = { 0 };
                snprintf(pretty, sizeof(pretty), "cat:%d,sub:%d,rd:%d,rn:%d,rm:%d,imm:%lld",
                         (int)writer_decoded.cat, (int)writer_decoded.subtype, writer_decoded.Rd,
                         writer_decoded.Rn, writer_decoded.Rm, (long long)writer_decoded.imm);

                char payload[640];
                snprintf(
                    payload, sizeof(payload),
                    "attempt:-1,guest_pid:%d,guest_pc:0x%llx,block_start:0x%llx,block_end:0x%llx,"
                    "arch_base_reg:%d,arch_base_val:0x%llx,carrier_val:0x%llx,cpu_slot_val:0x%llx,"
                    "is_zero:%d",
                    current ? current->pid : -1, (unsigned long long)pc_before_execute,
                    (unsigned long long)block->start_pc, (unsigned long long)block->end_pc,
                    writer_decoded.Rn, (unsigned long long)cpu->x[writer_decoded.Rn],
                    (unsigned long long)cpu->x[writer_decoded.Rn],
                    (unsigned long long)cpu->x[writer_decoded.Rn],
                    cpu->x[writer_decoded.Rn] == 0 ? 1 : 0);
                trace_base6a628_event("base6a628.block_entry", payload);

                snprintf(payload, sizeof(payload),
                         "attempt:-1,guest_pid:%d,guest_pc:0x%llx,raw_opcode:0x%08x,mnemonic:%s,"
                         "addr_mode:%d,base_reg:%d,target_reg:%d,writeback:%d,sequencing:access_"
                         "before_writeback",
                         current ? current->pid : -1, (unsigned long long)pc_before_execute,
                         writer_raw, pretty, (int)writer_decoded.idx_mode, writer_decoded.Rn,
                         writer_decoded.Rd,
                         (writer_decoded.idx_mode == A64_POST_INDEX ||
                          writer_decoded.idx_mode == A64_PRE_INDEX)
                             ? 1
                             : 0);
                trace_base6a628_event("base6a628.decode", payload);

                if (g_base6a628_last_writer.valid) {
                    snprintf(payload, sizeof(payload),
                             "attempt:-1,guest_pid:%d,guest_pc:0x%llx,last_writer_pc:0x%llx,last_"
                             "writer_raw:0x%08x,"
                             "last_writer_mnemonic:%s,last_writer_rd:%d,last_writer_rn:%d,"
                             "block_start:0x%llx,block_end:0x%llx,x3_before:0x%llx,x3_after:0x%llx,"
                             "is_zero_after:%d",
                             current ? current->pid : -1, (unsigned long long)pc_before_execute,
                             (unsigned long long)g_base6a628_last_writer.writer_pc,
                             g_base6a628_last_writer.writer_raw,
                             g_base6a628_last_writer.writer_mnemonic,
                             g_base6a628_last_writer.writer_rd, g_base6a628_last_writer.writer_rn,
                             (unsigned long long)g_base6a628_last_writer.block_start,
                             (unsigned long long)g_base6a628_last_writer.block_end,
                             (unsigned long long)g_base6a628_last_writer.x3_before,
                             (unsigned long long)g_base6a628_last_writer.x3_after,
                             g_base6a628_last_writer.x3_after == 0 ? 1 : 0);
                } else {
                    snprintf(payload, sizeof(payload),
                             "attempt:-1,guest_pid:%d,guest_pc:0x%llx,last_writer_pc:none",
                             current ? current->pid : -1, (unsigned long long)pc_before_execute);
                }
                trace_base6a628_event("base6a628.last_writer", payload);
            }

            if (pc_before_execute == 0x6a620ULL) {
                char payload[640];
                snprintf(
                    payload, sizeof(payload),
                    "attempt:-1,guest_pid:%d,guest_pc:0x%llx,raw_opcode:0x%08x,mnemonic:mov x3,x0,"
                    "x0_val:0x%llx,x3_val:0x%llx,x3_produced:0x%llx,is_x0_zero:%d,"
                    "block_start:0x%llx,block_end:0x%llx",
                    current ? current->pid : -1, (unsigned long long)pc_before_execute, writer_raw,
                    (unsigned long long)cpu->x[0], (unsigned long long)cpu->x[3],
                    (unsigned long long)cpu->x[0], cpu->x[0] == 0 ? 1 : 0,
                    (unsigned long long)block->start_pc, (unsigned long long)block->end_pc);
                trace_x0chain_event("x0chain.consumer_at_0x6a620", payload);

                snprintf(
                    payload, sizeof(payload),
                    "attempt:-1,guest_pid:%d,guest_pc:0x%llx,block_start:0x%llx,block_end:0x%llx,"
                    "x0_val:0x%llx,carrier_val:0x%llx,cpu_slot_val:0x%llx,is_zero:%d",
                    current ? current->pid : -1, (unsigned long long)pc_before_execute,
                    (unsigned long long)block->start_pc, (unsigned long long)block->end_pc,
                    (unsigned long long)cpu->x[0], (unsigned long long)cpu->x[0],
                    (unsigned long long)cpu->x[0], cpu->x[0] == 0 ? 1 : 0);
                trace_x0chain_event("x0chain.block_entry", payload);

                if (g_x0chain_last_writer.valid) {
                    snprintf(payload, sizeof(payload),
                             "attempt:-1,guest_pid:%d,guest_pc:0x%llx,last_writer_pc:0x%llx,last_"
                             "writer_raw:0x%08x,"
                             "last_writer_mnemonic:%s,last_writer_rd:%d,last_writer_rn:%d,"
                             "block_start:0x%llx,block_end:0x%llx,x0_before:0x%llx,x0_after:0x%llx,"
                             "is_zero_after:%d",
                             current ? current->pid : -1, (unsigned long long)pc_before_execute,
                             (unsigned long long)g_x0chain_last_writer.writer_pc,
                             g_x0chain_last_writer.writer_raw,
                             g_x0chain_last_writer.writer_mnemonic, g_x0chain_last_writer.writer_rd,
                             g_x0chain_last_writer.writer_rn,
                             (unsigned long long)g_x0chain_last_writer.block_start,
                             (unsigned long long)g_x0chain_last_writer.block_end,
                             (unsigned long long)g_x0chain_last_writer.x0_before,
                             (unsigned long long)g_x0chain_last_writer.x0_after,
                             g_x0chain_last_writer.x0_after == 0 ? 1 : 0);
                } else {
                    snprintf(payload, sizeof(payload),
                             "attempt:-1,guest_pid:%d,guest_pc:0x%llx,last_writer_pc:none",
                             current ? current->pid : -1, (unsigned long long)pc_before_execute);
                }
                trace_x0chain_event("x0chain.last_writer", payload);
            }

            if (pc_before_execute == 0x6a650ULL) {
                uint64_t base_val = a64_read_reg_or_sp(cpu, writer_decoded.Rn, true);
                uint64_t src_val = trace_ldst_reg_or_zr(cpu, writer_decoded.Rd);
                uint64_t idx_val = trace_ldst_reg_or_zr(cpu, writer_decoded.Rm);
                uint64_t guest_ea = trace_ldst_effective_address(cpu, writer_raw, &writer_decoded,
                                                                 base_val, idx_val);
                const char *mnemonic = trace_ldst_mnemonic(bit(writer_raw, 22), writer_decoded.size,
                                                           writer_decoded.is_signed ? 1 : 0);

                ixland_guest_trace_field_t block_entry_fields[] = {
                    TRACE_FIELD_U64_HEX("guest_pc", pc_before_execute),
                    TRACE_FIELD_U64_HEX("raw_opcode", writer_raw),
                    TRACE_FIELD_STR("mnemonic", mnemonic),
                    TRACE_FIELD_I64("base_reg", writer_decoded.Rn),
                    TRACE_FIELD_U64_HEX("base_val", base_val),
                    TRACE_FIELD_I64("src_reg", writer_decoded.Rd),
                    TRACE_FIELD_U64_HEX("src_val", src_val),
                    TRACE_FIELD_I64("idx_reg", writer_decoded.Rm),
                    TRACE_FIELD_U64_HEX("idx_val", idx_val),
                    TRACE_FIELD_I64("imm", writer_decoded.imm),
                    TRACE_FIELD_I64("idx_mode", writer_decoded.idx_mode),
                    TRACE_FIELD_U64_HEX("guest_ea", guest_ea),
                    TRACE_FIELD_STR("host_probe", "pending"),
                    TRACE_FIELD_STR("translation_fault", "pending"),
                    TRACE_FIELD_I64("signal_follow_immediate", 0),
                };
                trace_str6a650_event_fields("str6a650.block_entry", block_entry_fields,
                                            sizeof(block_entry_fields) /
                                                sizeof(block_entry_fields[0]));

                ixland_guest_trace_field_t consumer_fields[] = {
                    TRACE_FIELD_U64_HEX("guest_pc", 0x6a650ULL),
                    TRACE_FIELD_U64_HEX("raw_opcode", writer_raw),
                    TRACE_FIELD_STR("mnemonic", "str"),
                    TRACE_FIELD_U64_HEX("sp", cpu->sp),
                    TRACE_FIELD_U64_HEX("x7", cpu->x[7]),
                    TRACE_FIELD_U64_HEX("x2", cpu->x[2]),
                    TRACE_FIELD_U64_HEX("guest_ea", guest_ea),
                };
                trace_insn64_event_fields("insn6a650.consumer", consumer_fields,
                                          sizeof(consumer_fields) / sizeof(consumer_fields[0]));

                ixland_guest_trace_field_t decode_fields[] = {
                    TRACE_FIELD_U64_HEX("guest_pc", pc_before_execute),
                    TRACE_FIELD_U64_HEX("raw_opcode", writer_raw),
                    TRACE_FIELD_STR("mnemonic", mnemonic),
                    TRACE_FIELD_I64("base_reg", writer_decoded.Rn),
                    TRACE_FIELD_I64("src_reg", writer_decoded.Rd),
                    TRACE_FIELD_I64("idx_reg", writer_decoded.Rm),
                    TRACE_FIELD_I64("imm", writer_decoded.imm),
                    TRACE_FIELD_I64("idx_mode", writer_decoded.idx_mode),
                    TRACE_FIELD_I64("is_load", bit(writer_raw, 22) ? 1 : 0),
                    TRACE_FIELD_STR("sequencing", "access_before_writeback"),
                    TRACE_FIELD_I64("writeback", (writer_decoded.idx_mode == A64_POST_INDEX ||
                                                  writer_decoded.idx_mode == A64_PRE_INDEX)
                                                     ? 1
                                                     : 0),
                };
                trace_str6a650_event_fields("str6a650.decode", decode_fields,
                                            sizeof(decode_fields) / sizeof(decode_fields[0]));

                ixland_guest_trace_field_t pre_access_fields[] = {
                    TRACE_FIELD_U64_HEX("guest_pc", pc_before_execute),
                    TRACE_FIELD_U64_HEX("raw_opcode", writer_raw),
                    TRACE_FIELD_STR("mnemonic", mnemonic),
                    TRACE_FIELD_I64("base_reg", writer_decoded.Rn),
                    TRACE_FIELD_U64_HEX("base_val", base_val),
                    TRACE_FIELD_I64("src_reg", writer_decoded.Rd),
                    TRACE_FIELD_U64_HEX("src_val", src_val),
                    TRACE_FIELD_I64("idx_reg", writer_decoded.Rm),
                    TRACE_FIELD_U64_HEX("idx_val", idx_val),
                    TRACE_FIELD_I64("imm", writer_decoded.imm),
                    TRACE_FIELD_I64("idx_mode", writer_decoded.idx_mode),
                    TRACE_FIELD_U64_HEX("guest_ea", guest_ea),
                };
                trace_str6a650_event_fields("str6a650.pre_access", pre_access_fields,
                                            sizeof(pre_access_fields) /
                                                sizeof(pre_access_fields[0]));

                if (g_str6a650_last_writer.valid) {
                    ixland_guest_trace_field_t fields[] = {
                        TRACE_FIELD_U64_HEX("guest_pc", pc_before_execute),
                        TRACE_FIELD_U64_HEX("last_writer_pc", g_str6a650_last_writer.writer_pc),
                        TRACE_FIELD_U64_HEX("last_writer_raw", g_str6a650_last_writer.writer_raw),
                        TRACE_FIELD_STR("last_writer_mnemonic",
                                        g_str6a650_last_writer.writer_mnemonic),
                        TRACE_FIELD_I64("last_writer_rd", g_str6a650_last_writer.writer_rd),
                        TRACE_FIELD_I64("last_writer_rn", g_str6a650_last_writer.writer_rn),
                        TRACE_FIELD_I64("last_writer_rm", g_str6a650_last_writer.writer_rm),
                        TRACE_FIELD_I64("last_writer_idx_mode",
                                        g_str6a650_last_writer.writer_idx_mode),
                        TRACE_FIELD_I64("last_writer_imm", g_str6a650_last_writer.writer_imm),
                        TRACE_FIELD_U64_HEX("block_start", g_str6a650_last_writer.block_start),
                        TRACE_FIELD_U64_HEX("block_end", g_str6a650_last_writer.block_end),
                        TRACE_FIELD_U64_HEX("x2_before", g_str6a650_last_writer.x2_before),
                        TRACE_FIELD_U64_HEX("x2_after", g_str6a650_last_writer.x2_after),
                        TRACE_FIELD_I64("is_zero_after",
                                        g_str6a650_last_writer.x2_after == 0 ? 1 : 0),
                    };
                    trace_str6a650_event_fields("str6a650.last_base_writer", fields,
                                                sizeof(fields) / sizeof(fields[0]));
                } else {
                    ixland_guest_trace_field_t fields[] = {
                        TRACE_FIELD_U64_HEX("guest_pc", pc_before_execute),
                        TRACE_FIELD_STR("last_writer_pc", "none"),
                    };
                    trace_str6a650_event_fields("str6a650.last_base_writer", fields,
                                                sizeof(fields) / sizeof(fields[0]));
                }

                if (g_x7chain_last_writer.valid) {
                    ixland_guest_trace_field_t fields[] = {
                        TRACE_FIELD_U64_HEX("guest_pc", pc_before_execute),
                        TRACE_FIELD_U64_HEX("last_writer_pc", g_x7chain_last_writer.writer_pc),
                        TRACE_FIELD_U64_HEX("last_writer_raw", g_x7chain_last_writer.writer_raw),
                        TRACE_FIELD_STR("last_writer_mnemonic",
                                        g_x7chain_last_writer.writer_mnemonic),
                        TRACE_FIELD_I64("last_writer_rd", g_x7chain_last_writer.writer_rd),
                        TRACE_FIELD_I64("last_writer_rn", g_x7chain_last_writer.writer_rn),
                        TRACE_FIELD_I64("last_writer_rm", g_x7chain_last_writer.writer_rm),
                        TRACE_FIELD_I64("last_writer_idx_mode",
                                        g_x7chain_last_writer.writer_idx_mode),
                        TRACE_FIELD_I64("last_writer_imm", g_x7chain_last_writer.writer_imm),
                        TRACE_FIELD_U64_HEX("block_start", g_x7chain_last_writer.block_start),
                        TRACE_FIELD_U64_HEX("block_end", g_x7chain_last_writer.block_end),
                        TRACE_FIELD_U64_HEX("x7_before", g_x7chain_last_writer.x7_before),
                        TRACE_FIELD_U64_HEX("x7_after", g_x7chain_last_writer.x7_after),
                        TRACE_FIELD_I64("is_one_after",
                                        g_x7chain_last_writer.x7_after == 1 ? 1 : 0),
                        TRACE_FIELD_I64("is_zero_after",
                                        g_x7chain_last_writer.x7_after == 0 ? 1 : 0),
                    };
                    trace_x7chain_event_fields("x7chain.last_writer", fields,
                                               sizeof(fields) / sizeof(fields[0]));
                } else {
                    ixland_guest_trace_field_t fields[] = {
                        TRACE_FIELD_U64_HEX("guest_pc", pc_before_execute),
                        TRACE_FIELD_STR("last_writer_pc", "none"),
                    };
                    trace_x7chain_event_fields("x7chain.last_writer", fields,
                                               sizeof(fields) / sizeof(fields[0]));
                }

                ixland_guest_trace_field_t x7_block_entry_fields[] = {
                    TRACE_FIELD_U64_HEX("guest_pc", pc_before_execute),
                    TRACE_FIELD_U64_HEX("raw_opcode", writer_raw),
                    TRACE_FIELD_STR("mnemonic", "mov_x2_x7"),
                    TRACE_FIELD_I64("src_reg", 7),
                    TRACE_FIELD_I64("dst_reg", 2),
                    TRACE_FIELD_U64_HEX("x7", cpu->x[7]),
                    TRACE_FIELD_U64_HEX("x2", cpu->x[2]),
                    TRACE_FIELD_U64_HEX("block_start", block->start_pc),
                    TRACE_FIELD_U64_HEX("block_end", block->end_pc),
                    TRACE_FIELD_I64("is_x7_one", cpu->x[7] == 1 ? 1 : 0),
                    TRACE_FIELD_I64("is_x2_one", cpu->x[2] == 1 ? 1 : 0),
                };
                trace_x7chain_event_fields("x7chain.block_entry", x7_block_entry_fields,
                                           sizeof(x7_block_entry_fields) /
                                               sizeof(x7_block_entry_fields[0]));

                if (writer_raw == 0xaa0703e2U) {
                    ixland_guest_trace_field_t decode_fields[] = {
                        TRACE_FIELD_U64_HEX("guest_pc", pc_before_execute),
                        TRACE_FIELD_U64_HEX("raw_opcode", writer_raw),
                        TRACE_FIELD_STR("mnemonic", "orr x2,xzr,x7 (mov x2,x7)"),
                        TRACE_FIELD_I64("src_reg", 7),
                        TRACE_FIELD_I64("dst_reg", 2),
                        TRACE_FIELD_STR("expected_result", "x2_after_equals_x7_before"),
                    };
                    trace_x7chain_event_fields("mov6a64c.decode", decode_fields,
                                               sizeof(decode_fields) / sizeof(decode_fields[0]));

                    ixland_guest_trace_field_t pre_exec_fields[] = {
                        TRACE_FIELD_U64_HEX("guest_pc", pc_before_execute),
                        TRACE_FIELD_U64_HEX("raw_opcode", writer_raw),
                        TRACE_FIELD_STR("mnemonic", "mov_x2_x7"),
                        TRACE_FIELD_U64_HEX("x7_before", x7_before_execute),
                        TRACE_FIELD_U64_HEX("x2_before", x2_before_execute),
                        TRACE_FIELD_U64_HEX("block_start", block->start_pc),
                        TRACE_FIELD_U64_HEX("block_end", block->end_pc),
                    };
                    trace_x7chain_event_fields("mov6a64c.pre_exec", pre_exec_fields,
                                               sizeof(pre_exec_fields) /
                                                   sizeof(pre_exec_fields[0]));
                }

                if (g_str6a650_last_writer.writer_pc == 0x6a64cULL &&
                    g_str6a650_last_writer.writer_raw == 0xaa0703e2U) {
                    ixland_guest_trace_field_t consumer_fields[] = {
                        TRACE_FIELD_U64_HEX("guest_pc", 0x6a650ULL),
                        TRACE_FIELD_U64_HEX("raw_opcode", 0xf800845fULL),
                        TRACE_FIELD_STR("mnemonic", "str"),
                        TRACE_FIELD_U64_HEX("x7", cpu->x[7]),
                        TRACE_FIELD_U64_HEX("x2", cpu->x[2]),
                        TRACE_FIELD_U64_HEX("x7_before_mov", g_str6a650_last_writer.x2_after),
                        TRACE_FIELD_U64_HEX("x2_after_mov", g_str6a650_last_writer.x2_after),
                        TRACE_FIELD_I64("x2_after_eq_x7_before",
                                        cpu->x[2] == g_str6a650_last_writer.x2_after ? 1 : 0),
                    };
                    trace_x7chain_event_fields("mov6a64c.consumer_at_6a650", consumer_fields,
                                               sizeof(consumer_fields) /
                                                   sizeof(consumer_fields[0]));
                }
            }

            if (trace_instr_writes_rd(&writer_decoded, writer_raw) && writer_decoded.Rd == 3 &&
                pc_before_execute < 0x6a628ULL) {
                writer_candidate = true;
                char pretty[192] = { 0 };
                snprintf(pretty, sizeof(pretty), "cat:%d,sub:%d,rd:%d,rn:%d,rm:%d,imm:%lld",
                         (int)writer_decoded.cat, (int)writer_decoded.subtype, writer_decoded.Rd,
                         writer_decoded.Rn, writer_decoded.Rm, (long long)writer_decoded.imm);
                char payload[640];
                snprintf(
                    payload, sizeof(payload),
                    "attempt:-1,guest_pid:%d,guest_pc:0x%llx,raw_opcode:0x%08x,mnemonic:%s,"
                    "arch_base_reg:3,arch_base_val:0x%llx,carrier_val:0x%llx,cpu_slot_val:0x%llx,"
                    "block_start:0x%llx,block_end:0x%llx,is_zero:%d",
                    current ? current->pid : -1, (unsigned long long)pc_before_execute, writer_raw,
                    pretty, (unsigned long long)x3_before_execute,
                    (unsigned long long)x3_before_execute, (unsigned long long)x3_before_execute,
                    (unsigned long long)block->start_pc, (unsigned long long)block->end_pc,
                    x3_before_execute == 0 ? 1 : 0);
                trace_base6a628_event("base6a628.pre_writer_regs", payload);
            }

            if (trace_instr_writes_rd(&writer_decoded, writer_raw) && writer_decoded.Rd == 0 &&
                pc_before_execute < 0x6a620ULL) {
                x0_writer_candidate = true;
                char pretty[192] = { 0 };
                snprintf(pretty, sizeof(pretty), "cat:%d,sub:%d,rd:%d,rn:%d,rm:%d,imm:%lld",
                         (int)writer_decoded.cat, (int)writer_decoded.subtype, writer_decoded.Rd,
                         writer_decoded.Rn, writer_decoded.Rm, (long long)writer_decoded.imm);
                char payload[640];
                snprintf(payload, sizeof(payload),
                         "attempt:-1,guest_pid:%d,guest_pc:0x%llx,raw_opcode:0x%08x,mnemonic:%s,"
                         "x0_val:0x%llx,carrier_val:0x%llx,cpu_slot_val:0x%llx,"
                         "block_start:0x%llx,block_end:0x%llx,is_zero:%d",
                         current ? current->pid : -1, (unsigned long long)pc_before_execute,
                         writer_raw, pretty, (unsigned long long)x0_before_execute,
                         (unsigned long long)x0_before_execute,
                         (unsigned long long)x0_before_execute, (unsigned long long)block->start_pc,
                         (unsigned long long)block->end_pc, x0_before_execute == 0 ? 1 : 0);
                trace_x0chain_event("x0chain.pre_writer_regs", payload);
            }

            if (trace_instr_writes_rd(&writer_decoded, writer_raw) && writer_decoded.Rd == 2 &&
                pc_before_execute < 0x6a650ULL) {
                x2_writer_candidate = true;
                char pretty[192] = { 0 };
                snprintf(pretty, sizeof(pretty), "cat:%d,sub:%d,rd:%d,rn:%d,rm:%d,imm:%lld",
                         (int)writer_decoded.cat, (int)writer_decoded.subtype, writer_decoded.Rd,
                         writer_decoded.Rn, writer_decoded.Rm, (long long)writer_decoded.imm);
                ixland_guest_trace_field_t fields[] = {
                    TRACE_FIELD_U64_HEX("guest_pc", pc_before_execute),
                    TRACE_FIELD_U64_HEX("raw_opcode", writer_raw),
                    TRACE_FIELD_STR("mnemonic", pretty),
                    TRACE_FIELD_I64("base_reg", 2),
                    TRACE_FIELD_U64_HEX("base_val", x2_before_execute),
                    TRACE_FIELD_I64("src_reg", writer_decoded.Rd),
                    TRACE_FIELD_U64_HEX("src_val", trace_ldst_reg_or_zr(cpu, writer_decoded.Rd)),
                    TRACE_FIELD_I64("idx_reg", writer_decoded.Rm),
                    TRACE_FIELD_U64_HEX("idx_val", trace_ldst_reg_or_zr(cpu, writer_decoded.Rm)),
                    TRACE_FIELD_I64("imm", writer_decoded.imm),
                    TRACE_FIELD_I64("idx_mode", writer_decoded.idx_mode),
                    TRACE_FIELD_STR("guest_ea", "na"),
                    TRACE_FIELD_STR("host_probe", "na"),
                    TRACE_FIELD_STR("translation_fault", "na"),
                    TRACE_FIELD_I64("signal_follow_immediate", 0),
                };
                trace_str6a650_event_fields("str6a650.pre_writer_regs", fields,
                                            sizeof(fields) / sizeof(fields[0]));
            }

            if (trace_instr_writes_rd(&writer_decoded, writer_raw) && writer_decoded.Rd == 7 &&
                pc_before_execute < 0x6a64cULL) {
                x7_writer_candidate = true;
                char pretty[192] = { 0 };
                snprintf(pretty, sizeof(pretty), "cat:%d,sub:%d,rd:%d,rn:%d,rm:%d,imm:%lld",
                         (int)writer_decoded.cat, (int)writer_decoded.subtype, writer_decoded.Rd,
                         writer_decoded.Rn, writer_decoded.Rm, (long long)writer_decoded.imm);
                ixland_guest_trace_field_t fields[] = {
                    TRACE_FIELD_U64_HEX("guest_pc", pc_before_execute),
                    TRACE_FIELD_U64_HEX("raw_opcode", writer_raw),
                    TRACE_FIELD_STR("mnemonic", pretty),
                    TRACE_FIELD_U64_HEX("x7", x7_before_execute),
                    TRACE_FIELD_U64_HEX("x2", x2_before_execute),
                    TRACE_FIELD_U64_HEX("block_start", block->start_pc),
                    TRACE_FIELD_U64_HEX("block_end", block->end_pc),
                    TRACE_FIELD_I64("is_x7_one", x7_before_execute == 1 ? 1 : 0),
                    TRACE_FIELD_I64("is_x7_zero", x7_before_execute == 0 ? 1 : 0),
                };
                trace_x7chain_event_fields("x7chain.pre_writer_regs", fields,
                                           sizeof(fields) / sizeof(fields[0]));
            }
        }
        int exit_reason = a64_execute_block(cpu, block);
        uint64_t sp_after_execute = cpu->sp;
        uint64_t x7_after_execute = cpu->x[7];
        uint64_t x2_after_execute = cpu->x[2];

        if (pc_before_execute == 0x6a640ULL || pc_before_execute == 0x6a64cULL) {
            const char *mnemonic =
                pc_before_execute == 0x6a640ULL ? "add x7,sp,#8" : "orr x2,xzr,x7 (mov x2,x7)";
            ixland_guest_trace_field_t post_exec_fields[] = {
                TRACE_FIELD_U64_HEX("guest_pc", pc_before_execute),
                TRACE_FIELD_U64_HEX("raw_opcode", writer_raw),
                TRACE_FIELD_STR("mnemonic", mnemonic),
                TRACE_FIELD_U64_HEX("sp", sp_after_execute),
                TRACE_FIELD_U64_HEX("x7", x7_after_execute),
                TRACE_FIELD_U64_HEX("x2", x2_after_execute),
                TRACE_FIELD_I64("x2_after_eq_x7_before", x2_after_execute == x7_before_execute),
                TRACE_FIELD_I64("exit_reason", exit_reason),
            };
            trace_insn64_event_fields(
                pc_before_execute == 0x6a640ULL ? "insn6a640.post_exec" : "insn6a64c.post_exec",
                post_exec_fields, sizeof(post_exec_fields) / sizeof(post_exec_fields[0]));

            if (pc_before_execute == 0x6a640ULL && block && block->gadgets &&
                block->num_gadgets >= 4) {
                uint64_t inline_imm = 0;
                Dl_info info;
                const char *sym = "unknown";
                const char *next_sym = "unknown";
                if (dladdr((void *)block->gadgets[1], &info) != 0 && info.dli_sname)
                    sym = info.dli_sname;
                if (block->num_gadgets >= 5 && dladdr((void *)block->gadgets[4], &info) != 0 &&
                    info.dli_sname)
                    next_sym = info.dli_sname;
                if (strstr(sym, "gadget_mov_imm_14") != NULL)
                    inline_imm = (uint64_t)(uintptr_t)block->gadgets[2];

                uint64_t temp_x14_after = cpu->x[13];
                uint64_t temp_x15_after = cpu->x[14];
                uint64_t expected_add = temp_x14_after + temp_x15_after;

                ixland_guest_trace_field_t mov_post_fields[] = {
                    TRACE_FIELD_U64_HEX("guest_pc", 0x6a640ULL),
                    TRACE_FIELD_STR("gadget", "gadget_mov_imm_14"),
                    TRACE_FIELD_U64_HEX("temp_x15_after", temp_x15_after),
                    TRACE_FIELD_U64_HEX("consumed_imm", inline_imm),
                    TRACE_FIELD_I64("post_eq_imm", temp_x15_after == inline_imm),
                    TRACE_FIELD_U64_HEX("sp", cpu->sp),
                    TRACE_FIELD_U64_HEX("x7", cpu->x[7]),
                    TRACE_FIELD_U64_HEX("x2", cpu->x[2]),
                };
                trace_insn64_event_fields("gadget6a640.mov_imm.post", mov_post_fields,
                                          sizeof(mov_post_fields) / sizeof(mov_post_fields[0]));

                ixland_guest_trace_field_t add_post_fields[] = {
                    TRACE_FIELD_U64_HEX("guest_pc", 0x6a640ULL),
                    TRACE_FIELD_STR("gadget", "gadget_add_reg_7_13_14"),
                    TRACE_FIELD_U64_HEX("src_x14", temp_x14_after),
                    TRACE_FIELD_U64_HEX("src_x15", temp_x15_after),
                    TRACE_FIELD_U64_HEX("dst_x8_after", x7_after_execute),
                    TRACE_FIELD_U64_HEX("expected_add", expected_add),
                    TRACE_FIELD_I64("equal_add", x7_after_execute == expected_add),
                    TRACE_FIELD_I64("equal_sp_plus8", x7_after_execute == (cpu->sp + 8ULL)),
                    TRACE_FIELD_U64_HEX("sp", cpu->sp),
                    TRACE_FIELD_U64_HEX("x7", cpu->x[7]),
                    TRACE_FIELD_U64_HEX("x2", cpu->x[2]),
                };
                trace_insn64_event_fields("gadget6a640.add_reg.post", add_post_fields,
                                          sizeof(add_post_fields) / sizeof(add_post_fields[0]));

                ixland_guest_trace_field_t consistency_fields[] = {
                    TRACE_FIELD_U64_HEX("guest_pc", 0x6a640ULL),
                    TRACE_FIELD_U64_HEX("dst_x8_after", x7_after_execute),
                    TRACE_FIELD_U64_HEX("arch_x7", cpu->x[7]),
                    TRACE_FIELD_U64_HEX("carrier_x7", cpu->x[7]),
                    TRACE_FIELD_U64_HEX("cpu_state_x7", cpu->x[7]),
                    TRACE_FIELD_I64("all_equal", x7_after_execute == cpu->x[7]),
                };
                trace_insn64_event_fields("gadget6a640.consistency", consistency_fields,
                                          sizeof(consistency_fields) /
                                              sizeof(consistency_fields[0]));

                ixland_guest_trace_field_t next_gadget_fields[] = {
                    TRACE_FIELD_U64_HEX("guest_pc", 0x6a640ULL),
                    TRACE_FIELD_STR("gadget", "gadget_add_reg_7_13_14"),
                    TRACE_FIELD_STR("next_gadget", next_sym),
                    TRACE_FIELD_U64_HEX("next_addr", block->num_gadgets >= 5
                                                         ? (uint64_t)(uintptr_t)block->gadgets[4]
                                                         : 0),
                    TRACE_FIELD_U64_HEX("x14", temp_x14_after),
                    TRACE_FIELD_U64_HEX("x15", temp_x15_after),
                    TRACE_FIELD_U64_HEX("x8", x7_after_execute),
                    TRACE_FIELD_U64_HEX("x7", cpu->x[7]),
                    TRACE_FIELD_U64_HEX("x2", cpu->x[2]),
                    TRACE_FIELD_U64_HEX("sp", cpu->sp),
                    TRACE_FIELD_U64_HEX("cpu_state_x7", cpu->x[7]),
                    TRACE_FIELD_U64_HEX("cpu_state_x2", cpu->x[2]),
                    TRACE_FIELD_U64_HEX("cpu_state_sp", cpu->sp),
                };
                trace_insn64_event_fields("gadget6a640.next_gadget.pre", next_gadget_fields,
                                          sizeof(next_gadget_fields) /
                                              sizeof(next_gadget_fields[0]));
            }
        }

        if (writer_candidate) {
            char pretty[192] = { 0 };
            snprintf(pretty, sizeof(pretty), "cat:%d,sub:%d,rd:%d,rn:%d,rm:%d,imm:%lld",
                     (int)writer_decoded.cat, (int)writer_decoded.subtype, writer_decoded.Rd,
                     writer_decoded.Rn, writer_decoded.Rm, (long long)writer_decoded.imm);

            g_base6a628_last_writer.valid = 1;
            g_base6a628_last_writer.writer_pc = pc_before_execute;
            g_base6a628_last_writer.writer_raw = writer_raw;
            strncpy(g_base6a628_last_writer.writer_mnemonic, pretty,
                    sizeof(g_base6a628_last_writer.writer_mnemonic) - 1);
            g_base6a628_last_writer
                .writer_mnemonic[sizeof(g_base6a628_last_writer.writer_mnemonic) - 1] = '\0';
            g_base6a628_last_writer.writer_rd = writer_decoded.Rd;
            g_base6a628_last_writer.writer_rn = writer_decoded.Rn;
            g_base6a628_last_writer.x3_before = x3_before_execute;
            g_base6a628_last_writer.x3_after = cpu->x[3];
            g_base6a628_last_writer.block_start = block->start_pc;
            g_base6a628_last_writer.block_end = block->end_pc;

            char payload[640];
            snprintf(payload, sizeof(payload),
                     "attempt:-1,guest_pid:%d,guest_pc:0x%llx,raw_opcode:0x%08x,mnemonic:%s,"
                     "arch_base_reg:3,arch_base_val:0x%llx,carrier_val:0x%llx,cpu_slot_val:0x%llx,"
                     "block_start:0x%llx,block_end:0x%llx,is_zero:%d",
                     current ? current->pid : -1, (unsigned long long)pc_before_execute, writer_raw,
                     pretty, (unsigned long long)cpu->x[3], (unsigned long long)cpu->x[3],
                     (unsigned long long)cpu->x[3], (unsigned long long)block->start_pc,
                     (unsigned long long)block->end_pc, cpu->x[3] == 0 ? 1 : 0);
            trace_base6a628_event("base6a628.post_writer_regs", payload);
        }

        if (x0_writer_candidate) {
            char pretty[192] = { 0 };
            snprintf(pretty, sizeof(pretty), "cat:%d,sub:%d,rd:%d,rn:%d,rm:%d,imm:%lld",
                     (int)writer_decoded.cat, (int)writer_decoded.subtype, writer_decoded.Rd,
                     writer_decoded.Rn, writer_decoded.Rm, (long long)writer_decoded.imm);

            g_x0chain_last_writer.valid = 1;
            g_x0chain_last_writer.writer_pc = pc_before_execute;
            g_x0chain_last_writer.writer_raw = writer_raw;
            strncpy(g_x0chain_last_writer.writer_mnemonic, pretty,
                    sizeof(g_x0chain_last_writer.writer_mnemonic) - 1);
            g_x0chain_last_writer
                .writer_mnemonic[sizeof(g_x0chain_last_writer.writer_mnemonic) - 1] = '\0';
            g_x0chain_last_writer.writer_rd = writer_decoded.Rd;
            g_x0chain_last_writer.writer_rn = writer_decoded.Rn;
            g_x0chain_last_writer.x0_before = x0_before_execute;
            g_x0chain_last_writer.x0_after = cpu->x[0];
            g_x0chain_last_writer.block_start = block->start_pc;
            g_x0chain_last_writer.block_end = block->end_pc;

            char payload[640];
            snprintf(payload, sizeof(payload),
                     "attempt:-1,guest_pid:%d,guest_pc:0x%llx,raw_opcode:0x%08x,mnemonic:%s,"
                     "x0_val:0x%llx,carrier_val:0x%llx,cpu_slot_val:0x%llx,"
                     "block_start:0x%llx,block_end:0x%llx,is_zero:%d",
                     current ? current->pid : -1, (unsigned long long)pc_before_execute, writer_raw,
                     pretty, (unsigned long long)cpu->x[0], (unsigned long long)cpu->x[0],
                     (unsigned long long)cpu->x[0], (unsigned long long)block->start_pc,
                     (unsigned long long)block->end_pc, cpu->x[0] == 0 ? 1 : 0);
            trace_x0chain_event("x0chain.post_writer_regs", payload);
        }

        if (x2_writer_candidate) {
            char pretty[192] = { 0 };
            snprintf(pretty, sizeof(pretty), "cat:%d,sub:%d,rd:%d,rn:%d,rm:%d,imm:%lld",
                     (int)writer_decoded.cat, (int)writer_decoded.subtype, writer_decoded.Rd,
                     writer_decoded.Rn, writer_decoded.Rm, (long long)writer_decoded.imm);

            g_str6a650_last_writer.valid = 1;
            g_str6a650_last_writer.writer_pc = pc_before_execute;
            g_str6a650_last_writer.writer_raw = writer_raw;
            strncpy(g_str6a650_last_writer.writer_mnemonic, pretty,
                    sizeof(g_str6a650_last_writer.writer_mnemonic) - 1);
            g_str6a650_last_writer
                .writer_mnemonic[sizeof(g_str6a650_last_writer.writer_mnemonic) - 1] = '\0';
            g_str6a650_last_writer.writer_rd = writer_decoded.Rd;
            g_str6a650_last_writer.writer_rn = writer_decoded.Rn;
            g_str6a650_last_writer.writer_rm = writer_decoded.Rm;
            g_str6a650_last_writer.writer_idx_mode = writer_decoded.idx_mode;
            g_str6a650_last_writer.writer_imm = writer_decoded.imm;
            g_str6a650_last_writer.x2_before = x2_before_execute;
            g_str6a650_last_writer.x2_after = cpu->x[2];
            g_str6a650_last_writer.block_start = block->start_pc;
            g_str6a650_last_writer.block_end = block->end_pc;

            ixland_guest_trace_field_t fields[] = {
                TRACE_FIELD_U64_HEX("guest_pc", pc_before_execute),
                TRACE_FIELD_U64_HEX("raw_opcode", writer_raw),
                TRACE_FIELD_STR("mnemonic", pretty),
                TRACE_FIELD_I64("base_reg", 2),
                TRACE_FIELD_U64_HEX("base_val", cpu->x[2]),
                TRACE_FIELD_I64("src_reg", writer_decoded.Rd),
                TRACE_FIELD_U64_HEX("src_val", trace_ldst_reg_or_zr(cpu, writer_decoded.Rd)),
                TRACE_FIELD_I64("idx_reg", writer_decoded.Rm),
                TRACE_FIELD_U64_HEX("idx_val", trace_ldst_reg_or_zr(cpu, writer_decoded.Rm)),
                TRACE_FIELD_I64("imm", writer_decoded.imm),
                TRACE_FIELD_I64("idx_mode", writer_decoded.idx_mode),
                TRACE_FIELD_STR("guest_ea", "na"),
                TRACE_FIELD_STR("host_probe", "na"),
                TRACE_FIELD_STR("translation_fault", "na"),
                TRACE_FIELD_I64("signal_follow_immediate", 0),
            };
            trace_str6a650_event_fields("str6a650.post_writer_regs", fields,
                                        sizeof(fields) / sizeof(fields[0]));

            if (pc_before_execute == 0x6a64cULL && writer_raw == 0xaa0703e2U) {
                ixland_guest_trace_field_t fields[] = {
                    TRACE_FIELD_U64_HEX("guest_pc", pc_before_execute),
                    TRACE_FIELD_U64_HEX("raw_opcode", writer_raw),
                    TRACE_FIELD_STR("mnemonic", "mov_x2_x7"),
                    TRACE_FIELD_U64_HEX("x7_before", x7_before_execute),
                    TRACE_FIELD_U64_HEX("x2_after", cpu->x[2]),
                    TRACE_FIELD_U64_HEX("block_start", block->start_pc),
                    TRACE_FIELD_U64_HEX("block_end", block->end_pc),
                    TRACE_FIELD_I64("x2_after_eq_x7_before",
                                    cpu->x[2] == x7_before_execute ? 1 : 0),
                };
                trace_x7chain_event_fields("mov6a64c.post_exec", fields,
                                           sizeof(fields) / sizeof(fields[0]));
            }
        }

        if (x7_writer_candidate) {
            char pretty[192] = { 0 };
            snprintf(pretty, sizeof(pretty), "cat:%d,sub:%d,rd:%d,rn:%d,rm:%d,imm:%lld",
                     (int)writer_decoded.cat, (int)writer_decoded.subtype, writer_decoded.Rd,
                     writer_decoded.Rn, writer_decoded.Rm, (long long)writer_decoded.imm);

            g_x7chain_last_writer.valid = 1;
            g_x7chain_last_writer.writer_pc = pc_before_execute;
            g_x7chain_last_writer.writer_raw = writer_raw;
            strncpy(g_x7chain_last_writer.writer_mnemonic, pretty,
                    sizeof(g_x7chain_last_writer.writer_mnemonic) - 1);
            g_x7chain_last_writer
                .writer_mnemonic[sizeof(g_x7chain_last_writer.writer_mnemonic) - 1] = '\0';
            g_x7chain_last_writer.writer_rd = writer_decoded.Rd;
            g_x7chain_last_writer.writer_rn = writer_decoded.Rn;
            g_x7chain_last_writer.writer_rm = writer_decoded.Rm;
            g_x7chain_last_writer.writer_idx_mode = writer_decoded.idx_mode;
            g_x7chain_last_writer.writer_imm = writer_decoded.imm;
            g_x7chain_last_writer.x7_before = x7_before_execute;
            g_x7chain_last_writer.x7_after = cpu->x[7];
            g_x7chain_last_writer.block_start = block->start_pc;
            g_x7chain_last_writer.block_end = block->end_pc;

            ixland_guest_trace_field_t fields[] = {
                TRACE_FIELD_U64_HEX("guest_pc", pc_before_execute),
                TRACE_FIELD_U64_HEX("raw_opcode", writer_raw),
                TRACE_FIELD_STR("mnemonic", pretty),
                TRACE_FIELD_U64_HEX("x7", cpu->x[7]),
                TRACE_FIELD_U64_HEX("x2", cpu->x[2]),
                TRACE_FIELD_U64_HEX("block_start", block->start_pc),
                TRACE_FIELD_U64_HEX("block_end", block->end_pc),
                TRACE_FIELD_I64("is_x7_one", cpu->x[7] == 1 ? 1 : 0),
                TRACE_FIELD_I64("is_x7_zero", cpu->x[7] == 0 ? 1 : 0),
            };
            trace_x7chain_event_fields("x7chain.post_writer_regs", fields,
                                       sizeof(fields) / sizeof(fields[0]));
        }
        trace_cpu_run_checkpoint("task.proof.a64_cpu_run.exit_reason_set", current, cpu,
                                 exit_reason);
        {
            static int exit_reason_log_budget = 16;
            if (exit_reason_log_budget > 0) {
                char ev[96];
                snprintf(ev, sizeof(ev), "task.proof.a64_cpu_run.exit_reason_value=%d",
                         exit_reason);
                trace_record_event(TRACE_ORIGIN_EXEC, ev);
                exit_reason_log_budget--;
            }
        }

        if (trace_is_active() && cpu->pc >= 0xf7fa4604 && cpu->pc <= 0xf7fa4650) {
            interpreter_loop_active = true;
            trace_interpreter_edge_checkpoint("task.proof.interpreter.edge", cpu, block->start_pc,
                                              block->end_pc, same_block_repeat_count);

            if (cpu->pc == 0xf7fa4604 || cpu->pc == 0xf7fa4650) {
                uint32_t raw_insn = 0;
                a64_instr_t decoded;
                if (a64_fetch_insn(cpu, cpu->tlb, cpu->pc, &raw_insn) == 0 &&
                    a64_decode(raw_insn, &decoded) == 0) {
                    const char *decode_name = cpu->pc == 0xf7fa4604
                                                  ? "task.proof.interpreter.edge.decode.entry"
                                                  : "task.proof.interpreter.edge.decode.repeat";
                    trace_insn_decode_checkpoint(decode_name, cpu->pc, raw_insn, decoded.cat,
                                                 decoded.subtype, decoded.Rn, decoded.Rm,
                                                 decoded.imm);
                    if (cpu->pc == 0xf7fa4650) {
                        trace_interpreter_cmp_entry_asm_checkpoint(
                            "task.proof.interpreter.cmp.entry.asm", block->start_pc, block->end_pc,
                            same_block_repeat_count);
                        trace_interpreter_nzcv_chain_checkpoint("task.proof.interpreter.nzcv.chain",
                                                                block->start_pc, block->end_pc,
                                                                same_block_repeat_count);
                        trace_interpreter_store_edge_checkpoint("task.proof.interpreter.store.edge",
                                                                cpu, same_block_repeat_count);
                        trace_interpreter_loop_predicate_checkpoint(
                            "task.proof.interpreter.loop.predicate", cpu, cpu->tlb, block->start_pc,
                            block->end_pc, same_block_repeat_count);
                    }
                }
            }
        } else if (interpreter_loop_active && !(cpu->pc >= 0xf7f3b000 && cpu->pc <= 0xf7ffdf10)) {
            interpreter_loop_active = false;
        }

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

            trace_guest_first_fault_capture(cpu, guest_fault_signal);

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

            // Emit first-fault proof before INT_GPF handling can terminate the task.
            trace_guest_first_fault_events();

            if (cpu->pc == 0x6a650ULL && g_guest_first_fault.seen) {
                const char *mnemonic = trace_guest_mnemonic(&g_guest_first_fault);
                ixland_guest_trace_field_t pre_helper_fields[] = {
                    TRACE_FIELD_U64_HEX("guest_pc", g_guest_first_fault.pc),
                    TRACE_FIELD_U64_HEX("raw_opcode", g_guest_first_fault.raw),
                    TRACE_FIELD_STR("mnemonic", mnemonic),
                    TRACE_FIELD_I64("base_reg", g_guest_first_fault.rn),
                    TRACE_FIELD_U64_HEX("base_val", g_guest_first_fault.rn_val),
                    TRACE_FIELD_I64("src_reg", g_guest_first_fault.rd),
                    TRACE_FIELD_U64_HEX("src_val", g_guest_first_fault.rt_val),
                    TRACE_FIELD_I64("idx_reg", g_guest_first_fault.rm),
                    TRACE_FIELD_U64_HEX("idx_val", g_guest_first_fault.rm_val),
                    TRACE_FIELD_I64("idx_mode", g_guest_first_fault.idx_mode),
                    TRACE_FIELD_U64_HEX("guest_ea", g_guest_first_fault.guest_ea),
                    TRACE_FIELD_STR("host_probe", "pending"),
                    TRACE_FIELD_STR("translation_fault", "pending"),
                    TRACE_FIELD_I64("signal_follow_immediate", 1),
                };
                trace_str6a650_event_fields("str6a650.pre_helper", pre_helper_fields,
                                            sizeof(pre_helper_fields) /
                                                sizeof(pre_helper_fields[0]));

                ixland_guest_trace_field_t translation_fields[] = {
                    TRACE_FIELD_U64_HEX("guest_pc", g_guest_first_fault.pc),
                    TRACE_FIELD_U64_HEX("raw_opcode", g_guest_first_fault.raw),
                    TRACE_FIELD_STR("mnemonic", mnemonic),
                    TRACE_FIELD_I64("base_reg", g_guest_first_fault.rn),
                    TRACE_FIELD_U64_HEX("base_val", g_guest_first_fault.rn_val),
                    TRACE_FIELD_I64("src_reg", g_guest_first_fault.rd),
                    TRACE_FIELD_U64_HEX("src_val", g_guest_first_fault.rt_val),
                    TRACE_FIELD_I64("idx_reg", g_guest_first_fault.rm),
                    TRACE_FIELD_U64_HEX("idx_val", g_guest_first_fault.rm_val),
                    TRACE_FIELD_I64("idx_mode", g_guest_first_fault.idx_mode),
                    TRACE_FIELD_U64_HEX("guest_ea", g_guest_first_fault.guest_ea),
                    TRACE_FIELD_U64_HEX("host_probe", g_guest_first_fault.host_ptr_probe),
                    TRACE_FIELD_I64("translation_fault", g_guest_first_fault.translation_fault),
                    TRACE_FIELD_I64("signal_follow_immediate", 1),
                };
                trace_str6a650_event_fields("str6a650.translation", translation_fields,
                                            sizeof(translation_fields) /
                                                sizeof(translation_fields[0]));
            }

            // Mark context inactive before handling fault
            fiber_exec_ctx_put(ctx);
            trace_cpu_run_checkpoint("task.proof.a64_cpu_run.before_handle_interrupt", current, cpu,
                                     exit_reason);
            handle_interrupt(INT_GPF);
            trace_cpu_run_checkpoint("task.proof.a64_cpu_run.after_handle_interrupt_call", current,
                                     cpu, exit_reason);
            trace_cpu_run_checkpoint("task.proof.a64_cpu_run.after_handle_interrupt", current, cpu,
                                     exit_reason);

            if (pc_before_execute == 0x6a650ULL && g_guest_first_fault.seen) {
                const char *mnemonic = trace_guest_mnemonic(&g_guest_first_fault);
                ixland_guest_trace_field_t exit_fields[] = {
                    TRACE_FIELD_U64_HEX("guest_pc", g_guest_first_fault.pc),
                    TRACE_FIELD_U64_HEX("raw_opcode", g_guest_first_fault.raw),
                    TRACE_FIELD_STR("mnemonic", mnemonic),
                    TRACE_FIELD_I64("base_reg", g_guest_first_fault.rn),
                    TRACE_FIELD_U64_HEX("base_val", g_guest_first_fault.rn_val),
                    TRACE_FIELD_I64("src_reg", g_guest_first_fault.rd),
                    TRACE_FIELD_U64_HEX("src_val", g_guest_first_fault.rt_val),
                    TRACE_FIELD_I64("idx_reg", g_guest_first_fault.rm),
                    TRACE_FIELD_U64_HEX("idx_val", g_guest_first_fault.rm_val),
                    TRACE_FIELD_I64("idx_mode", g_guest_first_fault.idx_mode),
                    TRACE_FIELD_U64_HEX("guest_ea", g_guest_first_fault.guest_ea),
                    TRACE_FIELD_U64_HEX("host_probe", g_guest_first_fault.host_ptr_probe),
                    TRACE_FIELD_I64("translation_fault", g_guest_first_fault.translation_fault),
                    TRACE_FIELD_I64("signal_follow_immediate", 1),
                };
                trace_str6a650_event_fields("str6a650.exit", exit_fields,
                                            sizeof(exit_fields) / sizeof(exit_fields[0]));
            }
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

            if (cpu->pc == 0x6967cULL) {
                static int stuck_6967c_budget = 24;
                if (stuck_6967c_budget > 0) {
                    char ev[224];
                    snprintf(ev, sizeof(ev),
                             "task.proof.6967c.block=before:0x%llx,start:0x%llx,end:0x%llx,"
                             "explicit:%d,after:0x%llx,reason:%d,pc_writer:%s",
                             (unsigned long long)pc_before_execute,
                             (unsigned long long)block->start_pc, (unsigned long long)block->end_pc,
                             block->explicit_pc_on_exit ? 1 : 0, (unsigned long long)cpu->pc,
                             exit_reason,
                             block->explicit_pc_on_exit ? "terminal_gadget" : "cpu_run_end_pc");
                    trace_record_event(TRACE_ORIGIN_EXEC, ev);
                    stuck_6967c_budget--;
                }
            }

            if (cpu->pc == 0x69650ULL) {
                static int stuck_site_budget = 16;
                if (stuck_site_budget > 0) {
                    uint32_t stuck_raw = 0;
                    if (a64_fetch_insn(cpu, cpu->tlb, cpu->pc, &stuck_raw) == 0) {
                        char ev_raw[96];
                        snprintf(ev_raw, sizeof(ev_raw),
                                 "task.proof.a64_cpu_run.stuck_site.raw=0x%08x", stuck_raw);
                        trace_record_event(TRACE_ORIGIN_EXEC, ev_raw);

                        a64_instr_t stuck_decoded;
                        if (a64_decode(stuck_raw, &stuck_decoded) == 0) {
                            char ev_dec[192];
                            snprintf(ev_dec, sizeof(ev_dec),
                                     "task.proof.a64_cpu_run.stuck_site.decoded=cat:%d,sub:%d,rd:%"
                                     "d,rn:%d,rm:%d,idx:%d,imm:%d",
                                     stuck_decoded.cat, stuck_decoded.subtype, stuck_decoded.Rd,
                                     stuck_decoded.Rn, stuck_decoded.Rm, stuck_decoded.idx_mode,
                                     stuck_decoded.imm);
                            trace_record_event(TRACE_ORIGIN_EXEC, ev_dec);
                        }
                    }
                    stuck_site_budget--;
                }
            }
        }

        // Stage 3A.6: Track PC progression and detect loops
        // If we've executed many blocks without a syscall, we're in pre-syscall init
        if (total_blocks_executed >= 1000 && !first_user_entry && trace_is_active()) {
            trace_pre_syscall_checkpoint("task.proof.user.pre_syscall.loop_suspected", cpu->pc,
                                         last_block_start_pc, block->start_pc,
                                         same_block_repeat_count, total_blocks_executed);
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
                                 decoded.cat, decoded.subtype, decoded.Rn, decoded.Rm, decoded.imm);
                        trace_record_event(TRACE_ORIGIN_EXEC, dec_ev);
                    }
                }
            }
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
