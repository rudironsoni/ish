/*
 * Memory access gadgets for TCTI
 *
 * Memory-backed registers (x16-x30, SP) are stored in cpu_state memory.
 * To operate on them, we:
 *   1. Load into temp register (x14 for values, x15 for auxiliaries)
 *   2. Execute operation
 *   3. Store back to memory
 *
 * Register mapping:
 *   x0-x15 (guest)  -> x1-x16 (host)   [TCTI-mapped, always hot]
 *   x16-x30 (guest) -> memory only     [load/store via gadgets]
 *   SP (guest)      -> memory only     [load/store via gadgets]
 *   x14-x15 (host)  -> temps           [for memory-backed register ops]
 *   x27-x29 (host)  -> TCTI internals  [gadget ptr, bytecode, cpu_state]
 */

#if __has_include(<IXLandInstrumentationTracing/trace.h>)
#import <IXLandInstrumentationTracing/trace.h>
#elif __has_include(                                                                               \
    "../../../../Packages/IXLandInstrumentation/Sources/IXLandInstrumentationTracing/include/IXLandInstrumentationTracing/trace.h")
#import "../../../../Packages/IXLandInstrumentation/Sources/IXLandInstrumentationTracing/include/IXLandInstrumentationTracing/trace.h"
#else
#error "trace.h not found for gadgets_memory.c"
#endif
#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>
#import <IXLandLinuxRuntime/emu/aarch64/memory.h>
#import <IXLandLinuxRuntime/emu/aarch64/sysreg.h>
#import <IXLandLinuxRuntime/emu/tlb.h>
#import <IXLandLinuxRuntime/kernel/memory.h>
#import <IXLandLinuxRuntime/kernel/page_map.h>
#import <IXLandLinuxRuntime/kernel/task.h>
#import <IXLandLinuxRuntime/tcti/gadgets_tcti.h>
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// ============================================================================
// TCTI Helper Function Declarations
// ============================================================================
// These functions are called from naked assembly gadgets and must be
// declared before use to ensure proper symbol visibility.

extern int _a64_tcti_ldr_x_helper(struct cpu_state *cpu, uint64_t fault_pc, uint64_t rt,
                                  uint64_t rn, int64_t imm, uint64_t size, uint64_t idx_mode,
                                  uint64_t meta);
extern int _a64_tcti_str_x_helper(struct cpu_state *cpu, uint64_t fault_pc, uint64_t rt,
                                  uint64_t rn, int64_t imm, uint64_t size, uint64_t idx_mode,
                                  uint64_t meta);

static uint64_t tcti_extend_ldst_offset(struct cpu_state *cpu, int rm, int extend_type);

static const char *trace_mem_obj_kind_name(enum mem_object_kind kind)
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

static const char *trace_page0_pc_event_name(uint64_t fault_pc)
{
    switch (fault_pc) {
    case 0x6a628ULL:
        return "mm.page0.state.before_0x6a628";
    case 0x6a634ULL:
        return "mm.page0.state.before_0x6a634";
    case 0x6a650ULL:
        return "mm.page0.state.before_0x6a650";
    default:
        return NULL;
    }
}

static void trace_page0_translation_identity(struct cpu_state *cpu, addr_t addr, int is_load,
                                             uint64_t fault_pc, uint32_t raw_opcode,
                                             void *host_ptr_probe)
{
    static int budget = 12;
    if (budget <= 0 || PAGE(addr) != 0)
        return;

    struct mem *mem = cpu->mmu ? container_of(cpu->mmu, struct mem, mmu) : NULL;
    struct page_desc *desc = mem ? page_map_lookup(&mem->pages, 0) : NULL;
    struct mem_object *obj = desc ? desc->obj : NULL;

    void *host_ptr_from_map = NULL;
    if (obj && obj->host_base != NULL) {
        size_t host_off = desc->offset + PGOFFSET(addr);
        if (host_off < obj->host_size)
            host_ptr_from_map = (void *)((uint8_t *)obj->host_base + host_off);
    }

    struct tlb_entry tlb_snapshot = { 0 };
    if (cpu->tlb)
        tlb_snapshot = cpu->tlb->entries[TLB_INDEX(addr)];

    char ev[512];
    snprintf(ev, sizeof(ev),
             "mismatch.probe.identity=guest_pc:0x%llx,raw_opcode:0x%08x,guest_ea:0x%llx,"
             "guest_page:0x%llx,is_load:%d,cpu_mmu:%p,tlb:%p,tlb_mmu:%p,cpu_mmu_gen:%llu,"
             "tlb_mmu_gen:%llu,tlb_entry_gen:%llu,tlb_entry_page:0x%llx,tlb_entry_wpage:0x%llx",
             (unsigned long long)fault_pc, raw_opcode, (unsigned long long)addr,
             (unsigned long long)PAGE(addr), is_load ? 1 : 0, (void *)cpu->mmu, (void *)cpu->tlb,
             cpu->tlb ? (void *)cpu->tlb->mmu : NULL,
             (unsigned long long)(cpu->mmu ? cpu->mmu->generation : 0),
             (unsigned long long)(cpu->tlb && cpu->tlb->mmu ? cpu->tlb->mmu->generation : 0),
             (unsigned long long)tlb_snapshot.generation, (unsigned long long)tlb_snapshot.page,
             (unsigned long long)tlb_snapshot.page_if_writable);
    trace_record_event(TRACE_ORIGIN_EXEC, ev);

    snprintf(
        ev, sizeof(ev),
        "mismatch.probe.maplookup=guest_page:0x%llx,mapped:%s,page_desc:%p,obj:%p,obj_kind:%s,"
        "obj_name:%s,obj_host_base:%p,obj_host_size:%zu,obj_file_off:0x%zx,page_desc_off:0x%zx,"
        "page_flags:0x%x",
        (unsigned long long)PAGE(addr), desc ? "yes" : "no", (void *)desc, (void *)obj,
        obj ? trace_mem_obj_kind_name(obj->kind) : "none", (obj && obj->name) ? obj->name : "none",
        obj ? obj->host_base : NULL, obj ? obj->host_size : 0, obj ? obj->file_offset : 0,
        desc ? desc->offset : 0, desc ? desc->flags : 0);
    trace_record_event(TRACE_ORIGIN_EXEC, ev);

    snprintf(ev, sizeof(ev),
             "mismatch.probe.translation=guest_ea:0x%llx,host_ptr_probe:%p,host_ptr_from_map:%p,"
             "map_hit:%s,ptr_match:%s",
             (unsigned long long)addr, host_ptr_probe, host_ptr_from_map, desc ? "yes" : "no",
             (host_ptr_probe != NULL && host_ptr_probe == host_ptr_from_map) ? "yes" : "no");
    trace_record_event(TRACE_ORIGIN_EXEC, ev);

    const char *page0_event = trace_page0_pc_event_name(fault_pc);
    if (page0_event != NULL) {
        snprintf(ev, sizeof(ev),
                 "%s=mapped:%s,guest_page:0x%llx,guest_ea:0x%llx,backing:%s,host_ptr_probe:%p,"
                 "host_ptr_map:%p,guest_range:[0x%llx..0x%llx],task:%p,mm:%p,mem:%p,cpu:%p,tlb:%p,"
                 "page_desc:%p,obj:%p,cpu_mmu:%p,tlb_mmu:%p,cpu_mmu_gen:%llu,tlb_mmu_gen:%llu,"
                 "page_desc_off:0x%zx,page_flags:0x%x",
                 page0_event, desc ? "yes" : "no", (unsigned long long)PAGE(addr),
                 (unsigned long long)addr,
                 (obj && obj->name) ? obj->name
                                    : (obj ? trace_mem_obj_kind_name(obj->kind) : "none"),
                 host_ptr_probe, host_ptr_from_map, (unsigned long long)(PAGE(addr) << PAGE_BITS),
                 (unsigned long long)(((PAGE(addr) + 1) << PAGE_BITS) - 1), (void *)current,
                 current ? (void *)current->mm : NULL, current ? (void *)current->mem : NULL,
                 (void *)cpu, (void *)cpu->tlb, (void *)desc, (void *)obj, (void *)cpu->mmu,
                 cpu->tlb ? (void *)cpu->tlb->mmu : NULL,
                 (unsigned long long)(cpu->mmu ? cpu->mmu->generation : 0),
                 (unsigned long long)(cpu->tlb && cpu->tlb->mmu ? cpu->tlb->mmu->generation : 0),
                 desc ? desc->offset : 0, desc ? desc->flags : 0);
        trace_record_event(TRACE_ORIGIN_EXEC, ev);
    }

    budget--;
}

static void trace_base6a628_helper_event(const char *name, struct cpu_state *cpu, uint64_t fault_pc,
                                         uint32_t raw_opcode, const char *mnemonic,
                                         const a64_instr_t *decoded, uint64_t base_value,
                                         uint64_t guest_ea, void *host_ptr_probe,
                                         int helper_entry_reached, int signal_immediate)
{
    if (!name || !cpu || !decoded || fault_pc != 0x6a628ULL)
        return;

    static int budget = 24;
    if (budget <= 0)
        return;

    char ev[768];
    snprintf(ev, sizeof(ev),
             "%s=attempt:-1,guest_pid:%d,guest_pc:0x%llx,raw_opcode:0x%08x,mnemonic:%s,"
             "arch_base_reg:%d,arch_base_val:0x%llx,carrier_val:0x%llx,cpu_slot_val:0x%llx,"
             "block_start:0x0,block_end:0x0,is_zero:%d,helper_path_reached:%d,"
             "signal_immediate:%d,guest_ea:0x%llx,host_ptr_probe:0x%llx",
             name, current ? current->pid : -1, (unsigned long long)fault_pc, raw_opcode,
             mnemonic ? mnemonic : "unknown", decoded->Rn, (unsigned long long)base_value,
             (unsigned long long)base_value, (unsigned long long)cpu->x[decoded->Rn],
             base_value == 0 ? 1 : 0, helper_entry_reached, signal_immediate,
             (unsigned long long)guest_ea, (unsigned long long)(uintptr_t)host_ptr_probe);
    trace_record_event(TRACE_ORIGIN_EXEC, ev);
    budget--;
}

// Stub functions for removed diagnostics
void dump_str_wb_diag(void)
{
    (void)0;
}
void dump_cmp_capture(void)
{
    (void)0;
}
void dump_cmp_bcond_diag(void)
{
    (void)0;
}
void dump_runtime_diag(void)
{
    (void)0;
}

// Verify offset assumptions at compile time
#define XREG_OFFSET(n)   (offsetof(struct cpu_state, x[n]))
#define SP_OFFSET        offsetof(struct cpu_state, sp)
#define PC_OFFSET        offsetof(struct cpu_state, pc)
#define PSTATE_OFFSET    offsetof(struct cpu_state, pstate)
#define TCTI_EXIT_OFFSET offsetof(struct cpu_state, tcti_exit_reason)

// Guest x[0] is at offset 16 (after mmu pointer and cycle counter)
_Static_assert(XREG_OFFSET(0) == 16, "x[0] offset check");
_Static_assert(SP_OFFSET == 264, "SP offset check");
_Static_assert(PC_OFFSET == 272, "pc offset check");
_Static_assert(PSTATE_OFFSET == 280, "pstate offset check");
// TCTI_EXIT_OFFSET verified to be 364 via runtime check

#define REG_OFFSET(n) (XREG_OFFSET(n))

static uint64_t tcti_read_base_reg_or_sp(struct cpu_state *cpu, int reg)
{
    if (reg == 31)
        return cpu->sp;
    if (reg < 0 || reg > 30)
        return 0;
    return cpu->x[reg];
}

static uint64_t tcti_read_reg_or_zr(struct cpu_state *cpu, int reg)
{
    if (reg == 31)
        return 0;
    if (reg < 0 || reg > 30)
        return 0;
    return cpu->x[reg];
}

static int a64_tcti_mrs_helper(struct cpu_state *cpu, uint64_t sysreg, uint64_t rd)
{
    return a64_sysreg_read(cpu, (uint16_t)sysreg, rd);
}

static int a64_tcti_msr_helper(struct cpu_state *cpu, uint64_t sysreg, uint64_t rt)
{
    return a64_sysreg_write(cpu, (uint16_t)sysreg, rt);
}

static void trace_reg_write_checkpoint(const char *name, int reg, uint64_t old_val,
                                       uint64_t new_val)
{
    char reg_buf[8];
    char old_buf[24];
    char new_buf[24];

    snprintf(reg_buf, sizeof(reg_buf), "%d", reg);
    snprintf(old_buf, sizeof(old_buf), "0x%llx", (unsigned long long)old_val);
    snprintf(new_buf, sizeof(new_buf), "0x%llx", (unsigned long long)new_val);

    trace_attribute_t attrs[] = {
        { "reg", reg_buf },
        { "old_val", old_buf },
        { "new_val", new_buf },
    };

    (void)trace_begin_interval(TRACE_ORIGIN_EXEC, name, attrs, sizeof(attrs) / sizeof(attrs[0]));
}

/*
 * X2 Provenance Trace - Captures detailed information about X2 writes
 * for APPSIM-003 Phase 1 analysis
 */
static void trace_x2_provenance_checkpoint(uint64_t fault_pc, uint32_t raw_insn, uint64_t old_val,
                                           uint64_t new_val, const char *mnemonic, int rn, int rm,
                                           uint64_t rn_value, int64_t imm, int idx_mode,
                                           int is_load)
{
    char pc_buf[24];
    char insn_buf[16];
    char old_buf[24];
    char new_buf[24];
    char rn_buf[8];
    char rm_buf[8];
    char rn_val_buf[24];
    char imm_buf[24];
    char idx_mode_buf[8];

    snprintf(pc_buf, sizeof(pc_buf), "0x%llx", (unsigned long long)fault_pc);
    snprintf(insn_buf, sizeof(insn_buf), "0x%08x", raw_insn);
    snprintf(old_buf, sizeof(old_buf), "0x%llx", (unsigned long long)old_val);
    snprintf(new_buf, sizeof(new_buf), "0x%llx", (unsigned long long)new_val);
    snprintf(rn_buf, sizeof(rn_buf), "%d", rn);
    snprintf(rm_buf, sizeof(rm_buf), "%d", rm);
    snprintf(rn_val_buf, sizeof(rn_val_buf), "0x%llx", (unsigned long long)rn_value);
    snprintf(imm_buf, sizeof(imm_buf), "%lld", (long long)imm);
    snprintf(idx_mode_buf, sizeof(idx_mode_buf), "%d", idx_mode);

    trace_attribute_t attrs[] = {
        { "fault_pc", pc_buf },
        { "raw_insn", insn_buf },
        { "old_val", old_buf },
        { "new_val", new_buf },
        { "mnemonic", mnemonic },
        { "rn", rn_buf },
        { "rm", rm_buf },
        { "rn_value", rn_val_buf },
        { "imm", imm_buf },
        { "idx_mode", idx_mode_buf },
        { "is_load", is_load ? "1" : "0" },
    };

    (void)trace_begin_interval(TRACE_ORIGIN_EXEC, "task.proof.x2.provenance", attrs,
                               sizeof(attrs) / sizeof(attrs[0]));
}

/*
 * Get mnemonic string for load/store instructions
 */
static const char *get_ldst_mnemonic(int is_load, int size, int is_signed)
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
    } else {
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
}

static const char *get_ldst_extend_name(int extend_type)
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

static const char *get_ldst_idx_mode_name(uint64_t idx_mode)
{
    switch (idx_mode) {
    case A64_PRE_INDEX:
        return "pre_index";
    case A64_POST_INDEX:
        return "post_index";
    case A64_INDEX_OFFSET:
    default:
        return "index_offset";
    }
}

static void trace_6967c_ldst_probe(struct cpu_state *cpu, uint64_t fault_pc, uint64_t rt,
                                   uint64_t rn, int64_t imm, uint64_t size, uint64_t idx_mode,
                                   uint64_t meta, uint64_t is_load, uint64_t base, uint64_t addr,
                                   int mem_ret)
{
    static int budget = 24;
    if (fault_pc != 0x6967cULL || budget <= 0)
        return;

    uint32_t raw = 0;
    a64_instr_t decoded;
    bool decode_ok = false;
    if (a64_fetch_insn(cpu, cpu->tlb, fault_pc, &raw) == 0 && a64_decode(raw, &decoded) == 0)
        decode_ok = true;

    uint64_t is_signed = meta & 0xff;
    uint64_t is_reg_offset = (meta >> 8) & 0xff;
    int rm = (meta >> 16) & 0xff;
    int extend_type = (meta >> 24) & 0xff;
    int reg_shift = (meta >> 32) & 0xff;

    uint64_t rt_val = tcti_read_reg_or_zr(cpu, (int)rt);
    uint64_t rn_val = tcti_read_base_reg_or_sp(cpu, (int)rn);
    uint64_t rm_val = is_reg_offset ? tcti_read_reg_or_zr(cpu, rm) : 0;

    int writeback_expected =
        (!is_reg_offset && (idx_mode == A64_PRE_INDEX || idx_mode == A64_POST_INDEX)) ? 1 : 0;
    uint64_t writeback_val =
        writeback_expected ? ((idx_mode == A64_POST_INDEX) ? (base + imm) : base) : rn_val;

    struct mem *mem = cpu->mmu ? container_of(cpu->mmu, struct mem, mmu) : NULL;
    page_t page = PAGE(addr);
    struct page_desc *desc = mem ? page_map_lookup(&mem->pages, page) : NULL;
    uint64_t host_ptr = 0;
    if (desc && desc->obj) {
        host_ptr =
            (uint64_t)((char *)desc->obj->host_base + desc->offset + (unsigned)PGOFFSET(addr));
    }

    char ev[256];
    snprintf(ev, sizeof(ev), "task.proof.6967c.raw=0x%08x", raw);
    trace_record_event(TRACE_ORIGIN_EXEC, ev);

    if (decode_ok) {
        snprintf(ev, sizeof(ev),
                 "task.proof.6967c.decode=cat:%d,sub:%d,rt:%d,rn:%d,rm:%d,idx:%d,ext:%d,shift:%d",
                 decoded.cat, decoded.subtype, decoded.Rd, decoded.Rn, decoded.Rm, decoded.idx_mode,
                 decoded.extend_type, decoded.imm_shift);
        trace_record_event(TRACE_ORIGIN_EXEC, ev);
    }

    snprintf(ev, sizeof(ev), "task.proof.6967c.human=%s x%llu,[x%llu,x%d,%s #%d]",
             get_ldst_mnemonic((int)is_load, (int)size, (int)is_signed), (unsigned long long)rt,
             (unsigned long long)rn, rm, get_ldst_extend_name(extend_type), reg_shift);
    trace_record_event(TRACE_ORIGIN_EXEC, ev);

    snprintf(ev, sizeof(ev),
             "task.proof.6967c.operands=rt_val:0x%llx,rn_val:0x%llx,rm_val:0x%llx,imm:%lld",
             (unsigned long long)rt_val, (unsigned long long)rn_val, (unsigned long long)rm_val,
             (long long)imm);
    trace_record_event(TRACE_ORIGIN_EXEC, ev);

    snprintf(ev, sizeof(ev), "task.proof.6967c.guest_ea=0x%llx", (unsigned long long)addr);
    trace_record_event(TRACE_ORIGIN_EXEC, ev);

    snprintf(ev, sizeof(ev),
             "task.proof.6967c.translation=page:%s,host_ptr:0x%llx,mem_ret:%d,is_load:%llu",
             desc ? "hit" : "miss", (unsigned long long)host_ptr, mem_ret,
             (unsigned long long)is_load);
    trace_record_event(TRACE_ORIGIN_EXEC, ev);

    snprintf(ev, sizeof(ev), "task.proof.6967c.writeback=expected:%d,val:0x%llx",
             writeback_expected, (unsigned long long)writeback_val);
    trace_record_event(TRACE_ORIGIN_EXEC, ev);

    snprintf(ev, sizeof(ev), "task.proof.6967c.path=helper_slow");
    trace_record_event(TRACE_ORIGIN_EXEC, ev);

    budget--;
}

static void trace_live_ldr_probe(struct cpu_state *cpu, uint64_t instance_id, uint64_t fault_pc,
                                 uint64_t rt, uint64_t rn, int64_t imm, uint64_t size,
                                 uint64_t idx_mode, uint64_t meta, uint64_t is_load, uint64_t base,
                                 uint64_t addr, int mem_ret, int phase_pre)
{
    static int budget = 48;
    if (budget <= 0)
        return;

    uint32_t raw = 0;
    a64_instr_t decoded;
    bool decode_ok = false;
    if (a64_fetch_insn(cpu, cpu->tlb, fault_pc, &raw) == 0 && a64_decode(raw, &decoded) == 0)
        decode_ok = true;

    uint64_t is_signed = meta & 0xff;
    uint64_t is_reg_offset = (meta >> 8) & 0xff;
    int rm = (meta >> 16) & 0xff;
    int extend_type = (meta >> 24) & 0xff;
    int reg_shift = (meta >> 32) & 0xff;

    uint64_t rt_val = tcti_read_reg_or_zr(cpu, (int)rt);
    uint64_t rn_val = tcti_read_base_reg_or_sp(cpu, (int)rn);
    uint64_t rm_val = is_reg_offset ? tcti_read_reg_or_zr(cpu, rm) : 0;
    uint64_t offset_before_shift =
        is_reg_offset ? tcti_extend_ldst_offset(cpu, rm, extend_type) : (uint64_t)imm;
    uint64_t computed_offset = is_reg_offset ? (offset_before_shift << reg_shift) : (uint64_t)imm;

    int writeback_expected =
        (!is_reg_offset && (idx_mode == A64_PRE_INDEX || idx_mode == A64_POST_INDEX)) ? 1 : 0;
    uint64_t writeback_val =
        writeback_expected ? ((idx_mode == A64_POST_INDEX) ? (base + imm) : base) : rn_val;

    struct mem *mem = cpu->mmu ? container_of(cpu->mmu, struct mem, mmu) : NULL;
    page_t page = PAGE(addr);
    struct page_desc *desc = mem ? page_map_lookup(&mem->pages, page) : NULL;
    uint64_t host_ptr_page = 0;
    if (desc && desc->obj) {
        host_ptr_page =
            (uint64_t)((char *)desc->obj->host_base + desc->offset + (unsigned)PGOFFSET(addr));
    }

    char ev[320];
    snprintf(ev, sizeof(ev), "task.proof.ldr_live.phase=%s,instance:%llu",
             phase_pre ? "pre" : "post", (unsigned long long)instance_id);
    trace_record_event(TRACE_ORIGIN_EXEC, ev);

    if (phase_pre) {
        snprintf(ev, sizeof(ev), "task.proof.ldr_live.pc=0x%llx", (unsigned long long)fault_pc);
        trace_record_event(TRACE_ORIGIN_EXEC, ev);

        snprintf(ev, sizeof(ev), "task.proof.ldr_live.raw=0x%08x", raw);
        trace_record_event(TRACE_ORIGIN_EXEC, ev);

        if (decode_ok) {
            snprintf(ev, sizeof(ev),
                     "task.proof.ldr_live.decode=cat:%d,sub:%d,rt:%d,rn:%d,rm:%d,idx:%d,ext:%d,"
                     "shift:%d,imm:%lld",
                     decoded.cat, decoded.subtype, decoded.Rd, decoded.Rn, decoded.Rm,
                     decoded.idx_mode, decoded.extend_type, decoded.imm_shift,
                     (long long)decoded.imm);
            trace_record_event(TRACE_ORIGIN_EXEC, ev);
        }

        snprintf(ev, sizeof(ev), "task.proof.ldr_live.human=%s x%llu,[x%llu,x%d,%s #%d]",
                 get_ldst_mnemonic((int)is_load, (int)size, (int)is_signed), (unsigned long long)rt,
                 (unsigned long long)rn, rm, get_ldst_extend_name(extend_type), reg_shift);
        trace_record_event(TRACE_ORIGIN_EXEC, ev);

        snprintf(ev, sizeof(ev),
                 "task.proof.ldr_live.regs=is_load:%llu,is_reg_offset:%llu,rt_val:0x%llx,rn_val:"
                 "0x%llx,rm_val:0x%llx",
                 (unsigned long long)is_load, (unsigned long long)is_reg_offset,
                 (unsigned long long)rt_val, (unsigned long long)rn_val,
                 (unsigned long long)rm_val);
        trace_record_event(TRACE_ORIGIN_EXEC, ev);

        snprintf(ev, sizeof(ev),
                 "task.proof.ldr_live.address=offset_pre_shift:0x%llx,computed_offset:0x%llx,"
                 "guest_ea:0x%llx",
                 (unsigned long long)offset_before_shift, (unsigned long long)computed_offset,
                 (unsigned long long)addr);
        trace_record_event(TRACE_ORIGIN_EXEC, ev);

        snprintf(ev, sizeof(ev),
                 "task.proof.ldr_live.translation=page_lookup:%s,host_ptr_page:0x%llx",
                 desc ? "hit" : "miss", (unsigned long long)host_ptr_page);
        trace_record_event(TRACE_ORIGIN_EXEC, ev);

        snprintf(ev, sizeof(ev), "task.proof.ldr_live.writeback=expected:%d,val:0x%llx",
                 writeback_expected, (unsigned long long)writeback_val);
        trace_record_event(TRACE_ORIGIN_EXEC, ev);

        snprintf(ev, sizeof(ev), "task.proof.ldr_live.path=helper_slow");
        trace_record_event(TRACE_ORIGIN_EXEC, ev);
    }

    snprintf(ev, sizeof(ev), "task.proof.ldr_live.mem_result=mem_ret:%d", mem_ret);
    trace_record_event(TRACE_ORIGIN_EXEC, ev);

    budget--;
}

static int a64_tcti_guest_write64_strict(struct cpu_state *cpu, struct tlb *tlb, uint64_t addr,
                                         uint64_t val)
{
    void *fast = __tlb_write_ptr(tlb, addr);
    if (fast) {
        *(uint64_t *)fast = val;
        return A64_MEM_OK;
    }

    void *slow = tlb_handle_miss(tlb, addr, MEM_WRITE);
    if (!slow) {
        cpu->fault_addr = tlb->segfault_addr;
        return A64_MEM_FAULT;
    }

    *(uint64_t *)slow = val;
    return A64_MEM_OK;
}

static void tcti_write_base_reg_or_sp(struct cpu_state *cpu, int reg, uint64_t value, int is_64bit)
{
    uint64_t masked = is_64bit ? value : (uint32_t)value;
    uint64_t old_val = 0;

    // Capture old value for X2 tracing
    if (reg == 2) {
        old_val = cpu->x[2];
    }

    if (reg == 31) {
        cpu->sp = masked;
        return;
    }
    if (reg < 0 || reg > 30)
        return;

    // Trace X2 modifications for provenance analysis
    if (reg == 2) {
        trace_reg_write_checkpoint("task.proof.x2.write", reg, old_val, masked);
    }

    cpu->x[reg] = masked;
}

static void tcti_write_reg_or_zr(struct cpu_state *cpu, int reg, uint64_t value, int is_64bit)
{
    uint64_t masked = is_64bit ? value : (uint32_t)value;
    if (reg == 31)
        return;
    if (reg < 0 || reg > 30)
        return;
    cpu->x[reg] = masked;
}

static uint64_t tcti_extend_ldst_offset(struct cpu_state *cpu, int rm, int extend_type)
{
    uint64_t value = tcti_read_reg_or_zr(cpu, rm);

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

static uint64_t g_ldst_instance_id = 0;

static void trace_ldst_arch_checkpoint(const char *name, uint64_t fault_pc, uint64_t base,
                                       uint64_t addr, int rn, int64_t imm, int idx_mode,
                                       uint64_t instance_id)
{
    char fault_pc_buf[24];
    char base_buf[24];
    char addr_buf[24];
    char rn_buf[8];
    char imm_buf[24];
    char idx_mode_buf[8];
    char instance_buf[24];

    snprintf(fault_pc_buf, sizeof(fault_pc_buf), "0x%llx", (unsigned long long)fault_pc);
    snprintf(base_buf, sizeof(base_buf), "0x%llx", (unsigned long long)base);
    snprintf(addr_buf, sizeof(addr_buf), "0x%llx", (unsigned long long)addr);
    snprintf(rn_buf, sizeof(rn_buf), "%d", rn);
    snprintf(imm_buf, sizeof(imm_buf), "%lld", (long long)imm);
    snprintf(idx_mode_buf, sizeof(idx_mode_buf), "%d", idx_mode);
    snprintf(instance_buf, sizeof(instance_buf), "%llu", (unsigned long long)instance_id);

    trace_attribute_t attrs[] = {
        { "fault_pc", fault_pc_buf },
        { "base_reg", base_buf },
        { "access_addr", addr_buf },
        { "rn", rn_buf },
        { "imm", imm_buf },
        { "idx_mode", idx_mode_buf },
        { "instance_id", instance_buf },
    };

    (void)trace_begin_interval(TRACE_ORIGIN_EXEC, name, attrs, sizeof(attrs) / sizeof(attrs[0]));
}

void trace_add_imm_sp_to_x7_probe(struct cpu_state *cpu, uint64_t src_x14, uint64_t new_x7)
{
    (void)cpu;
    (void)src_x14;
    (void)new_x7;
}

void trace_mov_x7_to_x2_probe(struct cpu_state *cpu, uint64_t src_x7, uint64_t new_x2)
{
    (void)cpu;
    (void)src_x7;
    (void)new_x2;
}

void trace_probe_x7_x2_state(struct cpu_state *cpu, uint64_t guest_pc)
{
    (void)cpu;
    (void)guest_pc;
}

void trace_str_handoff_probe(struct cpu_state *cpu, uint64_t stage, uint64_t fault_pc,
                             uint64_t live_x3, uint64_t live_x8, uint64_t live_x14,
                             uint64_t live_x20, uint64_t live_x21)
{
    static int captured = 0;
    if (!cpu || fault_pc != 0x69650ULL || captured >= 6)
        return;
    captured++;

    char ev[512];
    snprintf(ev, sizeof(ev),
             "task.proof.69650.handoff=stage:%llu,fault_pc:0x%llx,live_x3:0x%llx,live_x8:0x%llx,"
             "live_x14:0x%llx,live_rt_x20:0x%llx,live_rn_x21:0x%llx,cpu_x2:0x%llx,cpu_x7:0x%llx,"
             "cpu_sp:0x%llx,cpu_pc:0x%llx",
             (unsigned long long)stage, (unsigned long long)fault_pc, (unsigned long long)live_x3,
             (unsigned long long)live_x8, (unsigned long long)live_x14,
             (unsigned long long)live_x20, (unsigned long long)live_x21,
             (unsigned long long)cpu->x[2], (unsigned long long)cpu->x[7],
             (unsigned long long)cpu->sp, (unsigned long long)cpu->pc);
    trace_record_event(TRACE_ORIGIN_EXEC, ev);
}

__attribute__((naked)) void gadget_probe_x7_x2_state(void)
{
    asm volatile("ldr x19, [x28], #8\n\t"
                 "stp x1, x2, [x29, #16]\n\t"
                 "stp x3, x4, [x29, #32]\n\t"
                 "stp x5, x6, [x29, #48]\n\t"
                 "stp x7, x8, [x29, #64]\n\t"
                 "stp x9, x10, [x29, #80]\n\t"
                 "stp x11, x12, [x29, #96]\n\t"
                 "stp x13, x14, [x29, #112]\n\t"
                 "stp x15, x16, [x29, #128]\n\t"
                 "bl _tcti_c_call_prologue\n\t"
                 "mov x0, x29\n\t"
                 "mov x1, x19\n\t"
                 "bl _trace_probe_x7_x2_state\n\t"
                 "bl _tcti_c_call_epilogue\n\t"
                 "ldp x1, x2, [x29, #16]\n\t"
                 "ldp x3, x4, [x29, #32]\n\t"
                 "ldp x5, x6, [x29, #48]\n\t"
                 "ldp x7, x8, [x29, #64]\n\t"
                 "ldp x9, x10, [x29, #80]\n\t"
                 "ldp x11, x12, [x29, #96]\n\t"
                 "ldp x13, x14, [x29, #112]\n\t"
                 "ldp x15, x16, [x29, #128]\n\t"
                 "ldr x27, [x28], #8\n\t"
                 "br x27\n\t");
}

__attribute__((naked)) void gadget_force_x7_from_sp_plus_8(void)
{
    asm volatile("ldr x8, [x29, %[sp_off]]\n\t"
                 "add x8, x8, #8\n\t"
                 "ldr x27, [x28], #8\n\t"
                 "br x27\n\t"
                 :
                 : [sp_off] "i"(SP_OFFSET));
}

__attribute__((naked)) void gadget_probe_live_x3_x8(void)
{
    asm volatile("ldr x19, [x28], #8\n\t"
                 "ldr x20, [x28], #8\n\t"
                 "bl _tcti_c_call_prologue\n\t"
                 // CRITICAL FIX: Restore x3 and x8 from stack after prologue
                 // Prologue does 13 pushes (208 bytes total):
                 //   [sp+0] = NZCV, [sp+16] = x28
                 //   [sp+32] = x1/x2, [sp+48] = x3/x4
                 //   [sp+64] = x5/x6, [sp+80] = x7/x8
                 // So x3 is at [sp+48], x8 is at [sp+80]
                 "ldr x3, [sp, #48]\n\t"
                 "ldr x8, [sp, #80]\n\t"
                 "mov x0, x29\n\t"
                 "mov x1, x19\n\t"
                 "mov x2, x20\n\t"
                 "mov x4, x8\n\t"
                 "mov x5, x14\n\t"
                 "mov x6, #0\n\t"
                 "mov x7, #0\n\t"
                 "bl _trace_str_handoff_probe\n\t"
                 "bl _tcti_c_call_epilogue\n\t"
                 "ldr x27, [x28], #8\n\t"
                 "br x27\n\t");
}

static int a64_tcti_ldst_helper(struct cpu_state *cpu, uint64_t fault_pc, uint64_t rt, uint64_t rn,
                                int64_t imm, uint64_t size, uint64_t idx_mode, uint64_t meta,
                                uint64_t is_load)
{
    // Generate unique instance ID for correlation
    uint64_t instance_id = ++g_ldst_instance_id;

    // FIRST FAULT GUARD: emits detailed analysis once at actual fault decision point
    static int first_fault_captured = 0;
    static int fault_69650_captured = 0;

    // EMIT MEMORY TRANSLATION TRACE EVENTS (before translation)
    // These trace the TCTI boundary: fault PC, Rn value, immediate, idx_mode
    trace_emit_gadget_ldr_fault_pc(fault_pc);

    static int ldst_fault_trace_budget = 24;
    int trace_ldst_fault = (ldst_fault_trace_budget > 0);
    uint32_t fault_raw_opcode = 0;
    a64_instr_t fault_decoded;
    int decode_ok = 0;
    if (trace_ldst_fault) {
        if (a64_fetch_insn(cpu, cpu->tlb, fault_pc, &fault_raw_opcode) == 0 &&
            a64_decode(fault_raw_opcode, &fault_decoded) == 0) {
            decode_ok = 1;
        }
    }

    uint64_t base = tcti_read_base_reg_or_sp(cpu, (int)rn);
    trace_emit_gadget_ldr_rn_value(base);
    trace_emit_gadget_ldr_imm_value((uint64_t)imm);
    trace_emit_gadget_ldr_idx_mode(idx_mode);

    uint64_t addr = base;
    uint64_t is_signed = meta & 0xff;
    uint64_t is_reg_offset = (meta >> 8) & 0xff;
    int rm = (meta >> 16) & 0xff;
    int extend_type = (meta >> 24) & 0xff;
    int reg_shift = (meta >> 32) & 0xff;
    int width;
    const char *decoded_mode = "index_offset";
    int writeback_enabled = 0;
    uint64_t writeback_value = base;
    int helper_entry_reached = 1;
    int fast_path_taken = 0;

    static int ldr69634_trace_budget = 24;
    int trace_ldr69634 = (is_load && fault_pc == 0x69634ULL && ldr69634_trace_budget > 0);

    static int str_helper_trace_budget = 0;
    int trace_str_helper = (!is_load && (fault_pc == 0x69650ULL || fault_pc == 0x6d1a4ULL) &&
                            str_helper_trace_budget < 8);
    uint32_t helper_raw_opcode = 0;
    int helper_mem_result = A64_MEM_FAULT;
    void *helper_host_ptr_probe = NULL;

    if (trace_str_helper) {
        str_helper_trace_budget++;
        (void)a64_fetch_insn(cpu, cpu->tlb, fault_pc, &helper_raw_opcode);
    }

    if (trace_ldr69634) {
        uint64_t rm_val_pre = is_reg_offset ? tcti_read_reg_or_zr(cpu, rm) : 0;
        uint64_t offset_before = is_reg_offset ? rm_val_pre : (uint64_t)imm;
        uint64_t offset_after = is_reg_offset
                                    ? (tcti_extend_ldst_offset(cpu, rm, extend_type) << reg_shift)
                                    : (uint64_t)imm;
        const char *mnemonic = get_ldst_mnemonic((int)is_load, (int)size, (int)is_signed);
        const char *idx_name = get_ldst_idx_mode_name(idx_mode);
        const char *extend_name = is_reg_offset ? get_ldst_extend_name(extend_type) : "none";
        char ev[1024];
        snprintf(
            ev, sizeof(ev),
            "ldr69634.helper_entry=attempt:%d,guest_pid:%d,guest_pc:0x%llx,raw_opcode:0x%08x,"
            "mnemonic:%s,idx_mode:%s,base_reg:%llu,base_val:0x%llx,off_reg:%d,off_val:0x%llx,"
            "extend:%s,shift:%d,offset_before:0x%llx,offset_after:0x%llx,guest_ea:0x%llx,"
            "host_ptr_probe:0x%llx,mem_result:%d,helper_entry_reached:%d,signal_exit_immediate:%d",
            -1, -1, (unsigned long long)fault_pc, fault_raw_opcode, mnemonic, idx_name,
            (unsigned long long)rn, (unsigned long long)base, is_reg_offset ? rm : -1,
            (unsigned long long)rm_val_pre, extend_name, reg_shift,
            (unsigned long long)offset_before, (unsigned long long)offset_after,
            (unsigned long long)(base + offset_after), 0ULL, -1, 1, 0);
        trace_record_event(TRACE_ORIGIN_EXEC, ev);
    }

    if (is_reg_offset) {
        decoded_mode = "register_offset";
        uint64_t offset = tcti_extend_ldst_offset(cpu, rm, extend_type);
        addr = base + (offset << reg_shift);

        // Trace register offset addressing details for fault analysis
        // Capture x2 and x3 values when they are used in the address computation
        if (rn == 3) {
            char x3_val_buf[24];
            snprintf(x3_val_buf, sizeof(x3_val_buf), "0x%llx", (unsigned long long)base);
            trace_attribute_t x3_attr[] = { { "x3_base", x3_val_buf } };
            trace_begin_interval(TRACE_ORIGIN_EXEC, "task.proof.ldst.x3_base", x3_attr, 1);
        }
        if (rm == 2) {
            uint64_t x2_raw = tcti_read_reg_or_zr(cpu, 2);
            char x2_val_buf[24];
            char offset_buf[24];
            char extend_buf[8];
            snprintf(x2_val_buf, sizeof(x2_val_buf), "0x%llx", (unsigned long long)x2_raw);
            snprintf(offset_buf, sizeof(offset_buf), "0x%llx",
                     (unsigned long long)(offset << reg_shift));
            snprintf(extend_buf, sizeof(extend_buf), "%d", extend_type);
            trace_attribute_t x2_attr[] = {
                { "x2_raw", x2_val_buf },
                { "offset_shifted", offset_buf },
                { "extend_type", extend_buf },
            };
            trace_begin_interval(TRACE_ORIGIN_EXEC, "task.proof.ldst.x2_offset", x2_attr, 3);
        }
    } else {
        switch (idx_mode) {
        case A64_PRE_INDEX:
            decoded_mode = "pre_index";
            base += imm;
            addr = base;
            break;
        case A64_POST_INDEX:
            decoded_mode = "post_index";
            addr = base;
            break;
        case A64_INDEX_OFFSET:
        default:
            decoded_mode = "index_offset";
            addr = base + imm;
            break;
        }
    }

    if (trace_ldr69634) {
        uint64_t rm_val_pre = is_reg_offset ? tcti_read_reg_or_zr(cpu, rm) : 0;
        uint64_t offset_before = is_reg_offset ? rm_val_pre : (uint64_t)imm;
        uint64_t offset_after = is_reg_offset
                                    ? (tcti_extend_ldst_offset(cpu, rm, extend_type) << reg_shift)
                                    : (uint64_t)imm;
        const char *mnemonic = get_ldst_mnemonic((int)is_load, (int)size, (int)is_signed);
        const char *idx_name = get_ldst_idx_mode_name(idx_mode);
        const char *extend_name = is_reg_offset ? get_ldst_extend_name(extend_type) : "none";
        char ev[1024];
        snprintf(
            ev, sizeof(ev),
            "ldr69634.pre_addr_regs=attempt:%d,guest_pid:%d,guest_pc:0x%llx,raw_opcode:0x%08x,"
            "mnemonic:%s,idx_mode:%s,base_reg:%llu,base_val:0x%llx,off_reg:%d,off_val:0x%llx,"
            "extend:%s,shift:%d,offset_before:0x%llx,offset_after:0x%llx,guest_ea:0x%llx,"
            "host_ptr_probe:0x%llx,mem_result:%d,helper_entry_reached:%d,signal_exit_immediate:%d",
            -1, -1, (unsigned long long)fault_pc, fault_raw_opcode, mnemonic, idx_name,
            (unsigned long long)rn, (unsigned long long)base, is_reg_offset ? rm : -1,
            (unsigned long long)rm_val_pre, extend_name, reg_shift,
            (unsigned long long)offset_before, (unsigned long long)offset_after,
            (unsigned long long)addr, 0ULL, -1, 1, 0);
        trace_record_event(TRACE_ORIGIN_EXEC, ev);

        snprintf(
            ev, sizeof(ev),
            "ldr69634.addr_calc=attempt:%d,guest_pid:%d,guest_pc:0x%llx,raw_opcode:0x%08x,"
            "mnemonic:%s,idx_mode:%s,base_reg:%llu,base_val:0x%llx,off_reg:%d,off_val:0x%llx,"
            "extend:%s,shift:%d,offset_before:0x%llx,offset_after:0x%llx,guest_ea:0x%llx,"
            "host_ptr_probe:0x%llx,mem_result:%d,helper_entry_reached:%d,signal_exit_immediate:%d",
            -1, -1, (unsigned long long)fault_pc, fault_raw_opcode, mnemonic, idx_name,
            (unsigned long long)rn, (unsigned long long)base, is_reg_offset ? rm : -1,
            (unsigned long long)rm_val_pre, extend_name, reg_shift,
            (unsigned long long)offset_before, (unsigned long long)offset_after,
            (unsigned long long)addr, 0ULL, -1, 1, 0);
        trace_record_event(TRACE_ORIGIN_EXEC, ev);
    }

    writeback_enabled =
        (!is_reg_offset && (idx_mode == A64_PRE_INDEX || idx_mode == A64_POST_INDEX)) ? 1 : 0;
    writeback_value =
        writeback_enabled ? ((idx_mode == A64_POST_INDEX) ? (base + imm) : base) : base;

    void *ldst_host_ptr_probe = NULL;
    if (trace_ldst_fault) {
        const char *mnemonic = get_ldst_mnemonic((int)is_load, (int)size, (int)is_signed);
        const char *idx_name = get_ldst_idx_mode_name(idx_mode);
        int decoded_rt = decode_ok ? fault_decoded.Rd : (int)rt;
        int decoded_rn = decode_ok ? fault_decoded.Rn : (int)rn;
        int decoded_rm = decode_ok ? fault_decoded.Rm : (is_reg_offset ? rm : -1);
        char ev[640];
        snprintf(ev, sizeof(ev),
                 "ldst.fault.entry=attempt:%d,guest_pid:%d,guest_pc:0x%llx,raw_opcode:0x%08x,"
                 "mnemonic:%s,decoded_category:%d,decoded_subtype:%d,is_load:%llu,rt:%d,rn:%d,"
                 "rm:%d,idx_mode:%s,imm:%lld,base_value:0x%llx,helper_entry_reached:%d,"
                 "fast_path_taken:%d",
                 -1, -1, (unsigned long long)fault_pc, fault_raw_opcode, mnemonic,
                 decode_ok ? (int)fault_decoded.cat : -1,
                 decode_ok ? (int)fault_decoded.subtype : -1, (unsigned long long)is_load,
                 decoded_rt, decoded_rn, decoded_rm, idx_name, (long long)imm,
                 (unsigned long long)base, helper_entry_reached, fast_path_taken);
        trace_record_event(TRACE_ORIGIN_EXEC, ev);

        trace_base6a628_helper_event("base6a628.fault_entry", cpu, fault_pc, fault_raw_opcode,
                                     mnemonic, &fault_decoded, base, addr, NULL,
                                     helper_entry_reached, 0);

        snprintf(ev, sizeof(ev),
                 "ldst.fault.decode=attempt:%d,guest_pid:%d,guest_pc:0x%llx,raw_opcode:0x%08x,"
                 "mnemonic:%s,decoded_category:%d,decoded_subtype:%d,is_load:%llu,rt:%d,rn:%d,"
                 "rm:%d,idx_mode:%s,imm:%lld,base_value:0x%llx,helper_entry_reached:%d,"
                 "fast_path_taken:%d",
                 -1, -1, (unsigned long long)fault_pc, fault_raw_opcode, mnemonic,
                 decode_ok ? (int)fault_decoded.cat : -1,
                 decode_ok ? (int)fault_decoded.subtype : -1, (unsigned long long)is_load,
                 decoded_rt, decoded_rn, decoded_rm, idx_name, (long long)imm,
                 (unsigned long long)base, helper_entry_reached, fast_path_taken);
        trace_record_event(TRACE_ORIGIN_EXEC, ev);

        snprintf(ev, sizeof(ev),
                 "ldst.fault.path=attempt:%d,guest_pid:%d,guest_pc:0x%llx,raw_opcode:0x%08x,"
                 "mnemonic:%s,decoded_category:%d,decoded_subtype:%d,is_load:%llu,rt:%d,rn:%d,"
                 "rm:%d,idx_mode:%s,imm:%lld,base_value:0x%llx,path:helper_slow,"
                 "helper_entry_reached:%d,fast_path_taken:%d",
                 -1, -1, (unsigned long long)fault_pc, fault_raw_opcode, mnemonic,
                 decode_ok ? (int)fault_decoded.cat : -1,
                 decode_ok ? (int)fault_decoded.subtype : -1, (unsigned long long)is_load,
                 decoded_rt, decoded_rn, decoded_rm, idx_name, (long long)imm,
                 (unsigned long long)base, helper_entry_reached, fast_path_taken);
        trace_record_event(TRACE_ORIGIN_EXEC, ev);
    }

    if (trace_ldr69634) {
        uint64_t rm_val_pre = is_reg_offset ? tcti_read_reg_or_zr(cpu, rm) : 0;
        uint64_t offset_before = is_reg_offset ? rm_val_pre : (uint64_t)imm;
        uint64_t offset_after = is_reg_offset
                                    ? (tcti_extend_ldst_offset(cpu, rm, extend_type) << reg_shift)
                                    : (uint64_t)imm;
        const char *mnemonic = get_ldst_mnemonic((int)is_load, (int)size, (int)is_signed);
        const char *idx_name = get_ldst_idx_mode_name(idx_mode);
        const char *extend_name = is_reg_offset ? get_ldst_extend_name(extend_type) : "none";
        char ev[1024];
        snprintf(
            ev, sizeof(ev),
            "ldr69634.translation=attempt:%d,guest_pid:%d,guest_pc:0x%llx,raw_opcode:0x%08x,"
            "mnemonic:%s,idx_mode:%s,base_reg:%llu,base_val:0x%llx,off_reg:%d,off_val:0x%llx,"
            "extend:%s,shift:%d,offset_before:0x%llx,offset_after:0x%llx,guest_ea:0x%llx,"
            "host_ptr_probe:0x%llx,mem_result:%d,helper_entry_reached:%d,signal_exit_immediate:%d",
            -1, -1, (unsigned long long)fault_pc, fault_raw_opcode, mnemonic, idx_name,
            (unsigned long long)rn, (unsigned long long)base, is_reg_offset ? rm : -1,
            (unsigned long long)rm_val_pre, extend_name, reg_shift,
            (unsigned long long)offset_before, (unsigned long long)offset_after,
            (unsigned long long)addr, (unsigned long long)(uintptr_t)ldst_host_ptr_probe, -1, 1, 0);
        trace_record_event(TRACE_ORIGIN_EXEC, ev);
    }

    if (trace_str_helper) {
        helper_host_ptr_probe = a64_guest_to_host(cpu, cpu->tlb, addr, 1);

        char ev[512];
        snprintf(
            ev, sizeof(ev),
            "str.helper.entry.raw_regs=attempt:%d,guest_pid:%d,guest_pc:0x%llx,raw_opcode:0x%08x,"
            "decoded_mode:%s,cpu_x2:0x%llx,carrier_x2:0x%llx,cpu_x7:0x%llx,carrier_x7:0x%llx,"
            "rt:%llu,rn:%llu,imm:%lld,writeback_enabled:%d",
            -1, -1, (unsigned long long)fault_pc, helper_raw_opcode, decoded_mode,
            (unsigned long long)cpu->x[2], (unsigned long long)cpu->x[2],
            (unsigned long long)cpu->x[7], (unsigned long long)cpu->x[7], (unsigned long long)rt,
            (unsigned long long)rn, (long long)imm, writeback_enabled);
        trace_record_event(TRACE_ORIGIN_EXEC, ev);

        snprintf(
            ev, sizeof(ev),
            "str.helper.entry.carriers=attempt:%d,guest_pid:%d,guest_pc:0x%llx,raw_opcode:0x%08x,"
            "decoded_mode:%s,carrier_x2:0x%llx,carrier_x7:0x%llx,access_addr:0x%llx,"
            "writeback_value:0x%llx,host_ptr_probe:0x%llx,writeback_enabled:%d",
            -1, -1, (unsigned long long)fault_pc, helper_raw_opcode, decoded_mode,
            (unsigned long long)cpu->x[2], (unsigned long long)cpu->x[7], (unsigned long long)addr,
            (unsigned long long)writeback_value,
            (unsigned long long)(uintptr_t)helper_host_ptr_probe, writeback_enabled);
        trace_record_event(TRACE_ORIGIN_EXEC, ev);

        snprintf(
            ev, sizeof(ev),
            "str.helper.pre_access=attempt:%d,guest_pid:%d,guest_pc:0x%llx,raw_opcode:0x%08x,"
            "decoded_mode:%s,cpu_x2:0x%llx,carrier_x2:0x%llx,cpu_x7:0x%llx,carrier_x7:0x%llx,"
            "access_addr:0x%llx,writeback_value:0x%llx,host_ptr_probe:0x%llx,writeback_enabled:%d",
            -1, -1, (unsigned long long)fault_pc, helper_raw_opcode, decoded_mode,
            (unsigned long long)cpu->x[2], (unsigned long long)cpu->x[2],
            (unsigned long long)cpu->x[7], (unsigned long long)cpu->x[7], (unsigned long long)addr,
            (unsigned long long)writeback_value,
            (unsigned long long)(uintptr_t)helper_host_ptr_probe, writeback_enabled);
        trace_record_event(TRACE_ORIGIN_EXEC, ev);
    }

    ldst_host_ptr_probe = a64_guest_to_host(cpu, cpu->tlb, addr, is_load ? 0 : 1);
    trace_base6a628_helper_event("base6a628.pre_helper", cpu, fault_pc, fault_raw_opcode,
                                 get_ldst_mnemonic((int)is_load, (int)size, (int)is_signed),
                                 &fault_decoded, base, addr, ldst_host_ptr_probe,
                                 helper_entry_reached, 0);
    trace_page0_translation_identity(cpu, addr, (int)is_load, fault_pc, fault_raw_opcode,
                                     ldst_host_ptr_probe);

    if (trace_ldst_fault) {
        const char *mnemonic = get_ldst_mnemonic((int)is_load, (int)size, (int)is_signed);
        const char *idx_name = get_ldst_idx_mode_name(idx_mode);
        int decoded_rt = decode_ok ? fault_decoded.Rd : (int)rt;
        int decoded_rn = decode_ok ? fault_decoded.Rn : (int)rn;
        int decoded_rm = decode_ok ? fault_decoded.Rm : (is_reg_offset ? rm : -1);
        char ev[640];
        snprintf(ev, sizeof(ev),
                 "ldst.fault.translation=attempt:%d,guest_pid:%d,guest_pc:0x%llx,raw_opcode:"
                 "0x%08x,mnemonic:%s,decoded_category:%d,decoded_subtype:%d,is_load:%llu,rt:%d,"
                 "rn:%d,rm:%d,idx_mode:%s,imm:%lld,base_value:0x%llx,guest_ea:0x%llx,"
                 "host_ptr_probe:0x%llx,helper_entry_reached:%d,fast_path_taken:%d",
                 -1, -1, (unsigned long long)fault_pc, fault_raw_opcode, mnemonic,
                 decode_ok ? (int)fault_decoded.cat : -1,
                 decode_ok ? (int)fault_decoded.subtype : -1, (unsigned long long)is_load,
                 decoded_rt, decoded_rn, decoded_rm, idx_name, (long long)imm,
                 (unsigned long long)base, (unsigned long long)addr,
                 (unsigned long long)(uintptr_t)ldst_host_ptr_probe, helper_entry_reached,
                 fast_path_taken);
        trace_record_event(TRACE_ORIGIN_EXEC, ev);
    }

    // EMIT GUEST VIRTUAL ADDRESS (after computing effective address)
    trace_emit_gadget_ldr_guest_vaddr(addr);

    if (fault_pc == 0x69650ULL && !fault_69650_captured) {
        fault_69650_captured = 1;

        void *host_ptr_probe = a64_guest_to_host(cpu, cpu->tlb, addr, is_load ? 0 : 1);
        struct mem *fault_mem = cpu->mmu ? container_of(cpu->mmu, struct mem, mmu) : NULL;
        struct page_desc *fault_desc =
            fault_mem ? page_map_lookup(&fault_mem->pages, PAGE(addr)) : NULL;
        unsigned fault_flags = fault_desc ? fault_desc->flags : 0;
        int writeback_expected =
            (!is_reg_offset && (idx_mode == A64_PRE_INDEX || idx_mode == A64_POST_INDEX)) ? 1 : 0;
        uint64_t writeback_val = base;
        if (writeback_expected) {
            writeback_val = (idx_mode == A64_POST_INDEX) ? (base + imm) : base;
        }

        char ev[384];
        snprintf(
            ev, sizeof(ev),
            "task.proof.69650.helper_regs=x0:0x%llx,x1:0x%llx,x2:0x%llx,x3:0x%llx,x4:0x%llx,"
            "x5:0x%llx,x6:0x%llx,x7:0x%llx,x8:0x%llx,sp:0x%llx,pc:0x%llx,rt:%llu,rn:%llu,rm:%d",
            (unsigned long long)cpu->x[0], (unsigned long long)cpu->x[1],
            (unsigned long long)cpu->x[2], (unsigned long long)cpu->x[3],
            (unsigned long long)cpu->x[4], (unsigned long long)cpu->x[5],
            (unsigned long long)cpu->x[6], (unsigned long long)cpu->x[7],
            (unsigned long long)cpu->x[8], (unsigned long long)cpu->sp, (unsigned long long)cpu->pc,
            (unsigned long long)rt, (unsigned long long)rn, is_reg_offset ? rm : -1);
        trace_record_event(TRACE_ORIGIN_EXEC, ev);

        snprintf(ev, sizeof(ev),
                 "task.proof.69650.helper_access=is_load:%llu,idx:%llu,imm:%lld,guest_ea:0x%llx,"
                 "host_ptr_probe:0x%llx,page_lookup:%s,page_flags:0x%x,writeback_expected:%d,"
                 "writeback_val:0x%llx",
                 (unsigned long long)is_load, (unsigned long long)idx_mode, (long long)imm,
                 (unsigned long long)addr, (unsigned long long)(uintptr_t)host_ptr_probe,
                 fault_desc ? "hit" : "miss", fault_flags, writeback_expected,
                 (unsigned long long)writeback_val);
        trace_record_event(TRACE_ORIGIN_EXEC, ev);
    }

    // ARCHITECTURAL CHECKPOINT: Pre-access state for fault analysis
    // Captures base (pre-writeback) and addr (effective address for access)
    // SAMPLE POINT 1: Before memory access, base unchanged, addr computed
    if (fault_pc == 0xf7fa4650ULL) {
        trace_ldst_arch_checkpoint("task.proof.ldst.arch_pre_access", fault_pc, base, addr, (int)rn,
                                   imm, (int)idx_mode, instance_id);
    }

    switch (size) {
    case A64_SIZE_B:
        width = 1;
        break;
    case A64_SIZE_H:
        width = 2;
        break;
    case A64_SIZE_W:
        width = 4;
        break;
    case A64_SIZE_X:
        width = 8;
        break;
    default:
        return TCTI_EXIT_FAULT;
    }

    if (is_load) {
        uint64_t value = 0;
        int mem_ret = A64_MEM_FAULT;

        trace_live_ldr_probe(cpu, instance_id, fault_pc, rt, rn, imm, size, idx_mode, meta, is_load,
                             base, addr, -1, 1);

        switch (width) {
        case 1: {
            uint8_t tmp;
            mem_ret = a64_guest_read8(cpu, cpu->tlb, addr, &tmp);
            value = is_signed ? (uint64_t)(int64_t)(int8_t)tmp : tmp;
            break;
        }
        case 2: {
            uint16_t tmp;
            mem_ret = a64_guest_read16(cpu, cpu->tlb, addr, &tmp);
            value = is_signed ? (uint64_t)(int64_t)(int16_t)tmp : tmp;
            break;
        }
        case 4: {
            uint32_t tmp;
            mem_ret = a64_guest_read32(cpu, cpu->tlb, addr, &tmp);
            value = is_signed ? (uint64_t)(int64_t)(int32_t)tmp : tmp;
            break;
        }
        case 8: {
            uint64_t tmp;
            mem_ret = a64_guest_read64(cpu, cpu->tlb, addr, &tmp);
            value = tmp;
            break;
        }
        default:
            mem_ret = A64_MEM_FAULT;
            break;
        }

        helper_mem_result = mem_ret;

        trace_live_ldr_probe(cpu, instance_id, fault_pc, rt, rn, imm, size, idx_mode, meta, is_load,
                             base, addr, mem_ret, 0);

        trace_6967c_ldst_probe(cpu, fault_pc, rt, rn, imm, size, idx_mode, meta, is_load, base,
                               addr, mem_ret);

        if (mem_ret != A64_MEM_OK) {
            if (trace_ldr69634) {
                uint64_t rm_val_pre = is_reg_offset ? tcti_read_reg_or_zr(cpu, rm) : 0;
                uint64_t offset_before = is_reg_offset ? rm_val_pre : (uint64_t)imm;
                uint64_t offset_after =
                    is_reg_offset ? (tcti_extend_ldst_offset(cpu, rm, extend_type) << reg_shift)
                                  : (uint64_t)imm;
                const char *mnemonic = get_ldst_mnemonic((int)is_load, (int)size, (int)is_signed);
                const char *idx_name = get_ldst_idx_mode_name(idx_mode);
                const char *extend_name =
                    is_reg_offset ? get_ldst_extend_name(extend_type) : "none";
                char ev[1024];
                snprintf(
                    ev, sizeof(ev),
                    "ldr69634.exit=attempt:%d,guest_pid:%d,guest_pc:0x%llx,raw_opcode:0x%08x,"
                    "mnemonic:%s,"
                    "idx_mode:%s,base_reg:%llu,base_val:0x%llx,off_reg:%d,off_val:0x%llx,extend:%s,"
                    "shift:%d,offset_before:0x%llx,offset_after:0x%llx,guest_ea:0x%llx,host_ptr_"
                    "probe:0x%llx,"
                    "mem_result:%d,helper_entry_reached:%d,signal_exit_immediate:%d",
                    -1, -1, (unsigned long long)fault_pc, fault_raw_opcode, mnemonic, idx_name,
                    (unsigned long long)rn, (unsigned long long)base, is_reg_offset ? rm : -1,
                    (unsigned long long)rm_val_pre, extend_name, reg_shift,
                    (unsigned long long)offset_before, (unsigned long long)offset_after,
                    (unsigned long long)addr, (unsigned long long)(uintptr_t)ldst_host_ptr_probe,
                    mem_ret, 1, 1);
                trace_record_event(TRACE_ORIGIN_EXEC, ev);
                ldr69634_trace_budget--;
            }
            if (trace_ldst_fault) {
                const char *mnemonic = get_ldst_mnemonic((int)is_load, (int)size, (int)is_signed);
                const char *idx_name = get_ldst_idx_mode_name(idx_mode);
                int decoded_rt = decode_ok ? fault_decoded.Rd : (int)rt;
                int decoded_rn = decode_ok ? fault_decoded.Rn : (int)rn;
                int decoded_rm = decode_ok ? fault_decoded.Rm : (is_reg_offset ? rm : -1);
                char ev[640];
                snprintf(ev, sizeof(ev),
                         "ldst.fault.exit=attempt:%d,guest_pid:%d,guest_pc:0x%llx,raw_opcode:"
                         "0x%08x,mnemonic:%s,decoded_category:%d,decoded_subtype:%d,is_load:%llu,"
                         "rt:%d,rn:%d,rm:%d,idx_mode:%s,imm:%lld,base_value:0x%llx,"
                         "guest_ea:0x%llx,host_ptr_probe:0x%llx,mem_result:%d,"
                         "helper_entry_reached:%d,fast_path_taken:%d,signal_exit_immediate:1",
                         -1, -1, (unsigned long long)fault_pc, fault_raw_opcode, mnemonic,
                         decode_ok ? (int)fault_decoded.cat : -1,
                         decode_ok ? (int)fault_decoded.subtype : -1, (unsigned long long)is_load,
                         decoded_rt, decoded_rn, decoded_rm, idx_name, (long long)imm,
                         (unsigned long long)base, (unsigned long long)addr,
                         (unsigned long long)(uintptr_t)ldst_host_ptr_probe, mem_ret,
                         helper_entry_reached, fast_path_taken);
                trace_record_event(TRACE_ORIGIN_EXEC, ev);
                ldst_fault_trace_budget--;
            }

            trace_base6a628_helper_event("base6a628.fault_exit", cpu, fault_pc, fault_raw_opcode,
                                         get_ldst_mnemonic((int)is_load, (int)size, (int)is_signed),
                                         &fault_decoded, base, addr, ldst_host_ptr_probe,
                                         helper_entry_reached, 1);

            if (!first_fault_captured) {
                first_fault_captured = 1;

                uint32_t raw_insn = 0;
                (void)a64_fetch_insn(cpu, cpu->tlb, fault_pc, &raw_insn);

                uint64_t offset_reg_val = is_reg_offset ? tcti_read_reg_or_zr(cpu, rm) : 0;
                uint64_t offset_before_add =
                    is_reg_offset ? tcti_extend_ldst_offset(cpu, rm, extend_type) : (uint64_t)imm;
                uint64_t computed_offset =
                    is_reg_offset ? (offset_before_add << reg_shift) : (uint64_t)imm;
                uint64_t guest_ea = addr;

                struct mem *mem = cpu->mmu ? container_of(cpu->mmu, struct mem, mmu) : NULL;
                page_t page = PAGE(guest_ea);
                struct page_desc *desc = mem ? page_map_lookup(&mem->pages, page) : NULL;
                uint64_t host_ptr = 0;
                unsigned desc_flags = 0;
                if (desc) {
                    host_ptr = (uint64_t)((char *)desc->obj->host_base + desc->offset +
                                          PGOFFSET(guest_ea));
                    desc_flags = desc->flags;
                }

                const char *insn_form = "other";
                if (is_reg_offset) {
                    insn_form = "register offset";
                } else {
                    if (idx_mode == A64_PRE_INDEX)
                        insn_form = "immediate pre-index";
                    else if (idx_mode == A64_POST_INDEX)
                        insn_form = "immediate post-index";
                    else
                        insn_form = "immediate unsigned offset";
                }

                const char *failure_reason = "other";
                if (!desc) {
                    failure_reason = "page_lookup_miss";
                } else if (host_ptr == 0) {
                    failure_reason = "translation_rejected";
                }

                char fault_pc_buf[24], raw_insn_buf[16], rn_buf[8], rm_buf[8], rt_buf[8];
                char base_buf[24], idx_val_buf[24], imm_buf[24], extend_buf[8], shift_flag_buf[8];
                char shift_amt_buf[8], off_before_add_buf[24], comp_off_buf[24], ea_buf[24];
                char trans_in_buf[24], host_ptr_buf[24], page_buf[24], desc_ptr_buf[24],
                    flags_buf[24];
                snprintf(fault_pc_buf, sizeof(fault_pc_buf), "0x%llx",
                         (unsigned long long)fault_pc);
                snprintf(raw_insn_buf, sizeof(raw_insn_buf), "0x%08x", raw_insn);
                snprintf(rn_buf, sizeof(rn_buf), "%d", (int)rn);
                snprintf(rm_buf, sizeof(rm_buf), "%d", is_reg_offset ? rm : -1);
                snprintf(rt_buf, sizeof(rt_buf), "%d", (int)rt);
                snprintf(base_buf, sizeof(base_buf), "0x%llx", (unsigned long long)base);
                snprintf(idx_val_buf, sizeof(idx_val_buf), "0x%llx",
                         (unsigned long long)offset_reg_val);
                snprintf(imm_buf, sizeof(imm_buf), "%lld", (long long)imm);
                snprintf(extend_buf, sizeof(extend_buf), "%d", is_reg_offset ? extend_type : -1);
                snprintf(shift_flag_buf, sizeof(shift_flag_buf), "%d", reg_shift ? 1 : 0);
                snprintf(shift_amt_buf, sizeof(shift_amt_buf), "%d", reg_shift);
                snprintf(off_before_add_buf, sizeof(off_before_add_buf), "0x%llx",
                         (unsigned long long)offset_before_add);
                snprintf(comp_off_buf, sizeof(comp_off_buf), "0x%llx",
                         (unsigned long long)computed_offset);
                snprintf(ea_buf, sizeof(ea_buf), "0x%llx", (unsigned long long)guest_ea);
                snprintf(trans_in_buf, sizeof(trans_in_buf), "0x%llx",
                         (unsigned long long)guest_ea);
                snprintf(host_ptr_buf, sizeof(host_ptr_buf), "0x%llx",
                         (unsigned long long)host_ptr);
                snprintf(page_buf, sizeof(page_buf), "0x%llx", (unsigned long long)page);
                snprintf(desc_ptr_buf, sizeof(desc_ptr_buf), "%p", (void *)desc);
                snprintf(flags_buf, sizeof(flags_buf), "0x%x", desc_flags);

                trace_attribute_t attrs[] = {
                    { "live_site", "a64_tcti_ldst_helper.pre_fault" },
                    { "fault_pc", fault_pc_buf },
                    { "raw_opcode", raw_insn_buf },
                    { "decoded_form", insn_form },
                    { "rt", rt_buf },
                    { "rn", rn_buf },
                    { "rn_val", base_buf },
                    { "rm", rm_buf },
                    { "rm_val", idx_val_buf },
                    { "imm", imm_buf },
                    { "extend_type", extend_buf },
                    { "shift_flag", shift_flag_buf },
                    { "shift_amt", shift_amt_buf },
                    { "offset_before_add", off_before_add_buf },
                    { "computed_offset", comp_off_buf },
                    { "guest_ea", ea_buf },
                    { "translation_in", trans_in_buf },
                    { "translation_out", host_ptr_buf },
                    { "page", page_buf },
                    { "page_desc", desc_ptr_buf },
                    { "page_flags", flags_buf },
                    { "lookup_result", desc ? "hit" : "miss" },
                    { "fault_reason", failure_reason },
                };
                (void)trace_begin_interval(TRACE_ORIGIN_EXEC, "a64.ldr.first_fault_analysis", attrs,
                                           sizeof(attrs) / sizeof(attrs[0]));

                // Emit key-value first-fault proof as visible semantic events (once)
                char ev[192];
                snprintf(ev, sizeof(ev), "a64.ldr.first.live_site=a64_tcti_ldst_helper.pre_fault");
                trace_record_event(TRACE_ORIGIN_EXEC, ev);
                snprintf(ev, sizeof(ev), "a64.ldr.first.fault_pc=0x%llx",
                         (unsigned long long)fault_pc);
                trace_record_event(TRACE_ORIGIN_EXEC, ev);
                snprintf(ev, sizeof(ev), "a64.ldr.first.raw_opcode=0x%08x", raw_insn);
                trace_record_event(TRACE_ORIGIN_EXEC, ev);
                snprintf(ev, sizeof(ev), "a64.ldr.first.decoded_form=%s", insn_form);
                trace_record_event(TRACE_ORIGIN_EXEC, ev);
                snprintf(ev, sizeof(ev), "a64.ldr.first.rt=%d", (int)rt);
                trace_record_event(TRACE_ORIGIN_EXEC, ev);
                snprintf(ev, sizeof(ev), "a64.ldr.first.rn=%d", (int)rn);
                trace_record_event(TRACE_ORIGIN_EXEC, ev);
                snprintf(ev, sizeof(ev), "a64.ldr.first.rn_val=0x%llx", (unsigned long long)base);
                trace_record_event(TRACE_ORIGIN_EXEC, ev);
                snprintf(ev, sizeof(ev), "a64.ldr.first.rm=%d", is_reg_offset ? rm : -1);
                trace_record_event(TRACE_ORIGIN_EXEC, ev);
                snprintf(ev, sizeof(ev), "a64.ldr.first.rm_val=0x%llx",
                         (unsigned long long)offset_reg_val);
                trace_record_event(TRACE_ORIGIN_EXEC, ev);
                snprintf(ev, sizeof(ev), "a64.ldr.first.extend_type=%d",
                         is_reg_offset ? extend_type : -1);
                trace_record_event(TRACE_ORIGIN_EXEC, ev);
                snprintf(ev, sizeof(ev), "a64.ldr.first.shift_amt=%d", reg_shift);
                trace_record_event(TRACE_ORIGIN_EXEC, ev);
                snprintf(ev, sizeof(ev), "a64.ldr.first.offset_before_add=0x%llx",
                         (unsigned long long)offset_before_add);
                trace_record_event(TRACE_ORIGIN_EXEC, ev);
                snprintf(ev, sizeof(ev), "a64.ldr.first.computed_offset=0x%llx",
                         (unsigned long long)computed_offset);
                trace_record_event(TRACE_ORIGIN_EXEC, ev);
                snprintf(ev, sizeof(ev), "a64.ldr.first.guest_ea=0x%llx",
                         (unsigned long long)guest_ea);
                trace_record_event(TRACE_ORIGIN_EXEC, ev);
                snprintf(ev, sizeof(ev), "a64.ldr.first.translation_out=0x%llx",
                         (unsigned long long)host_ptr);
                trace_record_event(TRACE_ORIGIN_EXEC, ev);
                snprintf(ev, sizeof(ev), "a64.ldr.first.page=0x%llx", (unsigned long long)page);
                trace_record_event(TRACE_ORIGIN_EXEC, ev);
                snprintf(ev, sizeof(ev), "a64.ldr.first.lookup_result=%s", desc ? "hit" : "miss");
                trace_record_event(TRACE_ORIGIN_EXEC, ev);
                snprintf(ev, sizeof(ev), "a64.ldr.first.fault_reason=%s", failure_reason);
                trace_record_event(TRACE_ORIGIN_EXEC, ev);
            }

            cpu->pc = fault_pc;
            cpu->fault_was_write = false;
            return TCTI_EXIT_FAULT;
        }

        // EMIT HOST POINTER TRACE (after successful translation)
        // Note: Actual host pointer is internal to TLB; using addr as correlation ID
        trace_emit_gadget_ldr_host_ptr(addr);

        tcti_write_reg_or_zr(cpu, (int)rt, value, size == A64_SIZE_X);

        // PHASE 1: X2 Provenance Tracking - Capture first write to X2
        if (rt == 2 && is_load) {
            // Fetch raw instruction word at fault PC
            uint32_t raw_insn = 0;
            a64_fetch_insn(cpu, cpu->tlb, fault_pc, &raw_insn);

            // Get old X2 value before this write
            uint64_t old_x2 = cpu->x[2];

            // Get mnemonic
            const char *mnemonic = get_ldst_mnemonic(1, (int)size, (int)is_signed);

            // Emit detailed provenance checkpoint
            trace_x2_provenance_checkpoint(fault_pc, raw_insn, old_x2, value, mnemonic, (int)rn, rm,
                                           base, imm, (int)idx_mode, 1);

            // Also emit standard X2 write checkpoint
            trace_reg_write_checkpoint("task.proof.x2.write", 2, old_x2, value);
        }
    } else {
        uint64_t value = tcti_read_reg_or_zr(cpu, (int)rt);
        int mem_ret = A64_MEM_FAULT;

        trace_live_ldr_probe(cpu, instance_id, fault_pc, rt, rn, imm, size, idx_mode, meta, is_load,
                             base, addr, -1, 1);

        switch (width) {
        case 1:
            mem_ret = a64_guest_write8(cpu, cpu->tlb, addr, (uint8_t)value);
            break;
        case 2:
            mem_ret = a64_guest_write16(cpu, cpu->tlb, addr, (uint16_t)value);
            break;
        case 4:
            mem_ret = a64_guest_write32(cpu, cpu->tlb, addr, (uint32_t)value);
            break;
        case 8:
            mem_ret = a64_tcti_guest_write64_strict(cpu, cpu->tlb, addr, value);
            break;
        default:
            mem_ret = A64_MEM_FAULT;
            break;
        }

        helper_mem_result = mem_ret;

        trace_live_ldr_probe(cpu, instance_id, fault_pc, rt, rn, imm, size, idx_mode, meta, is_load,
                             base, addr, mem_ret, 0);

        trace_6967c_ldst_probe(cpu, fault_pc, rt, rn, imm, size, idx_mode, meta, is_load, base,
                               addr, mem_ret);

        // SAMPLE POINT 2: After memory access, check if fault occurred
        if (mem_ret != A64_MEM_OK) {
            if (trace_ldr69634) {
                uint64_t rm_val_pre = is_reg_offset ? tcti_read_reg_or_zr(cpu, rm) : 0;
                uint64_t offset_before = is_reg_offset ? rm_val_pre : (uint64_t)imm;
                uint64_t offset_after =
                    is_reg_offset ? (tcti_extend_ldst_offset(cpu, rm, extend_type) << reg_shift)
                                  : (uint64_t)imm;
                const char *mnemonic = get_ldst_mnemonic((int)is_load, (int)size, (int)is_signed);
                const char *idx_name = get_ldst_idx_mode_name(idx_mode);
                const char *extend_name =
                    is_reg_offset ? get_ldst_extend_name(extend_type) : "none";
                char ev[1024];
                snprintf(
                    ev, sizeof(ev),
                    "ldr69634.exit=attempt:%d,guest_pid:%d,guest_pc:0x%llx,raw_opcode:0x%08x,"
                    "mnemonic:%s,"
                    "idx_mode:%s,base_reg:%llu,base_val:0x%llx,off_reg:%d,off_val:0x%llx,extend:%s,"
                    "shift:%d,offset_before:0x%llx,offset_after:0x%llx,guest_ea:0x%llx,host_ptr_"
                    "probe:0x%llx,"
                    "mem_result:%d,helper_entry_reached:%d,signal_exit_immediate:%d",
                    -1, -1, (unsigned long long)fault_pc, fault_raw_opcode, mnemonic, idx_name,
                    (unsigned long long)rn, (unsigned long long)base, is_reg_offset ? rm : -1,
                    (unsigned long long)rm_val_pre, extend_name, reg_shift,
                    (unsigned long long)offset_before, (unsigned long long)offset_after,
                    (unsigned long long)addr, (unsigned long long)(uintptr_t)ldst_host_ptr_probe,
                    mem_ret, 1, 1);
                trace_record_event(TRACE_ORIGIN_EXEC, ev);
                ldr69634_trace_budget--;
            }
            if (trace_ldst_fault) {
                const char *mnemonic = get_ldst_mnemonic((int)is_load, (int)size, (int)is_signed);
                const char *idx_name = get_ldst_idx_mode_name(idx_mode);
                int decoded_rt = decode_ok ? fault_decoded.Rd : (int)rt;
                int decoded_rn = decode_ok ? fault_decoded.Rn : (int)rn;
                int decoded_rm = decode_ok ? fault_decoded.Rm : (is_reg_offset ? rm : -1);
                char ev[640];
                snprintf(ev, sizeof(ev),
                         "ldst.fault.exit=attempt:%d,guest_pid:%d,guest_pc:0x%llx,raw_opcode:"
                         "0x%08x,mnemonic:%s,decoded_category:%d,decoded_subtype:%d,is_load:%llu,"
                         "rt:%d,rn:%d,rm:%d,idx_mode:%s,imm:%lld,base_value:0x%llx,"
                         "guest_ea:0x%llx,host_ptr_probe:0x%llx,mem_result:%d,"
                         "helper_entry_reached:%d,fast_path_taken:%d,signal_exit_immediate:1",
                         -1, -1, (unsigned long long)fault_pc, fault_raw_opcode, mnemonic,
                         decode_ok ? (int)fault_decoded.cat : -1,
                         decode_ok ? (int)fault_decoded.subtype : -1, (unsigned long long)is_load,
                         decoded_rt, decoded_rn, decoded_rm, idx_name, (long long)imm,
                         (unsigned long long)base, (unsigned long long)addr,
                         (unsigned long long)(uintptr_t)ldst_host_ptr_probe, mem_ret,
                         helper_entry_reached, fast_path_taken);
                trace_record_event(TRACE_ORIGIN_EXEC, ev);
                ldst_fault_trace_budget--;
            }

            cpu->pc = fault_pc;
            cpu->fault_was_write = true;
            return TCTI_EXIT_FAULT;
        }
    }

    if (trace_str_helper) {
        char ev[512];
        snprintf(
            ev, sizeof(ev),
            "str.helper.access_result=attempt:%d,guest_pid:%d,guest_pc:0x%llx,raw_opcode:0x%08x,"
            "decoded_mode:%s,cpu_x2:0x%llx,carrier_x2:0x%llx,cpu_x7:0x%llx,carrier_x7:0x%llx,"
            "access_addr:0x%llx,writeback_value:0x%llx,host_ptr_probe:0x%llx,mem_result:%d,"
            "writeback_enabled:%d",
            -1, -1, (unsigned long long)fault_pc, helper_raw_opcode, decoded_mode,
            (unsigned long long)cpu->x[2], (unsigned long long)cpu->x[2],
            (unsigned long long)cpu->x[7], (unsigned long long)cpu->x[7], (unsigned long long)addr,
            (unsigned long long)writeback_value,
            (unsigned long long)(uintptr_t)helper_host_ptr_probe, helper_mem_result,
            writeback_enabled);
        trace_record_event(TRACE_ORIGIN_EXEC, ev);
    }

    if (!is_reg_offset && (idx_mode == A64_PRE_INDEX || idx_mode == A64_POST_INDEX)) {
        uint64_t updated = (idx_mode == A64_POST_INDEX) ? (base + imm) : base;

        if (trace_str_helper) {
            char ev[512];
            snprintf(
                ev, sizeof(ev),
                "str.helper.pre_writeback=attempt:%d,guest_pid:%d,guest_pc:0x%llx,raw_opcode:0x%"
                "08x,"
                "decoded_mode:%s,cpu_x2:0x%llx,carrier_x2:0x%llx,cpu_x7:0x%llx,carrier_x7:0x%llx,"
                "access_addr:0x%llx,writeback_value:0x%llx,host_ptr_probe:0x%llx,mem_result:%d,"
                "writeback_enabled:%d",
                -1, -1, (unsigned long long)fault_pc, helper_raw_opcode, decoded_mode,
                (unsigned long long)cpu->x[2], (unsigned long long)cpu->x[2],
                (unsigned long long)cpu->x[7], (unsigned long long)cpu->x[7],
                (unsigned long long)addr, (unsigned long long)updated,
                (unsigned long long)(uintptr_t)helper_host_ptr_probe, helper_mem_result,
                writeback_enabled);
            trace_record_event(TRACE_ORIGIN_EXEC, ev);
        }

        if (rn == 2 && idx_mode == A64_POST_INDEX) {
            static int wb_probe_budget = 32;
            if (wb_probe_budget > 0) {
                char ev[224];
                snprintf(ev, sizeof(ev),
                         "task.proof.ldst.writeback.rn2.post=pc:0x%llx,base:0x%llx,imm:%lld,"
                         "updated:0x%llx",
                         (unsigned long long)fault_pc, (unsigned long long)base, (long long)imm,
                         (unsigned long long)updated);
                trace_record_event(TRACE_ORIGIN_EXEC, ev);
                wb_probe_budget--;
            }
        }

        // PHASE 1: Track X2 writeback (pre/post-index addressing)
        if (rn == 2) {
            uint64_t old_x2 = cpu->x[2];

            // Fetch raw instruction word at fault PC
            uint32_t raw_insn = 0;
            a64_fetch_insn(cpu, cpu->tlb, fault_pc, &raw_insn);

            // Classify as derived arithmetic (base + offset calculation)
            const char *mnemonic = (idx_mode == A64_PRE_INDEX) ? "ldr_pre_idx" : "ldr_post_idx";

            trace_x2_provenance_checkpoint(fault_pc, raw_insn, old_x2, updated, mnemonic, (int)rn,
                                           -1, base, imm, (int)idx_mode, (int)is_load);
            trace_reg_write_checkpoint("task.proof.x2.write", 2, old_x2, updated);
        }

        tcti_write_base_reg_or_sp(cpu, (int)rn, updated, true);

        if (trace_str_helper) {
            uint64_t post_wb = tcti_read_base_reg_or_sp(cpu, (int)rn);
            char ev[512];
            snprintf(
                ev, sizeof(ev),
                "str.helper.post_writeback=attempt:%d,guest_pid:%d,guest_pc:0x%llx,raw_opcode:0x%"
                "08x,"
                "decoded_mode:%s,cpu_x2:0x%llx,carrier_x2:0x%llx,cpu_x7:0x%llx,carrier_x7:0x%llx,"
                "access_addr:0x%llx,writeback_value:0x%llx,host_ptr_probe:0x%llx,mem_result:%d,"
                "writeback_enabled:%d",
                -1, -1, (unsigned long long)fault_pc, helper_raw_opcode, decoded_mode,
                (unsigned long long)cpu->x[2], (unsigned long long)post_wb,
                (unsigned long long)cpu->x[7], (unsigned long long)cpu->x[7],
                (unsigned long long)addr, (unsigned long long)post_wb,
                (unsigned long long)(uintptr_t)helper_host_ptr_probe, helper_mem_result,
                writeback_enabled);
            trace_record_event(TRACE_ORIGIN_EXEC, ev);
        }

        if (rn == 2 && idx_mode == A64_POST_INDEX) {
            static int wb_commit_budget = 32;
            if (wb_commit_budget > 0) {
                uint64_t committed = tcti_read_base_reg_or_sp(cpu, 2);
                char ev[224];
                snprintf(ev, sizeof(ev),
                         "task.proof.ldst.writeback.rn2.committed=pc:0x%llx,val:0x%llx",
                         (unsigned long long)fault_pc, (unsigned long long)committed);
                trace_record_event(TRACE_ORIGIN_EXEC, ev);
                wb_commit_budget--;
            }
        }

        // ARCHITECTURAL CHECKPOINT: Post-writeback state
        // SAMPLE POINT 3: After writeback, base modified
        if (fault_pc == 0xf7fa4650ULL) {
            trace_ldst_arch_checkpoint("task.proof.ldst.arch_post_writeback", fault_pc, updated,
                                       addr, (int)rn, imm, (int)idx_mode, instance_id);
        }
    }

    if (trace_str_helper) {
        char ev[512];
        snprintf(
            ev, sizeof(ev),
            "str.helper.exit.raw_regs=attempt:%d,guest_pid:%d,guest_pc:0x%llx,raw_opcode:0x%08x,"
            "decoded_mode:%s,cpu_x2:0x%llx,carrier_x2:0x%llx,cpu_x7:0x%llx,carrier_x7:0x%llx,"
            "access_addr:0x%llx,writeback_value:0x%llx,host_ptr_probe:0x%llx,mem_result:%d,"
            "writeback_enabled:%d",
            -1, -1, (unsigned long long)fault_pc, helper_raw_opcode, decoded_mode,
            (unsigned long long)cpu->x[2], (unsigned long long)cpu->x[2],
            (unsigned long long)cpu->x[7], (unsigned long long)cpu->x[7], (unsigned long long)addr,
            (unsigned long long)writeback_value,
            (unsigned long long)(uintptr_t)helper_host_ptr_probe, helper_mem_result,
            writeback_enabled);
        trace_record_event(TRACE_ORIGIN_EXEC, ev);

        snprintf(
            ev, sizeof(ev),
            "str.helper.exit.cpu_regs=attempt:%d,guest_pid:%d,guest_pc:0x%llx,raw_opcode:0x%08x,"
            "decoded_mode:%s,cpu_x2:0x%llx,carrier_x2:0x%llx,cpu_x7:0x%llx,carrier_x7:0x%llx,"
            "access_addr:0x%llx,writeback_value:0x%llx,host_ptr_probe:0x%llx,mem_result:%d,"
            "writeback_enabled:%d",
            -1, -1, (unsigned long long)fault_pc, helper_raw_opcode, decoded_mode,
            (unsigned long long)cpu->x[2], (unsigned long long)cpu->x[2],
            (unsigned long long)cpu->x[7], (unsigned long long)cpu->x[7], (unsigned long long)addr,
            (unsigned long long)writeback_value,
            (unsigned long long)(uintptr_t)helper_host_ptr_probe, helper_mem_result,
            writeback_enabled);
        trace_record_event(TRACE_ORIGIN_EXEC, ev);
    }

    return 0;
}

int a64_tcti_ldr_x_helper(struct cpu_state *cpu, uint64_t fault_pc, uint64_t rt, uint64_t rn,
                          int64_t imm, uint64_t size, uint64_t idx_mode, uint64_t meta)
{
    return a64_tcti_ldst_helper(cpu, fault_pc, rt, rn, imm, size, idx_mode, meta, 1);
}

int a64_tcti_str_x_helper(struct cpu_state *cpu, uint64_t fault_pc, uint64_t rt, uint64_t rn,
                          int64_t imm, uint64_t size, uint64_t idx_mode, uint64_t meta)
{
    return a64_tcti_ldst_helper(cpu, fault_pc, rt, rn, imm, size, idx_mode, meta, 0);
}

// Assembly-visible wrappers with underscore prefix (used by gadget bl instructions)
int _a64_tcti_ldr_x_helper(struct cpu_state *cpu, uint64_t fault_pc, uint64_t rt, uint64_t rn,
                           int64_t imm, uint64_t size, uint64_t idx_mode, uint64_t meta)
{
    static int ldr_helper_reach_budget = 24;
    if (ldr_helper_reach_budget > 0) {
        char ev[224];
        snprintf(
            ev, sizeof(ev),
            "ldst.fault.helper_reach=kind:ldr,guest_pc:0x%llx,rt:%llu,rn:%llu,idx:%llu,imm:%lld",
            (unsigned long long)fault_pc, (unsigned long long)rt, (unsigned long long)rn,
            (unsigned long long)idx_mode, (long long)imm);
        trace_record_event(TRACE_ORIGIN_EXEC, ev);
        ldr_helper_reach_budget--;
    }
    return a64_tcti_ldr_x_helper(cpu, fault_pc, rt, rn, imm, size, idx_mode, meta);
}

int _a64_tcti_str_x_helper(struct cpu_state *cpu, uint64_t fault_pc, uint64_t rt, uint64_t rn,
                           int64_t imm, uint64_t size, uint64_t idx_mode, uint64_t meta)
{
    static int str_helper_reach_budget = 24;
    if (str_helper_reach_budget > 0) {
        char ev[224];
        snprintf(
            ev, sizeof(ev),
            "ldst.fault.helper_reach=kind:str,guest_pc:0x%llx,rt:%llu,rn:%llu,idx:%llu,imm:%lld",
            (unsigned long long)fault_pc, (unsigned long long)rt, (unsigned long long)rn,
            (unsigned long long)idx_mode, (long long)imm);
        trace_record_event(TRACE_ORIGIN_EXEC, ev);
        str_helper_reach_budget--;
    }
    return a64_tcti_str_x_helper(cpu, fault_pc, rt, rn, imm, size, idx_mode, meta);
}

// ============================================================================
// Exit Gadget - Marks end of gadget stream
// ============================================================================

__attribute__((naked)) void gadget_exit_impl(void)
{
    asm volatile("mov x0, #0\n\t"         // Exit reason = normal
                 "b _tcti_exit_block\n\t" // Jump to exit handler
    );
}

tcti_gadget_t gadget_exit = gadget_exit_impl;

// ============================================================================
// Memory-backed register load/store gadgets
//
// Each gadget:
//   - Loads/stores one guest register to/from temp x14
//   - Advances to next gadget via x28 bytecode pointer
//   - Branches to next gadget
//
// Index mapping: 0=x16, 1=x17, ..., 14=x30 (15 entries)
// ============================================================================

// Load guest x[16 + idx] into host temp x14
#define GEN_LOAD_XREG(idx)                                                                         \
    __attribute__((naked)) void gadget_load_x##idx##_impl(void)                                    \
    {                                                                                              \
        asm volatile("ldr x14, [x29, %[off]]\n\t"                                                  \
                     "ldr x27, [x28], #8\n\t"                                                      \
                     "br x27\n\t"                                                                  \
                     :                                                                             \
                     : [off] "i"(XREG_OFFSET(16 + idx)));                                          \
    }

// Store host temp x14 to guest x[16 + idx]
#define GEN_STORE_XREG(idx)                                                                        \
    __attribute__((naked)) void gadget_store_x##idx##_impl(void)                                   \
    {                                                                                              \
        asm volatile("str x14, [x29, %[off]]\n\t"                                                  \
                     "ldr x27, [x28], #8\n\t"                                                      \
                     "br x27\n\t"                                                                  \
                     :                                                                             \
                     : [off] "i"(XREG_OFFSET(16 + idx)));                                          \
    }

// Generate load gadgets for x16-x30 (indices 0-14)
GEN_LOAD_XREG(0)  // x16
GEN_LOAD_XREG(1)  // x17
GEN_LOAD_XREG(2)  // x13
GEN_LOAD_XREG(3)  // x19
GEN_LOAD_XREG(4)  // x20
GEN_LOAD_XREG(5)  // x21
GEN_LOAD_XREG(6)  // x22
GEN_LOAD_XREG(7)  // x23
GEN_LOAD_XREG(8)  // x24
GEN_LOAD_XREG(9)  // x25
GEN_LOAD_XREG(10) // x26
GEN_LOAD_XREG(11) // x27
GEN_LOAD_XREG(12) // x28
GEN_LOAD_XREG(13) // x29
GEN_LOAD_XREG(14) // x30

// Generate store gadgets for x16-x30 (indices 0-14)
GEN_STORE_XREG(0)  // x16
GEN_STORE_XREG(1)  // x17
GEN_STORE_XREG(2)  // x13
GEN_STORE_XREG(3)  // x19
GEN_STORE_XREG(4)  // x20
GEN_STORE_XREG(5)  // x21
GEN_STORE_XREG(6)  // x22
GEN_STORE_XREG(7)  // x23
GEN_STORE_XREG(8)  // x24
GEN_STORE_XREG(9)  // x25
GEN_STORE_XREG(10) // x26
GEN_STORE_XREG(11) // x27
GEN_STORE_XREG(12) // x28
GEN_STORE_XREG(13) // x29
GEN_STORE_XREG(14) // x30

// x30 (LR) load/store uses the same x14 temp contract as the table gadgets
__attribute__((naked)) void gadget_load_x30_impl(void)
{
    asm volatile("ldr x14, [x29, %[off]]\n\t"
                 "ldr x27, [x28], #8\n\t"
                 "br x27\n\t"
                 :
                 : [off] "i"(XREG_OFFSET(30)));
}

__attribute__((naked)) void gadget_store_x30_impl(void)
{
    asm volatile("str x14, [x29, %[off]]\n\t"
                 "ldr x27, [x28], #8\n\t"
                 "br x27\n\t"
                 :
                 : [off] "i"(XREG_OFFSET(30)));
}

// SP load/store - uses x14 as temp to match gen.c temp conventions
// These are the actual implementations matching header declarations
__attribute__((naked)) void gadget_load_sp(void)
{
    asm volatile("ldr x14, [x29, %[off]]\n\t"
                 "ldr x27, [x28], #8\n\t"
                 "br x27\n\t"
                 :
                 : [off] "i"(SP_OFFSET));
}

__attribute__((naked)) void gadget_store_sp(void)
{
    asm volatile("str x14, [x29, %[off]]\n\t"
                 "ldr x27, [x28], #8\n\t"
                 "br x27\n\t"
                 :
                 : [off] "i"(SP_OFFSET));
}

// ============================================================================
// Function pointer tables for load/store
//
// These are indexed by (guest_reg - 16) for x16-x30
// ============================================================================

// Load table: index 0=x16, 14=x30
const tcti_gadget_t gadget_load_xreg_16_to_30[15] = {
    gadget_load_x0_impl,  // x16
    gadget_load_x1_impl,  // x17
    gadget_load_x2_impl,  // x13
    gadget_load_x3_impl,  // x19
    gadget_load_x4_impl,  // x20
    gadget_load_x5_impl,  // x21
    gadget_load_x6_impl,  // x22
    gadget_load_x7_impl,  // x23
    gadget_load_x8_impl,  // x24
    gadget_load_x9_impl,  // x25
    gadget_load_x10_impl, // x26
    gadget_load_x11_impl, // x27
    gadget_load_x12_impl, // x28
    gadget_load_x13_impl, // x29
    gadget_load_x14_impl, // x30
};

// Store table: index 0=x16, 14=x30
const tcti_gadget_t gadget_store_xreg_16_to_30[15] = {
    gadget_store_x0_impl,  // x16
    gadget_store_x1_impl,  // x17
    gadget_store_x2_impl,  // x13
    gadget_store_x3_impl,  // x19
    gadget_store_x4_impl,  // x20
    gadget_store_x5_impl,  // x21
    gadget_store_x6_impl,  // x22
    gadget_store_x7_impl,  // x23
    gadget_store_x8_impl,  // x24
    gadget_store_x9_impl,  // x25
    gadget_store_x10_impl, // x26
    gadget_store_x11_impl, // x27
    gadget_store_x12_impl, // x28
    gadget_store_x13_impl, // x29
    gadget_store_x14_impl, // x30
};

// SP accessors - these match the header declarations as function prototypes
// The actual implementations are gadget_load_sp_impl and gadget_store_sp_impl
// which are naked functions, not variables

// ============================================================================
// TCTI Load/Store Gadgets for memory access
// ============================================================================
//
// These gadgets perform actual memory loads/stores through the TLB.
// They use x16 as the address temp and x17 as the data temp.

// Load 64-bit value from memory[Rn + offset] into x17
// Address calculation: x29 (cpu) + x[n]*8 + 16 (x[0] offset)
__attribute__((naked)) void gadget_tcti_ldr_64(void)
{
    asm volatile(
        // x0 contains the base address (from Rn gadget)
        // Load from [x0] into x17
        "ldr x17, [x0]\n\t"
        // Move to destination register (handled by caller)
        // For now, keep in x17 and chain
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t");
}

// Store 64-bit value from x17 to memory[Rn + offset]
__attribute__((naked)) void gadget_tcti_str_64(void)
{
    asm volatile(
        // x0 contains the base address
        // Store x17 to [x0]
        "str x17, [x0]\n\t"
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t");
}

// Compute address: x29 + offset + (Rn * 8) for guest register access
// This loads the guest register value from cpu_state
__attribute__((naked)) void gadget_tcti_compute_reg_addr(void)
{
    asm volatile(
        // Rn index is in bytecode (next 8 bytes)
        "ldr x16, [x28], #8\n\t" // Load Rn index
        // Compute: x29 + 16 + (x16 * 8)
        "lsl x16, x16, #3\n\t"  // x16 = Rn * 8
        "add x16, x16, #16\n\t" // x16 = Rn*8 + 16
        "add x0, x29, x16\n\t"  // x0 = cpu + offset = &x[Rn]
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t");
}

// Compute address: x29 + offset for immediate offset
__attribute__((naked)) void gadget_tcti_compute_imm_addr(void)
{
    asm volatile(
        // Offset is in bytecode (next 8 bytes)
        "ldr x16, [x28], #8\n\t" // Load offset
        "add x0, x29, x16\n\t"   // x0 = cpu + offset
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t");
}

tcti_gadget_t gadget_tcti_ldr = gadget_tcti_ldr_64;
tcti_gadget_t gadget_tcti_str = gadget_tcti_str_64;
tcti_gadget_t gadget_tcti_compute_reg_addr_fn = gadget_tcti_compute_reg_addr;
tcti_gadget_t gadget_tcti_compute_imm_addr_fn = gadget_tcti_compute_imm_addr;

// ============================================================================
// Helper functions for C code to access memory-backed registers
// ============================================================================

uint64_t a64_read_xreg(struct cpu_state *cpu, int reg)
{
    if (reg >= 0 && reg < 31)
        return cpu->x[reg];
    return 0;
}

void a64_write_xreg(struct cpu_state *cpu, int reg, uint64_t val)
{
    if (reg >= 0 && reg < 31)
        cpu->x[reg] = val;
}

uint64_t a64_read_sp(struct cpu_state *cpu)
{
    return cpu->sp;
}

void a64_write_sp(struct cpu_state *cpu, uint64_t val)
{
    cpu->sp = val;
}

/* ============================================================================
 * Complex Gadgets (moved from separate file to be included in build)
 * ============================================================================ */

__attribute__((naked)) void gadget_b_impl(void)
{
    asm volatile("ldr x0, [x28], #8\n\t"
                 "str x0, [x29, #272]\n\t"
                 "mov x0, #0\n\t"
                 "b _tcti_exit_block\n\t");
}

tcti_gadget_t gadget_b = gadget_b_impl;

#define GEN_BCOND(name, cond)                                                                      \
    __attribute__((naked)) void gadget_bcond_##name##_impl(void)                                   \
    {                                                                                              \
        asm volatile("ldr x16, [x28], #8\n\t"                                                      \
                     "ldr x17, [x28], #8\n\t"                                                      \
                     "ldr x15, [x29, #280]\n\t"                                                    \
                     "msr nzcv, x15\n\t"                                                           \
                     "b." #cond " 1f\n\t"                                                          \
                     "mov x16, x17\n\t"                                                            \
                     "1:\n\t"                                                                      \
                     "str x16, [x29, %[pc_off]]\n\t"                                               \
                     "mov x0, #0\n\t"                                                              \
                     "b _tcti_exit_block\n\t"                                                      \
                     :                                                                             \
                     : [pc_off] "i"(PC_OFFSET)                                                     \
                     : "x15");                                                                     \
    }

GEN_BCOND(eq, eq);

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

struct tcti_bcond_ne_probe tcti_bcond_ne_probe = { 0 };

// B.NE gadget
__attribute__((naked)) void gadget_bcond_ne_impl(void)
{
    asm volatile(
        "ldr x16, [x28], #8\n\t" // Load target PC
        "ldr x17, [x28], #8\n\t" // Load fallthrough PC
        "adrp x26, _tcti_bcond_ne_probe@PAGE\n\t"
        "add x26, x26, _tcti_bcond_ne_probe@PAGEOFF\n\t"
        "sub x27, x17, #4\n\t"
        "str x27, [x26, %[branch_site_off]]\n\t"
        "str x16, [x26, %[target_off]]\n\t"
        "str x17, [x26, %[fallthrough_off]]\n\t"
        "ldr x15, [x29, #280]\n\t" // Load NZCV from cpu->pstate
        "str x15, [x26, %[x15_off]]\n\t"
        "msr nzcv, x15\n\t" // Restore NZCV
        "mrs x25, nzcv\n\t"
        "str x25, [x26, %[x25_off]]\n\t"
        "and x14, x25, #0x20000000\n\t"
        "str x14, [x26, %[x14_and_off]]\n\t"
        "str x14, [x26, %[x14_and_mirror_off]]\n\t"
        "cbz x14, 1f\n\t" // Branch when equality bit is clear (NE)
        "movz x27, #0x1111\n\t"
        "movk x27, #0x1111, lsl #16\n\t"
        "movk x27, #0x1111, lsl #32\n\t"
        "movk x27, #0x1111, lsl #48\n\t"
        "str x27, [x26, %[path_marker_off]]\n\t"
        "str x17, [x26, %[path_pc_off]]\n\t"
        "mov x27, #0\n\t"
        "str x27, [x26, %[branch_result_off]]\n\t"
        "mov x16, x17\n\t" // Use fallthrough PC (equal case)
        "b 2f\n\t"
        "1:\n\t"
        "movz x27, #0x2222\n\t"
        "movk x27, #0x2222, lsl #16\n\t"
        "movk x27, #0x2222, lsl #32\n\t"
        "movk x27, #0x2222, lsl #48\n\t"
        "str x27, [x26, %[path_marker_off]]\n\t"
        "str x16, [x26, %[path_pc_off]]\n\t"
        "mov x27, #1\n\t"
        "str x27, [x26, %[branch_result_off]]\n\t"
        "2:\n\t"
        "mov w25, #1\n\t"
        "strb w25, [x26, %[captured_off]]\n\t"
        "str x16, [x29, %[pc_off]]\n\t" // Store to cpu->pc
        "mov x0, #0\n\t"
        "b _tcti_exit_block\n\t"
        :
        : [pc_off] "i"(PC_OFFSET),
          [branch_site_off] "i"(offsetof(struct tcti_bcond_ne_probe, branch_site_pc)),
          [target_off] "i"(offsetof(struct tcti_bcond_ne_probe, target_pc)),
          [fallthrough_off] "i"(offsetof(struct tcti_bcond_ne_probe, fallthrough_pc)),
          [x15_off] "i"(offsetof(struct tcti_bcond_ne_probe, x15_loaded)),
          [x25_off] "i"(offsetof(struct tcti_bcond_ne_probe, x25_after_mrs)),
          [x14_and_off] "i"(offsetof(struct tcti_bcond_ne_probe, x14_after_and)),
          [x14_and_mirror_off] "i"(offsetof(struct tcti_bcond_ne_probe, x14_after_and_mirror)),
          [path_marker_off] "i"(offsetof(struct tcti_bcond_ne_probe, path_marker)),
          [path_pc_off] "i"(offsetof(struct tcti_bcond_ne_probe, path_pc)),
          [branch_result_off] "i"(offsetof(struct tcti_bcond_ne_probe, branch_path_result)),
          [captured_off] "i"(offsetof(struct tcti_bcond_ne_probe, captured))
        : "x14", "x15", "x16", "x17", "x25", "x26", "x27");
}

GEN_BCOND(cs, cs);
GEN_BCOND(cc, cc);
GEN_BCOND(mi, mi);
GEN_BCOND(pl, pl);
GEN_BCOND(vs, vs);
GEN_BCOND(vc, vc);
GEN_BCOND(hi, hi);
GEN_BCOND(ls, ls);
GEN_BCOND(ge, ge);
GEN_BCOND(lt, lt);
GEN_BCOND(gt, gt);
GEN_BCOND(le, le);
GEN_BCOND(al, al);
GEN_BCOND(nv, al);

const tcti_gadget_t gadget_bcond[16] = {
    gadget_bcond_eq_impl, gadget_bcond_ne_impl, gadget_bcond_cs_impl, gadget_bcond_cc_impl,
    gadget_bcond_mi_impl, gadget_bcond_pl_impl, gadget_bcond_vs_impl, gadget_bcond_vc_impl,
    gadget_bcond_hi_impl, gadget_bcond_ls_impl, gadget_bcond_ge_impl, gadget_bcond_lt_impl,
    gadget_bcond_gt_impl, gadget_bcond_le_impl, gadget_bcond_al_impl, gadget_bcond_nv_impl,
};

__attribute__((naked)) void gadget_br_impl(void)
{
    asm volatile("ldr x0, [x28], #8\n\t"
                 "ldr x26, [x28], #8\n\t"
                 "ldr x17, [x28], #8\n\t"
                 "cmp x0, #31\n\t"
                 "b.eq 1f\n\t"
                 "add x27, x29, #16\n\t"
                 "lsl x19, x0, #3\n\t"
                 "ldr x0, [x27, x19]\n\t"
                 "b 2f\n\t"
                 "1:\n\t"
                 "ldr x0, [x29, #264]\n\t"
                 "2:\n\t"
                 "cmp x26, #0\n\t"
                 "b.eq 3f\n\t"
                 "str x17, [x29, #256]\n\t"
                 "3:\n\t"
                 "str x0, [x29, #272]\n\t"
                 "mov x0, #0\n\t"
                 "b _tcti_exit_block\n\t");
}

tcti_gadget_t gadget_br = gadget_br_impl;

#define GEN_CBZ_TABLE(kind, mnemonic, hostreg, idx)                                                \
    __attribute__((naked)) void gadget_##kind##_##idx##_impl(void)                                 \
    {                                                                                              \
        asm volatile("ldr x16, [x28], #8\n\t"                                                      \
                     "ldr x17, [x28], #8\n\t" mnemonic " x" #hostreg ", 1f\n\t"                    \
                     "mov x16, x17\n\t"                                                            \
                     "1:\n\t"                                                                      \
                     "str x16, [x29, %[pc_off]]\n\t"                                               \
                     "mov x0, #0\n\t"                                                              \
                     "b _tcti_exit_block\n\t"                                                      \
                     :                                                                             \
                     : [pc_off] "i"(PC_OFFSET));                                                   \
    }

GEN_CBZ_TABLE(cbz_reg, "cbz", 1, 0);
GEN_CBZ_TABLE(cbz_reg, "cbz", 2, 1);
GEN_CBZ_TABLE(cbz_reg, "cbz", 3, 2);
GEN_CBZ_TABLE(cbz_reg, "cbz", 4, 3);
GEN_CBZ_TABLE(cbz_reg, "cbz", 5, 4);
GEN_CBZ_TABLE(cbz_reg, "cbz", 6, 5);
GEN_CBZ_TABLE(cbz_reg, "cbz", 7, 6);
GEN_CBZ_TABLE(cbz_reg, "cbz", 8, 7);
GEN_CBZ_TABLE(cbz_reg, "cbz", 9, 8);
GEN_CBZ_TABLE(cbz_reg, "cbz", 10, 9);
GEN_CBZ_TABLE(cbz_reg, "cbz", 11, 10);
GEN_CBZ_TABLE(cbz_reg, "cbz", 12, 11);
GEN_CBZ_TABLE(cbz_reg, "cbz", 13, 12);
GEN_CBZ_TABLE(cbz_reg, "cbz", 14, 13);
GEN_CBZ_TABLE(cbz_reg, "cbz", 15, 14);
GEN_CBZ_TABLE(cbz_reg, "cbz", 16, 15);

GEN_CBZ_TABLE(cbnz_reg, "cbnz", 1, 0);
GEN_CBZ_TABLE(cbnz_reg, "cbnz", 2, 1);
GEN_CBZ_TABLE(cbnz_reg, "cbnz", 3, 2);
GEN_CBZ_TABLE(cbnz_reg, "cbnz", 4, 3);
GEN_CBZ_TABLE(cbnz_reg, "cbnz", 5, 4);
GEN_CBZ_TABLE(cbnz_reg, "cbnz", 6, 5);
GEN_CBZ_TABLE(cbnz_reg, "cbnz", 7, 6);
GEN_CBZ_TABLE(cbnz_reg, "cbnz", 8, 7);
GEN_CBZ_TABLE(cbnz_reg, "cbnz", 9, 8);
GEN_CBZ_TABLE(cbnz_reg, "cbnz", 10, 9);
GEN_CBZ_TABLE(cbnz_reg, "cbnz", 11, 10);
GEN_CBZ_TABLE(cbnz_reg, "cbnz", 12, 11);
GEN_CBZ_TABLE(cbnz_reg, "cbnz", 13, 12);
GEN_CBZ_TABLE(cbnz_reg, "cbnz", 14, 13);
GEN_CBZ_TABLE(cbnz_reg, "cbnz", 15, 14);
GEN_CBZ_TABLE(cbnz_reg, "cbnz", 16, 15);

const tcti_gadget_t gadget_cbz_reg[16] = {
    gadget_cbz_reg_0_impl,  gadget_cbz_reg_1_impl,  gadget_cbz_reg_2_impl,  gadget_cbz_reg_3_impl,
    gadget_cbz_reg_4_impl,  gadget_cbz_reg_5_impl,  gadget_cbz_reg_6_impl,  gadget_cbz_reg_7_impl,
    gadget_cbz_reg_8_impl,  gadget_cbz_reg_9_impl,  gadget_cbz_reg_10_impl, gadget_cbz_reg_11_impl,
    gadget_cbz_reg_12_impl, gadget_cbz_reg_13_impl, gadget_cbz_reg_14_impl, gadget_cbz_reg_15_impl,
};

const tcti_gadget_t gadget_cbnz_reg[16] = {
    gadget_cbnz_reg_0_impl,  gadget_cbnz_reg_1_impl,  gadget_cbnz_reg_2_impl,
    gadget_cbnz_reg_3_impl,  gadget_cbnz_reg_4_impl,  gadget_cbnz_reg_5_impl,
    gadget_cbnz_reg_6_impl,  gadget_cbnz_reg_7_impl,  gadget_cbnz_reg_8_impl,
    gadget_cbnz_reg_9_impl,  gadget_cbnz_reg_10_impl, gadget_cbnz_reg_11_impl,
    gadget_cbnz_reg_12_impl, gadget_cbnz_reg_13_impl, gadget_cbnz_reg_14_impl,
    gadget_cbnz_reg_15_impl,
};

#define GEN_TBZ_TABLE(kind, mnemonic, hostreg, idx)                                                \
    __attribute__((naked)) void gadget_##kind##_##idx##_impl(void)                                 \
    {                                                                                              \
        asm volatile("ldr x17, [x28], #8\n\t"                                                      \
                     "ldr x19, [x28], #8\n\t"                                                      \
                     "ldr x26, [x28], #8\n\t"                                                      \
                     "lsr x27, x" #hostreg ", x17\n\t"                                             \
                     "and x27, x27, #1\n\t" mnemonic " x27, 1f\n\t"                                \
                     "mov x19, x26\n\t"                                                            \
                     "1:\n\t"                                                                      \
                     "str x19, [x29, %[pc_off]]\n\t"                                               \
                     "mov x0, #0\n\t"                                                              \
                     "b _tcti_exit_block\n\t"                                                      \
                     :                                                                             \
                     : [pc_off] "i"(PC_OFFSET));                                                   \
    }

GEN_TBZ_TABLE(tbz_reg, "cbz", 1, 0);
GEN_TBZ_TABLE(tbz_reg, "cbz", 2, 1);
GEN_TBZ_TABLE(tbz_reg, "cbz", 3, 2);
GEN_TBZ_TABLE(tbz_reg, "cbz", 4, 3);
GEN_TBZ_TABLE(tbz_reg, "cbz", 5, 4);
GEN_TBZ_TABLE(tbz_reg, "cbz", 6, 5);
GEN_TBZ_TABLE(tbz_reg, "cbz", 7, 6);
GEN_TBZ_TABLE(tbz_reg, "cbz", 8, 7);
GEN_TBZ_TABLE(tbz_reg, "cbz", 9, 8);
GEN_TBZ_TABLE(tbz_reg, "cbz", 10, 9);
GEN_TBZ_TABLE(tbz_reg, "cbz", 11, 10);
GEN_TBZ_TABLE(tbz_reg, "cbz", 12, 11);
GEN_TBZ_TABLE(tbz_reg, "cbz", 13, 12);
GEN_TBZ_TABLE(tbz_reg, "cbz", 14, 13);
GEN_TBZ_TABLE(tbz_reg, "cbz", 15, 14);
GEN_TBZ_TABLE(tbz_reg, "cbz", 16, 15);

GEN_TBZ_TABLE(tbnz_reg, "cbnz", 1, 0);
GEN_TBZ_TABLE(tbnz_reg, "cbnz", 2, 1);
GEN_TBZ_TABLE(tbnz_reg, "cbnz", 3, 2);
GEN_TBZ_TABLE(tbnz_reg, "cbnz", 4, 3);
GEN_TBZ_TABLE(tbnz_reg, "cbnz", 5, 4);
GEN_TBZ_TABLE(tbnz_reg, "cbnz", 6, 5);
GEN_TBZ_TABLE(tbnz_reg, "cbnz", 7, 6);
GEN_TBZ_TABLE(tbnz_reg, "cbnz", 8, 7);
GEN_TBZ_TABLE(tbnz_reg, "cbnz", 9, 8);
GEN_TBZ_TABLE(tbnz_reg, "cbnz", 10, 9);
GEN_TBZ_TABLE(tbnz_reg, "cbnz", 11, 10);
GEN_TBZ_TABLE(tbnz_reg, "cbnz", 12, 11);
GEN_TBZ_TABLE(tbnz_reg, "cbnz", 13, 12);
GEN_TBZ_TABLE(tbnz_reg, "cbnz", 14, 13);
GEN_TBZ_TABLE(tbnz_reg, "cbnz", 15, 14);
GEN_TBZ_TABLE(tbnz_reg, "cbnz", 16, 15);

const tcti_gadget_t gadget_tbz_reg[16] = {
    gadget_tbz_reg_0_impl,  gadget_tbz_reg_1_impl,  gadget_tbz_reg_2_impl,  gadget_tbz_reg_3_impl,
    gadget_tbz_reg_4_impl,  gadget_tbz_reg_5_impl,  gadget_tbz_reg_6_impl,  gadget_tbz_reg_7_impl,
    gadget_tbz_reg_8_impl,  gadget_tbz_reg_9_impl,  gadget_tbz_reg_10_impl, gadget_tbz_reg_11_impl,
    gadget_tbz_reg_12_impl, gadget_tbz_reg_13_impl, gadget_tbz_reg_14_impl, gadget_tbz_reg_15_impl,
};

const tcti_gadget_t gadget_tbnz_reg[16] = {
    gadget_tbnz_reg_0_impl,  gadget_tbnz_reg_1_impl,  gadget_tbnz_reg_2_impl,
    gadget_tbnz_reg_3_impl,  gadget_tbnz_reg_4_impl,  gadget_tbnz_reg_5_impl,
    gadget_tbnz_reg_6_impl,  gadget_tbnz_reg_7_impl,  gadget_tbnz_reg_8_impl,
    gadget_tbnz_reg_9_impl,  gadget_tbnz_reg_10_impl, gadget_tbnz_reg_11_impl,
    gadget_tbnz_reg_12_impl, gadget_tbnz_reg_13_impl, gadget_tbnz_reg_14_impl,
    gadget_tbnz_reg_15_impl,
};

__attribute__((naked)) void gadget_sbfm_impl(void)
{
    asm volatile(
        // Save guest registers (x0-x15 in guest = x1-x16 in host) to cpu_state
        "stp x1, x2, [x29, #16]\n\t"
        "stp x3, x4, [x29, #32]\n\t"
        "stp x5, x6, [x29, #48]\n\t"
        "stp x7, x8, [x29, #64]\n\t"
        "stp x9, x10, [x29, #80]\n\t"
        "stp x11, x12, [x29, #96]\n\t"
        "stp x13, x14, [x29, #112]\n\t"
        "stp x15, x16, [x29, #128]\n\t"
        // Load parameters (x19-x24 are preserved by ABI, safe to use)
        "ldr x19, [x28], #8\n\t" // fault_pc
        "ldr x20, [x28], #8\n\t" // rd
        "ldr x21, [x28], #8\n\t" // rn
        "ldr x22, [x28], #8\n\t" // immr
        "ldr x23, [x28], #8\n\t" // imms
        "ldr x24, [x28], #8\n\t" // is_64bit
        // Load source operand (using x17 as temp, preserved by TCTI entry)
        "cmp x21, #31\n\t"
        "b.eq 1f\n\t"
        "add x17, x29, #16\n\t"
        "add x17, x17, x21, lsl #3\n\t"
        "ldr x17, [x17]\n\t"
        "b 2f\n\t"
        "1:\n\t"
        "ldr x17, [x29, #264]\n\t" // SP
        "2:\n\t"
        // Compute datasize_mask (x25)
        "mov x0, #0xffffffff\n\t"
        "mov x25, #-1\n\t"
        "cmp x24, #0\n\t"
        "csel x25, x0, x25, eq\n\t"
        "and x17, x17, x25\n\t" // src &= datasize_mask
        // Compute datasize (x26)
        "cmp x24, #0\n\t"
        "mov x0, #32\n\t"
        "mov x26, #64\n\t"
        "csel x26, x0, x26, eq\n\t"
        // ROR(src, immr) - result in x17
        // Use x19 as temp since params are already loaded
        "sub x19, x26, x22\n\t" // x19 = datasize - immr (shift amount)
        "lsr x0, x17, x22\n\t"  // x0 = src >> immr
        "lsl x19, x17, x19\n\t" // x19 = src << (datasize - immr)
        "orr x17, x19, x0\n\t"  // x17 = ROR result
        "and x17, x17, x25\n\t" // mask to datasize
        // Compute width = (imms - immr + 1) mod datasize
        "cmp x23, x22\n\t"
        "b.hs 3f\n\t"
        "add x0, x23, #1\n\t"
        "add x0, x0, x26\n\t"
        "sub x0, x0, x22\n\t"
        "b 4f\n\t"
        "3:\n\t"
        "sub x0, x23, x22\n\t"
        "add x0, x0, #1\n\t"
        "4:\n\t"
        // Compute wmask = (1 << width) - 1, or all 1s if width == datasize
        "cmp x0, x26\n\t"
        "b.eq 5f\n\t"
        "mov x25, #1\n\t"
        "lsl x25, x25, x0\n\t"
        "sub x25, x25, #1\n\t"
        "b 6f\n\t"
        "5:\n\t"
        "mov x25, #-1\n\t"
        "6:\n\t"
        "and x17, x17, x25\n\t" // result = ROR & wmask
        // Sign extend: if result[width-1] == 1, set bits [datasize-1:width]
        "sub x0, x0, #1\n\t" // x0 = width - 1 (MSB position)
        "mov x26, #1\n\t"
        "lsl x26, x26, x0\n\t"  // x26 = 1 << (width-1)
        "tst x17, x26\n\t"      // Test MSB
        "b.eq 9f\n\t"           // MSB = 0, no sign extension
        "sub x26, x26, #1\n\t"  // x26 = (1 << (width-1)) - 1 = lower mask
        "mvn x26, x26\n\t"      // x26 = ~lower mask = upper bits set
        "orr x17, x17, x26\n\t" // Set upper bits
        "9:\n\t"
        // Store result
        "cmp x20, #31\n\t"
        "b.eq 10f\n\t"
        "add x0, x29, #16\n\t"
        "add x0, x0, x20, lsl #3\n\t"
        "str x17, [x0]\n\t"
        "b 11f\n\t"
        "10:\n\t"
        "str x17, [x29, #264]\n\t" // xzr
        "11:\n\t"
        // Restore guest registers
        "ldp x1, x2, [x29, #16]\n\t"
        "ldp x3, x4, [x29, #32]\n\t"
        "ldp x5, x6, [x29, #48]\n\t"
        "ldp x7, x8, [x29, #64]\n\t"
        "ldp x9, x10, [x29, #80]\n\t"
        "ldp x11, x12, [x29, #96]\n\t"
        "ldp x13, x14, [x29, #112]\n\t"
        "ldp x15, x16, [x29, #128]\n\t"
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t");
}

tcti_gadget_t gadget_sbfm = gadget_sbfm_impl;

__attribute__((naked)) void gadget_bfm_impl(void)
{
    asm volatile("ldr x19, [x28], #8\n\t" // fault_pc
                 "ldr x20, [x28], #8\n\t" // rd
                 "ldr x21, [x28], #8\n\t" // rn
                 "ldr x22, [x28], #8\n\t" // immr
                 "ldr x23, [x28], #8\n\t" // imms
                 "ldr x24, [x28], #8\n\t" // is_64bit
                 "stp x1, x2, [x29, #16]\n\t"
                 "stp x3, x4, [x29, #32]\n\t"
                 "stp x5, x6, [x29, #48]\n\t"
                 "stp x7, x8, [x29, #64]\n\t"
                 "stp x9, x10, [x29, #80]\n\t"
                 "stp x11, x12, [x29, #96]\n\t"
                 "stp x13, x14, [x29, #112]\n\t"
                 "stp x15, x16, [x29, #128]\n\t"
                 "cmp x21, #31\n\t"
                 "b.eq 1f\n\t"
                 "add x0, x29, #16\n\t"
                 "add x0, x0, x21, lsl #3\n\t"
                 "ldr x0, [x0]\n\t"
                 "b 2f\n\t"
                 "1:\n\t"
                 "ldr x0, [x29, #264]\n\t"
                 "2:\n\t"
                 "cmp x20, #31\n\t"
                 "b.eq 12f\n\t"
                 "add x1, x29, #16\n\t"
                 "add x1, x1, x20, lsl #3\n\t"
                 "ldr x1, [x1]\n\t"
                 "b 13f\n\t"
                 "12:\n\t"
                 "ldr x1, [x29, #264]\n\t"
                 "13:\n\t"
                 "mov x2, #0xffffffff\n\t"
                 "mov x3, #-1\n\t"
                 "cmp x24, #0\n\t"
                 "csel x3, x2, x3, eq\n\t"
                 "cmp x24, #0\n\t"
                 "mov x2, #32\n\t"
                 "mov x4, #64\n\t"
                 "csel x2, x2, x4, eq\n\t"
                 "and x0, x0, x3\n\t"
                 "and x1, x1, x3\n\t"
                 "sub x4, x2, x22\n\t"
                 "sub x5, x2, #1\n\t"
                 "and x4, x4, x5\n\t"
                 "lsr x6, x0, x22\n\t"
                 "lsl x7, x0, x4\n\t"
                 "orr x6, x6, x7\n\t"
                 "and x6, x6, x3\n\t"
                 "cmp x23, x22\n\t"
                 "b.hs 3f\n\t"
                 "add x9, x23, #1\n\t"
                 "add x9, x9, x2\n\t"
                 "sub x9, x9, x22\n\t"
                 "b 4f\n\t"
                 "3:\n\t"
                 "sub x9, x23, x22\n\t"
                 "add x9, x9, #1\n\t"
                 "4:\n\t"
                 "cmp x9, x2\n\t"
                 "b.eq 5f\n\t"
                 "mov x10, #1\n\t"
                 "lsl x10, x10, x9\n\t"
                 "sub x10, x10, #1\n\t"
                 "b 6f\n\t"
                 "5:\n\t"
                 "mov x10, #-1\n\t"
                 "6:\n\t"
                 "and x10, x10, x3\n\t"
                 "and x11, x6, x10\n\t"
                 "mvn x12, x10\n\t"
                 "and x1, x1, x12\n\t"
                 "orr x11, x11, x1\n\t"
                 "and x11, x11, x3\n\t"
                 "cmp x20, #31\n\t"
                 "b.eq 10f\n\t"
                 "add x12, x29, #16\n\t"
                 "add x12, x12, x20, lsl #3\n\t"
                 "str x11, [x12]\n\t"
                 "b 11f\n\t"
                 "10:\n\t"
                 "str x11, [x29, #264]\n\t"
                 "11:\n\t"
                 "ldp x1, x2, [x29, #16]\n\t"
                 "ldp x3, x4, [x29, #32]\n\t"
                 "ldp x5, x6, [x29, #48]\n\t"
                 "ldp x7, x8, [x29, #64]\n\t"
                 "ldp x9, x10, [x29, #80]\n\t"
                 "ldp x11, x12, [x29, #96]\n\t"
                 "ldp x13, x14, [x29, #112]\n\t"
                 "ldp x15, x16, [x29, #128]\n\t"
                 "ldr x27, [x28], #8\n\t"
                 "br x27\n\t");
}

tcti_gadget_t gadget_bfm = gadget_bfm_impl;

__attribute__((naked)) void gadget_ubfm_impl(void)
{
    asm volatile("ldr x19, [x28], #8\n\t" // fault_pc
                 "ldr x20, [x28], #8\n\t" // rd
                 "ldr x21, [x28], #8\n\t" // rn
                 "ldr x22, [x28], #8\n\t" // immr
                 "ldr x23, [x28], #8\n\t" // imms
                 "ldr x24, [x28], #8\n\t" // is_64bit
                 "stp x1, x2, [x29, #16]\n\t"
                 "stp x3, x4, [x29, #32]\n\t"
                 "stp x5, x6, [x29, #48]\n\t"
                 "stp x7, x8, [x29, #64]\n\t"
                 "stp x9, x10, [x29, #80]\n\t"
                 "stp x11, x12, [x29, #96]\n\t"
                 "stp x13, x14, [x29, #112]\n\t"
                 "stp x15, x16, [x29, #128]\n\t"
                 "cmp x21, #31\n\t"
                 "b.eq 1f\n\t"
                 "add x0, x29, #16\n\t"
                 "add x0, x0, x21, lsl #3\n\t"
                 "ldr x0, [x0]\n\t"
                 "b 2f\n\t"
                 "1:\n\t"
                 "ldr x0, [x29, #264]\n\t"
                 "2:\n\t"
                 "mov x2, #0xffffffff\n\t"
                 "mov x3, #-1\n\t"
                 "cmp x24, #0\n\t"
                 "csel x3, x2, x3, eq\n\t"
                 "cmp x24, #0\n\t"
                 "mov x2, #32\n\t"
                 "mov x4, #64\n\t"
                 "csel x2, x2, x4, eq\n\t"
                 "and x0, x0, x3\n\t"
                 "sub x4, x2, x22\n\t"
                 "sub x5, x2, #1\n\t"
                 "and x4, x4, x5\n\t"
                 "lsr x6, x0, x22\n\t"
                 "lsl x7, x0, x4\n\t"
                 "orr x6, x6, x7\n\t"
                 "and x6, x6, x3\n\t"
                 "cmp x23, x22\n\t"
                 "b.hs 3f\n\t"
                 "add x9, x23, #1\n\t"
                 "add x9, x9, x2\n\t"
                 "sub x9, x9, x22\n\t"
                 "b 4f\n\t"
                 "3:\n\t"
                 "sub x9, x23, x22\n\t"
                 "add x9, x9, #1\n\t"
                 "4:\n\t"
                 "cmp x9, x2\n\t"
                 "b.eq 5f\n\t"
                 "mov x10, #1\n\t"
                 "lsl x10, x10, x9\n\t"
                 "sub x10, x10, #1\n\t"
                 "b 6f\n\t"
                 "5:\n\t"
                 "mov x10, #-1\n\t"
                 "6:\n\t"
                 "and x10, x10, x3\n\t"
                 "and x11, x6, x10\n\t"
                 "cmp x20, #31\n\t"
                 "b.eq 10f\n\t"
                 "add x12, x29, #16\n\t"
                 "add x12, x12, x20, lsl #3\n\t"
                 "str x11, [x12]\n\t"
                 "b 11f\n\t"
                 "10:\n\t"
                 "str x11, [x29, #264]\n\t"
                 "11:\n\t"
                 "ldp x1, x2, [x29, #16]\n\t"
                 "ldp x3, x4, [x29, #32]\n\t"
                 "ldp x5, x6, [x29, #48]\n\t"
                 "ldp x7, x8, [x29, #64]\n\t"
                 "ldp x9, x10, [x29, #80]\n\t"
                 "ldp x11, x12, [x29, #96]\n\t"
                 "ldp x13, x14, [x29, #112]\n\t"
                 "ldp x15, x16, [x29, #128]\n\t"
                 "ldr x27, [x28], #8\n\t"
                 "br x27\n\t");
}

tcti_gadget_t gadget_ubfm = gadget_ubfm_impl;

__attribute__((naked)) void gadget_ldr_x_impl(void)
{
    asm volatile(
        // =========================================================================
        // GADGET ENTRY TRACING - Spill-First Boundary Instrumentation
        // =========================================================================
        // Capture exact values at gadget entry for forensic analysis of:
        //   1. x28 (bytecode pointer) - shows where we are in the gadget stream
        //   2. Fault address calculation - shows which address we're about to access
        //
        // At this point:
        //   x28 = bytecode pointer (already advanced past this gadget's entry)
        //   x29 = cpu_state pointer
        //   x27 = previous gadget's next pointer (not meaningful here)
        //
        // Parameters (loaded AFTER trace calls to avoid register corruption):
        //   x19 = fault_pc (the guest PC that caused this load)
        //   x20 = Rt (destination register)
        //   x21 = Rn (base register)
        //   x22 = immediate offset
        //   x23 = size (0=B, 1=H, 2=W, 3=X)
        //   x24 = idx_mode (0=offset, 1=pre-index, 2=post-index)
        //   x25 = meta (extension type, etc.)
        // =========================================================================

        // Trace gadget entry with x28 (bytecode pointer) - BEFORE loading parameters
        // since C trace functions may clobber x19-x25
        "stp x0, x1, [sp, #-16]!\n\t"
        "stp x2, x3, [sp, #-16]!\n\t"
        "stp x4, x5, [sp, #-16]!\n\t"
        "stp x6, x7, [sp, #-16]!\n\t"
        "stp x8, x9, [sp, #-16]!\n\t"
        "stp x10, x11, [sp, #-16]!\n\t"
        "stp x12, x13, [sp, #-16]!\n\t"
        "stp x14, x15, [sp, #-16]!\n\t"
        "stp x16, x17, [sp, #-16]!\n\t"
        "mov x0, x28\n\t" // x28 to emit
        "bl _trace_emit_gadget_entry_x28\n\t"
        "ldp x16, x17, [sp], #16\n\t"
        "ldp x14, x15, [sp], #16\n\t"
        "ldp x12, x13, [sp], #16\n\t"
        "ldp x10, x11, [sp], #16\n\t"
        "ldp x8, x9, [sp], #16\n\t"
        "ldp x6, x7, [sp], #16\n\t"
        "ldp x4, x5, [sp], #16\n\t"
        "ldp x2, x3, [sp], #16\n\t"
        "ldp x0, x1, [sp], #16\n\t"

        // Load parameters from bytecode (AFTER all trace calls to avoid corruption)
        "ldr x19, [x28], #8\n\t" // fault_pc
        "ldr x20, [x28], #8\n\t" // Rt (destination reg)
        "ldr x21, [x28], #8\n\t" // Rn (base reg)
        "ldr x22, [x28], #8\n\t" // immediate offset
        "ldr x23, [x28], #8\n\t" // size
        "ldr x24, [x28], #8\n\t" // idx_mode
        "ldr x25, [x28], #8\n\t" // meta

        // Patch 1B.1: Hot-hot fast path
        // Requirements: both Rn and Rt in 0-15, 64-bit, offset mode, meta=0
        "cmp x20, #16\n\t" // Is Rt hot (0-15)?
        "b.hs 91f\n\t"     // Branch to nonhot counter
        "cmp x21, #16\n\t" // Is Rn hot (0-15)?
        "b.hs 91f\n\t"
        "cmp x23, #3\n\t" // Is size 64-bit?
        "b.ne 92f\n\t"    // Branch to size counter
        "cmp x24, #0\n\t" // Is idx_mode offset (no writeback)?
        "b.ne 93f\n\t"    // Branch to idxmode counter
        "cmp x25, #0\n\t" // Is meta 0 (no reg offset, not signed)?
        "b.ne 94f\n\t"    // Branch to meta counter

        // PRE/POST-INDEX forms must go through helper to apply base writeback.
        // Fast path only supports offset addressing semantics.
        "cmp x24, #0\n\t"
        "b.ne 99f\n\t"

        // Get base register value (hot, in x1-x16) using computed goto
        // Branch table for Rn 0-15
        "adr x26, 70f\n\t"              // x26 = base of branch table
        "add x26, x26, x21, lsl #2\n\t" // x26 = &table[Rn] (b instructions are 4 bytes)
        "br x26\n\t"

        // Branch table - each entry is a direct branch to the load code
        "70:\n\t"
        "b 80f\n\t"  // Rn=0 -> load from x1
        "b 81f\n\t"  // Rn=1 -> load from x2
        "b 82f\n\t"  // Rn=2 -> load from x3
        "b 83f\n\t"  // Rn=3 -> load from x4
        "b 84f\n\t"  // Rn=4 -> load from x5
        "b 85f\n\t"  // Rn=5 -> load from x6
        "b 86f\n\t"  // Rn=6 -> load from x7
        "b 87f\n\t"  // Rn=7 -> load from x8
        "b 88f\n\t"  // Rn=8 -> load from x9
        "b 89f\n\t"  // Rn=9 -> load from x10
        "b 100f\n\t" // Rn=10 -> load from x11
        "b 101f\n\t" // Rn=11 -> load from x12
        "b 102f\n\t" // Rn=12 -> load from x13
        "b 103f\n\t" // Rn=13 -> load from x14
        "b 104f\n\t" // Rn=14 -> load from x15
        "b 105f\n\t" // Rn=15 -> load from x16

        // Load base register value into x17
        "80:\n\tmov x17, x1\n\tb 110f\n\t"
        "81:\n\tmov x17, x2\n\tb 110f\n\t"
        "82:\n\tmov x17, x3\n\tb 110f\n\t"
        "83:\n\tmov x17, x4\n\tb 110f\n\t"
        "84:\n\tmov x17, x5\n\tb 110f\n\t"
        "85:\n\tmov x17, x6\n\tb 110f\n\t"
        "86:\n\tmov x17, x7\n\tb 110f\n\t"
        "87:\n\tmov x17, x8\n\tb 110f\n\t"
        "88:\n\tmov x17, x9\n\tb 110f\n\t"
        "89:\n\tmov x17, x10\n\tb 110f\n\t"
        "100:\n\tmov x17, x11\n\tb 110f\n\t"
        "101:\n\tmov x17, x12\n\tb 110f\n\t"
        "102:\n\tmov x17, x13\n\tb 110f\n\t"
        "103:\n\tmov x17, x14\n\tb 110f\n\t"
        "104:\n\tmov x17, x15\n\tb 110f\n\t"
        "105:\n\tmov x17, x16\n\tb 110f\n\t"

        // Continue after base register load
        "110:\n\t"

        // Add immediate offset: x17 = base + offset
        "add x17, x17, x22\n\t"

        // Check alignment: addr & 7 == 0
        "tst x17, #7\n\t"
        "b.ne 95f\n\t" // Branch to align counter

        // Check cross-page: (addr & 0xFFF) <= 0xFF8
        "and x0, x17, #0xFFF\n\t"
        "cmp x0, #0xFF8\n\t"
        "b.hi 96f\n\t" // Branch to crosspg counter

        // Inline TLB lookup
        // All original args (x19-x25) remain stable
        // Use x26, x27 as scratch for TLB operations

        "ldr x26, [x29, %[cpu_tlb_off]]\n\t" // x26 = cpu->tlb
        "cbz x26, 98f\n\t"                   // Branch to notlb counter

        // TLB index: ((addr >> 12) & 1023) ^ (addr >> 22)
        "lsr x27, x17, #12\n\t"
        "and x27, x27, #1023\n\t"
        "lsr x0, x17, #22\n\t"
        "eor x27, x27, x0\n\t"

        // Load tlb entry at &entries[index]
        // entries is at offset 32 in struct tlb (64-bit page fields)
        "add x0, x26, #32\n\t"      // x0 = &tlb->entries[0]
        "mov x26, #24\n\t"          // x26 = sizeof(tlb_entry)
        "madd x0, x27, x26, x0\n\t" // x0 = &tlb->entries[index] (x0 + x27*24)
        "ldr x27, [x0]\n\t"         // x27 = entry.page

        // Compare page (clear lower 12 bits via shift)
        "lsr x26, x17, #12\n\t" // x26 = addr >> 12
        "lsl x26, x26, #12\n\t" // x26 = (addr >> 12) << 12 = page base
        "cmp x27, x26\n\t"
        "b.ne 97f\n\t" // Branch to tlbmiss counter

        // Compute host address and load
        // data_minus_addr is at offset 16 in tlb_entry (after two 8-byte page fields)
        "ldr x27, [x0, #16]\n\t" // x27 = entry.data_minus_addr
        "add x17, x27, x17\n\t"  // x17 = host address
        "ldr x0, [x17]\n\t"      // x0 = loaded value

        // Store to hot destination register using computed goto
        // x20 still holds original Rt (0-15)
        "adr x26, 120f\n\t"             // x26 = base of store table
        "add x26, x26, x20, lsl #2\n\t" // x26 = &table[Rt] (b instructions are 4 bytes)
        "br x26\n\t"

        // Store branch table
        "120:\n\t"
        "b 130f\n\t"
        "b 131f\n\t"
        "b 132f\n\t"
        "b 133f\n\t"
        "b 134f\n\t"
        "b 135f\n\t"
        "b 136f\n\t"
        "b 137f\n\t"
        "b 138f\n\t"
        "b 139f\n\t"
        "b 140f\n\t"
        "b 141f\n\t"
        "b 142f\n\t"
        "b 143f\n\t"
        "b 144f\n\t"
        "b 145f\n\t"

        // Store to destination register
        "130:\n\tmov x1, x0\n\tb 150f\n\t"
        "131:\n\tmov x2, x0\n\tb 150f\n\t"
        "132:\n\tmov x3, x0\n\tb 150f\n\t"
        "133:\n\tmov x4, x0\n\tb 150f\n\t"
        "134:\n\tmov x5, x0\n\tb 150f\n\t"
        "135:\n\tmov x6, x0\n\tb 150f\n\t"
        "136:\n\tmov x7, x0\n\tb 150f\n\t"
        "137:\n\tmov x8, x0\n\tb 150f\n\t"
        "138:\n\tmov x9, x0\n\tb 150f\n\t"
        "139:\n\tmov x10, x0\n\tb 150f\n\t"
        "140:\n\tmov x11, x0\n\tb 150f\n\t"
        "141:\n\tmov x12, x0\n\tb 150f\n\t"
        "142:\n\tmov x13, x0\n\tb 150f\n\t"
        "143:\n\tmov x14, x0\n\tb 150f\n\t"
        "144:\n\tmov x15, x0\n\tb 150f\n\t"
        "145:\n\tmov x16, x0\n\tb 150f\n\t"

        // Fast path complete - increment counter and advance to next gadget
        "150:\n\t"
        "ldr x26, [x29, %[ldr_fast_hits_off]]\n\t"
        "add x26, x26, #1\n\t"
        "str x26, [x29, %[ldr_fast_hits_off]]\n\t"
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t"

        // Per-reason fallback counters
        "91:\n\t" // TCTI_FALLBACK_NONHOT
        "ldr x26, [x29, %[ldr_fallback_nonhot_off]]\n\t"
        "add x26, x26, #1\n\t"
        "str x26, [x29, %[ldr_fallback_nonhot_off]]\n\t"
        "b 99f\n\t"

        "92:\n\t" // TCTI_FALLBACK_SIZE
        "ldr x26, [x29, %[ldr_fallback_size_off]]\n\t"
        "add x26, x26, #1\n\t"
        "str x26, [x29, %[ldr_fallback_size_off]]\n\t"
        "b 99f\n\t"

        "93:\n\t" // TCTI_FALLBACK_IDXMODE
        "ldr x26, [x29, %[ldr_fallback_idxmode_off]]\n\t"
        "add x26, x26, #1\n\t"
        "str x26, [x29, %[ldr_fallback_idxmode_off]]\n\t"
        "b 99f\n\t"

        "94:\n\t" // TCTI_FALLBACK_META
        "ldr x26, [x29, %[ldr_fallback_meta_off]]\n\t"
        "add x26, x26, #1\n\t"
        "str x26, [x29, %[ldr_fallback_meta_off]]\n\t"
        "b 99f\n\t"

        "95:\n\t" // TCTI_FALLBACK_ALIGN
        "ldr x26, [x29, %[ldr_fallback_align_off]]\n\t"
        "add x26, x26, #1\n\t"
        "str x26, [x29, %[ldr_fallback_align_off]]\n\t"
        "b 99f\n\t"

        "96:\n\t" // TCTI_FALLBACK_CROSSPG
        "ldr x26, [x29, %[ldr_fallback_crosspg_off]]\n\t"
        "add x26, x26, #1\n\t"
        "str x26, [x29, %[ldr_fallback_crosspg_off]]\n\t"
        "b 99f\n\t"

        "97:\n\t" // TCTI_FALLBACK_TLBMISS
        "ldr x26, [x29, %[ldr_fallback_tlbmiss_off]]\n\t"
        "add x26, x26, #1\n\t"
        "str x26, [x29, %[ldr_fallback_tlbmiss_off]]\n\t"
        "b 99f\n\t"

        "98:\n\t" // TCTI_FALLBACK_NOTLB
        "ldr x26, [x29, %[ldr_fallback_notlb_off]]\n\t"
        "add x26, x26, #1\n\t"
        "str x26, [x29, %[ldr_fallback_notlb_off]]\n\t"
        // fall through to 99

        // Common slow path after per-reason counter (label 99)
        "99:\n\t"
        "ldr x26, [x29, %[ldr_fallback_off]]\n\t"
        "add x26, x26, #1\n\t"
        "str x26, [x29, %[ldr_fallback_off]]\n\t"
        // Save registers and call C helper
        "stp x1, x2, [x29, #16]\n\t"
        "stp x3, x4, [x29, #32]\n\t"
        "stp x5, x6, [x29, #48]\n\t"
        "stp x7, x8, [x29, #64]\n\t"
        "stp x9, x10, [x29, #80]\n\t"
        "stp x11, x12, [x29, #96]\n\t"
        "stp x13, x14, [x29, #112]\n\t"
        "stp x15, x16, [x29, #128]\n\t"
        "bl _tcti_c_call_prologue\n\t"
        "mov x0, x29\n\t"
        "mov x1, x19\n\t" // fault_pc
        "mov x2, x20\n\t" // Rt
        "mov x3, x21\n\t" // Rn
        "mov x4, x22\n\t" // imm
        "mov x5, x23\n\t" // size
        "mov x6, x24\n\t" // idx_mode
        "mov x7, x25\n\t" // meta
        "bl _a64_tcti_ldr_x_helper\n\t"
        "bl _tcti_c_call_epilogue\n\t"
        "ldp x1, x2, [x29, #16]\n\t"
        "ldp x3, x4, [x29, #32]\n\t"
        "ldp x5, x6, [x29, #48]\n\t"
        "ldp x7, x8, [x29, #64]\n\t"
        "ldp x9, x10, [x29, #80]\n\t"
        "ldp x11, x12, [x29, #96]\n\t"
        "ldp x13, x14, [x29, #112]\n\t"
        "ldp x15, x16, [x29, #128]\n\t"
        "cmp x0, #0\n\t"
        "b.ne 1f\n\t"
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t"
        "1:\n\t"
        "b _tcti_exit_block\n\t"
        :
        : [cpu_tlb_off] "i"(CPU_TLB_OFFSET), [ldr_fast_hits_off] "i"(STAT_LDR_FAST_HITS_OFFSET),
          [ldr_fallback_off] "i"(STAT_LDR_FALLBACK_OFFSET),
          [ldr_fallback_nonhot_off] "i"(STAT_LDR_FALLBACK_NONHOT_OFFSET),
          [ldr_fallback_size_off] "i"(STAT_LDR_FALLBACK_SIZE_OFFSET),
          [ldr_fallback_idxmode_off] "i"(STAT_LDR_FALLBACK_IDXMODE_OFFSET),
          [ldr_fallback_meta_off] "i"(STAT_LDR_FALLBACK_META_OFFSET),
          [ldr_fallback_align_off] "i"(STAT_LDR_FALLBACK_ALIGN_OFFSET),
          [ldr_fallback_crosspg_off] "i"(STAT_LDR_FALLBACK_CROSSPG_OFFSET),
          [ldr_fallback_tlbmiss_off] "i"(STAT_LDR_FALLBACK_TLBMISS_OFFSET),
          [ldr_fallback_notlb_off] "i"(STAT_LDR_FALLBACK_NOTLB_OFFSET)
        : "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x19", "x20", "x21", "x22", "x23", "x24",
          "x25", "x26", "x27", "memory");
}

tcti_gadget_t gadget_ldr_x = gadget_ldr_x_impl;

__attribute__((naked)) void gadget_str_x_impl(void)
{
    asm volatile(
        // Load parameters from bytecode
        "ldr x19, [x28], #8\n\t" // fault_pc
        "ldr x20, [x28], #8\n\t" // Rt (source reg)
        "ldr x21, [x28], #8\n\t" // Rn (base reg)
        "ldr x22, [x28], #8\n\t" // immediate offset
        "ldr x23, [x28], #8\n\t" // size
        "ldr x24, [x28], #8\n\t" // idx_mode
        "ldr x25, [x28], #8\n\t" // meta

        // Patch 1B.2: Hot-hot fast path for STR
        // Requirements: both Rn and Rt in 0-15, 64-bit, offset mode, meta=0
        "cmp x20, #16\n\t" // Is Rt hot (0-15)?
        "b.hs 91f\n\t"     // Branch to nonhot counter
        "cmp x21, #16\n\t" // Is Rn hot (0-15)?
        "b.hs 91f\n\t"
        "cmp x23, #3\n\t" // Is size 64-bit?
        "b.ne 92f\n\t"    // Branch to size counter
        "cmp x24, #0\n\t" // Is idx_mode offset (no writeback)?
        "b.ne 93f\n\t"    // Branch to idxmode counter
        "cmp x25, #0\n\t" // Is meta 0 (no reg offset, not signed)?
        "b.ne 94f\n\t"    // Branch to meta counter

        // PRE/POST-INDEX forms must go through helper to apply base writeback.
        // Fast path only supports offset addressing semantics.
        "cmp x24, #0\n\t"
        "b.ne 99f\n\t"


        // Get base register value (hot, in x1-x16) using computed goto
        // Branch table for Rn 0-15
        "adr x26, 70f\n\t"              // x26 = base of branch table
        "add x26, x26, x21, lsl #2\n\t" // x26 = &table[Rn] (b instructions are 4 bytes)
        "br x26\n\t"

        // Branch table - each entry is a direct branch to the load code
        "70:\n\t"
        "b 80f\n\t"  // Rn=0 -> load from x1
        "b 81f\n\t"  // Rn=1 -> load from x2
        "b 82f\n\t"  // Rn=2 -> load from x3
        "b 83f\n\t"  // Rn=3 -> load from x4
        "b 84f\n\t"  // Rn=4 -> load from x5
        "b 85f\n\t"  // Rn=5 -> load from x6
        "b 86f\n\t"  // Rn=6 -> load from x7
        "b 87f\n\t"  // Rn=7 -> load from x8
        "b 88f\n\t"  // Rn=8 -> load from x9
        "b 89f\n\t"  // Rn=9 -> load from x10
        "b 100f\n\t" // Rn=10 -> load from x11
        "b 101f\n\t" // Rn=11 -> load from x12
        "b 102f\n\t" // Rn=12 -> load from x13
        "b 103f\n\t" // Rn=13 -> load from x14
        "b 104f\n\t" // Rn=14 -> load from x15
        "b 105f\n\t" // Rn=15 -> load from x16

        // Load base register value into x17
        "80:\n\tmov x17, x1\n\tb 110f\n\t"
        "81:\n\tmov x17, x2\n\tb 110f\n\t"
        "82:\n\tmov x17, x3\n\tb 110f\n\t"
        "83:\n\tmov x17, x4\n\tb 110f\n\t"
        "84:\n\tmov x17, x5\n\tb 110f\n\t"
        "85:\n\tmov x17, x6\n\tb 110f\n\t"
        "86:\n\tmov x17, x7\n\tb 110f\n\t"
        "87:\n\tmov x17, x8\n\tb 110f\n\t"
        "88:\n\tmov x17, x9\n\tb 110f\n\t"
        "89:\n\tmov x17, x10\n\tb 110f\n\t"
        "100:\n\tmov x17, x11\n\tb 110f\n\t"
        "101:\n\tmov x17, x12\n\tb 110f\n\t"
        "102:\n\tmov x17, x13\n\tb 110f\n\t"
        "103:\n\tmov x17, x14\n\tb 110f\n\t"
        "104:\n\tmov x17, x15\n\tb 110f\n\t"
        "105:\n\tmov x17, x16\n\tb 110f\n\t"

        // Continue after base register load
        "110:\n\t"

        // Add immediate offset: x17 = base + offset
        "add x17, x17, x22\n\t"

        // Check alignment: addr & 7 == 0
        "tst x17, #7\n\t"
        "b.ne 95f\n\t" // Branch to align counter

        // Check cross-page: (addr & 0xFFF) <= 0xFF8
        "and x0, x17, #0xFFF\n\t"
        "cmp x0, #0xFF8\n\t"
        "b.hi 96f\n\t" // Branch to crosspg counter

        // Get source register value (Rt, hot, in x1-x16) using computed goto
        // x20 still holds original Rt (0-15)
        "adr x26, 120f\n\t"             // x26 = base of branch table
        "add x26, x26, x20, lsl #2\n\t" // x26 = &table[Rt] (b instructions are 4 bytes)
        "br x26\n\t"

        // Branch table for source value load
        "120:\n\t"
        "b 130f\n\t" // Rt=0 -> load from x1
        "b 131f\n\t" // Rt=1 -> load from x2
        "b 132f\n\t" // Rt=2 -> load from x3
        "b 133f\n\t" // Rt=3 -> load from x4
        "b 134f\n\t" // Rt=4 -> load from x5
        "b 135f\n\t" // Rt=5 -> load from x6
        "b 136f\n\t" // Rt=6 -> load from x7
        "b 137f\n\t" // Rt=7 -> load from x8
        "b 138f\n\t" // Rt=8 -> load from x9
        "b 139f\n\t" // Rt=9 -> load from x10
        "b 140f\n\t" // Rt=10 -> load from x11
        "b 141f\n\t" // Rt=11 -> load from x12
        "b 142f\n\t" // Rt=12 -> load from x13
        "b 143f\n\t" // Rt=13 -> load from x14
        "b 144f\n\t" // Rt=14 -> load from x15
        "b 145f\n\t" // Rt=15 -> load from x16

        // Load source register value into x0
        "130:\n\tmov x0, x1\n\tb 150f\n\t"
        "131:\n\tmov x0, x2\n\tb 150f\n\t"
        "132:\n\tmov x0, x3\n\tb 150f\n\t"
        "133:\n\tmov x0, x4\n\tb 150f\n\t"
        "134:\n\tmov x0, x5\n\tb 150f\n\t"
        "135:\n\tmov x0, x6\n\tb 150f\n\t"
        "136:\n\tmov x0, x7\n\tb 150f\n\t"
        "137:\n\tmov x0, x8\n\tb 150f\n\t"
        "138:\n\tmov x0, x9\n\tb 150f\n\t"
        "139:\n\tmov x0, x10\n\tb 150f\n\t"
        "140:\n\tmov x0, x11\n\tb 150f\n\t"
        "141:\n\tmov x0, x12\n\tb 150f\n\t"
        "142:\n\tmov x0, x13\n\tb 150f\n\t"
        "143:\n\tmov x0, x14\n\tb 150f\n\t"
        "144:\n\tmov x0, x15\n\tb 150f\n\t"
        "145:\n\tmov x0, x16\n\tb 150f\n\t"

        // Continue after source register load
        "150:\n\t"

        // Inline TLB lookup
        // CRITICAL: x20 contains original Rt - do NOT clobber it
        // Use x0 as scratch for TLB operations to avoid clobbering hot guest regs

        "ldr x26, [x29, %[cpu_tlb_off]]\n\t" // x26 = cpu->tlb
        "cbz x26, 98f\n\t"                   // Branch to notlb counter

        // TLB index: ((addr >> 12) & 1023) ^ (addr >> 22)
        "lsr x27, x17, #12\n\t"
        "and x27, x27, #1023\n\t"
        "lsr x0, x17, #22\n\t"
        "eor x27, x27, x0\n\t"

        // Load tlb entry at &entries[index]
        // entries is at offset 32 in struct tlb (64-bit page fields)
        "add x0, x26, #32\n\t"      // x0 = &tlb->entries[0]
        "mov x26, #24\n\t"          // x26 = sizeof(tlb_entry)
        "madd x0, x27, x26, x0\n\t" // x0 = &tlb->entries[index] (x0 + x27*24)
        "ldr x27, [x0]\n\t"         // x27 = entry.page

        // Compare page (clear lower 12 bits via shift)
        "lsr x26, x17, #12\n\t" // x26 = addr >> 12
        "lsl x26, x26, #12\n\t" // x26 = (addr >> 12) << 12 = page base
        "cmp x27, x26\n\t"
        "b.ne 97f\n\t" // Branch to tlbmiss counter

        // Compute host address and store
        // data_minus_addr is at offset 16 in tlb_entry
        "ldr x27, [x0, #16]\n\t" // x27 = entry.data_minus_addr
        "add x17, x27, x17\n\t"  // x17 = host address
        "str x0, [x17]\n\t"      // store value from x0

        // Fast path complete - increment counter and advance to next gadget
        "ldr x26, [x29, %[str_fast_hits_off]]\n\t"
        "add x26, x26, #1\n\t"
        "str x26, [x29, %[str_fast_hits_off]]\n\t"
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t"

        // Per-reason fallback counters for STR
        "91:\n\t" // TCTI_FALLBACK_NONHOT
        "ldr x26, [x29, %[str_fallback_nonhot_off]]\n\t"
        "add x26, x26, #1\n\t"
        "str x26, [x29, %[str_fallback_nonhot_off]]\n\t"
        "b 99f\n\t"

        "92:\n\t" // TCTI_FALLBACK_SIZE
        "ldr x26, [x29, %[str_fallback_size_off]]\n\t"
        "add x26, x26, #1\n\t"
        "str x26, [x29, %[str_fallback_size_off]]\n\t"
        "b 99f\n\t"

        "93:\n\t" // TCTI_FALLBACK_IDXMODE
        "ldr x26, [x29, %[str_fallback_idxmode_off]]\n\t"
        "add x26, x26, #1\n\t"
        "str x26, [x29, %[str_fallback_idxmode_off]]\n\t"
        "b 99f\n\t"

        "94:\n\t" // TCTI_FALLBACK_META
        "ldr x26, [x29, %[str_fallback_meta_off]]\n\t"
        "add x26, x26, #1\n\t"
        "str x26, [x29, %[str_fallback_meta_off]]\n\t"
        "b 99f\n\t"

        "95:\n\t" // TCTI_FALLBACK_ALIGN
        "ldr x26, [x29, %[str_fallback_align_off]]\n\t"
        "add x26, x26, #1\n\t"
        "str x26, [x29, %[str_fallback_align_off]]\n\t"
        "b 99f\n\t"

        "96:\n\t" // TCTI_FALLBACK_CROSSPG
        "ldr x26, [x29, %[str_fallback_crosspg_off]]\n\t"
        "add x26, x26, #1\n\t"
        "str x26, [x29, %[str_fallback_crosspg_off]]\n\t"
        "b 99f\n\t"

        "97:\n\t" // TCTI_FALLBACK_TLBMISS
        "ldr x26, [x29, %[str_fallback_tlbmiss_off]]\n\t"
        "add x26, x26, #1\n\t"
        "str x26, [x29, %[str_fallback_tlbmiss_off]]\n\t"
        "b 99f\n\t"

        "98:\n\t" // TCTI_FALLBACK_NOTLB
        "ldr x26, [x29, %[str_fallback_notlb_off]]\n\t"
        "add x26, x26, #1\n\t"
        "str x26, [x29, %[str_fallback_notlb_off]]\n\t"
        // fall through to 99

        // Common slow path after per-reason counter (label 99)
        "99:\n\t"
        "ldr x26, [x29, %[str_fallback_off]]\n\t"
        "add x26, x26, #1\n\t"
        "str x26, [x29, %[str_fallback_off]]\n\t"
        "stp x1, x2, [x29, #16]\n\t"
        "stp x3, x4, [x29, #32]\n\t"
        "stp x5, x6, [x29, #48]\n\t"
        "stp x7, x8, [x29, #64]\n\t"
        "stp x9, x10, [x29, #80]\n\t"
        "stp x11, x12, [x29, #96]\n\t"
        "stp x13, x14, [x29, #112]\n\t"
        "stp x15, x16, [x29, #128]\n\t"
        "bl _tcti_c_call_prologue\n\t"
        "mov x0, x29\n\t"
        "mov x1, x19\n\t" // fault_pc
        "mov x2, x20\n\t" // Rt
        "mov x3, x21\n\t" // Rn
        "mov x4, x22\n\t" // imm
        "mov x5, x23\n\t" // size
        "mov x6, x24\n\t" // idx_mode
        "mov x7, x25\n\t" // meta
        "bl _a64_tcti_str_x_helper\n\t"
        "bl _tcti_c_call_epilogue\n\t"
        "ldp x1, x2, [x29, #16]\n\t"
        "ldp x3, x4, [x29, #32]\n\t"
        "ldp x5, x6, [x29, #48]\n\t"
        "ldp x7, x8, [x29, #64]\n\t"
        "ldp x9, x10, [x29, #80]\n\t"
        "ldp x11, x12, [x29, #96]\n\t"
        "ldp x13, x14, [x29, #112]\n\t"
        "ldp x15, x16, [x29, #128]\n\t"
        "cmp x0, #0\n\t" // x0 still has return value from helper
        "b.ne 1f\n\t"
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t"
        "1:\n\t"
        "b _tcti_exit_block\n\t"
        :
        : [cpu_tlb_off] "i"(CPU_TLB_OFFSET), [str_fast_hits_off] "i"(STAT_STR_FAST_HITS_OFFSET),
          [str_fallback_off] "i"(STAT_STR_FALLBACK_OFFSET),
          [str_fallback_nonhot_off] "i"(STAT_STR_FALLBACK_NONHOT_OFFSET),
          [str_fallback_size_off] "i"(STAT_STR_FALLBACK_SIZE_OFFSET),
          [str_fallback_idxmode_off] "i"(STAT_STR_FALLBACK_IDXMODE_OFFSET),
          [str_fallback_meta_off] "i"(STAT_STR_FALLBACK_META_OFFSET),
          [str_fallback_align_off] "i"(STAT_STR_FALLBACK_ALIGN_OFFSET),
          [str_fallback_crosspg_off] "i"(STAT_STR_FALLBACK_CROSSPG_OFFSET),
          [str_fallback_tlbmiss_off] "i"(STAT_STR_FALLBACK_TLBMISS_OFFSET),
          [str_fallback_notlb_off] "i"(STAT_STR_FALLBACK_NOTLB_OFFSET)
        : "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x19", "x20", "x21", "x22", "x23", "x24",
          "x25", "x26", "x27", "memory");
}

tcti_gadget_t gadget_str_x = gadget_str_x_impl;

__attribute__((naked)) void gadget_nop_impl(void)
{
    asm volatile("ldr x27, [x28], #8\n\t"
                 "br x27\n\t");
}

tcti_gadget_t gadget_nop = gadget_nop_impl;

__attribute__((naked)) void gadget_pc_advance_impl(void)
{
    asm volatile("ldr x0, [x28], #8\n\t"
                 "str x0, [x29, #272]\n\t"
                 "ldr x27, [x28], #8\n\t"
                 "br x27\n\t");
}

tcti_gadget_t gadget_pc_advance = gadget_pc_advance_impl;

__attribute__((naked)) void gadget_sysreg_unsupported_impl(void)
{
    asm volatile("mov x0, #5\n\t"
                 "b _tcti_exit_block\n\t");
}

tcti_gadget_t gadget_sysreg_unsupported = gadget_sysreg_unsupported_impl;

__attribute__((naked)) void gadget_mrs_impl(void)
{
    asm volatile("ldr x19, [x28], #8\n\t"
                 "ldr x20, [x28], #8\n\t"
                 "stp x1, x2, [x29, #16]\n\t"
                 "stp x3, x4, [x29, #32]\n\t"
                 "stp x5, x6, [x29, #48]\n\t"
                 "stp x7, x8, [x29, #64]\n\t"
                 "stp x9, x10, [x29, #80]\n\t"
                 "stp x11, x12, [x29, #96]\n\t"
                 "stp x13, x14, [x29, #112]\n\t"
                 "stp x15, x16, [x29, #128]\n\t"
                 "bl _tcti_c_call_prologue\n\t"
                 "mov x0, x29\n\t"
                 "mov x1, x19\n\t"
                 "mov x2, x20\n\t"
                 "bl _a64_tcti_mrs_helper\n\t"
                 "bl _tcti_c_call_epilogue\n\t"
                 "ldp x1, x2, [x29, #16]\n\t"
                 "ldp x3, x4, [x29, #32]\n\t"
                 "ldp x5, x6, [x29, #48]\n\t"
                 "ldp x7, x8, [x29, #64]\n\t"
                 "ldp x9, x10, [x29, #80]\n\t"
                 "ldp x11, x12, [x29, #96]\n\t"
                 "ldp x13, x14, [x29, #112]\n\t"
                 "ldp x15, x16, [x29, #128]\n\t"
                 "cmp x0, #0\n\t"
                 "b.ne 1f\n\t"
                 "ldr x27, [x28], #8\n\t"
                 "br x27\n\t"
                 "1:\n\t"
                 "b _tcti_exit_block\n\t");
}

tcti_gadget_t gadget_mrs = gadget_mrs_impl;

__attribute__((visibility("default"))) int _a64_tcti_mrs_helper(struct cpu_state *cpu,
                                                                uint64_t sysreg, uint64_t rd)
{
    return a64_tcti_mrs_helper(cpu, sysreg, rd);
}

__attribute__((naked)) void gadget_msr_impl(void)
{
    asm volatile("ldr x19, [x28], #8\n\t"
                 "ldr x20, [x28], #8\n\t"
                 "stp x1, x2, [x29, #16]\n\t"
                 "stp x3, x4, [x29, #32]\n\t"
                 "stp x5, x6, [x29, #48]\n\t"
                 "stp x7, x8, [x29, #64]\n\t"
                 "stp x9, x10, [x29, #80]\n\t"
                 "stp x11, x12, [x29, #96]\n\t"
                 "stp x13, x14, [x29, #112]\n\t"
                 "stp x15, x16, [x29, #128]\n\t"
                 "bl _tcti_c_call_prologue\n\t"
                 "mov x0, x29\n\t"
                 "mov x1, x19\n\t"
                 "mov x2, x20\n\t"
                 "bl _a64_tcti_msr_helper\n\t"
                 "bl _tcti_c_call_epilogue\n\t"
                 "ldp x1, x2, [x29, #16]\n\t"
                 "ldp x3, x4, [x29, #32]\n\t"
                 "ldp x5, x6, [x29, #48]\n\t"
                 "ldp x7, x8, [x29, #64]\n\t"
                 "ldp x9, x10, [x29, #80]\n\t"
                 "ldp x11, x12, [x29, #96]\n\t"
                 "ldp x13, x14, [x29, #112]\n\t"
                 "ldp x15, x16, [x29, #128]\n\t"
                 "mov x22, #0x5a10\n\t"
                 "cmp x19, x22\n\t"
                 "b.ne 0f\n\t"
                 "ldr x21, [x29, #280]\n\t"
                 "msr nzcv, x21\n\t"
                 "0:\n\t"
                 "cmp x0, #0\n\t"
                 "b.ne 1f\n\t"
                 "ldr x27, [x28], #8\n\t"
                 "br x27\n\t"
                 "1:\n\t"
                 "b _tcti_exit_block\n\t");
}

tcti_gadget_t gadget_msr = gadget_msr_impl;

__attribute__((visibility("default"))) int _a64_tcti_msr_helper(struct cpu_state *cpu,
                                                                uint64_t sysreg, uint64_t rt)
{
    return a64_tcti_msr_helper(cpu, sysreg, rt);
}

__attribute__((naked)) void gadget_svc_impl(void)
{
    asm volatile("ldr x0, [x28], #8\n\t"
                 "str x0, [x29, #272]\n\t"
                 "mov x0, #1\n\t"
                 "b _tcti_exit_block\n\t");
}

tcti_gadget_t gadget_svc = gadget_svc_impl;
