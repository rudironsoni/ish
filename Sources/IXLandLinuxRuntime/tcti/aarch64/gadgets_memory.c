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
#import <IXLandLinuxRuntime/emu/aarch64/decode.h>
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
#include <string.h>

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

static uint64_t tcti_replicate16(uint16_t value)
{
    uint64_t lane = value;
    return lane | (lane << 16) | (lane << 32) | (lane << 48);
}

static uint64_t tcti_replicate32(uint32_t value)
{
    uint64_t lane = value;
    return lane | (lane << 32);
}

static int tcti_simd_vec_access(struct cpu_state *cpu, uint64_t addr, uint64_t vt,
                                uint64_t vec_bytes, int is_load)
{
    if (vt >= 32 || (vec_bytes != 1 && vec_bytes != 2 && vec_bytes != 4 &&
                     vec_bytes != 8 && vec_bytes != 16)) {
        return TCTI_EXIT_FAULT;
    }

    if (vec_bytes <= 8) {
        if (is_load)
            return a64_guest_read(cpu, cpu->tlb, addr, cpu->vregs[vt].b, (int)vec_bytes) ==
                           A64_MEM_OK
                       ? TCTI_EXIT_NORMAL
                       : TCTI_EXIT_FAULT;
        return a64_guest_write(cpu, cpu->tlb, addr, cpu->vregs[vt].b, (int)vec_bytes) ==
                       A64_MEM_OK
                   ? TCTI_EXIT_NORMAL
                   : TCTI_EXIT_FAULT;
    }

    if (is_load) {
        if (a64_guest_read64(cpu, cpu->tlb, addr, &cpu->vregs[vt].d[0]) != A64_MEM_OK)
            return TCTI_EXIT_FAULT;
        if (a64_guest_read64(cpu, cpu->tlb, addr + 8, &cpu->vregs[vt].d[1]) != A64_MEM_OK)
            return TCTI_EXIT_FAULT;
        return TCTI_EXIT_NORMAL;
    }

    if (a64_guest_write64(cpu, cpu->tlb, addr, cpu->vregs[vt].d[0]) != A64_MEM_OK)
        return TCTI_EXIT_FAULT;
    if (a64_guest_write64(cpu, cpu->tlb, addr + 8, cpu->vregs[vt].d[1]) != A64_MEM_OK)
        return TCTI_EXIT_FAULT;
    return TCTI_EXIT_NORMAL;
}

__attribute__((used)) static void tcti_simd_dup_gpr_helper(struct cpu_state *cpu, uint64_t vd,
                                                          uint64_t rn,
                                                          uint64_t vec_bytes)
{
    uint64_t value = tcti_read_reg_or_zr(cpu, (int)rn);
    if (vd >= 32)
        return;

    switch (vec_bytes) {
    case 1: {
        uint8_t byte = (uint8_t)value;
        memset(cpu->vregs[vd].b, byte, sizeof(cpu->vregs[vd].b));
        break;
    }
    case 2:
        for (int i = 0; i < 8; i++)
            cpu->vregs[vd].h[i] = (uint16_t)value;
        break;
    case 4:
        for (int i = 0; i < 4; i++)
            cpu->vregs[vd].s[i] = (uint32_t)value;
        break;
    case 8:
        for (int i = 0; i < 2; i++)
            cpu->vregs[vd].d[i] = value;
        break;
    default:
        break;
    }
}

static uint64_t tcti_advsimd_modified_immediate64(uint64_t imm8_value, uint64_t cmode,
                                                  uint64_t op)
{
    uint8_t imm8 = (uint8_t)imm8_value;

    if (cmode <= 7) {
        uint32_t lane = (uint32_t)imm8 << ((cmode >> 1) * 8);
        if (op)
            lane = ~lane;
        return tcti_replicate32(lane);
    }

    if ((cmode & 0xe) == 8) {
        uint16_t lane = (uint16_t)imm8 << ((cmode & 1) * 8);
        if (op)
            lane = (uint16_t)~lane;
        return tcti_replicate16(lane);
    }

    if (cmode == 12 || cmode == 13) {
        uint32_t lane = cmode == 12 ? (((uint32_t)imm8 << 8) | 0x000000ffu)
                                    : (((uint32_t)imm8 << 16) | 0x0000ffffu);
        if (op)
            lane = ~lane;
        return tcti_replicate32(lane);
    }

    if (cmode == 14 && !op) {
        uint64_t byte = imm8;
        byte |= byte << 8;
        byte |= byte << 16;
        byte |= byte << 32;
        return byte;
    }

    if (cmode == 14 && op) {
        uint64_t lane = 0;
        for (unsigned bit_index = 0; bit_index < 8; bit_index++) {
            if (imm8 & (1u << bit_index))
                lane |= 0xffULL << (bit_index * 8);
        }
        return lane;
    }

    return 0;
}

__attribute__((used)) static void tcti_simd_movi_imm_helper(struct cpu_state *cpu, uint64_t vd,
                                                           uint64_t imm8, uint64_t cmode,
                                                           uint64_t op, uint64_t q)
{
    if (vd >= 32)
        return;

    uint64_t low = tcti_advsimd_modified_immediate64(imm8, cmode, op);
    cpu->vregs[vd].d[0] = low;
    cpu->vregs[vd].d[1] = q ? low : 0;
}

__attribute__((used)) static void tcti_simd_mov_gpr_from_vec_helper(struct cpu_state *cpu,
                                                                   uint64_t rd,
                                                                   uint64_t vn,
                                                                   uint64_t vec_bytes,
                                                                   uint64_t vec_index,
                                                                   uint64_t is_64bit)
{
    uint64_t value = 0;

    if (rd >= 31 || vn >= 32)
        return;

    switch (vec_bytes) {
    case 1:
        if (vec_index < 16)
            value = cpu->vregs[vn].b[vec_index];
        break;
    case 2:
        if (vec_index < 8)
            value = cpu->vregs[vn].h[vec_index];
        break;
    case 4:
        if (vec_index < 4)
            value = cpu->vregs[vn].s[vec_index];
        break;
    case 8:
        if (vec_index < 2)
            value = cpu->vregs[vn].d[vec_index];
        break;
    default:
        break;
    }

    cpu->x[rd] = is_64bit ? value : (uint32_t)value;
}

__attribute__((used)) static int tcti_simd_ldst_helper(struct cpu_state *cpu, uint64_t fault_pc,
                                                      uint64_t rt, uint64_t rt2,
                                                      uint64_t rn, int64_t imm,
                                                      uint64_t vec_bytes,
                                                      uint64_t idx_mode,
                                                      uint64_t is_pair,
                                                      uint64_t is_load)
{
    uint64_t base = tcti_read_base_reg_or_sp(cpu, (int)rn);
    uint64_t addr = base + (uint64_t)imm;
    int ret;

    if (idx_mode == A64_POST_INDEX) {
        addr = base;
    } else if (idx_mode == A64_PRE_INDEX) {
        base += (uint64_t)imm;
        addr = base;
    }

    ret = tcti_simd_vec_access(cpu, addr, rt, vec_bytes, (int)is_load);
    if (ret != TCTI_EXIT_NORMAL)
        goto fault;

    if (is_pair) {
        ret = tcti_simd_vec_access(cpu, addr + vec_bytes, rt2, vec_bytes, (int)is_load);
        if (ret != TCTI_EXIT_NORMAL)
            goto fault;
    }

    if (idx_mode == A64_PRE_INDEX || idx_mode == A64_POST_INDEX) {
        uint64_t writeback = idx_mode == A64_POST_INDEX ? base + (uint64_t)imm : base;
        if (rn == 31)
            cpu->sp = writeback;
        else if (rn < 31)
            cpu->x[rn] = writeback;
    }

    return TCTI_EXIT_NORMAL;

fault:
    cpu->pc = fault_pc;
    cpu->fault_was_write = !is_load;
    return TCTI_EXIT_FAULT;
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
    case A64_EXT_UXTB:
        return "uxtb";
    case A64_EXT_UXTH:
        return "uxth";
    case A64_EXT_UXTW:
        return "uxtw";
    case A64_EXT_UXTX:
        return "uxtx";
    case A64_EXT_SXTB:
        return "sxtb";
    case A64_EXT_SXTH:
        return "sxth";
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

static int tcti_atomic_ldst_helper(struct cpu_state *cpu, uint64_t fault_pc, uint64_t rt,
                                   uint64_t rn, uint64_t rs, uint64_t size, uint64_t is_load)
{
    uint64_t addr = tcti_read_base_reg_or_sp(cpu, (int)rn);
    int ret = A64_MEM_FAULT;

    if (is_load) {
        switch (size) {
        case A64_SIZE_B: {
            uint8_t value = 0;
            ret = a64_guest_ldxr8(cpu, cpu->tlb, addr, &value);
            if (ret == A64_MEM_OK)
                tcti_write_reg_or_zr(cpu, (int)rt, value, 0);
            break;
        }
        case A64_SIZE_H: {
            uint16_t value = 0;
            ret = a64_guest_ldxr16(cpu, cpu->tlb, addr, &value);
            if (ret == A64_MEM_OK)
                tcti_write_reg_or_zr(cpu, (int)rt, value, 0);
            break;
        }
        case A64_SIZE_W: {
            uint32_t value = 0;
            ret = a64_guest_ldxr32(cpu, cpu->tlb, addr, &value);
            if (ret == A64_MEM_OK)
                tcti_write_reg_or_zr(cpu, (int)rt, value, 0);
            break;
        }
        case A64_SIZE_X: {
            uint64_t value = 0;
            ret = a64_guest_ldxr64(cpu, cpu->tlb, addr, &value);
            if (ret == A64_MEM_OK)
                tcti_write_reg_or_zr(cpu, (int)rt, value, 1);
            break;
        }
        default:
            ret = A64_MEM_FAULT;
            break;
        }
    } else {
        uint64_t value = tcti_read_reg_or_zr(cpu, (int)rt);
        int success = 0;

        switch (size) {
        case A64_SIZE_B:
            ret = a64_guest_stxr8(cpu, cpu->tlb, addr, (uint8_t)value, &success);
            break;
        case A64_SIZE_H:
            ret = a64_guest_stxr16(cpu, cpu->tlb, addr, (uint16_t)value, &success);
            break;
        case A64_SIZE_W:
            ret = a64_guest_stxr32(cpu, cpu->tlb, addr, (uint32_t)value, &success);
            break;
        case A64_SIZE_X:
            ret = a64_guest_stxr64(cpu, cpu->tlb, addr, value, &success);
            break;
        default:
            ret = A64_MEM_FAULT;
            break;
        }

        if (ret == A64_MEM_OK)
            tcti_write_reg_or_zr(cpu, (int)rs, success ? 0 : 1, 0);
    }

    if (ret == A64_MEM_OK)
        return TCTI_EXIT_NORMAL;

    cpu->pc = fault_pc;
    cpu->fault_addr = addr;
    cpu->fault_was_write = !is_load;
    return TCTI_EXIT_FAULT;
}

static uint64_t tcti_extend_ldst_offset(struct cpu_state *cpu, int rm, int extend_type)
{
    uint64_t value = tcti_read_reg_or_zr(cpu, rm);

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
                 "cmp x19, #16\n\t"
                 "b.hs 1f\n\t"
                 "mov x26, x19\n\t"
                 "bl _tcti_sync_hot_reg_from_cpu\n\t"
                 "1:\n\t"
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

    static int ldst_fault_trace_budget = 0;
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

    uint64_t addr = base;
    uint64_t is_signed = meta & 0xff;
    uint64_t is_reg_offset = (meta >> 8) & 0xff;
    uint64_t load_writes_64 = (meta >> 40) & 0x1;
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
        ldst_fault_trace_budget--;
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

    cpu->fault_addr = addr;
    cpu->fault_was_write = is_load ? false : true;

    // PROOF: Capture exact value read by ldrh at 0x6d1c0 (guest_ea 0x1036)
    if (fault_pc == 0x6d1c0ULL && is_load && size == A64_SIZE_H && imm == 54) {
        uint16_t probe_value = 0;
        int probe_ret = a64_guest_read16(cpu, cpu->tlb, addr, &probe_value);
        char ev[256];
        snprintf(ev, sizeof(ev),
                 "task.proof.6d1c0.ldrh_result=addr:0x%llx,read_val:0x%04x,mem_ret:%d,host_ptr:0x%llx",
                 (unsigned long long)addr, (unsigned int)probe_value, probe_ret,
                 (unsigned long long)(uintptr_t)ldst_host_ptr_probe);
        trace_record_event(TRACE_ORIGIN_EXEC, ev);
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
        int trace_6a990 = fault_pc == 0x6a990ULL;

        if (trace_6a990) {
            char ev[384];
            snprintf(ev, sizeof(ev),
                     "task.proof.6a990.load.pre=rt:%llu,rn:%llu,base:0x%llx,imm:%lld,"
                     "addr:0x%llx,width:%d,size:%llu,is_signed:%llu,host_ptr:0x%llx",
                     (unsigned long long)rt, (unsigned long long)rn,
                     (unsigned long long)base, (long long)imm, (unsigned long long)addr, width,
                     (unsigned long long)size, (unsigned long long)is_signed,
                     (unsigned long long)(uintptr_t)ldst_host_ptr_probe);
            trace_record_event(TRACE_ORIGIN_EXEC, ev);
        }

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

        if (trace_6a990) {
            char ev[384];
            snprintf(ev, sizeof(ev),
                     "task.proof.6a990.load.post_mem=mem_ret:%d,value:0x%llx,x3_before:0x%llx,"
                     "x0:0x%llx,x4:0x%llx,pc:0x%llx",
                     mem_ret, (unsigned long long)value, (unsigned long long)cpu->x[3],
                     (unsigned long long)cpu->x[0], (unsigned long long)cpu->x[4],
                     (unsigned long long)cpu->pc);
            trace_record_event(TRACE_ORIGIN_EXEC, ev);
        }

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

        if (trace_6a990) {
            char ev[256];
            snprintf(ev, sizeof(ev),
                     "task.proof.6a990.load.pre_writeback=rt:%llu,value:0x%llx,write64:%d",
                     (unsigned long long)rt, (unsigned long long)value,
                     size == A64_SIZE_X || load_writes_64);
            trace_record_event(TRACE_ORIGIN_EXEC, ev);
        }

        tcti_write_reg_or_zr(cpu, (int)rt, value, size == A64_SIZE_X || load_writes_64);

        if (trace_6a990) {
            char ev[384];
            snprintf(ev, sizeof(ev),
                     "task.proof.6a990.load.post_writeback=x3_after:0x%llx,x0:0x%llx,x4:0x%llx",
                     (unsigned long long)cpu->x[3], (unsigned long long)cpu->x[0],
                     (unsigned long long)cpu->x[4]);
            trace_record_event(TRACE_ORIGIN_EXEC, ev);
        }

        // PROOF: Trace x0 immediately after ldrh writeback at 0x6d1c0
        if (fault_pc == 0x6d1c0ULL && is_load && size == A64_SIZE_H && rt == 0) {
            char ev[256];
            snprintf(ev, sizeof(ev),
                     "task.proof.6d1c0.writeback=x0_after_write:0x%llx,value:0x%llx,"
                     "rt:%llu,size:%llu,is_64bit:%d",
                     (unsigned long long)cpu->x[0], (unsigned long long)value,
                     (unsigned long long)rt, (unsigned long long)size, size == A64_SIZE_X ? 1 : 0);
            trace_record_event(TRACE_ORIGIN_EXEC, ev);
        }

        // PROOF: Trace every write to x0 within interpreter seamblock [0x6d184-0x6d1d0]
        if (rt == 0 && fault_pc >= 0x6d184ULL && fault_pc <= 0x6d1d0ULL) {
            char ev[256];
            snprintf(ev, sizeof(ev),
                     "task.proof.x0.mutation=pc:0x%llx,new_x0:0x%llx,old_x0:0x%llx,"
                     "value:0x%llx,size:%llu,is_64bit:%d",
                     (unsigned long long)fault_pc, (unsigned long long)cpu->x[0],
                     (unsigned long long)(cpu->x[0] ^ value), /* pre-write estimate */
                     (unsigned long long)value, (unsigned long long)size,
                     size == A64_SIZE_X ? 1 : 0);
            trace_record_event(TRACE_ORIGIN_EXEC, ev);
        }

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
            mem_ret = a64_guest_write64(cpu, cpu->tlb, addr, value);
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
    static int ldr_helper_reach_budget = 0;
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
    static int str_helper_reach_budget = 0;
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
                 "ldr x26, [x28], #8\n\t"
                 "ldr x17, [x28], #8\n\t"
                 "cbz x26, 1f\n\t"
                 "str x17, [x29, #256]\n\t"
                 "1:\n\t"
                 "str x0, [x29, #272]\n\t"
                 "mov x0, #0\n\t"
                 "b _tcti_exit_block\n\t");
}

tcti_gadget_t gadget_b = gadget_b_impl;

#define GEN_BCOND(name, cond)                                                                      \
    __attribute__((naked)) void gadget_bcond_##name##_impl(void)                                   \
    {                                                                                              \
        asm volatile("ldr x24, [x28], #8\n\t"                                                      \
                     "ldr x25, [x28], #8\n\t"                                                      \
                     "ldr x26, [x29, #280]\n\t"                                                    \
                     "msr nzcv, x26\n\t"                                                           \
                     "b." #cond " 1f\n\t"                                                          \
                     "mov x24, x25\n\t"                                                            \
                     "1:\n\t"                                                                      \
                     "str x24, [x29, %[pc_off]]\n\t"                                               \
                     "mov x0, #0\n\t"                                                              \
                     "b _tcti_exit_block\n\t"                                                      \
                     :                                                                             \
                     : [pc_off] "i"(PC_OFFSET)                                                     \
                     : "x24", "x25", "x26");                                                      \
    }

GEN_BCOND(eq, eq);
GEN_BCOND(ne, ne);
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
                 "b.eq 31f\n\t"
                 "cmp x0, #16\n\t"
                 "b.hs 32f\n\t"
                 "adr x27, 10f\n\t"
                 "add x27, x27, x0, lsl #2\n\t"
                 "br x27\n\t"
                 "10:\n\t"
                 "b 11f\n\t"
                 "b 12f\n\t"
                 "b 13f\n\t"
                 "b 14f\n\t"
                 "b 15f\n\t"
                 "b 16f\n\t"
                 "b 17f\n\t"
                 "b 18f\n\t"
                 "b 19f\n\t"
                 "b 20f\n\t"
                 "b 21f\n\t"
                 "b 22f\n\t"
                 "b 23f\n\t"
                 "b 24f\n\t"
                 "b 25f\n\t"
                 "b 26f\n\t"
                 "11:\n\tmov x0, x1\n\tb 40f\n\t"
                 "12:\n\tmov x0, x2\n\tb 40f\n\t"
                 "13:\n\tmov x0, x3\n\tb 40f\n\t"
                 "14:\n\tmov x0, x4\n\tb 40f\n\t"
                 "15:\n\tmov x0, x5\n\tb 40f\n\t"
                 "16:\n\tmov x0, x6\n\tb 40f\n\t"
                 "17:\n\tmov x0, x7\n\tb 40f\n\t"
                 "18:\n\tmov x0, x8\n\tb 40f\n\t"
                 "19:\n\tmov x0, x9\n\tb 40f\n\t"
                 "20:\n\tmov x0, x10\n\tb 40f\n\t"
                 "21:\n\tmov x0, x11\n\tb 40f\n\t"
                 "22:\n\tmov x0, x12\n\tb 40f\n\t"
                 "23:\n\tmov x0, x13\n\tb 40f\n\t"
                 "24:\n\tmov x0, x14\n\tb 40f\n\t"
                 "25:\n\tmov x0, x15\n\tb 40f\n\t"
                 "26:\n\tmov x0, x16\n\tb 40f\n\t"
                 "31:\n\t"
                 "ldr x0, [x29, #264]\n\t"
                 "b 40f\n\t"
                 "32:\n\t"
                 "add x27, x29, #16\n\t"
                 "lsl x19, x0, #3\n\t"
                 "ldr x0, [x27, x19]\n\t"
                 "40:\n\t"
                 "cmp x26, #0\n\t"
                 "b.eq 41f\n\t"
                 "str x17, [x29, #256]\n\t"
                 "41:\n\t"
                 "str x0, [x29, #272]\n\t"
                 "mov x0, #0\n\t"
                 "b _tcti_exit_block\n\t");
}

tcti_gadget_t gadget_br = gadget_br_impl;

#define GEN_CBZ_TABLE(kind, mnemonic, reg_prefix, hostreg, idx)                                    \
    __attribute__((naked)) void gadget_##kind##_##idx##_impl(void)                                 \
    {                                                                                              \
        asm volatile("ldr x25, [x28], #8\n\t"                                                      \
                     "ldr x26, [x28], #8\n\t" mnemonic " " reg_prefix #hostreg ", 1f\n\t"         \
                     "mov x25, x26\n\t"                                                            \
                     "1:\n\t"                                                                      \
                     "str x25, [x29, %[pc_off]]\n\t"                                               \
                     "mov x0, #0\n\t"                                                              \
                     "b _tcti_exit_block\n\t"                                                      \
                     :                                                                             \
                     : [pc_off] "i"(PC_OFFSET));                                                   \
    }

GEN_CBZ_TABLE(cbz_wreg, "cbz", "w", 1, 0);
GEN_CBZ_TABLE(cbz_wreg, "cbz", "w", 2, 1);
GEN_CBZ_TABLE(cbz_wreg, "cbz", "w", 3, 2);
GEN_CBZ_TABLE(cbz_wreg, "cbz", "w", 4, 3);
GEN_CBZ_TABLE(cbz_wreg, "cbz", "w", 5, 4);
GEN_CBZ_TABLE(cbz_wreg, "cbz", "w", 6, 5);
GEN_CBZ_TABLE(cbz_wreg, "cbz", "w", 7, 6);
GEN_CBZ_TABLE(cbz_wreg, "cbz", "w", 8, 7);
GEN_CBZ_TABLE(cbz_wreg, "cbz", "w", 9, 8);
GEN_CBZ_TABLE(cbz_wreg, "cbz", "w", 10, 9);
GEN_CBZ_TABLE(cbz_wreg, "cbz", "w", 11, 10);
GEN_CBZ_TABLE(cbz_wreg, "cbz", "w", 12, 11);
GEN_CBZ_TABLE(cbz_wreg, "cbz", "w", 13, 12);
GEN_CBZ_TABLE(cbz_wreg, "cbz", "w", 14, 13);
GEN_CBZ_TABLE(cbz_wreg, "cbz", "w", 15, 14);
GEN_CBZ_TABLE(cbz_wreg, "cbz", "w", 16, 15);

GEN_CBZ_TABLE(cbnz_wreg, "cbnz", "w", 1, 0);
GEN_CBZ_TABLE(cbnz_wreg, "cbnz", "w", 2, 1);
GEN_CBZ_TABLE(cbnz_wreg, "cbnz", "w", 3, 2);
GEN_CBZ_TABLE(cbnz_wreg, "cbnz", "w", 4, 3);
GEN_CBZ_TABLE(cbnz_wreg, "cbnz", "w", 5, 4);
GEN_CBZ_TABLE(cbnz_wreg, "cbnz", "w", 6, 5);
GEN_CBZ_TABLE(cbnz_wreg, "cbnz", "w", 7, 6);
GEN_CBZ_TABLE(cbnz_wreg, "cbnz", "w", 8, 7);
GEN_CBZ_TABLE(cbnz_wreg, "cbnz", "w", 9, 8);
GEN_CBZ_TABLE(cbnz_wreg, "cbnz", "w", 10, 9);
GEN_CBZ_TABLE(cbnz_wreg, "cbnz", "w", 11, 10);
GEN_CBZ_TABLE(cbnz_wreg, "cbnz", "w", 12, 11);
GEN_CBZ_TABLE(cbnz_wreg, "cbnz", "w", 13, 12);
GEN_CBZ_TABLE(cbnz_wreg, "cbnz", "w", 14, 13);
GEN_CBZ_TABLE(cbnz_wreg, "cbnz", "w", 15, 14);
GEN_CBZ_TABLE(cbnz_wreg, "cbnz", "w", 16, 15);

GEN_CBZ_TABLE(cbz_xreg, "cbz", "x", 1, 0);
GEN_CBZ_TABLE(cbz_xreg, "cbz", "x", 2, 1);
GEN_CBZ_TABLE(cbz_xreg, "cbz", "x", 3, 2);
GEN_CBZ_TABLE(cbz_xreg, "cbz", "x", 4, 3);
GEN_CBZ_TABLE(cbz_xreg, "cbz", "x", 5, 4);
GEN_CBZ_TABLE(cbz_xreg, "cbz", "x", 6, 5);
GEN_CBZ_TABLE(cbz_xreg, "cbz", "x", 7, 6);
GEN_CBZ_TABLE(cbz_xreg, "cbz", "x", 8, 7);
GEN_CBZ_TABLE(cbz_xreg, "cbz", "x", 9, 8);
GEN_CBZ_TABLE(cbz_xreg, "cbz", "x", 10, 9);
GEN_CBZ_TABLE(cbz_xreg, "cbz", "x", 11, 10);
GEN_CBZ_TABLE(cbz_xreg, "cbz", "x", 12, 11);
GEN_CBZ_TABLE(cbz_xreg, "cbz", "x", 13, 12);
GEN_CBZ_TABLE(cbz_xreg, "cbz", "x", 14, 13);
GEN_CBZ_TABLE(cbz_xreg, "cbz", "x", 15, 14);
GEN_CBZ_TABLE(cbz_xreg, "cbz", "x", 16, 15);

GEN_CBZ_TABLE(cbnz_xreg, "cbnz", "x", 1, 0);
GEN_CBZ_TABLE(cbnz_xreg, "cbnz", "x", 2, 1);
GEN_CBZ_TABLE(cbnz_xreg, "cbnz", "x", 3, 2);
GEN_CBZ_TABLE(cbnz_xreg, "cbnz", "x", 4, 3);
GEN_CBZ_TABLE(cbnz_xreg, "cbnz", "x", 5, 4);
GEN_CBZ_TABLE(cbnz_xreg, "cbnz", "x", 6, 5);
GEN_CBZ_TABLE(cbnz_xreg, "cbnz", "x", 7, 6);
GEN_CBZ_TABLE(cbnz_xreg, "cbnz", "x", 8, 7);
GEN_CBZ_TABLE(cbnz_xreg, "cbnz", "x", 9, 8);
GEN_CBZ_TABLE(cbnz_xreg, "cbnz", "x", 10, 9);
GEN_CBZ_TABLE(cbnz_xreg, "cbnz", "x", 11, 10);
GEN_CBZ_TABLE(cbnz_xreg, "cbnz", "x", 12, 11);
GEN_CBZ_TABLE(cbnz_xreg, "cbnz", "x", 13, 12);
GEN_CBZ_TABLE(cbnz_xreg, "cbnz", "x", 14, 13);
GEN_CBZ_TABLE(cbnz_xreg, "cbnz", "x", 15, 14);
GEN_CBZ_TABLE(cbnz_xreg, "cbnz", "x", 16, 15);

const tcti_gadget_t gadget_cbz_wreg[16] = {
    gadget_cbz_wreg_0_impl,  gadget_cbz_wreg_1_impl,  gadget_cbz_wreg_2_impl,
    gadget_cbz_wreg_3_impl,  gadget_cbz_wreg_4_impl,  gadget_cbz_wreg_5_impl,
    gadget_cbz_wreg_6_impl,  gadget_cbz_wreg_7_impl,  gadget_cbz_wreg_8_impl,
    gadget_cbz_wreg_9_impl,  gadget_cbz_wreg_10_impl, gadget_cbz_wreg_11_impl,
    gadget_cbz_wreg_12_impl, gadget_cbz_wreg_13_impl, gadget_cbz_wreg_14_impl,
    gadget_cbz_wreg_15_impl,
};

const tcti_gadget_t gadget_cbnz_wreg[16] = {
    gadget_cbnz_wreg_0_impl,  gadget_cbnz_wreg_1_impl,  gadget_cbnz_wreg_2_impl,
    gadget_cbnz_wreg_3_impl,  gadget_cbnz_wreg_4_impl,  gadget_cbnz_wreg_5_impl,
    gadget_cbnz_wreg_6_impl,  gadget_cbnz_wreg_7_impl,  gadget_cbnz_wreg_8_impl,
    gadget_cbnz_wreg_9_impl,  gadget_cbnz_wreg_10_impl, gadget_cbnz_wreg_11_impl,
    gadget_cbnz_wreg_12_impl, gadget_cbnz_wreg_13_impl, gadget_cbnz_wreg_14_impl,
    gadget_cbnz_wreg_15_impl,
};

const tcti_gadget_t gadget_cbz_xreg[16] = {
    gadget_cbz_xreg_0_impl,  gadget_cbz_xreg_1_impl,  gadget_cbz_xreg_2_impl,
    gadget_cbz_xreg_3_impl,  gadget_cbz_xreg_4_impl,  gadget_cbz_xreg_5_impl,
    gadget_cbz_xreg_6_impl,  gadget_cbz_xreg_7_impl,  gadget_cbz_xreg_8_impl,
    gadget_cbz_xreg_9_impl,  gadget_cbz_xreg_10_impl, gadget_cbz_xreg_11_impl,
    gadget_cbz_xreg_12_impl, gadget_cbz_xreg_13_impl, gadget_cbz_xreg_14_impl,
    gadget_cbz_xreg_15_impl,
};

const tcti_gadget_t gadget_cbnz_xreg[16] = {
    gadget_cbnz_xreg_0_impl,  gadget_cbnz_xreg_1_impl,  gadget_cbnz_xreg_2_impl,
    gadget_cbnz_xreg_3_impl,  gadget_cbnz_xreg_4_impl,  gadget_cbnz_xreg_5_impl,
    gadget_cbnz_xreg_6_impl,  gadget_cbnz_xreg_7_impl,  gadget_cbnz_xreg_8_impl,
    gadget_cbnz_xreg_9_impl,  gadget_cbnz_xreg_10_impl, gadget_cbnz_xreg_11_impl,
    gadget_cbnz_xreg_12_impl, gadget_cbnz_xreg_13_impl, gadget_cbnz_xreg_14_impl,
    gadget_cbnz_xreg_15_impl,
};

const tcti_gadget_t gadget_cbz_reg[16] = {
    gadget_cbz_xreg_0_impl,  gadget_cbz_xreg_1_impl,  gadget_cbz_xreg_2_impl,
    gadget_cbz_xreg_3_impl,  gadget_cbz_xreg_4_impl,  gadget_cbz_xreg_5_impl,
    gadget_cbz_xreg_6_impl,  gadget_cbz_xreg_7_impl,  gadget_cbz_xreg_8_impl,
    gadget_cbz_xreg_9_impl,  gadget_cbz_xreg_10_impl, gadget_cbz_xreg_11_impl,
    gadget_cbz_xreg_12_impl, gadget_cbz_xreg_13_impl, gadget_cbz_xreg_14_impl,
    gadget_cbz_xreg_15_impl,
};

const tcti_gadget_t gadget_cbnz_reg[16] = {
    gadget_cbnz_xreg_0_impl,  gadget_cbnz_xreg_1_impl,  gadget_cbnz_xreg_2_impl,
    gadget_cbnz_xreg_3_impl,  gadget_cbnz_xreg_4_impl,  gadget_cbnz_xreg_5_impl,
    gadget_cbnz_xreg_6_impl,  gadget_cbnz_xreg_7_impl,  gadget_cbnz_xreg_8_impl,
    gadget_cbnz_xreg_9_impl,  gadget_cbnz_xreg_10_impl, gadget_cbnz_xreg_11_impl,
    gadget_cbnz_xreg_12_impl, gadget_cbnz_xreg_13_impl, gadget_cbnz_xreg_14_impl,
    gadget_cbnz_xreg_15_impl,
};

#define GEN_TBZ_TABLE(kind, mnemonic, reg_prefix, hostreg, idx)                                    \
    __attribute__((naked)) void gadget_##kind##_##idx##_impl(void)                                 \
    {                                                                                              \
        asm volatile("ldr x17, [x28], #8\n\t"                                                      \
                     "ldr x19, [x28], #8\n\t"                                                      \
                     "ldr x26, [x28], #8\n\t"                                                      \
                     "lsr " reg_prefix "27, " reg_prefix #hostreg ", " reg_prefix "17\n\t"       \
                     "and x27, x27, #1\n\t" mnemonic " x27, 1f\n\t"                                \
                     "mov x19, x26\n\t"                                                            \
                     "1:\n\t"                                                                      \
                     "str x19, [x29, %[pc_off]]\n\t"                                               \
                     "mov x0, #0\n\t"                                                              \
                     "b _tcti_exit_block\n\t"                                                      \
                     :                                                                             \
                     : [pc_off] "i"(PC_OFFSET));                                                   \
    }

GEN_TBZ_TABLE(tbz_wreg, "cbz", "w", 1, 0);
GEN_TBZ_TABLE(tbz_wreg, "cbz", "w", 2, 1);
GEN_TBZ_TABLE(tbz_wreg, "cbz", "w", 3, 2);
GEN_TBZ_TABLE(tbz_wreg, "cbz", "w", 4, 3);
GEN_TBZ_TABLE(tbz_wreg, "cbz", "w", 5, 4);
GEN_TBZ_TABLE(tbz_wreg, "cbz", "w", 6, 5);
GEN_TBZ_TABLE(tbz_wreg, "cbz", "w", 7, 6);
GEN_TBZ_TABLE(tbz_wreg, "cbz", "w", 8, 7);
GEN_TBZ_TABLE(tbz_wreg, "cbz", "w", 9, 8);
GEN_TBZ_TABLE(tbz_wreg, "cbz", "w", 10, 9);
GEN_TBZ_TABLE(tbz_wreg, "cbz", "w", 11, 10);
GEN_TBZ_TABLE(tbz_wreg, "cbz", "w", 12, 11);
GEN_TBZ_TABLE(tbz_wreg, "cbz", "w", 13, 12);
GEN_TBZ_TABLE(tbz_wreg, "cbz", "w", 14, 13);
GEN_TBZ_TABLE(tbz_wreg, "cbz", "w", 15, 14);
GEN_TBZ_TABLE(tbz_wreg, "cbz", "w", 16, 15);

GEN_TBZ_TABLE(tbnz_wreg, "cbnz", "w", 1, 0);
GEN_TBZ_TABLE(tbnz_wreg, "cbnz", "w", 2, 1);
GEN_TBZ_TABLE(tbnz_wreg, "cbnz", "w", 3, 2);
GEN_TBZ_TABLE(tbnz_wreg, "cbnz", "w", 4, 3);
GEN_TBZ_TABLE(tbnz_wreg, "cbnz", "w", 5, 4);
GEN_TBZ_TABLE(tbnz_wreg, "cbnz", "w", 6, 5);
GEN_TBZ_TABLE(tbnz_wreg, "cbnz", "w", 7, 6);
GEN_TBZ_TABLE(tbnz_wreg, "cbnz", "w", 8, 7);
GEN_TBZ_TABLE(tbnz_wreg, "cbnz", "w", 9, 8);
GEN_TBZ_TABLE(tbnz_wreg, "cbnz", "w", 10, 9);
GEN_TBZ_TABLE(tbnz_wreg, "cbnz", "w", 11, 10);
GEN_TBZ_TABLE(tbnz_wreg, "cbnz", "w", 12, 11);
GEN_TBZ_TABLE(tbnz_wreg, "cbnz", "w", 13, 12);
GEN_TBZ_TABLE(tbnz_wreg, "cbnz", "w", 14, 13);
GEN_TBZ_TABLE(tbnz_wreg, "cbnz", "w", 15, 14);
GEN_TBZ_TABLE(tbnz_wreg, "cbnz", "w", 16, 15);

GEN_TBZ_TABLE(tbz_xreg, "cbz", "x", 1, 0);
GEN_TBZ_TABLE(tbz_xreg, "cbz", "x", 2, 1);
GEN_TBZ_TABLE(tbz_xreg, "cbz", "x", 3, 2);
GEN_TBZ_TABLE(tbz_xreg, "cbz", "x", 4, 3);
GEN_TBZ_TABLE(tbz_xreg, "cbz", "x", 5, 4);
GEN_TBZ_TABLE(tbz_xreg, "cbz", "x", 6, 5);
GEN_TBZ_TABLE(tbz_xreg, "cbz", "x", 7, 6);
GEN_TBZ_TABLE(tbz_xreg, "cbz", "x", 8, 7);
GEN_TBZ_TABLE(tbz_xreg, "cbz", "x", 9, 8);
GEN_TBZ_TABLE(tbz_xreg, "cbz", "x", 10, 9);
GEN_TBZ_TABLE(tbz_xreg, "cbz", "x", 11, 10);
GEN_TBZ_TABLE(tbz_xreg, "cbz", "x", 12, 11);
GEN_TBZ_TABLE(tbz_xreg, "cbz", "x", 13, 12);
GEN_TBZ_TABLE(tbz_xreg, "cbz", "x", 14, 13);
GEN_TBZ_TABLE(tbz_xreg, "cbz", "x", 15, 14);
GEN_TBZ_TABLE(tbz_xreg, "cbz", "x", 16, 15);

GEN_TBZ_TABLE(tbnz_xreg, "cbnz", "x", 1, 0);
GEN_TBZ_TABLE(tbnz_xreg, "cbnz", "x", 2, 1);
GEN_TBZ_TABLE(tbnz_xreg, "cbnz", "x", 3, 2);
GEN_TBZ_TABLE(tbnz_xreg, "cbnz", "x", 4, 3);
GEN_TBZ_TABLE(tbnz_xreg, "cbnz", "x", 5, 4);
GEN_TBZ_TABLE(tbnz_xreg, "cbnz", "x", 6, 5);
GEN_TBZ_TABLE(tbnz_xreg, "cbnz", "x", 7, 6);
GEN_TBZ_TABLE(tbnz_xreg, "cbnz", "x", 8, 7);
GEN_TBZ_TABLE(tbnz_xreg, "cbnz", "x", 9, 8);
GEN_TBZ_TABLE(tbnz_xreg, "cbnz", "x", 10, 9);
GEN_TBZ_TABLE(tbnz_xreg, "cbnz", "x", 11, 10);
GEN_TBZ_TABLE(tbnz_xreg, "cbnz", "x", 12, 11);
GEN_TBZ_TABLE(tbnz_xreg, "cbnz", "x", 13, 12);
GEN_TBZ_TABLE(tbnz_xreg, "cbnz", "x", 14, 13);
GEN_TBZ_TABLE(tbnz_xreg, "cbnz", "x", 15, 14);
GEN_TBZ_TABLE(tbnz_xreg, "cbnz", "x", 16, 15);

const tcti_gadget_t gadget_tbz_wreg[16] = {
    gadget_tbz_wreg_0_impl,  gadget_tbz_wreg_1_impl,  gadget_tbz_wreg_2_impl,
    gadget_tbz_wreg_3_impl,  gadget_tbz_wreg_4_impl,  gadget_tbz_wreg_5_impl,
    gadget_tbz_wreg_6_impl,  gadget_tbz_wreg_7_impl,  gadget_tbz_wreg_8_impl,
    gadget_tbz_wreg_9_impl,  gadget_tbz_wreg_10_impl, gadget_tbz_wreg_11_impl,
    gadget_tbz_wreg_12_impl, gadget_tbz_wreg_13_impl, gadget_tbz_wreg_14_impl,
    gadget_tbz_wreg_15_impl,
};

const tcti_gadget_t gadget_tbnz_wreg[16] = {
    gadget_tbnz_wreg_0_impl,  gadget_tbnz_wreg_1_impl,  gadget_tbnz_wreg_2_impl,
    gadget_tbnz_wreg_3_impl,  gadget_tbnz_wreg_4_impl,  gadget_tbnz_wreg_5_impl,
    gadget_tbnz_wreg_6_impl,  gadget_tbnz_wreg_7_impl,  gadget_tbnz_wreg_8_impl,
    gadget_tbnz_wreg_9_impl,  gadget_tbnz_wreg_10_impl, gadget_tbnz_wreg_11_impl,
    gadget_tbnz_wreg_12_impl, gadget_tbnz_wreg_13_impl, gadget_tbnz_wreg_14_impl,
    gadget_tbnz_wreg_15_impl,
};

const tcti_gadget_t gadget_tbz_xreg[16] = {
    gadget_tbz_xreg_0_impl,  gadget_tbz_xreg_1_impl,  gadget_tbz_xreg_2_impl,
    gadget_tbz_xreg_3_impl,  gadget_tbz_xreg_4_impl,  gadget_tbz_xreg_5_impl,
    gadget_tbz_xreg_6_impl,  gadget_tbz_xreg_7_impl,  gadget_tbz_xreg_8_impl,
    gadget_tbz_xreg_9_impl,  gadget_tbz_xreg_10_impl, gadget_tbz_xreg_11_impl,
    gadget_tbz_xreg_12_impl, gadget_tbz_xreg_13_impl, gadget_tbz_xreg_14_impl,
    gadget_tbz_xreg_15_impl,
};

const tcti_gadget_t gadget_tbnz_xreg[16] = {
    gadget_tbnz_xreg_0_impl,  gadget_tbnz_xreg_1_impl,  gadget_tbnz_xreg_2_impl,
    gadget_tbnz_xreg_3_impl,  gadget_tbnz_xreg_4_impl,  gadget_tbnz_xreg_5_impl,
    gadget_tbnz_xreg_6_impl,  gadget_tbnz_xreg_7_impl,  gadget_tbnz_xreg_8_impl,
    gadget_tbnz_xreg_9_impl,  gadget_tbnz_xreg_10_impl, gadget_tbnz_xreg_11_impl,
    gadget_tbnz_xreg_12_impl, gadget_tbnz_xreg_13_impl, gadget_tbnz_xreg_14_impl,
    gadget_tbnz_xreg_15_impl,
};

const tcti_gadget_t gadget_tbz_reg[16] = {
    gadget_tbz_xreg_0_impl,  gadget_tbz_xreg_1_impl,  gadget_tbz_xreg_2_impl,
    gadget_tbz_xreg_3_impl,  gadget_tbz_xreg_4_impl,  gadget_tbz_xreg_5_impl,
    gadget_tbz_xreg_6_impl,  gadget_tbz_xreg_7_impl,  gadget_tbz_xreg_8_impl,
    gadget_tbz_xreg_9_impl,  gadget_tbz_xreg_10_impl, gadget_tbz_xreg_11_impl,
    gadget_tbz_xreg_12_impl, gadget_tbz_xreg_13_impl, gadget_tbz_xreg_14_impl,
    gadget_tbz_xreg_15_impl,
};

const tcti_gadget_t gadget_tbnz_reg[16] = {
    gadget_tbnz_xreg_0_impl,  gadget_tbnz_xreg_1_impl,  gadget_tbnz_xreg_2_impl,
    gadget_tbnz_xreg_3_impl,  gadget_tbnz_xreg_4_impl,  gadget_tbnz_xreg_5_impl,
    gadget_tbnz_xreg_6_impl,  gadget_tbnz_xreg_7_impl,  gadget_tbnz_xreg_8_impl,
    gadget_tbnz_xreg_9_impl,  gadget_tbnz_xreg_10_impl, gadget_tbnz_xreg_11_impl,
    gadget_tbnz_xreg_12_impl, gadget_tbnz_xreg_13_impl, gadget_tbnz_xreg_14_impl,
    gadget_tbnz_xreg_15_impl,
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
                 "cmp x19, #16\n\t"
                 "b.hs 1f\n\t"
                 "mov x26, x19\n\t"
                 "bl _tcti_sync_hot_reg_from_cpu\n\t"
                 "1:\n\t"
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
                 "cmp x19, #16\n\t"
                 "b.hs 1f\n\t"
                 "mov x26, x19\n\t"
                 "bl _tcti_sync_hot_reg_from_cpu\n\t"
                 "1:\n\t"
                 "ldr x27, [x28], #8\n\t"
                 "br x27\n\t");
}

tcti_gadget_t gadget_ubfm = gadget_ubfm_impl;

__attribute__((used)) static void tcti_write_reg_imm_helper(struct cpu_state *cpu, uint64_t rd,
                                                            uint64_t value, uint64_t is_64bit,
                                                            uint64_t rd_is_sp)
{
    if (!is_64bit)
        value = (uint32_t)value;
    if (rd == 31) {
        if (rd_is_sp)
            cpu->sp = value;
        return;
    }
    if (rd < 31) {
        cpu->x[rd] = value;
        if (rd == 21) {
            char ev[160];
            snprintf(ev, sizeof(ev), "tcti.dpimm.write_reg=rd:%llu,value:0x%llx,is64:%llu",
                     (unsigned long long)rd, (unsigned long long)value,
                     (unsigned long long)is_64bit);
            trace_record_event(TRACE_ORIGIN_EXEC, ev);
        }
    }
}

__attribute__((used)) static void tcti_addsub_imm_helper(struct cpu_state *cpu, uint64_t rd,
                                                            uint64_t rn, uint64_t imm,
                                                            uint64_t is_sub, uint64_t set_flags,
                                                            uint64_t is_64bit, uint64_t rd_is_sp,
                                                            uint64_t rn_is_sp)
{
    uint64_t mask = is_64bit ? UINT64_MAX : UINT32_MAX;
    uint64_t lhs = 0;
    if (rn == 31) {
        lhs = rn_is_sp ? cpu->sp : 0;
    } else if (rn < 31) {
        lhs = cpu->x[rn];
    }
    lhs &= mask;

    uint64_t rhs = imm & mask;
    uint64_t result = is_sub ? ((lhs - rhs) & mask) : ((lhs + rhs) & mask);

    if (set_flags) {
        uint64_t sign_bit = is_64bit ? (1ULL << 63) : (1ULL << 31);
        uint64_t nzcv = 0;
        if (result & sign_bit)
            nzcv |= 0x80000000ULL;
        if (result == 0)
            nzcv |= 0x40000000ULL;
        if (is_sub) {
            if (lhs >= rhs)
                nzcv |= 0x20000000ULL;
            if (((lhs ^ rhs) & (lhs ^ result) & sign_bit) != 0)
                nzcv |= 0x10000000ULL;
        } else {
            if (result < lhs)
                nzcv |= 0x20000000ULL;
            if (((~(lhs ^ rhs)) & (lhs ^ result) & sign_bit) != 0)
                nzcv |= 0x10000000ULL;
        }
        cpu->pstate = nzcv;
    }

    if (rd == 31) {
        if (rd_is_sp)
            cpu->sp = result;
        return;
    }
    if (rd < 31)
        cpu->x[rd] = result;
    if ((rd == 2 && rn == 21) || rd == 21) {
        char ev[192];
        snprintf(ev, sizeof(ev),
                 "tcti.dpimm.addsub=rd:%llu,rn:%llu,lhs:0x%llx,imm:0x%llx,result:0x%llx,is_sub:%llu",
                 (unsigned long long)rd, (unsigned long long)rn, (unsigned long long)lhs,
                 (unsigned long long)imm, (unsigned long long)result,
                 (unsigned long long)is_sub);
        trace_record_event(TRACE_ORIGIN_EXEC, ev);
    }
}

__attribute__((naked)) void gadget_write_reg_imm_impl(void)
{
    asm volatile("ldr x19, [x28], #8\n\t" // rd
                 "ldr x20, [x28], #8\n\t" // value
                 "ldr x21, [x28], #8\n\t" // is_64bit
                 "ldr x22, [x28], #8\n\t" // rd_is_sp
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
                 "mov x3, x21\n\t"
                 "mov x4, x22\n\t"
                 "bl _tcti_write_reg_imm_helper\n\t"
                 "bl _tcti_c_call_epilogue\n\t"
                 "ldp x1, x2, [x29, #16]\n\t"
                 "ldp x3, x4, [x29, #32]\n\t"
                 "ldp x5, x6, [x29, #48]\n\t"
                 "ldp x7, x8, [x29, #64]\n\t"
                 "ldp x9, x10, [x29, #80]\n\t"
                 "ldp x11, x12, [x29, #96]\n\t"
                 "ldp x13, x14, [x29, #112]\n\t"
                 "ldp x15, x16, [x29, #128]\n\t"
                 "cmp x19, #16\n\t"
                 "b.hs 1f\n\t"
                 "mov x26, x19\n\t"
                 "bl _tcti_sync_hot_reg_from_cpu\n\t"
                 "1:\n\t"
                 "ldr x27, [x28], #8\n\t"
                 "br x27\n\t");
}

tcti_gadget_t gadget_write_reg_imm = gadget_write_reg_imm_impl;

__attribute__((naked)) void gadget_addsub_imm_fallback_impl(void)
{
    asm volatile("ldr x19, [x28], #8\n\t" // rd
                 "ldr x20, [x28], #8\n\t" // rn
                 "ldr x21, [x28], #8\n\t" // imm
                 "ldr x22, [x28], #8\n\t" // is_sub
                 "ldr x23, [x28], #8\n\t" // set_flags
                 "ldr x24, [x28], #8\n\t" // is_64bit
                 "ldr x25, [x28], #8\n\t" // rd_is_sp
                 "ldr x26, [x28], #8\n\t" // rn_is_sp
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
                 "mov x3, x21\n\t"
                 "mov x4, x22\n\t"
                 "mov x5, x23\n\t"
                 "mov x6, x24\n\t"
                 "mov x7, x25\n\t"
                 "str x26, [sp, #-16]!\n\t"
                 "bl _tcti_addsub_imm_helper\n\t"
                 "add sp, sp, #16\n\t"
                 "bl _tcti_c_call_epilogue\n\t"
                 "ldp x1, x2, [x29, #16]\n\t"
                 "ldp x3, x4, [x29, #32]\n\t"
                 "ldp x5, x6, [x29, #48]\n\t"
                 "ldp x7, x8, [x29, #64]\n\t"
                 "ldp x9, x10, [x29, #80]\n\t"
                 "ldp x11, x12, [x29, #96]\n\t"
                 "ldp x13, x14, [x29, #112]\n\t"
                 "ldp x15, x16, [x29, #128]\n\t"
                 "cmp x19, #16\n\t"
                 "b.hs 1f\n\t"
                 "mov x26, x19\n\t"
                 "bl _tcti_sync_hot_reg_from_cpu\n\t"
                 "1:\n\t"
                 "cbz x23, 2f\n\t"
                 "ldr x17, [x29, #280]\n\t"
                 "msr nzcv, x17\n\t"
                 "2:\n\t"
                 "ldr x27, [x28], #8\n\t"
                 "br x27\n\t");
}

tcti_gadget_t gadget_addsub_imm_fallback = gadget_addsub_imm_fallback_impl;

__attribute__((used)) static void tcti_addsub_reg_helper(struct cpu_state *cpu, uint64_t rd,
                                                            uint64_t rn, uint64_t rm,
                                                            uint64_t shift_type,
                                                            uint64_t imm_shift, uint64_t is_sub,
                                                            uint64_t set_flags,
                                                            uint64_t is_64bit)
{
    uint64_t mask = is_64bit ? UINT64_MAX : UINT32_MAX;
    uint64_t lhs = tcti_read_reg_or_zr(cpu, (int)rn) & mask;
    uint64_t rhs = tcti_read_reg_or_zr(cpu, (int)rm) & mask;
    unsigned shift = (unsigned)(imm_shift & 0x3f);

    if (!is_64bit)
        shift &= 0x1f;

    switch (shift_type) {
    case A64_SHIFT_LSL:
        rhs = (rhs << shift) & mask;
        break;
    case A64_SHIFT_LSR:
        rhs = shift == 0 ? rhs : (rhs >> shift);
        break;
    case A64_SHIFT_ASR:
        if (is_64bit) {
            rhs = (uint64_t)(((int64_t)rhs) >> shift);
        } else {
            rhs = (uint32_t)(((int32_t)(uint32_t)rhs) >> shift);
        }
        rhs &= mask;
        break;
    default:
        return;
    }

    uint64_t result = is_sub ? ((lhs - rhs) & mask) : ((lhs + rhs) & mask);

    if (set_flags) {
        uint64_t sign_bit = is_64bit ? (1ULL << 63) : (1ULL << 31);
        uint64_t nzcv = 0;
        if (result & sign_bit)
            nzcv |= 0x80000000ULL;
        if (result == 0)
            nzcv |= 0x40000000ULL;
        if (is_sub) {
            if (lhs >= rhs)
                nzcv |= 0x20000000ULL;
            if (((lhs ^ rhs) & (lhs ^ result) & sign_bit) != 0)
                nzcv |= 0x10000000ULL;
        } else {
            if (result < lhs)
                nzcv |= 0x20000000ULL;
            if (((~(lhs ^ rhs)) & (lhs ^ result) & sign_bit) != 0)
                nzcv |= 0x10000000ULL;
        }
        cpu->pstate = nzcv;
    }

    tcti_write_reg_or_zr(cpu, (int)rd, result, is_64bit != 0);
}

__attribute__((naked)) void gadget_addsub_reg_fallback_impl(void)
{
    asm volatile("ldr x19, [x28], #8\n\t" // rd
                 "ldr x20, [x28], #8\n\t" // rn
                 "ldr x21, [x28], #8\n\t" // rm
                 "ldr x22, [x28], #8\n\t" // shift_type
                 "ldr x23, [x28], #8\n\t" // imm_shift
                 "ldr x24, [x28], #8\n\t" // is_sub
                 "ldr x25, [x28], #8\n\t" // set_flags
                 "ldr x26, [x28], #8\n\t" // is_64bit
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
                 "mov x3, x21\n\t"
                 "mov x4, x22\n\t"
                 "mov x5, x23\n\t"
                 "mov x6, x24\n\t"
                 "mov x7, x25\n\t"
                 "str x26, [sp, #-16]!\n\t"
                 "bl _tcti_addsub_reg_helper\n\t"
                 "add sp, sp, #16\n\t"
                 "bl _tcti_c_call_epilogue\n\t"
                 "ldp x1, x2, [x29, #16]\n\t"
                 "ldp x3, x4, [x29, #32]\n\t"
                 "ldp x5, x6, [x29, #48]\n\t"
                 "ldp x7, x8, [x29, #64]\n\t"
                 "ldp x9, x10, [x29, #80]\n\t"
                 "ldp x11, x12, [x29, #96]\n\t"
                 "ldp x13, x14, [x29, #112]\n\t"
                 "ldp x15, x16, [x29, #128]\n\t"
                 "cmp x19, #16\n\t"
                 "b.hs 1f\n\t"
                 "mov x26, x19\n\t"
                 "bl _tcti_sync_hot_reg_from_cpu\n\t"
                 "1:\n\t"
                 "cbz x25, 2f\n\t"
                 "ldr x17, [x29, #280]\n\t"
                 "msr nzcv, x17\n\t"
                 "2:\n\t"
                 "ldr x27, [x28], #8\n\t"
                 "br x27\n\t");
}

tcti_gadget_t gadget_addsub_reg_fallback = gadget_addsub_reg_fallback_impl;

__attribute__((used)) static void tcti_logical_imm_helper(struct cpu_state *cpu, uint64_t rd,
                                                            uint64_t rn, uint64_t imm,
                                                            uint64_t subtype, uint64_t set_flags,
                                                            uint64_t is_64bit)
{
    uint64_t mask = is_64bit ? UINT64_MAX : UINT32_MAX;
    uint64_t lhs = tcti_read_reg_or_zr(cpu, (int)rn) & mask;
    uint64_t rhs = imm & mask;
    uint64_t result;

    switch (subtype) {
    case 7:  // AND immediate
    case 10: // ANDS immediate
        result = lhs & rhs;
        break;
    case 8: // ORR immediate
        result = lhs | rhs;
        break;
    case 9: // EOR immediate
        result = lhs ^ rhs;
        break;
    default:
        return;
    }
    result &= mask;

    if (set_flags) {
        uint64_t sign_bit = is_64bit ? (1ULL << 63) : (1ULL << 31);
        uint64_t nzcv = 0;
        if (result & sign_bit)
            nzcv |= 0x80000000ULL;
        if (result == 0)
            nzcv |= 0x40000000ULL;
        cpu->pstate = nzcv;
    }

    tcti_write_reg_or_zr(cpu, (int)rd, result, is_64bit != 0);
}

__attribute__((naked)) void gadget_logical_imm_fallback_impl(void)
{
    asm volatile("ldr x19, [x28], #8\n\t" // rd
                 "ldr x20, [x28], #8\n\t" // rn
                 "ldr x21, [x28], #8\n\t" // imm
                 "ldr x22, [x28], #8\n\t" // subtype
                 "ldr x23, [x28], #8\n\t" // set_flags
                 "ldr x24, [x28], #8\n\t" // is_64bit
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
                 "mov x3, x21\n\t"
                 "mov x4, x22\n\t"
                 "mov x5, x23\n\t"
                 "mov x6, x24\n\t"
                 "bl _tcti_logical_imm_helper\n\t"
                 "bl _tcti_c_call_epilogue\n\t"
                 "ldp x1, x2, [x29, #16]\n\t"
                 "ldp x3, x4, [x29, #32]\n\t"
                 "ldp x5, x6, [x29, #48]\n\t"
                 "ldp x7, x8, [x29, #64]\n\t"
                 "ldp x9, x10, [x29, #80]\n\t"
                 "ldp x11, x12, [x29, #96]\n\t"
                 "ldp x13, x14, [x29, #112]\n\t"
                 "ldp x15, x16, [x29, #128]\n\t"
                 "cmp x19, #16\n\t"
                 "b.hs 1f\n\t"
                 "mov x26, x19\n\t"
                 "bl _tcti_sync_hot_reg_from_cpu\n\t"
                 "1:\n\t"
                 "cbz x23, 2f\n\t"
                 "ldr x17, [x29, #280]\n\t"
                 "msr nzcv, x17\n\t"
                 "2:\n\t"
                 "ldr x27, [x28], #8\n\t"
                 "br x27\n\t");
}

tcti_gadget_t gadget_logical_imm_fallback = gadget_logical_imm_fallback_impl;

__attribute__((used)) static void tcti_logical_reg_helper(struct cpu_state *cpu, uint64_t rd,
                                                             uint64_t rn, uint64_t rm,
                                                             uint64_t shift_type,
                                                             uint64_t imm_shift,
                                                             uint64_t subtype,
                                                             uint64_t set_flags,
                                                             uint64_t is_64bit)
{
    uint64_t mask = is_64bit ? UINT64_MAX : UINT32_MAX;
    uint64_t lhs = tcti_read_reg_or_zr(cpu, (int)rn) & mask;
    uint64_t rhs = tcti_read_reg_or_zr(cpu, (int)rm) & mask;
    unsigned shift = (unsigned)(imm_shift & 0x3f);

    if (!is_64bit)
        shift &= 0x1f;

    switch (shift_type) {
    case A64_SHIFT_LSL:
        rhs = (rhs << shift) & mask;
        break;
    case A64_SHIFT_LSR:
        rhs = shift == 0 ? rhs : (rhs >> shift);
        break;
    case A64_SHIFT_ASR:
        if (is_64bit) {
            rhs = (uint64_t)(((int64_t)rhs) >> shift);
        } else {
            rhs = (uint32_t)(((int32_t)(uint32_t)rhs) >> shift);
        }
        rhs &= mask;
        break;
    case A64_SHIFT_ROR:
        if (shift != 0) {
            unsigned width = is_64bit ? 64 : 32;
            rhs = ((rhs >> shift) | (rhs << (width - shift))) & mask;
        }
        break;
    default:
        return;
    }

    uint64_t result;
    switch (subtype) {
    case 0: // AND
    case 3: // ANDS
        result = lhs & rhs;
        break;
    case 1: // ORR
        result = lhs | rhs;
        break;
    case 2: // EOR
        result = lhs ^ rhs;
        break;
    case 4: // BIC
    case 7: // BICS
        result = lhs & ~rhs;
        break;
    case 5: // ORN
        result = lhs | ~rhs;
        break;
    case 6: // EON
        result = lhs ^ ~rhs;
        break;
    default:
        return;
    }
    result &= mask;

    if (set_flags) {
        uint64_t nzcv = 0;
        if (result & (is_64bit ? (1ULL << 63) : (1ULL << 31)))
            nzcv |= 0x80000000ULL;
        if (result == 0)
            nzcv |= 0x40000000ULL;
        cpu->pstate = nzcv;
    }

    tcti_write_reg_or_zr(cpu, (int)rd, result, is_64bit != 0);
}

__attribute__((naked)) void gadget_logical_reg_fallback_impl(void)
{
    asm volatile("ldr x19, [x28], #8\n\t" // rd
                 "ldr x20, [x28], #8\n\t" // rn
                 "ldr x21, [x28], #8\n\t" // rm
                 "ldr x22, [x28], #8\n\t" // shift_type
                 "ldr x23, [x28], #8\n\t" // imm_shift
                 "ldr x24, [x28], #8\n\t" // subtype
                 "ldr x25, [x28], #8\n\t" // set_flags
                 "ldr x26, [x28], #8\n\t" // is_64bit
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
                 "mov x3, x21\n\t"
                 "mov x4, x22\n\t"
                 "mov x5, x23\n\t"
                 "mov x6, x24\n\t"
                 "mov x7, x25\n\t"
                 "str x26, [sp, #-16]!\n\t"
                 "bl _tcti_logical_reg_helper\n\t"
                 "add sp, sp, #16\n\t"
                 "bl _tcti_c_call_epilogue\n\t"
                 "ldp x1, x2, [x29, #16]\n\t"
                 "ldp x3, x4, [x29, #32]\n\t"
                 "ldp x5, x6, [x29, #48]\n\t"
                 "ldp x7, x8, [x29, #64]\n\t"
                 "ldp x9, x10, [x29, #80]\n\t"
                 "ldp x11, x12, [x29, #96]\n\t"
                 "ldp x13, x14, [x29, #112]\n\t"
                 "ldp x15, x16, [x29, #128]\n\t"
                 "cmp x19, #16\n\t"
                 "b.hs 1f\n\t"
                 "mov x26, x19\n\t"
                 "bl _tcti_sync_hot_reg_from_cpu\n\t"
                 "1:\n\t"
                 "cbz x25, 2f\n\t"
                 "ldr x17, [x29, #280]\n\t"
                 "msr nzcv, x17\n\t"
                 "2:\n\t"
                 "ldr x27, [x28], #8\n\t"
                 "br x27\n\t");
}

tcti_gadget_t gadget_logical_reg_fallback = gadget_logical_reg_fallback_impl;

__attribute__((used)) static void tcti_multiply_add_helper(struct cpu_state *cpu, uint64_t rd,
                                                               uint64_t rn, uint64_t rm,
                                                               uint64_t ra, uint64_t subtype,
                                                               uint64_t is_64bit)
{
    uint64_t addend = tcti_read_reg_or_zr(cpu, (int)ra);
    uint64_t result;

    switch (subtype) {
    case A64_DP_REG_MADD:
    case A64_DP_REG_MSUB: {
        uint64_t mask = is_64bit ? UINT64_MAX : UINT32_MAX;
        uint64_t lhs = tcti_read_reg_or_zr(cpu, (int)rn) & mask;
        uint64_t rhs = tcti_read_reg_or_zr(cpu, (int)rm) & mask;
        uint64_t product = (lhs * rhs) & mask;
        addend &= mask;
        result = subtype == A64_DP_REG_MSUB ? ((addend - product) & mask)
                                            : ((addend + product) & mask);
        tcti_write_reg_or_zr(cpu, (int)rd, result, is_64bit != 0);
        return;
    }
    case A64_DP_REG_SMADDL:
    case A64_DP_REG_SMSUBL: {
        int64_t lhs = (int64_t)(int32_t)(uint32_t)tcti_read_reg_or_zr(cpu, (int)rn);
        int64_t rhs = (int64_t)(int32_t)(uint32_t)tcti_read_reg_or_zr(cpu, (int)rm);
        uint64_t product = (uint64_t)(lhs * rhs);
        result = subtype == A64_DP_REG_SMSUBL ? addend - product : addend + product;
        tcti_write_reg_or_zr(cpu, (int)rd, result, 1);
        return;
    }
    case A64_DP_REG_UMADDL:
    case A64_DP_REG_UMSUBL: {
        uint64_t lhs = (uint32_t)tcti_read_reg_or_zr(cpu, (int)rn);
        uint64_t rhs = (uint32_t)tcti_read_reg_or_zr(cpu, (int)rm);
        uint64_t product = lhs * rhs;
        result = subtype == A64_DP_REG_UMSUBL ? addend - product : addend + product;
        tcti_write_reg_or_zr(cpu, (int)rd, result, 1);
        return;
    }
    default:
        return;
    }
}

__attribute__((naked)) void gadget_multiply_add_fallback_impl(void)
{
    asm volatile("ldr x19, [x28], #8\n\t" // rd
                 "ldr x20, [x28], #8\n\t" // rn
                 "ldr x21, [x28], #8\n\t" // rm
                 "ldr x22, [x28], #8\n\t" // ra
                 "ldr x23, [x28], #8\n\t" // subtype
                 "ldr x24, [x28], #8\n\t" // is_64bit
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
                 "mov x3, x21\n\t"
                 "mov x4, x22\n\t"
                 "mov x5, x23\n\t"
                 "mov x6, x24\n\t"
                 "bl _tcti_multiply_add_helper\n\t"
                 "bl _tcti_c_call_epilogue\n\t"
                 "ldp x1, x2, [x29, #16]\n\t"
                 "ldp x3, x4, [x29, #32]\n\t"
                 "ldp x5, x6, [x29, #48]\n\t"
                 "ldp x7, x8, [x29, #64]\n\t"
                 "ldp x9, x10, [x29, #80]\n\t"
                 "ldp x11, x12, [x29, #96]\n\t"
                 "ldp x13, x14, [x29, #112]\n\t"
                 "ldp x15, x16, [x29, #128]\n\t"
                 "cmp x19, #16\n\t"
                 "b.hs 1f\n\t"
                 "mov x26, x19\n\t"
                 "bl _tcti_sync_hot_reg_from_cpu\n\t"
                 "1:\n\t"
                 "ldr x27, [x28], #8\n\t"
                 "br x27\n\t");
}

tcti_gadget_t gadget_multiply_add_fallback = gadget_multiply_add_fallback_impl;

__attribute__((used)) static void tcti_shift_reg_helper(struct cpu_state *cpu, uint64_t rd,
                                                          uint64_t rn, uint64_t rm,
                                                          uint64_t subtype, uint64_t is_64bit)
{
    uint64_t mask = is_64bit ? UINT64_MAX : UINT32_MAX;
    unsigned amount_mask = is_64bit ? 63 : 31;
    uint64_t lhs = tcti_read_reg_or_zr(cpu, (int)rn) & mask;
    unsigned amount = (unsigned)(tcti_read_reg_or_zr(cpu, (int)rm) & amount_mask);
    uint64_t result;

    switch (subtype) {
    case 16: // LSLV
        result = (lhs << amount) & mask;
        break;
    case 17: // LSRV
        result = lhs >> amount;
        break;
    case 18: // ASRV
        if (is_64bit) {
            result = (uint64_t)(((int64_t)lhs) >> amount);
        } else {
            result = (uint32_t)(((int32_t)(uint32_t)lhs) >> amount);
        }
        result &= mask;
        break;
    case 19: // RORV
        if (amount == 0) {
            result = lhs;
        } else if (is_64bit) {
            result = (lhs >> amount) | (lhs << (64 - amount));
        } else {
            uint32_t value = (uint32_t)lhs;
            result = (uint32_t)((value >> amount) | (value << (32 - amount)));
        }
        result &= mask;
        break;
    default:
        return;
    }

    tcti_write_reg_or_zr(cpu, (int)rd, result, is_64bit != 0);
}

__attribute__((naked)) void gadget_shift_reg_fallback_impl(void)
{
    asm volatile("ldr x19, [x28], #8\n\t" // rd
                 "ldr x20, [x28], #8\n\t" // rn
                 "ldr x21, [x28], #8\n\t" // rm
                 "ldr x22, [x28], #8\n\t" // subtype
                 "ldr x23, [x28], #8\n\t" // is_64bit
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
                 "mov x3, x21\n\t"
                 "mov x4, x22\n\t"
                 "mov x5, x23\n\t"
                 "bl _tcti_shift_reg_helper\n\t"
                 "bl _tcti_c_call_epilogue\n\t"
                 "ldp x1, x2, [x29, #16]\n\t"
                 "ldp x3, x4, [x29, #32]\n\t"
                 "ldp x5, x6, [x29, #48]\n\t"
                 "ldp x7, x8, [x29, #64]\n\t"
                 "ldp x9, x10, [x29, #80]\n\t"
                 "ldp x11, x12, [x29, #96]\n\t"
                 "ldp x13, x14, [x29, #112]\n\t"
                 "ldp x15, x16, [x29, #128]\n\t"
                 "cmp x19, #16\n\t"
                 "b.hs 1f\n\t"
                 "mov x26, x19\n\t"
                 "bl _tcti_sync_hot_reg_from_cpu\n\t"
                 "1:\n\t"
                 "ldr x27, [x28], #8\n\t"
                 "br x27\n\t");
}

tcti_gadget_t gadget_shift_reg_fallback = gadget_shift_reg_fallback_impl;

__attribute__((used)) static void tcti_div_helper(struct cpu_state *cpu, uint64_t rd,
                                                    uint64_t rn, uint64_t rm, uint64_t subtype,
                                                    uint64_t is_64bit)
{
    uint64_t mask = is_64bit ? UINT64_MAX : UINT32_MAX;
    uint64_t divisor = tcti_read_reg_or_zr(cpu, (int)rm) & mask;
    uint64_t result = 0;

    if (divisor != 0) {
        if (subtype == A64_DP_REG_UDIV) {
            uint64_t dividend = tcti_read_reg_or_zr(cpu, (int)rn) & mask;
            result = dividend / divisor;
        } else if (subtype == A64_DP_REG_SDIV) {
            if (is_64bit) {
                int64_t dividend = (int64_t)tcti_read_reg_or_zr(cpu, (int)rn);
                int64_t signed_divisor = (int64_t)divisor;
                if (dividend == INT64_MIN && signed_divisor == -1)
                    result = (uint64_t)INT64_MIN;
                else
                    result = (uint64_t)(dividend / signed_divisor);
            } else {
                int32_t dividend = (int32_t)(uint32_t)tcti_read_reg_or_zr(cpu, (int)rn);
                int32_t signed_divisor = (int32_t)(uint32_t)divisor;
                if (dividend == INT32_MIN && signed_divisor == -1)
                    result = (uint32_t)INT32_MIN;
                else
                    result = (uint32_t)(dividend / signed_divisor);
            }
        }
    }

    tcti_write_reg_or_zr(cpu, (int)rd, result & mask, is_64bit != 0);
}

__attribute__((naked)) void gadget_div_fallback_impl(void)
{
    asm volatile("ldr x19, [x28], #8\n\t" // rd
                 "ldr x20, [x28], #8\n\t" // rn
                 "ldr x21, [x28], #8\n\t" // rm
                 "ldr x22, [x28], #8\n\t" // subtype
                 "ldr x23, [x28], #8\n\t" // is_64bit
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
                 "mov x3, x21\n\t"
                 "mov x4, x22\n\t"
                 "mov x5, x23\n\t"
                 "bl _tcti_div_helper\n\t"
                 "bl _tcti_c_call_epilogue\n\t"
                 "ldp x1, x2, [x29, #16]\n\t"
                 "ldp x3, x4, [x29, #32]\n\t"
                 "ldp x5, x6, [x29, #48]\n\t"
                 "ldp x7, x8, [x29, #64]\n\t"
                 "ldp x9, x10, [x29, #80]\n\t"
                 "ldp x11, x12, [x29, #96]\n\t"
                 "ldp x13, x14, [x29, #112]\n\t"
                 "ldp x15, x16, [x29, #128]\n\t"
                 "cmp x19, #16\n\t"
                 "b.hs 1f\n\t"
                 "mov x26, x19\n\t"
                 "bl _tcti_sync_hot_reg_from_cpu\n\t"
                 "1:\n\t"
                 "ldr x27, [x28], #8\n\t"
                 "br x27\n\t");
}

tcti_gadget_t gadget_div_fallback = gadget_div_fallback_impl;

static int tcti_cond_holds(uint64_t nzcv, uint64_t cond)
{
    int n = (nzcv >> 31) & 1;
    int z = (nzcv >> 30) & 1;
    int c = (nzcv >> 29) & 1;
    int v = (nzcv >> 28) & 1;

    switch (cond & 0xf) {
    case 0x0:
        return z;
    case 0x1:
        return !z;
    case 0x2:
        return c;
    case 0x3:
        return !c;
    case 0x4:
        return n;
    case 0x5:
        return !n;
    case 0x6:
        return v;
    case 0x7:
        return !v;
    case 0x8:
        return c && !z;
    case 0x9:
        return !c || z;
    case 0xa:
        return n == v;
    case 0xb:
        return n != v;
    case 0xc:
        return !z && (n == v);
    case 0xd:
        return z || (n != v);
    case 0xe:
    case 0xf:
        return 1;
    default:
        return 0;
    }
}

__attribute__((used)) static void tcti_csel_helper(struct cpu_state *cpu, uint64_t rd,
                                                       uint64_t rn, uint64_t rm, uint64_t cond,
                                                       uint64_t subtype, uint64_t is_64bit)
{
    uint64_t true_value = tcti_read_reg_or_zr(cpu, (int)rn);
    uint64_t false_value = tcti_read_reg_or_zr(cpu, (int)rm);
    uint64_t width_mask = is_64bit ? UINT64_MAX : UINT32_MAX;

    true_value &= width_mask;
    false_value &= width_mask;

    switch (subtype) {
    case 0: // CSEL
        break;
    case 1: // CSINC
        false_value = (false_value + 1) & width_mask;
        break;
    case 2: // CSINV
        false_value = (~false_value) & width_mask;
        break;
    case 3: // CSNEG
        false_value = (uint64_t)(-(int64_t)false_value) & width_mask;
        break;
    default:
        return;
    }

    uint64_t value = tcti_cond_holds(cpu->pstate, cond) ? true_value : false_value;
    tcti_write_reg_or_zr(cpu, (int)rd, value, is_64bit != 0);
}

__attribute__((used)) static void tcti_bcond_helper(struct cpu_state *cpu, uint64_t cond,
                                                        uint64_t target_pc,
                                                        uint64_t fallthrough_pc)
{
    cpu->pc = tcti_cond_holds(cpu->pstate, cond) ? target_pc : fallthrough_pc;
}

static uint64_t tcti_addsub_nzcv(uint64_t lhs, uint64_t rhs, uint64_t is_sub, uint64_t is_64bit)
{
    uint64_t mask = is_64bit ? UINT64_MAX : UINT32_MAX;
    uint64_t sign_bit = is_64bit ? (1ULL << 63) : (1ULL << 31);
    uint64_t result = is_sub ? ((lhs - rhs) & mask) : ((lhs + rhs) & mask);
    uint64_t nzcv = 0;

    lhs &= mask;
    rhs &= mask;
    if (result & sign_bit)
        nzcv |= 0x80000000ULL;
    if (result == 0)
        nzcv |= 0x40000000ULL;
    if (is_sub) {
        if (lhs >= rhs)
            nzcv |= 0x20000000ULL;
        if (((lhs ^ rhs) & (lhs ^ result) & sign_bit) != 0)
            nzcv |= 0x10000000ULL;
    } else {
        if (result < lhs)
            nzcv |= 0x20000000ULL;
        if (((~(lhs ^ rhs)) & (lhs ^ result) & sign_bit) != 0)
            nzcv |= 0x10000000ULL;
    }
    return nzcv;
}

__attribute__((used)) static void tcti_ccmp_helper(struct cpu_state *cpu, uint64_t rn,
                                                       uint64_t rm, uint64_t imm_operand,
                                                       uint64_t cond, uint64_t nzcv,
                                                       uint64_t subtype, uint64_t is_64bit)
{
    uint64_t mask = is_64bit ? UINT64_MAX : UINT32_MAX;
    uint64_t next_nzcv;

    if (tcti_cond_holds(cpu->pstate, cond)) {
        uint64_t lhs = tcti_read_reg_or_zr(cpu, (int)rn) & mask;
        uint64_t rhs = (subtype == A64_DP_REG_CCMN_IMM || subtype == A64_DP_REG_CCMP_IMM)
                           ? (imm_operand & mask)
                           : (tcti_read_reg_or_zr(cpu, (int)rm) & mask);
        uint64_t is_sub = (subtype == A64_DP_REG_CCMP || subtype == A64_DP_REG_CCMP_IMM);
        next_nzcv = tcti_addsub_nzcv(lhs, rhs, is_sub, is_64bit);
    } else {
        next_nzcv = (nzcv & 0xf) << 28;
    }

    cpu->pstate = next_nzcv;
}

__attribute__((naked)) void gadget_bcond_fallback_impl(void)
{
    asm volatile("ldr x19, [x28], #8\n\t" // cond
                 "ldr x20, [x28], #8\n\t" // target_pc
                 "ldr x21, [x28], #8\n\t" // fallthrough_pc
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
                 "mov x3, x21\n\t"
                 "bl _tcti_bcond_helper\n\t"
                 "bl _tcti_c_call_epilogue\n\t"
                 "ldp x1, x2, [x29, #16]\n\t"
                 "ldp x3, x4, [x29, #32]\n\t"
                 "ldp x5, x6, [x29, #48]\n\t"
                 "ldp x7, x8, [x29, #64]\n\t"
                 "ldp x9, x10, [x29, #80]\n\t"
                 "ldp x11, x12, [x29, #96]\n\t"
                 "ldp x13, x14, [x29, #112]\n\t"
                 "ldp x15, x16, [x29, #128]\n\t"
                 "mov x0, #0\n\t"
                 "b _tcti_exit_block\n\t");
}

tcti_gadget_t gadget_bcond_fallback = gadget_bcond_fallback_impl;

__attribute__((naked)) void gadget_ccmp_fallback_impl(void)
{
    asm volatile("ldr x19, [x28], #8\n\t" // rn
                 "ldr x20, [x28], #8\n\t" // rm
                 "ldr x21, [x28], #8\n\t" // imm_operand
                 "ldr x22, [x28], #8\n\t" // cond
                 "ldr x23, [x28], #8\n\t" // nzcv
                 "ldr x24, [x28], #8\n\t" // subtype
                 "ldr x25, [x28], #8\n\t" // is_64bit
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
                 "mov x3, x21\n\t"
                 "mov x4, x22\n\t"
                 "mov x5, x23\n\t"
                 "mov x6, x24\n\t"
                 "mov x7, x25\n\t"
                 "bl _tcti_ccmp_helper\n\t"
                 "bl _tcti_c_call_epilogue\n\t"
                 "ldr x17, [x29, #280]\n\t"
                 "msr nzcv, x17\n\t"
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

tcti_gadget_t gadget_ccmp_fallback = gadget_ccmp_fallback_impl;

__attribute__((naked)) void gadget_csel_fallback_impl(void)
{
    asm volatile("ldr x19, [x28], #8\n\t" // rd
                 "ldr x20, [x28], #8\n\t" // rn
                 "ldr x21, [x28], #8\n\t" // rm
                 "ldr x22, [x28], #8\n\t" // cond
                 "ldr x23, [x28], #8\n\t" // subtype
                 "ldr x24, [x28], #8\n\t" // is_64bit
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
                 "mov x3, x21\n\t"
                 "mov x4, x22\n\t"
                 "mov x5, x23\n\t"
                 "mov x6, x24\n\t"
                 "bl _tcti_csel_helper\n\t"
                 "bl _tcti_c_call_epilogue\n\t"
                 "ldp x1, x2, [x29, #16]\n\t"
                 "ldp x3, x4, [x29, #32]\n\t"
                 "ldp x5, x6, [x29, #48]\n\t"
                 "ldp x7, x8, [x29, #64]\n\t"
                 "ldp x9, x10, [x29, #80]\n\t"
                 "ldp x11, x12, [x29, #96]\n\t"
                 "ldp x13, x14, [x29, #112]\n\t"
                 "ldp x15, x16, [x29, #128]\n\t"
                 "cmp x19, #16\n\t"
                 "b.hs 1f\n\t"
                 "mov x26, x19\n\t"
                 "bl _tcti_sync_hot_reg_from_cpu\n\t"
                 "1:\n\t"
                 "ldr x27, [x28], #8\n\t"
                 "br x27\n\t");
}

tcti_gadget_t gadget_csel_fallback = gadget_csel_fallback_impl;

__attribute__((naked)) void gadget_movk_impl(void)
{
    asm volatile("ldr x20, [x28], #8\n\t" // Rd
                 "ldr x21, [x28], #8\n\t" // imm16
                 "ldr x22, [x28], #8\n\t" // shift
                 "ldr x23, [x28], #8\n\t" // is_64bit
                 "cmp x20, #31\n\t"
                 "b.eq 99f\n\t"
                 "cmp x20, #16\n\t"
                 "b.hs 30f\n\t"
                 "adr x27, 1f\n\t"
                 "add x27, x27, x20, lsl #2\n\t"
                 "br x27\n\t"
                 "1:\n\t"
                 "b 10f\n\t"
                 "b 11f\n\t"
                 "b 12f\n\t"
                 "b 13f\n\t"
                 "b 14f\n\t"
                 "b 15f\n\t"
                 "b 16f\n\t"
                 "b 17f\n\t"
                 "b 18f\n\t"
                 "b 19f\n\t"
                 "b 20f\n\t"
                 "b 21f\n\t"
                 "b 22f\n\t"
                 "b 23f\n\t"
                 "b 24f\n\t"
                 "b 25f\n\t"
                 "10:\n\tmov x0, x1\n\tb 40f\n\t"
                 "11:\n\tmov x0, x2\n\tb 40f\n\t"
                 "12:\n\tmov x0, x3\n\tb 40f\n\t"
                 "13:\n\tmov x0, x4\n\tb 40f\n\t"
                 "14:\n\tmov x0, x5\n\tb 40f\n\t"
                 "15:\n\tmov x0, x6\n\tb 40f\n\t"
                 "16:\n\tmov x0, x7\n\tb 40f\n\t"
                 "17:\n\tmov x0, x8\n\tb 40f\n\t"
                 "18:\n\tmov x0, x9\n\tb 40f\n\t"
                 "19:\n\tmov x0, x10\n\tb 40f\n\t"
                 "20:\n\tmov x0, x11\n\tb 40f\n\t"
                 "21:\n\tmov x0, x12\n\tb 40f\n\t"
                 "22:\n\tmov x0, x13\n\tb 40f\n\t"
                 "23:\n\tmov x0, x14\n\tb 40f\n\t"
                 "24:\n\tmov x0, x15\n\tb 40f\n\t"
                 "25:\n\tmov x0, x16\n\tb 40f\n\t"
                 "30:\n\t"
                 "add x27, x29, #16\n\t"
                 "ldr x0, [x27, x20, lsl #3]\n\t"
                 "40:\n\t"
                 "mov x24, #0xffff\n\t"
                 "lsl x24, x24, x22\n\t"
                 "bic x0, x0, x24\n\t"
                 "lsl x21, x21, x22\n\t"
                 "orr x0, x0, x21\n\t"
                 "cbnz x23, 41f\n\t"
                 "mov w0, w0\n\t"
                 "41:\n\t"
                 "cmp x20, #16\n\t"
                 "b.hs 70f\n\t"
                 "adr x27, 50f\n\t"
                 "add x27, x27, x20, lsl #2\n\t"
                 "br x27\n\t"
                 "50:\n\t"
                 "b 51f\n\t"
                 "b 52f\n\t"
                 "b 53f\n\t"
                 "b 54f\n\t"
                 "b 55f\n\t"
                 "b 56f\n\t"
                 "b 57f\n\t"
                 "b 58f\n\t"
                 "b 59f\n\t"
                 "b 60f\n\t"
                 "b 61f\n\t"
                 "b 62f\n\t"
                 "b 63f\n\t"
                 "b 64f\n\t"
                 "b 65f\n\t"
                 "b 66f\n\t"
                 "51:\n\tmov x1, x0\n\tb 99f\n\t"
                 "52:\n\tmov x2, x0\n\tb 99f\n\t"
                 "53:\n\tmov x3, x0\n\tb 99f\n\t"
                 "54:\n\tmov x4, x0\n\tb 99f\n\t"
                 "55:\n\tmov x5, x0\n\tb 99f\n\t"
                 "56:\n\tmov x6, x0\n\tb 99f\n\t"
                 "57:\n\tmov x7, x0\n\tb 99f\n\t"
                 "58:\n\tmov x8, x0\n\tb 99f\n\t"
                 "59:\n\tmov x9, x0\n\tb 99f\n\t"
                 "60:\n\tmov x10, x0\n\tb 99f\n\t"
                 "61:\n\tmov x11, x0\n\tb 99f\n\t"
                 "62:\n\tmov x12, x0\n\tb 99f\n\t"
                 "63:\n\tmov x13, x0\n\tb 99f\n\t"
                 "64:\n\tmov x14, x0\n\tb 99f\n\t"
                 "65:\n\tmov x15, x0\n\tb 99f\n\t"
                 "66:\n\tmov x16, x0\n\tb 99f\n\t"
                 "70:\n\t"
                 "add x27, x29, #16\n\t"
                 "str x0, [x27, x20, lsl #3]\n\t"
                 "99:\n\t"
                 "ldr x27, [x28], #8\n\t"
                 "br x27\n\t");
}

tcti_gadget_t gadget_movk = gadget_movk_impl;

void tcti_trace_resume_after_ldst(uint64_t fault_pc, uint64_t rt, uint64_t rn, uint64_t x3)
{
    if (fault_pc == 0x6a990ULL) {
        char ev[224];
        snprintf(ev, sizeof(ev),
                 "task.proof.6a990.gadget_resume=rt:%llu,rn:%llu,host_x3:0x%llx",
                 (unsigned long long)rt, (unsigned long long)rn, (unsigned long long)x3);
        trace_record_event(TRACE_ORIGIN_EXEC, ev);
    }
}

void tcti_trace_branch_target(uint64_t target, uint64_t is_link, uint64_t ret_pc, uint64_t guest_x0)
{
    if ((target >= 0x6a990ULL && target <= 0x6aa00ULL) ||
        (target >= 0x3e300ULL && target <= 0x3e380ULL)) {
        char ev[224];
        snprintf(ev, sizeof(ev),
                 "task.proof.branch=target:0x%llx,is_link:%llu,ret_pc:0x%llx,guest_x0:0x%llx",
                 (unsigned long long)target, (unsigned long long)is_link,
                 (unsigned long long)ret_pc, (unsigned long long)guest_x0);
        trace_record_event(TRACE_ORIGIN_EXEC, ev);
    }
}

__attribute__((naked)) void tcti_sync_hot_reg_from_cpu(void)
{
    asm volatile("cmp x26, #16\n\t"
                 "b.hs 99f\n\t"
                 "add x27, x29, #16\n\t"
                 "ldr x17, [x27, x26, lsl #3]\n\t"
                 "adr x27, 1f\n\t"
                 "add x27, x27, x26, lsl #2\n\t"
                 "br x27\n\t"
                 "1:\n\t"
                 "b 10f\n\t"
                 "b 11f\n\t"
                 "b 12f\n\t"
                 "b 13f\n\t"
                 "b 14f\n\t"
                 "b 15f\n\t"
                 "b 16f\n\t"
                 "b 17f\n\t"
                 "b 18f\n\t"
                 "b 19f\n\t"
                 "b 20f\n\t"
                 "b 21f\n\t"
                 "b 22f\n\t"
                 "b 23f\n\t"
                 "b 24f\n\t"
                 "b 25f\n\t"
                 "10:\n\tmov x1, x17\n\tret\n\t"
                 "11:\n\tmov x2, x17\n\tret\n\t"
                 "12:\n\tmov x3, x17\n\tret\n\t"
                 "13:\n\tmov x4, x17\n\tret\n\t"
                 "14:\n\tmov x5, x17\n\tret\n\t"
                 "15:\n\tmov x6, x17\n\tret\n\t"
                 "16:\n\tmov x7, x17\n\tret\n\t"
                 "17:\n\tmov x8, x17\n\tret\n\t"
                 "18:\n\tmov x9, x17\n\tret\n\t"
                 "19:\n\tmov x10, x17\n\tret\n\t"
                 "20:\n\tmov x11, x17\n\tret\n\t"
                 "21:\n\tmov x12, x17\n\tret\n\t"
                 "22:\n\tmov x13, x17\n\tret\n\t"
                 "23:\n\tmov x14, x17\n\tret\n\t"
                 "24:\n\tmov x15, x17\n\tret\n\t"
                 "25:\n\tmov x16, x17\n\tret\n\t"
                 "99:\n\tret\n\t");
}

__attribute__((naked)) void gadget_ldr_x_impl(void)
{
    asm volatile(
        // =========================================================================
        // GADGET ENTRY TRACING - Spill-First Boundary Instrumentation
        // =========================================================================
        // Capture exact values at gadget entry for runtime analysis of:
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

        "mrs x17, nzcv\n\t"
        "str x17, [x29, %[pstate_off]]\n\t"

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
        // entries is at offset 32 in struct tlb.
        "add x0, x26, #32\n\t"      // x0 = &tlb->entries[0]
        "mov x26, #32\n\t"          // x26 = sizeof(tlb_entry)
        "madd x0, x27, x26, x0\n\t" // x0 = &tlb->entries[index]
        "ldr x27, [x0]\n\t"         // x27 = entry.page

        // Compare page (clear lower 12 bits via shift)
        "lsr x26, x17, #12\n\t" // x26 = addr >> 12
        "lsl x26, x26, #12\n\t" // x26 = (addr >> 12) << 12 = page base
        "cmp x27, x26\n\t"
        "b.ne 97f\n\t" // Branch to tlbmiss counter
        "ldr x27, [x0, %[tlb_entry_generation_off]]\n\t"
        "ldr x26, [x29, %[cpu_tlb_off]]\n\t"
        "ldr x26, [x26, %[tlb_mmu_off]]\n\t"
        "ldr x26, [x26, %[mmu_generation_off]]\n\t"
        "cmp x27, x26\n\t"
        "b.ne 97f\n\t" // Stale TLB entry: fall back through tlb_handle_miss.

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
        "ldr x17, [x29, %[pstate_off]]\n\t"
        "msr nzcv, x17\n\t"
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
        "ldr x17, [x29, %[pstate_off]]\n\t"
        "msr nzcv, x17\n\t"
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
        "cmp x20, #16\n\t"
        "b.hs 160f\n\t"
        "mov x26, x20\n\t"
        "bl _tcti_sync_hot_reg_from_cpu\n\t"
        "160:\n\t"
        "cmp x24, #0\n\t"
        "b.eq 161f\n\t"
        "cmp x21, #16\n\t"
        "b.hs 161f\n\t"
        "mov x26, x21\n\t"
        "bl _tcti_sync_hot_reg_from_cpu\n\t"
        "161:\n\t"
        "movz x26, #0xa990\n\t"
        "movk x26, #0x6, lsl #16\n\t"
        "cmp x19, x26\n\t"
        "b.ne 162f\n\t"
        "bl _tcti_c_call_prologue\n\t"
        "mov x0, x19\n\t"
        "mov x1, x20\n\t"
        "mov x2, x21\n\t"
        "mov x3, x4\n\t"
        "bl _tcti_trace_resume_after_ldst\n\t"
        "bl _tcti_c_call_epilogue\n\t"
        "162:\n\t"
        "ldr x17, [x29, %[pstate_off]]\n\t"
        "msr nzcv, x17\n\t"
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t"
        "1:\n\t"
        "ldr x17, [x29, %[pstate_off]]\n\t"
        "msr nzcv, x17\n\t"
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
          [ldr_fallback_notlb_off] "i"(STAT_LDR_FALLBACK_NOTLB_OFFSET),
          [tlb_mmu_off] "i"(TLB_MMU_OFFSET),
          [tlb_entry_generation_off] "i"(TLB_ENTRY_GENERATION_OFFSET),
          [mmu_generation_off] "i"(MMU_GENERATION_OFFSET),
          [pstate_off] "i"(PSTATE_OFFSET)
        : "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x19", "x20", "x21", "x22", "x23", "x24",
          "x25", "x26", "x27", "memory");
}

tcti_gadget_t gadget_ldr_x = gadget_ldr_x_impl;

__attribute__((naked)) void gadget_str_x_impl(void)
{
    asm volatile(
        "mrs x17, nzcv\n\t"
        "str x17, [x29, %[pstate_off]]\n\t"

        // Load parameters from bytecode
        "ldr x19, [x28], #8\n\t" // fault_pc
        "ldr x20, [x28], #8\n\t" // Rt (source reg)
        "ldr x21, [x28], #8\n\t" // Rn (base reg)
        "ldr x22, [x28], #8\n\t" // immediate offset
        "ldr x23, [x28], #8\n\t" // size
        "ldr x24, [x28], #8\n\t" // idx_mode
        "ldr x25, [x28], #8\n\t" // meta

        // Fast path supports aligned 64-bit offset stores with hot base registers.
        // Writeback forms stay on the helper path so architectural base updates and
        // host fault handling remain centralized.
        "cmp x21, #16\n\t" // Is Rn hot (0-15)?
        "b.hs 91f\n\t"
        "cmp x23, #3\n\t" // Is size 64-bit?
        "b.ne 92f\n\t"
        "cmp x24, #0\n\t" // Offset addressing only.
        "b.ne 93f\n\t"
        "cmp x25, #0\n\t" // No register offset / extension metadata.
        "b.ne 94f\n\t"
        "cmp x20, #16\n\t" // Rt hot is supported.
        "b.lo 60f\n\t"
        "cmp x20, #31\n\t" // XZR zero stores are supported.
        "b.ne 91f\n\t"

        "60:\n\t"

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

        "add x17, x17, x22\n\t"

        // Check alignment: addr & 7 == 0
        "tst x17, #7\n\t"
        "b.ne 95f\n\t" // Branch to align counter

        // Check cross-page: (addr & 0xFFF) <= 0xFF8
        "and x0, x17, #0xFFF\n\t"
        "cmp x0, #0xFF8\n\t"
        "b.hi 96f\n\t" // Branch to crosspg counter

        "b 150f\n\t"

        // Get source register value (Rt, hot, in x1-x16) using computed goto
        // x20 still holds original Rt (0-15)
        "119:\n\t"
        "cmp x20, #31\n\t"
        "b.eq 146f\n\t"
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
        "130:\n\tmov x0, x1\n\tb 151f\n\t"
        "131:\n\tmov x0, x2\n\tb 151f\n\t"
        "132:\n\tmov x0, x3\n\tb 151f\n\t"
        "133:\n\tmov x0, x4\n\tb 151f\n\t"
        "134:\n\tmov x0, x5\n\tb 151f\n\t"
        "135:\n\tmov x0, x6\n\tb 151f\n\t"
        "136:\n\tmov x0, x7\n\tb 151f\n\t"
        "137:\n\tmov x0, x8\n\tb 151f\n\t"
        "138:\n\tmov x0, x9\n\tb 151f\n\t"
        "139:\n\tmov x0, x10\n\tb 151f\n\t"
        "140:\n\tmov x0, x11\n\tb 151f\n\t"
        "141:\n\tmov x0, x12\n\tb 151f\n\t"
        "142:\n\tmov x0, x13\n\tb 151f\n\t"
        "143:\n\tmov x0, x14\n\tb 151f\n\t"
        "144:\n\tmov x0, x15\n\tb 151f\n\t"
        "145:\n\tmov x0, x16\n\tb 151f\n\t"
        "146:\n\tmov x0, xzr\n\tb 151f\n\t"

        // Continue after address validation.
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
        // entries is at offset 32 in struct tlb.
        "add x0, x26, #32\n\t"      // x0 = &tlb->entries[0]
        "mov x26, #32\n\t"          // x26 = sizeof(tlb_entry)
        "madd x0, x27, x26, x0\n\t" // x0 = &tlb->entries[index]
        "ldr x27, [x0, #8]\n\t"     // x27 = entry.page_if_writable

        // Compare page (clear lower 12 bits via shift)
        "lsr x26, x17, #12\n\t" // x26 = addr >> 12
        "lsl x26, x26, #12\n\t" // x26 = (addr >> 12) << 12 = page base
        "cmp x27, x26\n\t"
        "b.ne 97f\n\t" // Branch to tlbmiss counter
        "ldr x27, [x0, %[tlb_entry_generation_off]]\n\t"
        "ldr x26, [x29, %[cpu_tlb_off]]\n\t"
        "ldr x26, [x26, %[tlb_mmu_off]]\n\t"
        "ldr x26, [x26, %[mmu_generation_off]]\n\t"
        "cmp x27, x26\n\t"
        "b.ne 97f\n\t" // Stale TLB entry: fall back through tlb_handle_miss.

        // Compute host address and store
        // data_minus_addr is at offset 16 in tlb_entry
        "ldr x27, [x0, #16]\n\t" // x27 = entry.data_minus_addr
        "add x17, x27, x17\n\t"  // x17 = host address
        "b 119b\n\t"             // Load source value after TLB scratch use.
        "151:\n\t"
        "str x0, [x17]\n\t"      // store value from x0
        "cmp x24, #1\n\t"
        "b.ne 152f\n\t"
        "sub x17, x17, x27\n\t"  // Recover guest address from host address.
        "add x17, x17, x22\n\t"  // Post-index writeback value.
        "adr x26, 170f\n\t"
        "add x26, x26, x21, lsl #2\n\t"
        "br x26\n\t"
        "170:\n\t"
        "b 180f\n\t"
        "b 181f\n\t"
        "b 182f\n\t"
        "b 183f\n\t"
        "b 184f\n\t"
        "b 185f\n\t"
        "b 186f\n\t"
        "b 187f\n\t"
        "b 188f\n\t"
        "b 189f\n\t"
        "b 190f\n\t"
        "b 191f\n\t"
        "b 192f\n\t"
        "b 193f\n\t"
        "b 194f\n\t"
        "b 195f\n\t"
        "180:\n\tmov x1, x17\n\tb 152f\n\t"
        "181:\n\tmov x2, x17\n\tb 152f\n\t"
        "182:\n\tmov x3, x17\n\tb 152f\n\t"
        "183:\n\tmov x4, x17\n\tb 152f\n\t"
        "184:\n\tmov x5, x17\n\tb 152f\n\t"
        "185:\n\tmov x6, x17\n\tb 152f\n\t"
        "186:\n\tmov x7, x17\n\tb 152f\n\t"
        "187:\n\tmov x8, x17\n\tb 152f\n\t"
        "188:\n\tmov x9, x17\n\tb 152f\n\t"
        "189:\n\tmov x10, x17\n\tb 152f\n\t"
        "190:\n\tmov x11, x17\n\tb 152f\n\t"
        "191:\n\tmov x12, x17\n\tb 152f\n\t"
        "192:\n\tmov x13, x17\n\tb 152f\n\t"
        "193:\n\tmov x14, x17\n\tb 152f\n\t"
        "194:\n\tmov x15, x17\n\tb 152f\n\t"
        "195:\n\tmov x16, x17\n\tb 152f\n\t"

        // Fast path complete - increment counter and advance to next gadget
        "152:\n\t"
        "ldr x26, [x29, %[str_fast_hits_off]]\n\t"
        "add x26, x26, #1\n\t"
        "str x26, [x29, %[str_fast_hits_off]]\n\t"
        "ldr x17, [x29, %[pstate_off]]\n\t"
        "msr nzcv, x17\n\t"
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
        "ldr x17, [x29, %[pstate_off]]\n\t"
        "msr nzcv, x17\n\t"
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
        "cmp x24, #0\n\t"
        "b.eq 160f\n\t"
        "cmp x21, #16\n\t"
        "b.hs 160f\n\t"
        "mov x26, x21\n\t"
        "bl _tcti_sync_hot_reg_from_cpu\n\t"
        "160:\n\t"
        "ldr x17, [x29, %[pstate_off]]\n\t"
        "msr nzcv, x17\n\t"
        "ldr x27, [x28], #8\n\t"
        "br x27\n\t"
        "1:\n\t"
        "ldr x17, [x29, %[pstate_off]]\n\t"
        "msr nzcv, x17\n\t"
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
          [str_fallback_notlb_off] "i"(STAT_STR_FALLBACK_NOTLB_OFFSET),
          [tlb_mmu_off] "i"(TLB_MMU_OFFSET),
          [tlb_entry_generation_off] "i"(TLB_ENTRY_GENERATION_OFFSET),
          [mmu_generation_off] "i"(MMU_GENERATION_OFFSET),
          [pstate_off] "i"(PSTATE_OFFSET)
        : "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x19", "x20", "x21", "x22", "x23", "x24",
          "x25", "x26", "x27", "memory");
}

tcti_gadget_t gadget_str_x = gadget_str_x_impl;

__attribute__((naked)) void gadget_simd_dup_gpr_impl(void)
{
    asm volatile("ldr x19, [x28], #8\n\t"
                 "ldr x20, [x28], #8\n\t"
                 "ldr x21, [x28], #8\n\t"
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
                 "mov x3, x21\n\t"
                 "bl _tcti_simd_dup_gpr_helper\n\t"
                 "bl _tcti_c_call_epilogue\n\t"
                 "ldr x27, [x28], #8\n\t"
                 "br x27\n\t");
}

tcti_gadget_t gadget_simd_dup_gpr = gadget_simd_dup_gpr_impl;

__attribute__((visibility("default"))) void _tcti_simd_dup_gpr_helper(struct cpu_state *cpu,
                                                                       uint64_t vd,
                                                                       uint64_t rn,
                                                                       uint64_t vec_bytes)
{
    tcti_simd_dup_gpr_helper(cpu, vd, rn, vec_bytes);
}

__attribute__((naked)) void gadget_simd_movi_imm_impl(void)
{
    asm volatile("ldr x19, [x28], #8\n\t"
                 "ldr x20, [x28], #8\n\t"
                 "ldr x21, [x28], #8\n\t"
                 "ldr x22, [x28], #8\n\t"
                 "ldr x23, [x28], #8\n\t"
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
                 "mov x3, x21\n\t"
                 "mov x4, x22\n\t"
                 "mov x5, x23\n\t"
                 "bl _tcti_simd_movi_imm_helper\n\t"
                 "bl _tcti_c_call_epilogue\n\t"
                 "ldr x27, [x28], #8\n\t"
                 "br x27\n\t");
}

tcti_gadget_t gadget_simd_movi_imm = gadget_simd_movi_imm_impl;

__attribute__((visibility("default"))) void
_tcti_simd_movi_imm_helper(struct cpu_state *cpu, uint64_t vd, uint64_t imm8,
                           uint64_t cmode, uint64_t op, uint64_t q)
{
    tcti_simd_movi_imm_helper(cpu, vd, imm8, cmode, op, q);
}

__attribute__((naked)) void gadget_simd_mov_gpr_from_vec_impl(void)
{
    asm volatile("ldr x19, [x28], #8\n\t"
                 "ldr x20, [x28], #8\n\t"
                 "ldr x21, [x28], #8\n\t"
                 "ldr x22, [x28], #8\n\t"
                 "ldr x23, [x28], #8\n\t"
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
                 "mov x3, x21\n\t"
                 "mov x4, x22\n\t"
                 "mov x5, x23\n\t"
                 "bl _tcti_simd_mov_gpr_from_vec_helper\n\t"
                 "bl _tcti_c_call_epilogue\n\t"
                 "cmp x19, #16\n\t"
                 "b.hs 1f\n\t"
                 "mov x26, x19\n\t"
                 "bl _tcti_sync_hot_reg_from_cpu\n\t"
                 "1:\n\t"
                 "ldr x27, [x28], #8\n\t"
                 "br x27\n\t");
}

tcti_gadget_t gadget_simd_mov_gpr_from_vec = gadget_simd_mov_gpr_from_vec_impl;

__attribute__((visibility("default"))) void
_tcti_simd_mov_gpr_from_vec_helper(struct cpu_state *cpu, uint64_t rd, uint64_t vn,
                                   uint64_t vec_bytes, uint64_t vec_index,
                                   uint64_t is_64bit)
{
    tcti_simd_mov_gpr_from_vec_helper(cpu, rd, vn, vec_bytes, vec_index, is_64bit);
}

__attribute__((naked)) void gadget_atomic_ldst_impl(void)
{
    asm volatile("ldr x19, [x28], #8\n\t"
                 "ldr x20, [x28], #8\n\t"
                 "ldr x21, [x28], #8\n\t"
                 "ldr x22, [x28], #8\n\t"
                 "ldr x23, [x28], #8\n\t"
                 "ldr x24, [x28], #8\n\t"
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
                 "mov x3, x21\n\t"
                 "mov x4, x22\n\t"
                 "mov x5, x23\n\t"
                 "mov x6, x24\n\t"
                 "bl _tcti_atomic_ldst_helper\n\t"
                 "bl _tcti_c_call_epilogue\n\t"
                 "cmp x0, #0\n\t"
                 "b.ne 3f\n\t"
                 "cmp x20, #16\n\t"
                 "b.hs 1f\n\t"
                 "mov x26, x20\n\t"
                 "bl _tcti_sync_hot_reg_from_cpu\n\t"
                 "1:\n\t"
                 "cmp x22, #16\n\t"
                 "b.hs 2f\n\t"
                 "mov x26, x22\n\t"
                 "bl _tcti_sync_hot_reg_from_cpu\n\t"
                 "2:\n\t"
                 "ldr x27, [x28], #8\n\t"
                 "br x27\n\t"
                 "3:\n\t"
                 "b _tcti_exit_block\n\t");
}

tcti_gadget_t gadget_atomic_ldst = gadget_atomic_ldst_impl;

__attribute__((visibility("default"))) int _tcti_atomic_ldst_helper(
    struct cpu_state *cpu, uint64_t fault_pc, uint64_t rt, uint64_t rn, uint64_t rs,
    uint64_t size, uint64_t is_load)
{
    return tcti_atomic_ldst_helper(cpu, fault_pc, rt, rn, rs, size, is_load);
}

__attribute__((naked)) void gadget_simd_ldst_impl(void)
{
    asm volatile("ldr x19, [x28], #8\n\t"
                 "ldr x20, [x28], #8\n\t"
                 "ldr x21, [x28], #8\n\t"
                 "ldr x22, [x28], #8\n\t"
                 "ldr x23, [x28], #8\n\t"
                 "ldr x24, [x28], #8\n\t"
                 "ldr x25, [x28], #8\n\t"
                 "ldr x26, [x28], #8\n\t"
                 "ldr x27, [x28], #8\n\t"
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
                 "mov x3, x21\n\t"
                 "mov x4, x22\n\t"
                 "mov x5, x23\n\t"
                 "mov x6, x24\n\t"
                 "mov x7, x25\n\t"
                 "stp x26, x27, [sp, #-16]!\n\t"
                 "bl _tcti_simd_ldst_helper\n\t"
                 "add sp, sp, #16\n\t"
                 "bl _tcti_c_call_epilogue\n\t"
                 "cmp x0, #0\n\t"
                 "b.ne 2f\n\t"
                 "cmp x22, #16\n\t"
                 "b.hs 1f\n\t"
                 "mov x26, x22\n\t"
                 "bl _tcti_sync_hot_reg_from_cpu\n\t"
                 "1:\n\t"
                 "ldr x27, [x28], #8\n\t"
                 "br x27\n\t"
                 "2:\n\t"
                 "b _tcti_exit_block\n\t");
}

tcti_gadget_t gadget_simd_ldst = gadget_simd_ldst_impl;

__attribute__((visibility("default"))) int _tcti_simd_ldst_helper(
    struct cpu_state *cpu, uint64_t fault_pc, uint64_t rt, uint64_t rt2, uint64_t rn,
    int64_t imm, uint64_t vec_bytes, uint64_t idx_mode, uint64_t is_pair, uint64_t is_load)
{
    return tcti_simd_ldst_helper(cpu, fault_pc, rt, rt2, rn, imm, vec_bytes, idx_mode, is_pair,
                                 is_load);
}

__attribute__((naked)) void gadget_extend_x14_impl(void)
{
    asm volatile("ldr x19, [x28], #8\n\t"
                 "cmp x19, #5\n\t"
                 "b.eq 1f\n\t"
                 "cmp x19, #6\n\t"
                 "b.eq 2f\n\t"
                 "cmp x19, #0\n\t"
                 "b.eq 3f\n\t"
                 "cmp x19, #7\n\t"
                 "b.eq 4f\n\t"
                 "cmp x19, #8\n\t"
                 "b.eq 5f\n\t"
                 "cmp x19, #2\n\t"
                 "b.eq 6f\n\t"
                 "b 7f\n\t"
                 "1:\n\t"
                 "and x14, x14, #0xff\n\t"
                 "b 7f\n\t"
                 "2:\n\t"
                 "and x14, x14, #0xffff\n\t"
                 "b 7f\n\t"
                 "3:\n\t"
                 "uxtw x14, w14\n\t"
                 "b 7f\n\t"
                 "4:\n\t"
                 "sxtb x14, w14\n\t"
                 "b 7f\n\t"
                 "5:\n\t"
                 "sxth x14, w14\n\t"
                 "b 7f\n\t"
                 "6:\n\t"
                 "sxtw x14, w14\n\t"
                 "7:\n\t"
                 "ldr x27, [x28], #8\n\t"
                 "br x27\n\t");
}

tcti_gadget_t gadget_extend_x14 = gadget_extend_x14_impl;

__attribute__((naked)) void gadget_nop_impl(void)
{
    asm volatile("ldr x27, [x28], #8\n\t"
                 "br x27\n\t");
}

tcti_gadget_t gadget_nop = gadget_nop_impl;

__attribute__((naked)) void gadget_dmb_impl(void)
{
    asm volatile("dmb ish\n\t"
                 "ldr x27, [x28], #8\n\t"
                 "br x27\n\t");
}

tcti_gadget_t gadget_dmb = gadget_dmb_impl;

__attribute__((naked)) void gadget_dsb_impl(void)
{
    asm volatile("dsb sy\n\t"
                 "ldr x27, [x28], #8\n\t"
                 "br x27\n\t");
}

tcti_gadget_t gadget_dsb = gadget_dsb_impl;

__attribute__((naked)) void gadget_isb_impl(void)
{
    asm volatile("isb\n\t"
                 "ldr x27, [x28], #8\n\t"
                 "br x27\n\t");
}

tcti_gadget_t gadget_isb = gadget_isb_impl;

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
                 "cmp x20, #16\n\t"
                 "b.hs 2f\n\t"
                 "mov x26, x20\n\t"
                 "bl _tcti_sync_hot_reg_from_cpu\n\t"
                 "2:\n\t"
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
