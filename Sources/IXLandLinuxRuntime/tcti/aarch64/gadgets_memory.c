/*
 * Memory access gadgets for TCTI
 *
 * Memory-backed registers (x13-x30, SP) are stored in cpu_state memory.
 * To operate on them, we:
 *   1. Load into temp register (x14 for values, x15 for auxiliaries)
 *   2. Execute operation
 *   3. Store back to memory
 *
 * Register mapping:
 *   x0-x12 (guest)  -> x1-x13 (host)   [TCTI-mapped, always hot]
 *   x13-x30 (guest) -> memory only     [load/store via gadgets]
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
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TCTI_HOT_REG_COUNT 13
#define TCTI_MEM_REG_BASE 13
#define TCTI_MEM_REG_COUNT (31 - TCTI_MEM_REG_BASE)

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
static void tcti_trace_record_mem_access(struct cpu_state *cpu, uint64_t pc, uint32_t raw,
                                         uint64_t addr, uint64_t value, uint64_t base,
                                         uint64_t offset, uint8_t width, uint8_t is_load, int rt,
                                         int rn, int rm, int idx_mode);

// Stub functions retained for external diagnostic symbol compatibility.
void dump_str_wb_diag(void) { }
void dump_cmp_capture(void) { }
void dump_cmp_bcond_diag(void) { }
void dump_runtime_diag(void) { }

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
    if (!is_load) {
        uint64_t value = vec_bytes >= 8 ? cpu->vregs[rt].d[0] : 0;
        if (vec_bytes == 4)
            value = cpu->vregs[rt].s[0];
        else if (vec_bytes == 2)
            value = cpu->vregs[rt].h[0];
        else if (vec_bytes == 1)
            value = cpu->vregs[rt].b[0];
        tcti_trace_record_mem_access(cpu, fault_pc, 0, addr, value, base, addr - base,
                                     (uint8_t)(vec_bytes >= 8 ? 8 : vec_bytes), 0, (int)rt,
                                     (int)rn, -1, (int)idx_mode);
        if (vec_bytes == 16)
            tcti_trace_record_mem_access(cpu, fault_pc, 0, addr + 8, cpu->vregs[rt].d[1], base,
                                         addr + 8 - base, 8, 0, (int)rt, (int)rn, -1,
                                         (int)idx_mode);
    }

    if (is_pair) {
        ret = tcti_simd_vec_access(cpu, addr + vec_bytes, rt2, vec_bytes, (int)is_load);
        if (ret != TCTI_EXIT_NORMAL)
            goto fault;
        if (!is_load) {
            uint64_t value = vec_bytes >= 8 ? cpu->vregs[rt2].d[0] : 0;
            if (vec_bytes == 4)
                value = cpu->vregs[rt2].s[0];
            else if (vec_bytes == 2)
                value = cpu->vregs[rt2].h[0];
            else if (vec_bytes == 1)
                value = cpu->vregs[rt2].b[0];
            tcti_trace_record_mem_access(cpu, fault_pc, 0, addr + vec_bytes, value, base,
                                         addr + vec_bytes - base,
                                         (uint8_t)(vec_bytes >= 8 ? 8 : vec_bytes), 0,
                                         (int)rt2, (int)rn, -1, (int)idx_mode);
            if (vec_bytes == 16)
                tcti_trace_record_mem_access(cpu, fault_pc, 0, addr + vec_bytes + 8,
                                             cpu->vregs[rt2].d[1], base,
                                             addr + vec_bytes + 8 - base, 8, 0, (int)rt2,
                                             (int)rn, -1, (int)idx_mode);
        }
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

static void trace_tcti_reg_write(int reg, uint64_t old_val, uint64_t new_val, int is_64bit)
{
    trace_field_t fields[] = {
        { .key = "reg", .kind = TRACE_FIELD_I64_DEC, .i64_value = reg },
        { .key = "old_val", .kind = TRACE_FIELD_U64_HEX, .u64_value = old_val },
        { .key = "new_val", .kind = TRACE_FIELD_U64_HEX, .u64_value = new_val },
        { .key = "is_64bit", .kind = TRACE_FIELD_I64_DEC, .i64_value = is_64bit ? 1 : 0 },
    };
    trace_record_event_fields(TRACE_ORIGIN_TCTI, "tcti.reg.write", fields,
                              sizeof(fields) / sizeof(fields[0]));
}

static void trace_tcti_ldst_access(struct cpu_state *cpu, const char *event_name,
                                   uint64_t instance_id, uint64_t fault_pc, uint32_t raw_opcode,
                                   uint64_t rt, uint64_t rn, int64_t imm, uint64_t size,
                                   uint64_t idx_mode, uint64_t meta, uint64_t is_load,
                                   uint64_t base, uint64_t addr, uint64_t value, int mem_ret,
                                   int width, int writeback_enabled, uint64_t writeback_value)
{
    uint64_t is_signed = meta & 0xff;
    uint64_t is_reg_offset = (meta >> 8) & 0xff;
    int rm = (meta >> 16) & 0xff;
    int extend_type = (meta >> 24) & 0xff;
    int reg_shift = (meta >> 32) & 0xff;
    uint64_t rm_val = is_reg_offset ? tcti_read_reg_or_zr(cpu, rm) : 0;
    uint64_t offset_before_shift =
        is_reg_offset ? tcti_extend_ldst_offset(cpu, rm, extend_type) : (uint64_t)imm;
    uint64_t computed_offset = is_reg_offset ? (offset_before_shift << reg_shift) : (uint64_t)imm;

    struct mem *mem = cpu->mmu ? container_of(cpu->mmu, struct mem, mmu) : NULL;
    page_t page = PAGE(addr);
    struct page_desc *desc = mem ? page_map_lookup(&mem->pages, page) : NULL;
    uint64_t host_ptr = 0;
    if (desc && desc->obj) {
        host_ptr =
            (uint64_t)((char *)desc->obj->host_base + desc->offset + (unsigned)PGOFFSET(addr));
    }

    trace_field_t fields[] = {
        { .key = "instance_id", .kind = TRACE_FIELD_U64_DEC, .u64_value = instance_id },
        { .key = "guest_pc", .kind = TRACE_FIELD_U64_HEX, .u64_value = fault_pc },
        { .key = "raw_opcode", .kind = TRACE_FIELD_U64_HEX, .u64_value = raw_opcode },
        { .key = "mnemonic", .kind = TRACE_FIELD_STRING,
          .string_value = get_ldst_mnemonic((int)is_load, (int)size, (int)is_signed) },
        { .key = "is_load", .kind = TRACE_FIELD_I64_DEC, .i64_value = is_load ? 1 : 0 },
        { .key = "rt", .kind = TRACE_FIELD_I64_DEC, .i64_value = (int64_t)rt },
        { .key = "rn", .kind = TRACE_FIELD_I64_DEC, .i64_value = (int64_t)rn },
        { .key = "rm", .kind = TRACE_FIELD_I64_DEC, .i64_value = is_reg_offset ? rm : -1 },
        { .key = "idx_mode", .kind = TRACE_FIELD_STRING,
          .string_value = get_ldst_idx_mode_name(idx_mode) },
        { .key = "extend", .kind = TRACE_FIELD_STRING,
          .string_value = is_reg_offset ? get_ldst_extend_name(extend_type) : "none" },
        { .key = "shift", .kind = TRACE_FIELD_I64_DEC, .i64_value = reg_shift },
        { .key = "imm", .kind = TRACE_FIELD_I64_DEC, .i64_value = imm },
        { .key = "base", .kind = TRACE_FIELD_U64_HEX, .u64_value = base },
        { .key = "rm_val", .kind = TRACE_FIELD_U64_HEX, .u64_value = rm_val },
        { .key = "offset_before_shift", .kind = TRACE_FIELD_U64_HEX,
          .u64_value = offset_before_shift },
        { .key = "computed_offset", .kind = TRACE_FIELD_U64_HEX,
          .u64_value = computed_offset },
        { .key = "guest_ea", .kind = TRACE_FIELD_U64_HEX, .u64_value = addr },
        { .key = "page", .kind = TRACE_FIELD_U64_HEX, .u64_value = page },
        { .key = "host_ptr", .kind = TRACE_FIELD_U64_HEX, .u64_value = host_ptr },
        { .key = "page_lookup", .kind = TRACE_FIELD_STRING,
          .string_value = desc ? "hit" : "miss" },
        { .key = "value", .kind = TRACE_FIELD_U64_HEX, .u64_value = value },
        { .key = "mem_result", .kind = TRACE_FIELD_I64_DEC, .i64_value = mem_ret },
        { .key = "width", .kind = TRACE_FIELD_I64_DEC, .i64_value = width },
        { .key = "writeback_enabled", .kind = TRACE_FIELD_I64_DEC,
          .i64_value = writeback_enabled },
        { .key = "writeback_value", .kind = TRACE_FIELD_U64_HEX, .u64_value = writeback_value },
    };
    trace_record_event_fields(TRACE_ORIGIN_TCTI, event_name, fields,
                              sizeof(fields) / sizeof(fields[0]));
}

static void tcti_write_base_reg_or_sp(struct cpu_state *cpu, int reg, uint64_t value, int is_64bit)
{
    uint64_t masked = is_64bit ? value : (uint32_t)value;

    if (reg == 31) {
        uint64_t old_val = cpu->sp;
        cpu->sp = masked;
        trace_tcti_reg_write(reg, old_val, masked, is_64bit);
        return;
    }
    if (reg < 0 || reg > 30)
        return;

    uint64_t old_val = cpu->x[reg];
    cpu->x[reg] = masked;
    trace_tcti_reg_write(reg, old_val, masked, is_64bit);
}

static void tcti_write_reg_or_zr(struct cpu_state *cpu, int reg, uint64_t value, int is_64bit)
{
    uint64_t masked = is_64bit ? value : (uint32_t)value;
    if (reg == 31)
        return;
    if (reg < 0 || reg > 30)
        return;
    uint64_t old_val = cpu->x[reg];
    cpu->x[reg] = masked;
    trace_tcti_reg_write(reg, old_val, masked, is_64bit);
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

#define TCTI_TRACE_MEM_HISTORY_SIZE 2048
#define TCTI_TRACE_STORE_HISTORY_SIZE 65536

typedef struct {
    uint64_t pc;
    uint64_t addr;
    uint64_t value;
    uint64_t base;
    uint64_t offset;
    uint32_t raw;
    uint16_t sequence;
    uint8_t width;
    uint8_t is_load;
    int rt;
    int rn;
    int rm;
    int idx_mode;
} tcti_trace_mem_history_entry_t;

typedef struct {
    tcti_trace_mem_history_entry_t access;
    uint32_t store_sequence;
} tcti_trace_store_history_entry_t;

static tcti_trace_mem_history_entry_t g_tcti_trace_mem_history[TCTI_TRACE_MEM_HISTORY_SIZE];
static uint16_t g_tcti_trace_mem_history_next = 0;
static uint16_t g_tcti_trace_mem_history_count = 0;
static uint16_t g_tcti_trace_mem_history_sequence = 0;

static tcti_trace_store_history_entry_t
    g_tcti_trace_store_history[TCTI_TRACE_STORE_HISTORY_SIZE];
static uint32_t g_tcti_trace_store_history_next = 0;
static uint32_t g_tcti_trace_store_history_count = 0;
static uint32_t g_tcti_trace_store_history_sequence = 0;

static void tcti_trace_record_mem_access(struct cpu_state *cpu, uint64_t pc, uint32_t raw,
                                         uint64_t addr, uint64_t value, uint64_t base,
                                         uint64_t offset, uint8_t width, uint8_t is_load, int rt,
                                         int rn, int rm, int idx_mode)
{
    (void)cpu;
    tcti_trace_mem_history_entry_t *entry =
        &g_tcti_trace_mem_history[g_tcti_trace_mem_history_next];
    memset(entry, 0, sizeof(*entry));
    entry->pc = pc;
    entry->addr = addr;
    entry->value = value;
    entry->base = base;
    entry->offset = offset;
    entry->raw = raw;
    entry->sequence = g_tcti_trace_mem_history_sequence++;
    entry->width = width;
    entry->is_load = is_load;
    entry->rt = rt;
    entry->rn = rn;
    entry->rm = rm;
    entry->idx_mode = idx_mode;

    g_tcti_trace_mem_history_next =
        (uint16_t)((g_tcti_trace_mem_history_next + 1) % TCTI_TRACE_MEM_HISTORY_SIZE);
    if (g_tcti_trace_mem_history_count < TCTI_TRACE_MEM_HISTORY_SIZE)
        g_tcti_trace_mem_history_count++;

    if (!is_load) {
        tcti_trace_store_history_entry_t *store =
            &g_tcti_trace_store_history[g_tcti_trace_store_history_next];
        store->access = *entry;
        store->store_sequence = g_tcti_trace_store_history_sequence++;
        g_tcti_trace_store_history_next =
            (g_tcti_trace_store_history_next + 1) % TCTI_TRACE_STORE_HISTORY_SIZE;
        if (g_tcti_trace_store_history_count < TCTI_TRACE_STORE_HISTORY_SIZE)
            g_tcti_trace_store_history_count++;
    }
}

static int tcti_trace_matching_reg(const struct cpu_state *cpu, uint64_t value)
{
    if (!cpu || value == 0)
        return -1;
    for (int reg = 0; reg <= 30; reg++) {
        if (cpu->x[reg] == value)
            return reg;
    }
    return cpu->sp == value ? 31 : -1;
}

static void tcti_trace_emit_mem_history_entry(const char *event_name, uint32_t slot,
                                              const tcti_trace_mem_history_entry_t *entry,
                                              int value_match_reg)
{
    trace_field_t fields[] = {
        { .key = "slot", .kind = TRACE_FIELD_U64_DEC, .u64_value = slot },
        { .key = "sequence", .kind = TRACE_FIELD_U64_DEC, .u64_value = entry->sequence },
        { .key = "guest_pc", .kind = TRACE_FIELD_U64_HEX, .u64_value = entry->pc },
        { .key = "raw_opcode", .kind = TRACE_FIELD_U64_HEX, .u64_value = entry->raw },
        { .key = "addr", .kind = TRACE_FIELD_U64_HEX, .u64_value = entry->addr },
        { .key = "value", .kind = TRACE_FIELD_U64_HEX, .u64_value = entry->value },
        { .key = "base", .kind = TRACE_FIELD_U64_HEX, .u64_value = entry->base },
        { .key = "offset", .kind = TRACE_FIELD_U64_HEX, .u64_value = entry->offset },
        { .key = "width", .kind = TRACE_FIELD_U64_DEC, .u64_value = entry->width },
        { .key = "is_load", .kind = TRACE_FIELD_I64_DEC, .i64_value = entry->is_load },
        { .key = "rt", .kind = TRACE_FIELD_I64_DEC, .i64_value = entry->rt },
        { .key = "rn", .kind = TRACE_FIELD_I64_DEC, .i64_value = entry->rn },
        { .key = "rm", .kind = TRACE_FIELD_I64_DEC, .i64_value = entry->rm },
        { .key = "idx_mode", .kind = TRACE_FIELD_I64_DEC, .i64_value = entry->idx_mode },
        { .key = "value_match_reg", .kind = TRACE_FIELD_I64_DEC, .i64_value = value_match_reg },
    };
    trace_record_event_fields(TRACE_ORIGIN_TCTI, event_name, fields,
                              sizeof(fields) / sizeof(fields[0]));
}

static const tcti_trace_store_history_entry_t *
tcti_trace_find_last_store_covering(uint64_t addr, uint8_t width, uint32_t *slot_out)
{
    uint64_t end = addr + (width ? width : 1);

    for (uint32_t scanned = 0; scanned < g_tcti_trace_store_history_count; scanned++) {
        uint32_t slot = (uint32_t)((g_tcti_trace_store_history_next +
                                    TCTI_TRACE_STORE_HISTORY_SIZE - 1 - scanned) %
                                   TCTI_TRACE_STORE_HISTORY_SIZE);
        const tcti_trace_store_history_entry_t *store = &g_tcti_trace_store_history[slot];
        uint64_t store_start = store->access.addr;
        uint64_t store_end = store_start + (store->access.width ? store->access.width : 1);

        if (store_start < end && store_end > addr) {
            if (slot_out)
                *slot_out = slot;
            return store;
        }
    }

    return NULL;
}

static void tcti_trace_emit_store_source_entry(uint32_t source_slot, uint64_t source_addr,
                                               uint64_t source_value,
                                               const tcti_trace_store_history_entry_t *store,
                                               uint32_t store_slot)
{
    trace_field_t fields[] = {
        { .key = "source_slot", .kind = TRACE_FIELD_U64_DEC, .u64_value = source_slot },
        { .key = "source_addr", .kind = TRACE_FIELD_U64_HEX, .u64_value = source_addr },
        { .key = "source_value", .kind = TRACE_FIELD_U64_HEX, .u64_value = source_value },
        { .key = "store_slot", .kind = TRACE_FIELD_U64_DEC, .u64_value = store_slot },
        { .key = "store_sequence", .kind = TRACE_FIELD_U64_DEC,
          .u64_value = store->store_sequence },
        { .key = "store_guest_pc", .kind = TRACE_FIELD_U64_HEX,
          .u64_value = store->access.pc },
        { .key = "store_raw_opcode", .kind = TRACE_FIELD_U64_HEX,
          .u64_value = store->access.raw },
        { .key = "store_addr", .kind = TRACE_FIELD_U64_HEX, .u64_value = store->access.addr },
        { .key = "store_value", .kind = TRACE_FIELD_U64_HEX,
          .u64_value = store->access.value },
        { .key = "store_base", .kind = TRACE_FIELD_U64_HEX, .u64_value = store->access.base },
        { .key = "store_offset", .kind = TRACE_FIELD_U64_HEX,
          .u64_value = store->access.offset },
        { .key = "store_width", .kind = TRACE_FIELD_U64_DEC,
          .u64_value = store->access.width },
        { .key = "store_rt", .kind = TRACE_FIELD_I64_DEC, .i64_value = store->access.rt },
        { .key = "store_rn", .kind = TRACE_FIELD_I64_DEC, .i64_value = store->access.rn },
        { .key = "store_rm", .kind = TRACE_FIELD_I64_DEC, .i64_value = store->access.rm },
    };
    trace_record_event_fields(TRACE_ORIGIN_TCTI, "tcti.mem.history.source_store", fields,
                              sizeof(fields) / sizeof(fields[0]));
}

static void tcti_trace_emit_mem_history_on_fault(struct cpu_state *cpu, uint64_t fault_addr)
{
    if (!trace_should_emit_event("tcti.mem.history"))
        return;

    uint32_t recent_limit = 96;
    uint32_t emitted_recent = 0;
    uint32_t emitted_matches = 0;
    for (uint32_t i = 0; i < g_tcti_trace_mem_history_count; i++) {
        uint32_t index = (uint32_t)((g_tcti_trace_mem_history_next +
                                     TCTI_TRACE_MEM_HISTORY_SIZE -
                                     g_tcti_trace_mem_history_count + i) %
                                    TCTI_TRACE_MEM_HISTORY_SIZE);
        const tcti_trace_mem_history_entry_t *entry = &g_tcti_trace_mem_history[index];
        uint32_t remaining = g_tcti_trace_mem_history_count - i;
        int value_match_reg = tcti_trace_matching_reg(cpu, entry->value);
        bool addr_match = entry->addr == fault_addr;

        if (entry->is_load && entry->value == fault_addr) {
            uint32_t store_slot = 0;
            const tcti_trace_store_history_entry_t *store =
                tcti_trace_find_last_store_covering(entry->addr, entry->width, &store_slot);
            if (store)
                tcti_trace_emit_store_source_entry(i, entry->addr, entry->value, store,
                                                   store_slot);
        }

        if ((addr_match || value_match_reg >= 0) && emitted_matches < 96) {
            tcti_trace_emit_mem_history_entry("tcti.mem.history.match", i, entry,
                                              value_match_reg);
            emitted_matches++;
        }
        if (remaining <= recent_limit && emitted_recent < recent_limit) {
            tcti_trace_emit_mem_history_entry("tcti.mem.history.recent", emitted_recent, entry,
                                              value_match_reg);
            emitted_recent++;
        }
    }
}

static int a64_tcti_ldst_helper(struct cpu_state *cpu, uint64_t fault_pc, uint64_t rt, uint64_t rn,
                                int64_t imm, uint64_t size, uint64_t idx_mode, uint64_t meta,
                                uint64_t is_load)
{
    uint64_t instance_id = ++g_ldst_instance_id;
    uint32_t fault_raw_opcode = 0;
    (void)a64_fetch_insn(cpu, cpu->tlb, fault_pc, &fault_raw_opcode);

    uint64_t base = tcti_read_base_reg_or_sp(cpu, (int)rn);
    uint64_t addr = base;
    uint64_t is_signed = meta & 0xff;
    uint64_t is_reg_offset = (meta >> 8) & 0xff;
    uint64_t load_writes_64 = (meta >> 40) & 0x1;
    int rm = (meta >> 16) & 0xff;
    int extend_type = (meta >> 24) & 0xff;
    int reg_shift = (meta >> 32) & 0xff;
    int width = 0;
    int writeback_enabled = 0;
    uint64_t writeback_value = base;

    if (is_reg_offset) {
        uint64_t offset = tcti_extend_ldst_offset(cpu, rm, extend_type);
        addr = base + (offset << reg_shift);
    } else {
        switch (idx_mode) {
        case A64_PRE_INDEX:
            base += imm;
            addr = base;
            break;
        case A64_POST_INDEX:
            addr = base;
            break;
        case A64_INDEX_OFFSET:
        default:
            addr = base + imm;
            break;
        }
    }

    writeback_enabled =
        (!is_reg_offset && (idx_mode == A64_PRE_INDEX || idx_mode == A64_POST_INDEX)) ? 1 : 0;
    writeback_value =
        writeback_enabled ? ((idx_mode == A64_POST_INDEX) ? (base + imm) : base) : base;

    cpu->fault_addr = addr;
    cpu->fault_was_write = is_load ? false : true;

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
        cpu->pc = fault_pc;
        return TCTI_EXIT_FAULT;
    }

    if (is_load) {
        uint64_t value = 0;
        int mem_ret = A64_MEM_FAULT;

        switch (width) {
        case 1: {
            uint8_t tmp = 0;
            mem_ret = a64_guest_read8(cpu, cpu->tlb, addr, &tmp);
            value = is_signed ? (uint64_t)(int64_t)(int8_t)tmp : tmp;
            break;
        }
        case 2: {
            uint16_t tmp = 0;
            mem_ret = a64_guest_read16(cpu, cpu->tlb, addr, &tmp);
            value = is_signed ? (uint64_t)(int64_t)(int16_t)tmp : tmp;
            break;
        }
        case 4: {
            uint32_t tmp = 0;
            mem_ret = a64_guest_read32(cpu, cpu->tlb, addr, &tmp);
            value = is_signed ? (uint64_t)(int64_t)(int32_t)tmp : tmp;
            break;
        }
        case 8: {
            uint64_t tmp = 0;
            mem_ret = a64_guest_read64(cpu, cpu->tlb, addr, &tmp);
            value = tmp;
            break;
        }
        default:
            break;
        }

        if (mem_ret != A64_MEM_OK) {
            tcti_trace_emit_mem_history_on_fault(cpu, addr);
            trace_tcti_ldst_access(cpu, "tcti.ldst.fault", instance_id, fault_pc,
                                   fault_raw_opcode, rt, rn, imm, size, idx_mode, meta, is_load,
                                   base, addr, value, mem_ret, width, writeback_enabled,
                                   writeback_value);
            cpu->pc = fault_pc;
            cpu->fault_was_write = false;
            return TCTI_EXIT_FAULT;
        }

        tcti_trace_record_mem_access(cpu, fault_pc, fault_raw_opcode, addr, value, base,
                                     addr - base, (uint8_t)width, 1, (int)rt, (int)rn,
                                     is_reg_offset ? rm : -1, (int)idx_mode);
        trace_tcti_ldst_access(cpu, "tcti.ldst.access", instance_id, fault_pc, fault_raw_opcode,
                               rt, rn, imm, size, idx_mode, meta, is_load, base, addr, value,
                               mem_ret, width, writeback_enabled, writeback_value);
        tcti_write_reg_or_zr(cpu, (int)rt, value, size == A64_SIZE_X || load_writes_64);
    } else {
        uint64_t value = tcti_read_reg_or_zr(cpu, (int)rt);
        int mem_ret = A64_MEM_FAULT;

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
            break;
        }

        if (mem_ret != A64_MEM_OK) {
            tcti_trace_emit_mem_history_on_fault(cpu, addr);
            trace_tcti_ldst_access(cpu, "tcti.ldst.fault", instance_id, fault_pc,
                                   fault_raw_opcode, rt, rn, imm, size, idx_mode, meta, is_load,
                                   base, addr, value, mem_ret, width, writeback_enabled,
                                   writeback_value);
            cpu->pc = fault_pc;
            cpu->fault_was_write = true;
            return TCTI_EXIT_FAULT;
        }

        tcti_trace_record_mem_access(cpu, fault_pc, fault_raw_opcode, addr, value, base,
                                     addr - base, (uint8_t)width, 0, (int)rt, (int)rn,
                                     is_reg_offset ? rm : -1, (int)idx_mode);
        trace_tcti_ldst_access(cpu, "tcti.ldst.access", instance_id, fault_pc, fault_raw_opcode,
                               rt, rn, imm, size, idx_mode, meta, is_load, base, addr, value,
                               mem_ret, width, writeback_enabled, writeback_value);
    }

    if (writeback_enabled)
        tcti_write_base_reg_or_sp(cpu, (int)rn, writeback_value, true);

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
// Index mapping: 0=x15, 1=x16, ..., 15=x30 (16 entries)
// ============================================================================

// Load guest x[15 + idx] into host temp x14
#define GEN_LOAD_XREG(idx)                                                                         \
    __attribute__((naked)) void gadget_load_x##idx##_impl(void)                                    \
    {                                                                                              \
        asm volatile("ldr x14, [x29, %[off]]\n\t"                                                  \
                     "ldr x27, [x28], #8\n\t"                                                      \
                     "br x27\n\t"                                                                  \
                     :                                                                             \
                     : [off] "i"(XREG_OFFSET(TCTI_MEM_REG_BASE + idx)));                           \
    }

// Store host temp x14 to guest x[15 + idx]
#define GEN_STORE_XREG(idx)                                                                        \
    __attribute__((naked)) void gadget_store_x##idx##_impl(void)                                   \
    {                                                                                              \
        asm volatile("str x14, [x29, %[off]]\n\t"                                                  \
                     "ldr x27, [x28], #8\n\t"                                                      \
                     "br x27\n\t"                                                                  \
                     :                                                                             \
                     : [off] "i"(XREG_OFFSET(TCTI_MEM_REG_BASE + idx)));                           \
    }

// Generate load gadgets for x13-x30 (indices 0-17)
GEN_LOAD_XREG(0)  // x13
GEN_LOAD_XREG(1)  // x14
GEN_LOAD_XREG(2)  // x15
GEN_LOAD_XREG(3)  // x16
GEN_LOAD_XREG(4)  // x17
GEN_LOAD_XREG(5)  // x18
GEN_LOAD_XREG(6)  // x19
GEN_LOAD_XREG(7)  // x20
GEN_LOAD_XREG(8)  // x21
GEN_LOAD_XREG(9)  // x22
GEN_LOAD_XREG(10) // x23
GEN_LOAD_XREG(11) // x24
GEN_LOAD_XREG(12) // x25
GEN_LOAD_XREG(13) // x26
GEN_LOAD_XREG(14) // x27
GEN_LOAD_XREG(15) // x28
GEN_LOAD_XREG(16) // x29
GEN_LOAD_XREG(17) // x30

// Generate store gadgets for x13-x30 (indices 0-17)
GEN_STORE_XREG(0)  // x13
GEN_STORE_XREG(1)  // x14
GEN_STORE_XREG(2)  // x15
GEN_STORE_XREG(3)  // x16
GEN_STORE_XREG(4)  // x17
GEN_STORE_XREG(5)  // x18
GEN_STORE_XREG(6)  // x19
GEN_STORE_XREG(7)  // x20
GEN_STORE_XREG(8)  // x21
GEN_STORE_XREG(9)  // x22
GEN_STORE_XREG(10) // x23
GEN_STORE_XREG(11) // x24
GEN_STORE_XREG(12) // x25
GEN_STORE_XREG(13) // x26
GEN_STORE_XREG(14) // x27
GEN_STORE_XREG(15) // x28
GEN_STORE_XREG(16) // x29
GEN_STORE_XREG(17) // x30

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
// These are indexed by (guest_reg - 13) for x13-x30.
// ============================================================================

// Load table: index 0=x13, 17=x30.
const tcti_gadget_t gadget_load_xreg_16_to_30[TCTI_MEM_REG_COUNT] = {
    gadget_load_x0_impl,  // x13
    gadget_load_x1_impl,  // x14
    gadget_load_x2_impl,  // x15
    gadget_load_x3_impl,  // x16
    gadget_load_x4_impl,  // x17
    gadget_load_x5_impl,  // x18
    gadget_load_x6_impl,  // x19
    gadget_load_x7_impl,  // x20
    gadget_load_x8_impl,  // x21
    gadget_load_x9_impl,  // x22
    gadget_load_x10_impl, // x23
    gadget_load_x11_impl, // x24
    gadget_load_x12_impl, // x25
    gadget_load_x13_impl, // x26
    gadget_load_x14_impl, // x27
    gadget_load_x15_impl, // x28
    gadget_load_x16_impl, // x29
    gadget_load_x17_impl, // x30
};

// Store table: index 0=x13, 17=x30.
const tcti_gadget_t gadget_store_xreg_16_to_30[TCTI_MEM_REG_COUNT] = {
    gadget_store_x0_impl,  // x13
    gadget_store_x1_impl,  // x14
    gadget_store_x2_impl,  // x15
    gadget_store_x3_impl,  // x16
    gadget_store_x4_impl,  // x17
    gadget_store_x5_impl,  // x18
    gadget_store_x6_impl,  // x19
    gadget_store_x7_impl,  // x20
    gadget_store_x8_impl,  // x21
    gadget_store_x9_impl,  // x22
    gadget_store_x10_impl, // x23
    gadget_store_x11_impl, // x24
    gadget_store_x12_impl, // x25
    gadget_store_x13_impl, // x26
    gadget_store_x14_impl, // x27
    gadget_store_x15_impl, // x28
    gadget_store_x16_impl, // x29
    gadget_store_x17_impl, // x30
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
                 "cmp x0, #13\n\t"
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
                 "str x13, [x29, #112]\n\t"
                 "bl _tcti_c_call_prologue\n\t"
                 "mov x0, x29\n\t"
                 "mov x1, x20\n\t"
                 "mov x2, x21\n\t"
                 "mov x3, x22\n\t"
                 "mov x4, x23\n\t"
                 "mov x5, x24\n\t"
                 "mov x6, #11\n\t"
                 "bl _tcti_bitfield_helper\n\t"
                 "bl _tcti_c_call_epilogue\n\t"
                 "ldp x1, x2, [x29, #16]\n\t"
                 "ldp x3, x4, [x29, #32]\n\t"
                 "ldp x5, x6, [x29, #48]\n\t"
                 "ldp x7, x8, [x29, #64]\n\t"
                 "ldp x9, x10, [x29, #80]\n\t"
                 "ldp x11, x12, [x29, #96]\n\t"
                 "ldr x13, [x29, #112]\n\t"
                 "cmp x20, #13\n\t"
                 "b.hs 1f\n\t"
                 "mov x26, x20\n\t"
                 "bl _tcti_sync_hot_reg_from_cpu\n\t"
                 "1:\n\t"
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
                 "str x13, [x29, #112]\n\t"
                 "bl _tcti_c_call_prologue\n\t"
                 "mov x0, x29\n\t"
                 "mov x1, x20\n\t"
                 "mov x2, x21\n\t"
                 "mov x3, x22\n\t"
                 "mov x4, x23\n\t"
                 "mov x5, x24\n\t"
                 "mov x6, #12\n\t"
                 "bl _tcti_bitfield_helper\n\t"
                 "bl _tcti_c_call_epilogue\n\t"
                 "ldp x1, x2, [x29, #16]\n\t"
                 "ldp x3, x4, [x29, #32]\n\t"
                 "ldp x5, x6, [x29, #48]\n\t"
                 "ldp x7, x8, [x29, #64]\n\t"
                 "ldp x9, x10, [x29, #80]\n\t"
                 "ldp x11, x12, [x29, #96]\n\t"
                 "ldr x13, [x29, #112]\n\t"
                 "cmp x20, #13\n\t"
                 "b.hs 1f\n\t"
                 "mov x26, x20\n\t"
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
                 "str x13, [x29, #112]\n\t"
                 "bl _tcti_c_call_prologue\n\t"
                 "mov x0, x29\n\t"
                 "mov x1, x20\n\t"
                 "mov x2, x21\n\t"
                 "mov x3, x22\n\t"
                 "mov x4, x23\n\t"
                 "mov x5, x24\n\t"
                 "mov x6, #13\n\t"
                 "bl _tcti_bitfield_helper\n\t"
                 "bl _tcti_c_call_epilogue\n\t"
                 "ldp x1, x2, [x29, #16]\n\t"
                 "ldp x3, x4, [x29, #32]\n\t"
                 "ldp x5, x6, [x29, #48]\n\t"
                 "ldp x7, x8, [x29, #64]\n\t"
                 "ldp x9, x10, [x29, #80]\n\t"
                 "ldp x11, x12, [x29, #96]\n\t"
                 "ldr x13, [x29, #112]\n\t"
                 "cmp x20, #13\n\t"
                 "b.hs 1f\n\t"
                 "mov x26, x20\n\t"
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
                 "str x13, [x29, #112]\n\t"
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
                 "ldr x13, [x29, #112]\n\t"
                 "cmp x19, #13\n\t"
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
                 "str x13, [x29, #112]\n\t"
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
                 "ldr x13, [x29, #112]\n\t"
                 "cmp x19, #13\n\t"
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
                 "str x13, [x29, #112]\n\t"
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
                 "ldr x13, [x29, #112]\n\t"
                 "cmp x19, #13\n\t"
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
                 "str x13, [x29, #112]\n\t"
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
                 "ldr x13, [x29, #112]\n\t"
                 "cmp x19, #13\n\t"
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
                 "str x13, [x29, #112]\n\t"
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
                 "ldr x13, [x29, #112]\n\t"
                 "cmp x19, #13\n\t"
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
                 "str x13, [x29, #112]\n\t"
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
                 "ldr x13, [x29, #112]\n\t"
                 "cmp x19, #13\n\t"
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
                 "str x13, [x29, #112]\n\t"
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
                 "ldr x13, [x29, #112]\n\t"
                 "cmp x19, #13\n\t"
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
                 "str x13, [x29, #112]\n\t"
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
                 "ldr x13, [x29, #112]\n\t"
                 "cmp x19, #13\n\t"
                 "b.hs 1f\n\t"
                 "mov x26, x19\n\t"
                 "bl _tcti_sync_hot_reg_from_cpu\n\t"
                 "1:\n\t"
                 "ldr x27, [x28], #8\n\t"
                 "br x27\n\t");
}

tcti_gadget_t gadget_div_fallback = gadget_div_fallback_impl;

static unsigned tcti_highest_set_bit32(uint32_t value)
{
    for (int bit = 31; bit >= 0; bit--) {
        if (value & (1u << bit))
            return (unsigned)bit;
    }
    return UINT_MAX;
}

static uint64_t tcti_ones(unsigned width)
{
    if (width >= 64)
        return UINT64_MAX;
    if (width == 0)
        return 0;
    return (1ULL << width) - 1ULL;
}

static uint64_t tcti_ror_width(uint64_t value, unsigned amount, unsigned width)
{
    uint64_t mask = tcti_ones(width);
    value &= mask;
    amount %= width;
    if (amount == 0)
        return value;
    return ((value >> amount) | (value << (width - amount))) & mask;
}

static uint64_t tcti_replicate_element(uint64_t element, unsigned element_width,
                                       unsigned register_width)
{
    uint64_t result = 0;
    uint64_t mask = tcti_ones(element_width);
    element &= mask;
    for (unsigned bit = 0; bit < register_width; bit += element_width)
        result |= element << bit;
    return result & tcti_ones(register_width);
}

static int tcti_decode_bit_masks(unsigned n, unsigned imms, unsigned immr,
                                 unsigned register_width, uint64_t *wmask,
                                 uint64_t *tmask)
{
    uint32_t len_input = (uint32_t)((n << 6) | ((~imms) & 0x3f));
    unsigned len = tcti_highest_set_bit32(len_input);
    if (len == UINT_MAX || len < 1)
        return -1;
    if (register_width == 32 && len > 5)
        return -1;

    unsigned levels = (1u << len) - 1u;
    unsigned s = imms & levels;
    unsigned r = immr & levels;
    unsigned diff = (s - r) & levels;
    unsigned element_width = 1u << len;

    uint64_t welem = tcti_ones(s + 1);
    uint64_t telem = tcti_ones(diff + 1);
    *wmask = tcti_replicate_element(tcti_ror_width(welem, r, element_width), element_width,
                                    register_width);
    *tmask = tcti_replicate_element(telem, element_width, register_width);
    return 0;
}

__attribute__((used)) void tcti_bitfield_helper(struct cpu_state *cpu, uint64_t rd,
                                                uint64_t rn, uint64_t immr, uint64_t imms,
                                                uint64_t is_64bit, uint64_t subtype)
{
    unsigned register_width = is_64bit ? 64 : 32;
    uint64_t width_mask = tcti_ones(register_width);
    uint64_t src = tcti_read_reg_or_zr(cpu, (int)rn) & width_mask;
    uint64_t dst = tcti_read_reg_or_zr(cpu, (int)rd) & width_mask;
    uint64_t wmask = 0;
    uint64_t tmask = 0;
    unsigned n = is_64bit ? 1u : 0u;

    if (tcti_decode_bit_masks(n, (unsigned)imms, (unsigned)immr, register_width, &wmask,
                              &tmask) < 0)
        return;

    uint64_t bot = tcti_ror_width(src, (unsigned)immr, register_width) & wmask;
    uint64_t result;

    switch (subtype) {
    case 11: { // SBFM
        uint64_t sign = (src >> (imms & (register_width - 1))) & 1ULL;
        uint64_t top = sign ? (width_mask & ~tmask) : 0;
        result = top | bot;
        break;
    }
    case 12: // BFM
        result = (dst & ~wmask) | bot;
        break;
    case 13: // UBFM
        result = bot & tmask;
        break;
    default:
        return;
    }

    tcti_write_reg_or_zr(cpu, (int)rd, result & width_mask, is_64bit != 0);
}

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
                 "str x13, [x29, #112]\n\t"
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
                 "ldr x13, [x29, #112]\n\t"
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
                 "str x13, [x29, #112]\n\t"
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
                 "ldr x13, [x29, #112]\n\t"
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
                 "str x13, [x29, #112]\n\t"
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
                 "ldr x13, [x29, #112]\n\t"
                 "cmp x19, #13\n\t"
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
                 "cmp x20, #13\n\t"
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
                 "cmp x20, #13\n\t"
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

__attribute__((naked)) void tcti_sync_hot_reg_from_cpu(void)
{
    asm volatile("cmp x26, #13\n\t"
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
        // Requirements: both Rn and Rt in 0-12, 64-bit, offset mode, meta=0
        "cmp x20, #13\n\t" // Is Rt hot (0-12)?
        "b.hs 91f\n\t"     // Branch to nonhot counter
        "cmp x21, #13\n\t" // Is Rn hot (0-12)?
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

        // Get base register value (hot, in x1-x13) using computed goto
        // Branch table for Rn 0-12
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
        "b 105f\n\t" // Rn=15 -> memory-backed path before this table

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
        // x20 still holds original Rt (0-12)
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
        "str x13, [x29, #112]\n\t"
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
        "ldr x13, [x29, #112]\n\t"
        "cmp x0, #0\n\t"
        "b.ne 1f\n\t"
        "cmp x20, #13\n\t"
        "b.hs 160f\n\t"
        "mov x26, x20\n\t"
        "bl _tcti_sync_hot_reg_from_cpu\n\t"
        "160:\n\t"
        "cmp x24, #0\n\t"
        "b.eq 161f\n\t"
        "cmp x21, #13\n\t"
        "b.hs 161f\n\t"
        "mov x26, x21\n\t"
        "bl _tcti_sync_hot_reg_from_cpu\n\t"
        "161:\n\t"
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
        "cmp x21, #13\n\t" // Is Rn hot (0-12)?
        "b.hs 91f\n\t"
        "cmp x23, #3\n\t" // Is size 64-bit?
        "b.ne 92f\n\t"
        "cmp x24, #0\n\t" // Offset addressing only.
        "b.ne 93f\n\t"
        "cmp x25, #0\n\t" // No register offset / extension metadata.
        "b.ne 94f\n\t"
        "cmp x20, #13\n\t" // Rt hot is supported.
        "b.lo 60f\n\t"
        "cmp x20, #31\n\t" // XZR zero stores are supported.
        "b.ne 91f\n\t"

        "60:\n\t"

        // Get base register value (hot, in x1-x13) using computed goto
        // Branch table for Rn 0-12
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
        "b 105f\n\t" // Rn=15 -> memory-backed path before this table

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

        // Get source register value (Rt, hot, in x1-x13) using computed goto
        // x20 still holds original Rt (0-12)
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
        "b 145f\n\t" // Rt=15 -> memory-backed path before this table

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
        "str x13, [x29, #112]\n\t"
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
        "ldr x13, [x29, #112]\n\t"
        "cmp x0, #0\n\t" // x0 still has return value from helper
        "b.ne 1f\n\t"
        "cmp x24, #0\n\t"
        "b.eq 160f\n\t"
        "cmp x21, #13\n\t"
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
                 "str x13, [x29, #112]\n\t"
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
                 "str x13, [x29, #112]\n\t"
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
                 "str x13, [x29, #112]\n\t"
                 "bl _tcti_c_call_prologue\n\t"
                 "mov x0, x29\n\t"
                 "mov x1, x19\n\t"
                 "mov x2, x20\n\t"
                 "mov x3, x21\n\t"
                 "mov x4, x22\n\t"
                 "mov x5, x23\n\t"
                 "bl _tcti_simd_mov_gpr_from_vec_helper\n\t"
                 "bl _tcti_c_call_epilogue\n\t"
                 "cmp x19, #13\n\t"
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
                 "str x13, [x29, #112]\n\t"
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
                 "cmp x20, #13\n\t"
                 "b.hs 1f\n\t"
                 "mov x26, x20\n\t"
                 "bl _tcti_sync_hot_reg_from_cpu\n\t"
                 "1:\n\t"
                 "cmp x22, #13\n\t"
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
                 "str x13, [x29, #112]\n\t"
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
                 "cmp x22, #13\n\t"
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
                 "str x13, [x29, #112]\n\t"
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
                 "ldr x13, [x29, #112]\n\t"
                 "cmp x0, #0\n\t"
                 "b.ne 1f\n\t"
                 "cmp x20, #13\n\t"
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
                 "str x13, [x29, #112]\n\t"
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
                 "ldr x13, [x29, #112]\n\t"
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
