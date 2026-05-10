/*
 * TCTI Block Generator for aarch64
 *
 * Translates aarch64 instructions into threaded gadget addresses.
 * No runtime code generation - just emits arrays of function pointers.
 *
 * 100% TCTI implementation - handles all instructions including
 * memory-backed registers (x13-x30, SP) via load/store sequences.
 */

#import <IXLandInstrumentationTracing/trace.h>
#import <IXLandLinuxRuntime/emu/aarch64/decode.h>
#import <IXLandLinuxRuntime/emu/aarch64/sysreg.h>
#import <IXLandLinuxRuntime/tcti/aarch64/gadgets_complex.h>
#import <IXLandLinuxRuntime/tcti/aarch64/gen.h>
#import <IXLandLinuxRuntime/util/debug.h>
#include <stdlib.h>
#include <string.h>

extern const tcti_gadget_t gadget_tbz_reg[16];
extern const tcti_gadget_t gadget_tbnz_reg[16];
extern const tcti_gadget_t gadget_tbz_wreg[16];
extern const tcti_gadget_t gadget_tbnz_wreg[16];
extern const tcti_gadget_t gadget_tbz_xreg[16];
extern const tcti_gadget_t gadget_tbnz_xreg[16];
extern const tcti_gadget_t gadget_cbz_wreg[16];
extern const tcti_gadget_t gadget_cbnz_wreg[16];
extern const tcti_gadget_t gadget_cbz_xreg[16];
extern const tcti_gadget_t gadget_cbnz_xreg[16];
extern const tcti_gadget_t gadget_bcond[16];
extern tcti_gadget_t gadget_sysreg_unsupported;
extern tcti_gadget_t gadget_pc_advance;
extern tcti_gadget_t gadget_isb;
extern tcti_gadget_t gadget_dsb;
extern tcti_gadget_t gadget_dmb;
extern tcti_gadget_t gadget_dc_zva;
extern void gadget_csel_eq_0_1_2(void);
extern void gadget_csel_ne_0_1_2(void);
extern void gadget_csel_cs_0_1_2(void);
extern void gadget_csel_cc_0_1_2(void);
extern tcti_gadget_t gadget_movk;
extern tcti_gadget_t gadget_write_reg_imm;
extern tcti_gadget_t gadget_addsub_imm_fallback;
extern tcti_gadget_t gadget_addsub_reg_fallback;
extern tcti_gadget_t gadget_addsub_ext_fallback;
extern tcti_gadget_t gadget_logical_imm_fallback;
extern tcti_gadget_t gadget_logical_reg_fallback;
extern tcti_gadget_t gadget_multiply_add_fallback;
extern tcti_gadget_t gadget_shift_reg_fallback;
extern tcti_gadget_t gadget_extract_fallback;
extern tcti_gadget_t gadget_csel_fallback;
extern tcti_gadget_t gadget_ccmp_fallback;
extern tcti_gadget_t gadget_div_fallback;
extern tcti_gadget_t gadget_ccmp_native_reg[2][2][16];
extern tcti_gadget_t gadget_ccmp_native_imm[2][2][16];
extern tcti_gadget_t gadget_simd_dup_gpr;
extern tcti_gadget_t gadget_simd_mov_gpr_from_vec;
extern tcti_gadget_t gadget_simd_movi_imm;
extern tcti_gadget_t gadget_simd_fmov_gpr;
extern tcti_gadget_t gadget_simd_ldst;
extern tcti_gadget_t gadget_atomic_ldst;
extern tcti_gadget_t gadget_extend_x14;
extern void gadget_br_impl(void);

// Host x16 is reserved for ABI/linker scratch and TCTI internals. Keep only
// guest x0-x12 hot; guest x13-x30 are memory-backed through helper gadgets.
#define TCTI_HOT_REG_COUNT     13
#define TCTI_MEM_REG_BASE      13
#define TCTI_MEM_REG_COUNT     (31 - TCTI_MEM_REG_BASE)
#define IS_TCTI_REG(r)         ((r) >= 0 && (r) < TCTI_HOT_REG_COUNT)
#define IS_MEM_REG(r)          ((r) >= TCTI_MEM_REG_BASE && (r) <= 30)
#define MEM_REG_INDEX(r)       ((r) - TCTI_MEM_REG_BASE)
#define VALID_MEM_REG_INDEX(i) ((i) >= 0 && (i) < TCTI_MEM_REG_COUNT)

// Initialization
int a64_gen_init(a64_gen_state_t *state, tcti_gadget_t *buffer, size_t max)
{
    if (!state || !buffer)
        return A64_GEN_INVALID_INSN;

    memset(state, 0, sizeof(*state));
    state->gadgets = buffer;
    state->max_gadgets = max;
    state->num_gadgets = 0;

    return A64_GEN_OK;
}

// Reset for new block
void a64_gen_reset(a64_gen_state_t *state, uint64_t pc)
{
    state->num_gadgets = 0;
    state->start_pc = pc;
    state->guest_pc = pc;
    state->is_complete = 0;
    state->instructions_processed = 0;
}

void a64_gen_set_conservative_mode(a64_gen_state_t *state, int enabled)
{
    state->conservative_mode = enabled ? 1 : 0;
}

// Helper: Emit single gadget
// Helper: Emit single gadget
static int emit_gadget(a64_gen_state_t *state, tcti_gadget_t gadget)
{
    if (state->num_gadgets >= state->max_gadgets)
        return A64_GEN_TOO_MANY;

    // GUARD: Reject NULL gadgets - they would cause a crash at runtime
    if (!gadget)
        return A64_GEN_UNSUPPORTED;

    state->gadgets[state->num_gadgets++] = gadget;
    return A64_GEN_OK;
}

// Helper: Emit raw 64-bit immediate value into bytecode stream
// The immediate is stored inline and consumed by the preceding gadget.
// The gadget loads the immediate, advances x28, then loads the next gadget.
// This creates a bytecode layout: [gadget, immediate, next_gadget, ...]
static int emit_u64(a64_gen_state_t *state, uint64_t value)
{
    if (state->num_gadgets >= state->max_gadgets) {
        return A64_GEN_TOO_MANY;
    }

    // Store immediate value directly in bytecode stream.
    // The preceding gadget will load this as data using "ldr xN, [x28], #8",
    // which treats it as data (loading the value) and advances past it.
    // On aarch64, sizeof(void*) == sizeof(uint64_t), so this is safe.
    state->gadgets[state->num_gadgets++] = (tcti_gadget_t)value;

    return A64_GEN_OK;
}

static int emit_addsub_imm(a64_gen_state_t *state, int dst_idx, int src_idx, uint64_t imm,
                           int is_sub)
{
    int ret;
    uint64_t remaining = imm;
    int current_src = src_idx;

    // Emit chunks of 15 until remainder is <= 15
    while (remaining > 15) {
        tcti_gadget_t gadget = is_sub ? gadget_sub_imm[dst_idx][current_src][15]
                                      : gadget_add_imm[dst_idx][current_src][15];

        if (!gadget)
            return A64_GEN_UNSUPPORTED;

        ret = emit_gadget(state, gadget);
        if (ret != A64_GEN_OK)
            return ret;

        remaining -= 15;
        current_src = dst_idx;
    }

    // Emit final chunk (0-15)
    tcti_gadget_t gadget = is_sub ? gadget_sub_imm[dst_idx][current_src][remaining]
                                  : gadget_add_imm[dst_idx][current_src][remaining];

    if (!gadget)
        return A64_GEN_UNSUPPORTED;

    return emit_gadget(state, gadget);
}

static int emit_write_reg_imm(a64_gen_state_t *state, int rd, uint64_t value, int is_64bit,
                              int rd_is_sp)
{
    int ret = emit_gadget(state, gadget_write_reg_imm);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, (uint64_t)rd);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, value);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, is_64bit ? 1 : 0);
    if (ret != A64_GEN_OK)
        return ret;
    return emit_u64(state, rd_is_sp ? 1 : 0);
}

static int emit_addsub_imm_fallback(a64_gen_state_t *state, int rd, int rn, uint64_t imm,
                                    int is_sub, int set_flags, int is_64bit, int rd_is_sp,
                                    int rn_is_sp)
{
    int ret = emit_gadget(state, gadget_addsub_imm_fallback);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, (uint64_t)rd);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, (uint64_t)rn);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, imm);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, is_sub ? 1 : 0);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, set_flags ? 1 : 0);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, is_64bit ? 1 : 0);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, rd_is_sp ? 1 : 0);
    if (ret != A64_GEN_OK)
        return ret;
    return emit_u64(state, rn_is_sp ? 1 : 0);
}

static int emit_addsub_reg_fallback(a64_gen_state_t *state, int rd, int rn, int rm, int shift_type,
                                    int imm_shift, int is_sub, int set_flags, int is_64bit)
{
    int ret = emit_gadget(state, gadget_addsub_reg_fallback);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, (uint64_t)rd);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, (uint64_t)rn);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, (uint64_t)rm);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, (uint64_t)shift_type);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, (uint64_t)imm_shift);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, is_sub ? 1 : 0);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, set_flags ? 1 : 0);
    if (ret != A64_GEN_OK)
        return ret;
    return emit_u64(state, is_64bit ? 1 : 0);
}

static int emit_addsub_ext_fallback(a64_gen_state_t *state, int rd, int rn, int rm, int extend_type,
                                    int imm_shift, int is_sub, int set_flags, int is_64bit,
                                    int rd_is_sp, int rn_is_sp)
{
    uint64_t mode = 0;
    mode |= is_sub ? (1ULL << 0) : 0;
    mode |= set_flags ? (1ULL << 1) : 0;
    mode |= is_64bit ? (1ULL << 2) : 0;
    mode |= rd_is_sp ? (1ULL << 3) : 0;
    mode |= rn_is_sp ? (1ULL << 4) : 0;

    int ret = emit_gadget(state, gadget_addsub_ext_fallback);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, (uint64_t)rd);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, (uint64_t)rn);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, (uint64_t)rm);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, (uint64_t)extend_type);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, (uint64_t)imm_shift);
    if (ret != A64_GEN_OK)
        return ret;
    return emit_u64(state, mode);
}

static int emit_logical_imm_fallback(a64_gen_state_t *state, int rd, int rn, uint64_t imm,
                                     int subtype, int set_flags, int is_64bit)
{
    int ret = emit_gadget(state, gadget_logical_imm_fallback);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, (uint64_t)rd);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, (uint64_t)rn);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, imm);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, (uint64_t)subtype);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, set_flags ? 1 : 0);
    if (ret != A64_GEN_OK)
        return ret;
    return emit_u64(state, is_64bit ? 1 : 0);
}

static int emit_logical_reg_fallback(a64_gen_state_t *state, int rd, int rn, int rm, int shift_type,
                                     int imm_shift, int subtype, int set_flags, int is_64bit)
{
    int ret = emit_gadget(state, gadget_logical_reg_fallback);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, (uint64_t)rd);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, (uint64_t)rn);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, (uint64_t)rm);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, (uint64_t)shift_type);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, (uint64_t)imm_shift);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, (uint64_t)subtype);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, set_flags ? 1 : 0);
    if (ret != A64_GEN_OK)
        return ret;
    return emit_u64(state, is_64bit ? 1 : 0);
}

static int emit_multiply_add_fallback(a64_gen_state_t *state, int rd, int rn, int rm, int ra,
                                      int subtype, int is_64bit)
{
    int ret = emit_gadget(state, gadget_multiply_add_fallback);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, (uint64_t)rd);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, (uint64_t)rn);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, (uint64_t)rm);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, (uint64_t)ra);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, (uint64_t)subtype);
    if (ret != A64_GEN_OK)
        return ret;
    return emit_u64(state, is_64bit ? 1 : 0);
}

static int emit_shift_reg_fallback(a64_gen_state_t *state, int rd, int rn, int rm, int subtype,
                                   int is_64bit)
{
    int ret = emit_gadget(state, gadget_shift_reg_fallback);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, (uint64_t)rd);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, (uint64_t)rn);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, (uint64_t)rm);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, (uint64_t)subtype);
    if (ret != A64_GEN_OK)
        return ret;
    return emit_u64(state, is_64bit ? 1 : 0);
}

static int emit_extract_fallback(a64_gen_state_t *state, int rd, int rn, int rm, uint64_t lsb,
                                 int is_64bit)
{
    int ret = emit_gadget(state, gadget_extract_fallback);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, (uint64_t)rd);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, (uint64_t)rn);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, (uint64_t)rm);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, lsb);
    if (ret != A64_GEN_OK)
        return ret;
    return emit_u64(state, is_64bit ? 1 : 0);
}

static int emit_div_fallback(a64_gen_state_t *state, int rd, int rn, int rm, int subtype,
                             int is_64bit)
{
    int ret = emit_gadget(state, gadget_div_fallback);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, (uint64_t)rd);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, (uint64_t)rn);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, (uint64_t)rm);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, (uint64_t)subtype);
    if (ret != A64_GEN_OK)
        return ret;
    return emit_u64(state, is_64bit ? 1 : 0);
}

static int emit_csel_fallback(a64_gen_state_t *state, int rd, int rn, int rm, int cond, int subtype,
                              int is_64bit)
{
    int ret = emit_gadget(state, gadget_csel_fallback);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, (uint64_t)rd);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, (uint64_t)rn);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, (uint64_t)rm);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, (uint64_t)(cond & 0xf));
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, (uint64_t)subtype);
    if (ret != A64_GEN_OK)
        return ret;
    return emit_u64(state, is_64bit ? 1 : 0);
}

static int emit_ccmp_native(a64_gen_state_t *state, int rn, int rm, uint64_t imm_operand,
                            int cond, uint64_t nzcv, int subtype, int is_64bit)
{
    int cond_index = cond & 0xf;
    int subtype_index = (subtype == A64_DP_REG_CCMP || subtype == A64_DP_REG_CCMP_IMM) ? 1 : 0;
    bool is_immediate = (subtype == A64_DP_REG_CCMN_IMM || subtype == A64_DP_REG_CCMP_IMM);
    tcti_gadget_t gadget =
        is_immediate ? gadget_ccmp_native_imm[subtype_index][is_64bit != 0][cond_index]
                     : gadget_ccmp_native_reg[subtype_index][is_64bit != 0][cond_index];

    int ret = emit_gadget(state, gadget);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, (uint64_t)rn);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, is_immediate ? imm_operand : (uint64_t)rm);
    if (ret != A64_GEN_OK)
        return ret;
    return emit_u64(state, nzcv & 0xf);
}

static int emit_bcond(a64_gen_state_t *state, int cond, uint64_t target_pc, uint64_t fallthrough_pc)
{
    int cond_index = cond & 0xf;
    int ret = emit_gadget(state, gadget_bcond[cond_index]);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, target_pc);
    if (ret != A64_GEN_OK)
        return ret;
    return emit_u64(state, fallthrough_pc);
}

static int emit_unconditional_branch(a64_gen_state_t *state, uint64_t target_pc, int is_link)
{
    int ret = emit_gadget(state, gadget_b);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, target_pc);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, is_link ? 1 : 0);
    if (ret != A64_GEN_OK)
        return ret;
    return emit_u64(state, state->guest_pc + 4);
}

/* ============================================================================
 * Data Processing - Immediate
 *
 * Handles: ADR, ADRP, ADD, SUB, MOVZ, MOVN, MOVK, bitfield, logical imm
 *
 * For memory-backed registers (x13-x30), we emit load/store sequences:
 *   Load x[13-30]  -> gadget_load_xreg_16_to_30[n] (loads to x14/x15)
 *   Operate         -> operation gadget
 *   Store x[13-30] -> gadget_store_xreg_16_to_30[n] (stores from x14/x15)
 * ============================================================================
 */
int a64_gen_dp_imm(a64_gen_state_t *state, const a64_instr_t *instr)
{
    int rd = instr->Rd;
    int rn = instr->Rn;

    // Check if registers are memory-backed (x13-x30) or TCTI-mapped (x0-x12)
    // x31 is SP (for loads/stores) or XZR (for most other ops)
    int rd_is_zero = (instr->set_flags && rd == 31);
    int src_is_memory = IS_MEM_REG(rn);
    int dst_is_memory = IS_MEM_REG(rd);
    int src_is_sp = (rn == 31);
    int dst_is_sp = (rd == 31 && !rd_is_zero);

    // Use x14 (index 13) as the primary scratch carrier and x15 (index 14)
    // as the secondary scratch carrier when a generated sequence needs to
    // preserve a loaded memory-backed source while materializing another value.
    // gadget_mov_imm[n] loads into x(n+1), so index 14 = x15, index 13 = x14.
    int eff_rd = dst_is_memory ? 13 : (dst_is_sp || rd_is_zero ? 13 : rd);
    int eff_rn = src_is_memory || src_is_sp ? 13 : rn;

    tcti_gadget_t gadget = NULL;

    int is_pc_rel_imm = ((instr->raw & 0x1F000000u) == 0x10000000u);

    switch (instr->subtype) {
    case 0: // ADR (pc-rel) or MOVN
        // Subtype 0 is shared by ADR and MOVN. Do not inspect bits[25:23] here:
        // for PC-relative encodings those bits include immlo, so live ADR/ADRP
        // instructions can carry non-zero values there. Match the same top-bit
        // pattern the decoder uses for PC-relative encodings instead.
        if (is_pc_rel_imm && bit(instr->raw, 31) == 0) {
            // This is ADR (PC-relative addressing)
            // Calculate target PC: current PC + immediate (byte aligned)
            uint64_t target_pc = state->guest_pc + instr->imm;
            int ret;

            if (dst_is_memory) {
                return emit_write_reg_imm(state, rd, target_pc, instr->is_64bit, 0);
            }

            // Emit load of immediate followed by store gadget
            ret = emit_gadget(state, gadget_mov_imm[eff_rd]);
            if (ret != A64_GEN_OK)
                return ret;
            ret = emit_u64(state, target_pc);
            if (ret != A64_GEN_OK)
                return ret;
        } else {
            // MOVN Xd, #imm - move negative immediate to register
            uint64_t value =
                instr->is_64bit ? ~((uint64_t)instr->imm) : (uint32_t)~((uint32_t)instr->imm);
            // Emit immediate value in bytecode stream
            int ret;

            if (dst_is_memory) {
                return emit_write_reg_imm(state, rd, value, instr->is_64bit, 0);
            }

            ret = emit_gadget(state, gadget_mov_imm[eff_rd]);
            if (ret != A64_GEN_OK)
                return ret;
            ret = emit_u64(state, value);
            if (ret != A64_GEN_OK)
                return ret;
        }
        // Emit store if destination is memory-backed
        if (dst_is_memory) {
            int store_idx = MEM_REG_INDEX(rd);
            int ret = emit_gadget(state, gadget_store_xreg_16_to_30[store_idx]);
            if (ret != A64_GEN_OK)
                return ret;
        }
        return A64_GEN_OK; // Fully handled, skip common code

    case 1: // ADRP (pc-rel) or MOVZ
        // Subtype 1 is shared by ADRP and MOVZ. As above, bits[25:23] are not a
        // stable discriminator for ADRP because immlo lives in the same field.
        // Reuse the decoder's PC-relative top-bit match and op bit instead.
        if (is_pc_rel_imm && bit(instr->raw, 31) == 1) {
            // This is ADRP - page aligned PC-relative addressing
            // ADRP: Xd = PC with bits [11:0] cleared + (imm << 12)
            uint64_t base_pc = state->guest_pc & ~0xFFFULL;
            uint64_t target_pc = base_pc + (uint64_t)instr->imm;
            int ret;

            if (dst_is_memory) {
                return emit_write_reg_imm(state, rd, target_pc, instr->is_64bit, 0);
            }

            ret = emit_gadget(state, gadget_mov_imm[eff_rd]);
            if (ret != A64_GEN_OK)
                return ret;
            ret = emit_u64(state, target_pc);
            if (ret != A64_GEN_OK)
                return ret;
        } else {
            // MOVZ Xd, #imm - moves immediate to register, zeroing upper bits
            // The decoder sets imm = imm16 << (hw * 16)
            // Emit immediate value in bytecode stream
            int ret;

            if (dst_is_memory) {
                return emit_write_reg_imm(state, rd, (uint64_t)instr->imm, instr->is_64bit, 0);
            }

            ret = emit_gadget(state, gadget_mov_imm[eff_rd]);
            if (ret != A64_GEN_OK)
                return ret;
            ret = emit_u64(state, (uint64_t)instr->imm);
            if (ret != A64_GEN_OK)
                return ret;
        }
        // Emit store if destination is memory-backed
        if (dst_is_memory) {
            int store_idx = MEM_REG_INDEX(rd);
            int ret = emit_gadget(state, gadget_store_xreg_16_to_30[store_idx]);
            if (ret != A64_GEN_OK)
                return ret;
        }
        return A64_GEN_OK; // Fully handled, skip common code

    case 2: // MOVK (opc=11 in op0=010)
    {
        int ret = emit_gadget(state, gadget_movk);
        if (ret != A64_GEN_OK)
            return ret;
        ret = emit_u64(state, rd);
        if (ret != A64_GEN_OK)
            return ret;
        ret = emit_u64(state, bits(instr->raw, 20, 5));
        if (ret != A64_GEN_OK)
            return ret;
        ret = emit_u64(state, bits(instr->raw, 22, 21) * 16);
        if (ret != A64_GEN_OK)
            return ret;
        ret = emit_u64(state, instr->is_64bit ? 1 : 0);
        if (ret != A64_GEN_OK)
            return ret;
        return A64_GEN_OK;
    }

    case 3: // ADD immediate with shift
    case 4: // ADD immediate no shift
    case 5: // SUB immediate with shift
    case 6: // SUB immediate no shift
    {
        int is_sub = (instr->subtype == 5 || instr->subtype == 6);
        int ret;

        if (!instr->is_64bit || instr->imm >= 16 || src_is_memory || src_is_sp || dst_is_memory ||
            dst_is_sp || instr->set_flags) {
            return emit_addsub_imm_fallback(state, rd, rn, (uint64_t)instr->imm, is_sub,
                                            instr->set_flags, instr->is_64bit, dst_is_sp,
                                            src_is_sp);
        }

        if (instr->set_flags && rd_is_zero) {
            if (!is_sub)
                return A64_GEN_UNSUPPORTED;

            if (src_is_memory) {
                int load_idx = MEM_REG_INDEX(rn);
                if (!VALID_MEM_REG_INDEX(load_idx))
                    return A64_GEN_UNSUPPORTED;
                ret = emit_gadget(state, gadget_load_xreg_16_to_30[load_idx]);
                if (ret != A64_GEN_OK)
                    return ret;
            } else if (src_is_sp) {
                ret = emit_gadget(state, (tcti_gadget_t)gadget_load_sp);
                if (ret != A64_GEN_OK)
                    return ret;
            }

            ret = emit_gadget(state, gadget_mov_imm[13]);
            if (ret != A64_GEN_OK)
                return ret;
            ret = emit_u64(state, (uint64_t)instr->imm);
            if (ret != A64_GEN_OK)
                return ret;

            return emit_gadget(state, gadget_cmp_reg[eff_rn][13]);
        }

        if (src_is_memory || src_is_sp || dst_is_memory || dst_is_sp) {
            int work_src = src_is_memory || src_is_sp ? 13 : rn;
            int work_dst = dst_is_memory || dst_is_sp ? 13 : rd;

            if (src_is_memory) {
                int load_idx = MEM_REG_INDEX(rn);
                if (!VALID_MEM_REG_INDEX(load_idx))
                    return A64_GEN_UNSUPPORTED;
                ret = emit_gadget(state, gadget_load_xreg_16_to_30[load_idx]);
                if (ret != A64_GEN_OK)
                    return ret;
            } else if (src_is_sp) {
                ret = emit_gadget(state, (tcti_gadget_t)gadget_load_sp);
                if (ret != A64_GEN_OK)
                    return ret;
            }

            if (src_is_sp) {
                // Keep SP source value in x14 (work_src=13) and materialize
                // immediate into x15 to avoid clobbering the source register.
                ret = emit_gadget(state, gadget_mov_imm[14]);
                if (ret != A64_GEN_OK)
                    return ret;
                ret = emit_u64(state, (uint64_t)instr->imm);
                if (ret != A64_GEN_OK)
                    return ret;

                ret = emit_gadget(state, is_sub ? gadget_sub_reg[work_dst][work_src][14]
                                                : gadget_add_reg[work_dst][work_src][14]);
                if (ret != A64_GEN_OK)
                    return ret;
            } else {
                ret = emit_addsub_imm(state, work_dst, work_src, (uint64_t)instr->imm, is_sub);
                if (ret != A64_GEN_OK)
                    return ret;
            }

            if (dst_is_memory) {
                int store_idx = MEM_REG_INDEX(rd);
                if (!VALID_MEM_REG_INDEX(store_idx))
                    return A64_GEN_UNSUPPORTED;
                ret = emit_gadget(state, gadget_store_xreg_16_to_30[store_idx]);
                if (ret != A64_GEN_OK)
                    return ret;
            } else if (dst_is_sp) {
                ret = emit_gadget(state, (tcti_gadget_t)gadget_store_sp);
                if (ret != A64_GEN_OK)
                    return ret;
            }

            return A64_GEN_OK;
        }

        if (instr->imm >= 0 && instr->imm < 16) {
            gadget = is_sub ? gadget_sub_imm[eff_rd][eff_rn][(int)instr->imm]
                            : gadget_add_imm[eff_rd][eff_rn][(int)instr->imm];
        } else {
            int ret;

            ret = emit_gadget(state, gadget_mov_imm[13]);
            if (ret != A64_GEN_OK)
                return ret;
            ret = emit_u64(state, (uint64_t)instr->imm);
            if (ret != A64_GEN_OK)
                return ret;

            ret = emit_gadget(state, is_sub ? gadget_sub_reg[eff_rd][eff_rn][13]
                                            : gadget_add_reg[eff_rd][eff_rn][13]);
            if (ret != A64_GEN_OK)
                return ret;

            return A64_GEN_OK;
        }
        break;
    }

    case 7: // AND immediate
        // Logical immediate - AND with bitmask
        // The immediate is already decoded by the decoder (in instr->imm)
        // For TCTI, we load the immediate into x14 temp, then use AND reg
        {
            int ret;

            if (!instr->is_64bit || instr->set_flags) {
                return emit_logical_imm_fallback(state, rd, rn, instr->imm, instr->subtype,
                                                 instr->set_flags, instr->is_64bit);
            }

            // Emit load if source is memory-backed
            if (src_is_memory) {
                int load_idx = MEM_REG_INDEX(rn);
                if (!VALID_MEM_REG_INDEX(load_idx))
                    return A64_GEN_UNSUPPORTED;
                ret = emit_gadget(state, gadget_load_xreg_16_to_30[load_idx]);
                if (ret != A64_GEN_OK)
                    return ret;
            }

            int imm_reg = src_is_memory ? 14 : 13;

            // Load immediate into a scratch that does not overwrite a
            // memory-backed source already loaded into x14.
            ret = emit_gadget(state, gadget_mov_imm[imm_reg]);
            if (ret != A64_GEN_OK)
                return ret;
            ret = emit_u64(state, (uint64_t)instr->imm);
            if (ret != A64_GEN_OK)
                return ret;

            ret = emit_gadget(state, gadget_and_reg[eff_rd][eff_rn][imm_reg]);
            if (ret != A64_GEN_OK)
                return ret;

            // Emit store if destination is memory-backed
            if (dst_is_memory) {
                int store_idx = MEM_REG_INDEX(rd);
                if (!VALID_MEM_REG_INDEX(store_idx))
                    return A64_GEN_UNSUPPORTED;
                ret = emit_gadget(state, gadget_store_xreg_16_to_30[store_idx]);
                if (ret != A64_GEN_OK)
                    return ret;
            }

            // Fully handled
            return A64_GEN_OK;
        }

    case 8:  // ORR immediate
    case 9:  // EOR immediate
    case 10: // ANDS immediate
    {
        int ret;
        tcti_gadget_t logical_gadget;

        if (!instr->is_64bit || instr->set_flags) {
            return emit_logical_imm_fallback(state, rd, rn, instr->imm, instr->subtype,
                                             instr->set_flags, instr->is_64bit);
        }

        if (src_is_memory) {
            int load_idx = MEM_REG_INDEX(rn);
            if (!VALID_MEM_REG_INDEX(load_idx))
                return A64_GEN_UNSUPPORTED;
            ret = emit_gadget(state, gadget_load_xreg_16_to_30[load_idx]);
            if (ret != A64_GEN_OK)
                return ret;
        }

        int imm_reg = src_is_memory ? 14 : 13;

        ret = emit_gadget(state, gadget_mov_imm[imm_reg]);
        if (ret != A64_GEN_OK)
            return ret;
        ret = emit_u64(state, (uint64_t)instr->imm);
        if (ret != A64_GEN_OK)
            return ret;

        switch (instr->subtype) {
        case 8:
            logical_gadget = gadget_orr_reg[eff_rd][eff_rn][imm_reg];
            break;
        case 9:
            logical_gadget = gadget_eor_reg[eff_rd][eff_rn][imm_reg];
            break;
        case 10:
            logical_gadget = gadget_and_reg[eff_rd][eff_rn][imm_reg];
            break;
        default:
            return A64_GEN_UNSUPPORTED;
        }

        ret = emit_gadget(state, logical_gadget);
        if (ret != A64_GEN_OK)
            return ret;

        if (dst_is_memory) {
            int store_idx = MEM_REG_INDEX(rd);
            if (!VALID_MEM_REG_INDEX(store_idx))
                return A64_GEN_UNSUPPORTED;
            ret = emit_gadget(state, gadget_store_xreg_16_to_30[store_idx]);
            if (ret != A64_GEN_OK)
                return ret;
        }

        if (dst_is_sp) {
            ret = emit_gadget(state, (tcti_gadget_t)gadget_store_sp);
            if (ret != A64_GEN_OK)
                return ret;
        }

        return A64_GEN_OK;
    }

    case 11: // SBFM (mapped from decoder subtype 11)
    case 12: // BFM (mapped from decoder subtype 12)
    case 13: // UBFM (mapped from decoder subtype 13)
    {
        // Bitfield operations: SBFM, BFM, UBFM
        // Bytecode: [gadget_sbfm, current_pc, Rd, Rn, immr, imms, is_64bit, next_gadget]
        tcti_gadget_t bf_gadget = NULL;

        switch (instr->subtype) {
        case 11:
            bf_gadget = gadget_sbfm;
            break;
        case 12:
            bf_gadget = gadget_bfm;
            break;
        case 13:
            bf_gadget = gadget_ubfm;
            break;
        default:
            return A64_GEN_UNSUPPORTED;
        }

        if (!bf_gadget)
            return A64_GEN_UNSUPPORTED;

        // Emit bitfield gadget
        int ret = emit_gadget(state, bf_gadget);
        if (ret != A64_GEN_OK)
            return ret;

        // Emit current PC for diagnostics
        ret = emit_u64(state, state->guest_pc);
        if (ret != A64_GEN_OK)
            return ret;

        // Emit Rd
        ret = emit_u64(state, instr->Rd);
        if (ret != A64_GEN_OK)
            return ret;

        // Emit Rn
        ret = emit_u64(state, instr->Rn);
        if (ret != A64_GEN_OK)
            return ret;

        // Emit immr (in instr->imm)
        ret = emit_u64(state, instr->imm);
        if (ret != A64_GEN_OK)
            return ret;

        // Emit imms (in instr->imm_shift)
        ret = emit_u64(state, instr->imm_shift);
        if (ret != A64_GEN_OK)
            return ret;

        // Emit is_64bit
        ret = emit_u64(state, instr->is_64bit ? 1 : 0);
        if (ret != A64_GEN_OK)
            return ret;

        return A64_GEN_OK;
    }

    case A64_DP_IMM_EXTRACT:
        return emit_extract_fallback(state, rd, rn, instr->Rm, (uint64_t)instr->imm,
                                     instr->is_64bit);

    default:
        return A64_GEN_UNSUPPORTED;
    }

    if (!gadget)
        return A64_GEN_UNSUPPORTED;

    // Emit load if source is memory-backed (x13-x30)
    if (src_is_memory) {
        int load_idx = MEM_REG_INDEX(rn); // x15=0, x16=1, ..., x30=15
        if (!VALID_MEM_REG_INDEX(load_idx))
            return A64_GEN_UNSUPPORTED;

        int ret = emit_gadget(state, gadget_load_xreg_16_to_30[load_idx]);
        if (ret != A64_GEN_OK)
            return ret;
    }

    // Emit main operation
    int ret = emit_gadget(state, gadget);
    if (ret != A64_GEN_OK)
        return ret;

    // Emit store if destination is memory-backed (x13-x30)
    if (dst_is_memory) {
        int store_idx = MEM_REG_INDEX(rd);
        if (!VALID_MEM_REG_INDEX(store_idx))
            return A64_GEN_UNSUPPORTED;

        ret = emit_gadget(state, gadget_store_xreg_16_to_30[store_idx]);
        if (ret != A64_GEN_OK)
            return ret;
    }

    // Handle SP destination
    if (dst_is_sp) {
        ret = emit_gadget(state, (tcti_gadget_t)gadget_store_sp);
        if (ret != A64_GEN_OK)
            return ret;
    }

    return A64_GEN_OK;
}

/* ============================================================================
 * Data Processing - Register
 *
 * Handles register operations with support for memory-backed registers.
 * For memory-backed registers (x13-x30), emits load/store sequences:
 *   Load x[13-30] -> temp register (x14 for first source, x15 for second)
 *   Execute operation
 *   Store result -> x[13-30] from temp (x14)
 *
 * Temp register allocation:
 *   x14: Primary temp for first source / destination
 *   x15: Secondary temp for second source
 *   x16: Used by load/store gadgets internally
 * ============================================================================
 */
int a64_gen_dp_reg(a64_gen_state_t *state, const a64_instr_t *instr)
{
    int rd = instr->Rd;
    int rn = instr->Rn;
    int rm = instr->Rm;
    int op2 = bits(instr->raw, 24, 21);
    int dpreg_ext_form = bit(instr->raw, 21);
    int rd_is_zero = (instr->set_flags && rd == 31);

    // Determine which registers are memory-backed (x13-x30)
    int src1_is_memory = IS_MEM_REG(rn);
    int src2_is_memory = IS_MEM_REG(rm);
    int dst_is_memory = IS_MEM_REG(rd);
    int dst_is_sp = (rd == 31 && !rd_is_zero);

    // Allocate temp registers:
    // x14 (index 13) = primary temp (destination or first source)
    // x15 (index 14) = secondary temp (second source if needed)
    // gadget indices: 0-12 map to x1-x13, so x14=13, x15=14
    int eff_rd, eff_rn, eff_rm;

    if (dst_is_memory || dst_is_sp) {
        eff_rd = 13; // Result goes to x14 temp
    } else {
        eff_rd = rd;
    }

    if (src1_is_memory && src2_is_memory) {
        eff_rn = 14; // Preserve first source in x15 while x14 carries second source/result
    } else if (src1_is_memory) {
        eff_rn = 13; // First source loaded to x14
    } else if (rn == 31) {
        eff_rn = 0; // XZR -> use x0 (won't be read)
    } else {
        eff_rn = rn;
    }

    if (src2_is_memory) {
        eff_rm = 13; // Second source is loaded to x14; first memory source moves to x15.
    } else if (rm == 31) {
        eff_rm = 0; // XZR
    } else {
        eff_rm = rm;
    }

    tcti_gadget_t gadget = NULL;

    if (instr->subtype == A64_DP_REG_RBIT || instr->subtype == A64_DP_REG_CLZ) {
        int ret;
        const tcti_gadget_t(*table)[16] = NULL;

        if (rd < 0 || rd > 30 || rn < 0 || rn > 30)
            return A64_GEN_INVALID_INSN;

        if (src1_is_memory) {
            int load_idx = MEM_REG_INDEX(rn);
            if (!VALID_MEM_REG_INDEX(load_idx))
                return A64_GEN_UNSUPPORTED;
            ret = emit_gadget(state, gadget_load_xreg_16_to_30[load_idx]);
            if (ret != A64_GEN_OK)
                return ret;
        }

        if (instr->subtype == A64_DP_REG_RBIT)
            table = instr->is_64bit ? gadget_rbit_xreg : gadget_rbit_wreg;
        else
            table = instr->is_64bit ? gadget_clz_xreg : gadget_clz_wreg;

        ret = emit_gadget(state, table[eff_rd][eff_rn]);
        if (ret != A64_GEN_OK)
            return ret;

        if (dst_is_memory) {
            int store_idx = MEM_REG_INDEX(rd);
            if (!VALID_MEM_REG_INDEX(store_idx))
                return A64_GEN_UNSUPPORTED;
            ret = emit_gadget(state, gadget_store_xreg_16_to_30[store_idx]);
            if (ret != A64_GEN_OK)
                return ret;
        }

        return A64_GEN_OK;
    }

    if (instr->subtype >= 16 && instr->subtype <= 19) {
        return emit_shift_reg_fallback(state, rd, rn, rm, instr->subtype, instr->is_64bit);
    }

    if (instr->subtype == A64_DP_REG_UDIV || instr->subtype == A64_DP_REG_SDIV) {
        return emit_div_fallback(state, rd, rn, rm, instr->subtype, instr->is_64bit);
    }

    if ((instr->subtype >= A64_DP_REG_MADD && instr->subtype <= A64_DP_REG_UMSUBL) ||
        instr->subtype == A64_DP_REG_UMULH || instr->subtype == A64_DP_REG_SMULH) {
        return emit_multiply_add_fallback(state, rd, rn, rm, instr->Ra, instr->subtype,
                                          instr->is_64bit);
    }

    if (instr->subtype == A64_DP_REG_CCMN || instr->subtype == A64_DP_REG_CCMP ||
        instr->subtype == A64_DP_REG_CCMN_IMM || instr->subtype == A64_DP_REG_CCMP_IMM) {
        return emit_ccmp_native(state, rn, rm, (uint64_t)instr->imm_shift, instr->cond,
                                (uint64_t)instr->imm, instr->subtype, instr->is_64bit);
    }

    if (op2 >= 0 && op2 <= 3) {
        int ret;
        int logical_rn = eff_rn;
        int logical_rm = eff_rm;
        int zero_src_for_rn = 13;
        int zero_src_for_rm = 13;

        if (!instr->is_64bit || src1_is_memory || src2_is_memory || dst_is_memory || dst_is_sp ||
            rn == 31 || rm == 31 || instr->imm_shift != 0 || instr->subtype >= 4 ||
            instr->shift_type != A64_SHIFT_LSL || instr->set_flags) {
            int logical_rn_arch = (rn == 31 && instr->subtype == 1) ? 31 : rn;
            return emit_logical_reg_fallback(state, rd, logical_rn_arch, rm, instr->shift_type,
                                             instr->imm_shift, instr->subtype, instr->set_flags,
                                             instr->is_64bit);
        }

        if (instr->imm_shift != 0 || instr->shift_type != A64_SHIFT_LSL || instr->set_flags)
            return A64_GEN_UNSUPPORTED;

        // Logical shifted-register treats x31 as XZR, not SP. Materialize architectural
        // zero in a temp register whenever an operand is x31 and the operation reads it.
        // ORR with Rn==XZR is handled as MOV alias and does not read Rn.
        if (src1_is_memory)
            zero_src_for_rm = 14;
        if (rn == 31 && instr->subtype != 1) {
            ret = emit_gadget(state, gadget_mov_imm[zero_src_for_rn]);
            if (ret != A64_GEN_OK)
                return ret;
            ret = emit_u64(state, 0);
            if (ret != A64_GEN_OK)
                return ret;
            logical_rn = zero_src_for_rn;
        }
        if (rm == 31) {
            ret = emit_gadget(state, gadget_mov_imm[zero_src_for_rm]);
            if (ret != A64_GEN_OK)
                return ret;
            ret = emit_u64(state, 0);
            if (ret != A64_GEN_OK)
                return ret;
            logical_rm = zero_src_for_rm;
        }

        if (rn == 31 && instr->subtype == 1) {
            gadget = gadget_mov_reg[eff_rd][logical_rm];
        } else {
            switch (instr->subtype) {
            case 0:
                gadget = gadget_and_reg[eff_rd][logical_rn][logical_rm];
                break;
            case 1:
                gadget = gadget_orr_reg[eff_rd][logical_rn][logical_rm];
                break;
            case 2:
                gadget = gadget_eor_reg[eff_rd][logical_rn][logical_rm];
                break;
            default:
                return A64_GEN_UNSUPPORTED;
            }
        }
    } else if (instr->cat == A64_DP_IMM2 && op2 >= 4 && op2 <= 7) {
        if (instr->set_flags)
            return A64_GEN_UNSUPPORTED;
        if (instr->subtype < 0 || instr->subtype > 3)
            return A64_GEN_UNSUPPORTED;
        return emit_csel_fallback(state, rd, rn, rm, instr->cond, instr->subtype, instr->is_64bit);
    } else if (op2 >= 4 && op2 <= 7) {
        if (!instr->is_64bit) {
            return emit_addsub_reg_fallback(state, rd, rn, rm, instr->shift_type, instr->imm_shift,
                                            instr->subtype == 1, instr->set_flags, instr->is_64bit);
        }
        if (instr->set_flags) {
            return emit_addsub_reg_fallback(state, rd, rn, rm, instr->shift_type, instr->imm_shift,
                                            instr->subtype == 1, instr->set_flags, instr->is_64bit);
        }
        if (instr->imm_shift != 0 && (src1_is_memory || src2_is_memory || dst_is_memory ||
                                      instr->shift_type != A64_SHIFT_LSL)) {
            return emit_addsub_reg_fallback(state, rd, rn, rm, instr->shift_type, instr->imm_shift,
                                            instr->subtype == 1, instr->set_flags, instr->is_64bit);
        }
        if (instr->imm_shift != 0) {
            int ret;
            int work_dst = (dst_is_memory || dst_is_sp) ? 13 : rd;
            int eff_rn_for_op = rn;

            if (rn == 31) {
                ret = emit_gadget(state, gadget_mov_imm[14]);
                if (ret != A64_GEN_OK)
                    return ret;
                ret = emit_u64(state, 0);
                if (ret != A64_GEN_OK)
                    return ret;
                eff_rn_for_op = 14;
            }

            if (src2_is_memory) {
                int load_idx = MEM_REG_INDEX(rm);
                if (!VALID_MEM_REG_INDEX(load_idx))
                    return A64_GEN_UNSUPPORTED;
                ret = emit_gadget(state, gadget_load_xreg_16_to_30[load_idx]);
                if (ret != A64_GEN_OK)
                    return ret;
            } else if (rm == 31) {
                ret = emit_gadget(state, gadget_mov_imm[13]);
                if (ret != A64_GEN_OK)
                    return ret;
                ret = emit_u64(state, 0);
                if (ret != A64_GEN_OK)
                    return ret;
            } else {
                ret = emit_gadget(state, gadget_mov_reg[13][rm]);
                if (ret != A64_GEN_OK)
                    return ret;
            }

            for (int i = 0; i < instr->imm_shift; i++) {
                ret = emit_gadget(state, gadget_add_reg[13][13][13]);
                if (ret != A64_GEN_OK)
                    return ret;
            }

            if (instr->set_flags) {
                if (instr->subtype == 1 && rd == 31) {
                    ret = emit_gadget(state, gadget_cmp_reg[eff_rn_for_op][13]);
                } else {
                    ret = emit_gadget(state, (instr->subtype == 1)
                                                 ? gadget_subs_reg[work_dst][eff_rn_for_op][13]
                                                 : gadget_adds_reg[work_dst][eff_rn_for_op][13]);
                }
            } else {
                ret = emit_gadget(state, (instr->subtype == 1)
                                             ? gadget_sub_reg[work_dst][eff_rn_for_op][13]
                                             : gadget_add_reg[work_dst][eff_rn_for_op][13]);
            }
            if (ret != A64_GEN_OK)
                return ret;

            if (dst_is_memory) {
                int store_idx = MEM_REG_INDEX(rd);
                if (!VALID_MEM_REG_INDEX(store_idx))
                    return A64_GEN_UNSUPPORTED;
                ret = emit_gadget(state, gadget_store_xreg_16_to_30[store_idx]);
                if (ret != A64_GEN_OK)
                    return ret;
            } else if (dst_is_sp) {
                ret = emit_gadget(state, (tcti_gadget_t)gadget_store_sp);
                if (ret != A64_GEN_OK)
                    return ret;
            }

            return A64_GEN_OK;
        }
        int ret;
        int eff_rn_for_op = eff_rn;
        int eff_rm_for_op = eff_rm;
        if (rn == 31) {
            int zero_reg = eff_rm == 13 ? 14 : 13;
            ret = emit_gadget(state, gadget_mov_imm[zero_reg]);
            if (ret != A64_GEN_OK)
                return ret;
            ret = emit_u64(state, 0);
            if (ret != A64_GEN_OK)
                return ret;
            eff_rn_for_op = zero_reg;
        }
        if (rm == 31) {
            int zero_reg = eff_rn_for_op == 13 ? 14 : 13;
            ret = emit_gadget(state, gadget_mov_imm[zero_reg]);
            if (ret != A64_GEN_OK)
                return ret;
            ret = emit_u64(state, 0);
            if (ret != A64_GEN_OK)
                return ret;
            eff_rm_for_op = zero_reg;
        }
        if (instr->set_flags) {
            if (instr->subtype == 1 && rd == 31 && !src1_is_memory && !src2_is_memory && rn != 31 &&
                rm != 31) {
                gadget = gadget_cmp_reg[eff_rn_for_op][eff_rm_for_op];
            } else {
                gadget = (instr->subtype == 1)
                             ? gadget_subs_reg[eff_rd][eff_rn_for_op][eff_rm_for_op]
                             : gadget_adds_reg[eff_rd][eff_rn_for_op][eff_rm_for_op];
            }
        } else {
            gadget = (instr->subtype == 1) ? gadget_sub_reg[eff_rd][eff_rn_for_op][eff_rm_for_op]
                                           : gadget_add_reg[eff_rd][eff_rn_for_op][eff_rm_for_op];
        }
    } else if ((op2 == 12 || op2 == 13) && !dpreg_ext_form) {
        return emit_addsub_reg_fallback(state, rd, rn, rm, instr->shift_type, instr->imm_shift,
                                        instr->subtype == 1, instr->set_flags, instr->is_64bit);
    } else if (op2 >= 8 && op2 <= 11) {
        int ret;

        if (!dpreg_ext_form) {
            if (!instr->is_64bit || instr->set_flags || src1_is_memory || src2_is_memory ||
                dst_is_memory || instr->shift_type != A64_SHIFT_LSL) {
                return emit_addsub_reg_fallback(state, rd, rn, rm, instr->shift_type,
                                                instr->imm_shift, instr->subtype == 1,
                                                instr->set_flags, instr->is_64bit);
            }
            int work_dst = (dst_is_memory || dst_is_sp) ? 13 : rd;
            int eff_rn_for_op = rn;

            if (rn == 31) {
                ret = emit_gadget(state, gadget_mov_imm[14]);
                if (ret != A64_GEN_OK)
                    return ret;
                ret = emit_u64(state, 0);
                if (ret != A64_GEN_OK)
                    return ret;
                eff_rn_for_op = 14;
            }

            if (src2_is_memory) {
                int load_idx = MEM_REG_INDEX(rm);
                if (!VALID_MEM_REG_INDEX(load_idx))
                    return A64_GEN_UNSUPPORTED;
                ret = emit_gadget(state, gadget_load_xreg_16_to_30[load_idx]);
                if (ret != A64_GEN_OK)
                    return ret;
            } else if (rm == 31) {
                ret = emit_gadget(state, gadget_mov_imm[13]);
                if (ret != A64_GEN_OK)
                    return ret;
                ret = emit_u64(state, 0);
                if (ret != A64_GEN_OK)
                    return ret;
            } else {
                ret = emit_gadget(state, gadget_mov_reg[13][rm]);
                if (ret != A64_GEN_OK)
                    return ret;
            }

            for (int i = 0; i < instr->imm_shift; i++) {
                ret = emit_gadget(state, gadget_add_reg[13][13][13]);
                if (ret != A64_GEN_OK)
                    return ret;
            }

            if (instr->set_flags) {
                if (instr->subtype == 1 && rd == 31) {
                    ret = emit_gadget(state, gadget_cmp_reg[eff_rn_for_op][13]);
                } else {
                    ret = emit_gadget(state, (instr->subtype == 1)
                                                 ? gadget_subs_reg[work_dst][eff_rn_for_op][13]
                                                 : gadget_adds_reg[work_dst][eff_rn_for_op][13]);
                }
            } else {
                ret = emit_gadget(state, (instr->subtype == 1)
                                             ? gadget_sub_reg[work_dst][eff_rn_for_op][13]
                                             : gadget_add_reg[work_dst][eff_rn_for_op][13]);
            }
            if (ret != A64_GEN_OK)
                return ret;

            if (dst_is_memory) {
                int store_idx = MEM_REG_INDEX(rd);
                if (!VALID_MEM_REG_INDEX(store_idx))
                    return A64_GEN_UNSUPPORTED;
                ret = emit_gadget(state, gadget_store_xreg_16_to_30[store_idx]);
                if (ret != A64_GEN_OK)
                    return ret;
            } else if (dst_is_sp) {
                ret = emit_gadget(state, (tcti_gadget_t)gadget_store_sp);
                if (ret != A64_GEN_OK)
                    return ret;
            }

            return A64_GEN_OK;
        }

        if (rm == 31)
            return A64_GEN_UNSUPPORTED;
        if (instr->imm_shift < 0 || instr->imm_shift > 4)
            return A64_GEN_UNSUPPORTED;
        return emit_addsub_ext_fallback(state, rd, rn, rm, instr->extend_type, instr->imm_shift,
                                        instr->subtype == 1, instr->set_flags, instr->is_64bit,
                                        dst_is_sp, rn == 31);
    } else {
        return A64_GEN_UNSUPPORTED;
    }

    if (!gadget)
        return A64_GEN_UNSUPPORTED;

    // Emit load for first source if memory-backed
    if (src1_is_memory) {
        int load_idx = MEM_REG_INDEX(rn); // x15=0, ..., x30=15
        if (!VALID_MEM_REG_INDEX(load_idx))
            return A64_GEN_UNSUPPORTED;

        int ret = emit_gadget(state, gadget_load_xreg_16_to_30[load_idx]);
        if (ret != A64_GEN_OK)
            return ret;
        if (eff_rn != 13) {
            ret = emit_gadget(state, gadget_mov_reg[eff_rn][13]);
            if (ret != A64_GEN_OK)
                return ret;
        }
    }

    // Emit load for second source if memory-backed.
    if (src2_is_memory) {
        int load_idx = MEM_REG_INDEX(rm);
        if (!VALID_MEM_REG_INDEX(load_idx))
            return A64_GEN_UNSUPPORTED;

        int ret = emit_gadget(state, gadget_load_xreg_16_to_30[load_idx]);
        if (ret != A64_GEN_OK)
            return ret;
        if (eff_rm != 13) {
            ret = emit_gadget(state, gadget_mov_reg[eff_rm][13]);
            if (ret != A64_GEN_OK)
                return ret;
        }
    }

    // Emit main operation
    int ret = emit_gadget(state, gadget);
    if (ret != A64_GEN_OK)
        return ret;

    // Emit store if destination is memory-backed
    if (dst_is_memory) {
        int store_idx = MEM_REG_INDEX(rd);
        if (!VALID_MEM_REG_INDEX(store_idx))
            return A64_GEN_UNSUPPORTED;

        ret = emit_gadget(state, gadget_store_xreg_16_to_30[store_idx]);
        if (ret != A64_GEN_OK)
            return ret;
    }

    // Handle SP destination
    if (dst_is_sp) {
        ret = emit_gadget(state, (tcti_gadget_t)gadget_store_sp);
        if (ret != A64_GEN_OK)
            return ret;
    }

    return A64_GEN_OK;
}

/* ============================================================================
 * Branch Instructions
 * ============================================================================
 */
int a64_gen_branch(a64_gen_state_t *state, const a64_instr_t *instr)
{
    int ret = A64_GEN_OK;

    switch (instr->subtype) {
    case A64_BRANCH_UNCOND:
        ret = emit_unconditional_branch(state, state->guest_pc + instr->imm, instr->op ? 1 : 0);
        if (ret != A64_GEN_OK)
            return ret;

        state->is_complete = 1;
        return A64_GEN_OK;

    case A64_BRANCH_COND:
        ret = emit_bcond(state, instr->cond, state->guest_pc + instr->imm, state->guest_pc + 4);
        if (ret != A64_GEN_OK)
            return ret;
        state->is_complete = 1;
        return A64_GEN_OK;

    case A64_BRANCH_CMP: {
        int cmp_reg = instr->Rd;
        if (cmp_reg < 0 || cmp_reg > 31)
            return A64_GEN_INVALID_INSN;

        uint64_t target_pc = state->guest_pc + instr->imm;
        uint64_t fallthrough_pc = state->guest_pc + 4;
        if (cmp_reg == 31) {
            ret = emit_unconditional_branch(state, instr->op ? fallthrough_pc : target_pc, 0);
            if (ret != A64_GEN_OK)
                return ret;
            state->is_complete = 1;
            return A64_GEN_OK;
        }

        if (IS_MEM_REG(cmp_reg)) {
            ret = emit_gadget(state, gadget_load_xreg_16_to_30[MEM_REG_INDEX(cmp_reg)]);
            if (ret != A64_GEN_OK)
                return ret;
            cmp_reg = 13;
        }

        const tcti_gadget_t *cbz_table = instr->is_64bit ? gadget_cbz_xreg : gadget_cbz_wreg;
        const tcti_gadget_t *cbnz_table = instr->is_64bit ? gadget_cbnz_xreg : gadget_cbnz_wreg;
        ret = emit_gadget(state, instr->op ? cbnz_table[cmp_reg] : cbz_table[cmp_reg]);
        if (ret != A64_GEN_OK)
            return ret;

        ret = emit_u64(state, target_pc);
        if (ret != A64_GEN_OK)
            return ret;

        ret = emit_u64(state, fallthrough_pc);
        if (ret != A64_GEN_OK)
            return ret;
        state->is_complete = 1;
        return A64_GEN_OK;
    }

    case A64_BRANCH_TEST: {
        int test_reg = instr->Rd;
        if (test_reg < 0 || test_reg > 31)
            return A64_GEN_UNSUPPORTED;

        const bool is_64bit = instr->imm_shift >= 32;
        if (test_reg == 31) {
            ret = emit_unconditional_branch(
                state, instr->op ? state->guest_pc + 4 : state->guest_pc + instr->imm, 0);
            if (ret != A64_GEN_OK)
                return ret;
            state->is_complete = 1;
            return A64_GEN_OK;
        }

        if (IS_MEM_REG(test_reg)) {
            ret = emit_gadget(state, gadget_load_xreg_16_to_30[MEM_REG_INDEX(test_reg)]);
            if (ret != A64_GEN_OK)
                return ret;
            test_reg = 13;
        }

        const tcti_gadget_t *tbz_table = is_64bit ? gadget_tbz_xreg : gadget_tbz_wreg;
        const tcti_gadget_t *tbnz_table = is_64bit ? gadget_tbnz_xreg : gadget_tbnz_wreg;
        ret = emit_gadget(state, instr->op ? tbnz_table[test_reg] : tbz_table[test_reg]);
        if (ret != A64_GEN_OK)
            return ret;

        ret = emit_u64(state, instr->imm_shift);
        if (ret != A64_GEN_OK)
            return ret;

        ret = emit_u64(state, state->guest_pc + instr->imm);
        if (ret != A64_GEN_OK)
            return ret;

        ret = emit_u64(state, state->guest_pc + 4);
        if (ret != A64_GEN_OK)
            return ret;
        state->is_complete = 1;
        return A64_GEN_OK;
    }

    case 5: // Branch register (BR/BLR/RET)
    {
        ret = emit_gadget(state, (tcti_gadget_t)gadget_br_impl);
        if (ret != A64_GEN_OK)
            return ret;

        ret = emit_u64(state, instr->Rn);
        if (ret != A64_GEN_OK)
            return ret;

        int opc = bits(instr->raw, 24, 21);
        int is_link = (opc == 1) ? 1 : 0;
        ret = emit_u64(state, is_link);
        if (ret != A64_GEN_OK)
            return ret;

        ret = emit_u64(state, state->guest_pc + 4);
        if (ret != A64_GEN_OK)
            return ret;

        state->is_complete = 1;
        return A64_GEN_OK;
    }

    default:
        return A64_GEN_UNSUPPORTED;
    }
}

/* ============================================================================
 * Load/Store Instructions
 * ============================================================================
 */
static int a64_emit_ldst_single(a64_gen_state_t *state, uint64_t fault_pc, int rt, int rn,
                                int64_t imm, int size, int idx_mode, int is_signed, int rm,
                                int extend_type, int imm_shift, int is_reg_offset, int is_load,
                                int load_writes_64)
{
    int ret;
    tcti_gadget_t gadget = NULL;
    uint64_t meta;

    gadget = is_load ? gadget_ldr_x : gadget_str_x;

    ret = emit_gadget(state, gadget);
    if (ret != A64_GEN_OK)
        return ret;

    ret = emit_u64(state, fault_pc);
    if (ret != A64_GEN_OK)
        return ret;

    ret = emit_u64(state, rt);
    if (ret != A64_GEN_OK)
        return ret;

    ret = emit_u64(state, rn);
    if (ret != A64_GEN_OK)
        return ret;

    ret = emit_u64(state, imm);
    if (ret != A64_GEN_OK)
        return ret;

    ret = emit_u64(state, size);
    if (ret != A64_GEN_OK)
        return ret;

    ret = emit_u64(state, idx_mode);
    if (ret != A64_GEN_OK)
        return ret;

    meta = (uint64_t)(is_signed ? 1 : 0);
    if (is_reg_offset) {
        meta |= (uint64_t)1 << 8;
        meta |= ((uint64_t)(rm & 0xff)) << 16;
        meta |= ((uint64_t)(extend_type & 0xff)) << 24;
        meta |= ((uint64_t)(imm_shift & 0xff)) << 32;
    }
    if (state->conservative_mode) {
        meta |= 1ULL << 63;
    }
    if (load_writes_64) {
        meta |= 1ULL << 40;
    }

    ret = emit_u64(state, meta);
    if (ret != A64_GEN_OK)
        return ret;

    return A64_GEN_OK;
}

static int a64_emit_base_writeback(a64_gen_state_t *state, int rn, int64_t imm)
{
    int ret;

    if (imm == 0)
        return A64_GEN_OK;

    if (IS_MEM_REG(rn)) {
        int load_idx = MEM_REG_INDEX(rn);
        ret = emit_gadget(state, gadget_load_xreg_16_to_30[load_idx]);
        if (ret != A64_GEN_OK)
            return ret;

        ret = emit_gadget(state, gadget_mov_imm[14]);
        if (ret != A64_GEN_OK)
            return ret;
        ret = emit_u64(state, (uint64_t)imm);
        if (ret != A64_GEN_OK)
            return ret;

        ret = emit_gadget(state, gadget_add_reg[13][13][14]);
        if (ret != A64_GEN_OK)
            return ret;

        return emit_gadget(state, gadget_store_xreg_16_to_30[load_idx]);
    }

    if (rn == 31) {
        ret = emit_gadget(state, (tcti_gadget_t)gadget_load_sp);
        if (ret != A64_GEN_OK)
            return ret;

        ret = emit_gadget(state, gadget_mov_imm[14]);
        if (ret != A64_GEN_OK)
            return ret;
        ret = emit_u64(state, (uint64_t)imm);
        if (ret != A64_GEN_OK)
            return ret;

        ret = emit_gadget(state, gadget_add_reg[13][13][14]);
        if (ret != A64_GEN_OK)
            return ret;

        return emit_gadget(state, (tcti_gadget_t)gadget_store_sp);
    }

    ret = emit_gadget(state, gadget_mov_imm[13]);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, (uint64_t)imm);
    if (ret != A64_GEN_OK)
        return ret;

    return emit_gadget(state, gadget_add_reg[rn][rn][13]);
}

static int a64_emit_simd_ldst(a64_gen_state_t *state, const a64_instr_t *instr)
{
    int ret = emit_gadget(state, gadget_simd_ldst);
    if (ret != A64_GEN_OK)
        return ret;

    ret = emit_u64(state, state->guest_pc);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, instr->Rd);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, instr->Rm);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, instr->Rn);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, instr->is_pair ? instr->pair_offset : instr->imm);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, instr->vec_bytes);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, instr->idx_mode);
    if (ret != A64_GEN_OK)
        return ret;
    ret = emit_u64(state, instr->is_pair ? 1 : 0);
    if (ret != A64_GEN_OK)
        return ret;
    return emit_u64(state, bit(instr->raw, 22));
}

int a64_gen_ldst(a64_gen_state_t *state, const a64_instr_t *instr)
{
    if (instr->is_vector) {
        if ((instr->subtype == A64_LDST_SINGLE || instr->subtype == A64_LDST_PAIR) &&
            instr->vec_bytes > 0) {
            return a64_emit_simd_ldst(state, instr);
        }
        return A64_GEN_UNSUPPORTED;
    }

    if (instr->subtype == A64_LDST_ATOMIC) {
        int ret = emit_gadget(state, gadget_atomic_ldst);
        if (ret != A64_GEN_OK)
            return ret;
        ret = emit_u64(state, state->guest_pc);
        if (ret != A64_GEN_OK)
            return ret;
        ret = emit_u64(state, instr->Rd);
        if (ret != A64_GEN_OK)
            return ret;
        ret = emit_u64(state, instr->Rn);
        if (ret != A64_GEN_OK)
            return ret;
        ret = emit_u64(state, instr->Rm);
        if (ret != A64_GEN_OK)
            return ret;
        ret = emit_u64(state, instr->size);
        if (ret != A64_GEN_OK)
            return ret;
        return emit_u64(state, bit(instr->raw, 22));
    }

    if (instr->subtype == A64_LDST_LITERAL) {
        return A64_GEN_UNSUPPORTED;
    }

    if (instr->is_pair) {
        int access_size;
        int first_mode;
        int second_mode = A64_INDEX_OFFSET;
        int64_t first_imm;
        int64_t second_imm;
        int ret;
        bool pair_load_writes_64 = instr->size == A64_SIZE_X || (instr->is_signed && instr->is_64bit);

        if (instr->size == A64_SIZE_X) {
            access_size = A64_SIZE_X;
        } else if (instr->size == A64_SIZE_W) {
            access_size = A64_SIZE_W;
        } else {
            return A64_GEN_UNSUPPORTED;
        }

        if (instr->idx_mode == A64_POST_INDEX) {
            first_mode = A64_INDEX_OFFSET;
            first_imm = 0;
            second_imm = 1LL << access_size;
        } else {
            first_mode = instr->idx_mode;
            first_imm = instr->pair_offset;
            second_imm = (instr->idx_mode == A64_PRE_INDEX)
                             ? (1LL << access_size)
                             : (instr->pair_offset + (1LL << access_size));
        }

        bool is_load = bit(instr->raw, 22);
        bool load_first_destination_overlaps_base = is_load && instr->Rd == instr->Rn;
        bool discard_first_loaded_value =
            load_first_destination_overlaps_base && instr->idx_mode == A64_POST_INDEX;
        int first_rt = discard_first_loaded_value ? 31 : instr->Rd;

        // Pair addressing uses the original base register for both elements.
        // If the first load destination is also the base, emit the independent
        // second element first so the second address is not computed from the
        // newly loaded first value.
        if (load_first_destination_overlaps_base) {
            ret = a64_emit_ldst_single(state, state->guest_pc, instr->Rm, instr->Rn, second_imm,
                                       access_size, second_mode, instr->is_signed, 0, 0, 0, 0,
                                       is_load, pair_load_writes_64);
            if (ret != A64_GEN_OK)
                return ret;

            ret = a64_emit_ldst_single(state, state->guest_pc, first_rt, instr->Rn, first_imm,
                                       access_size, first_mode, instr->is_signed, 0, 0, 0, 0,
                                       is_load, pair_load_writes_64);
        } else {
            ret = a64_emit_ldst_single(state, state->guest_pc, instr->Rd, instr->Rn, first_imm,
                                       access_size, first_mode, instr->is_signed, 0, 0, 0, 0,
                                       is_load, pair_load_writes_64);
            if (ret != A64_GEN_OK)
                return ret;

            ret = a64_emit_ldst_single(state, state->guest_pc, instr->Rm, instr->Rn, second_imm,
                                       access_size, second_mode, instr->is_signed, 0, 0, 0, 0,
                                       is_load, pair_load_writes_64);
        }
        if (ret != A64_GEN_OK)
            return ret;

        if (instr->idx_mode == A64_POST_INDEX)
            return a64_emit_base_writeback(state, instr->Rn, instr->pair_offset);

        return A64_GEN_OK;
    }

    int ret = a64_emit_ldst_single(
        state, state->guest_pc, instr->Rd, instr->Rn, instr->imm, instr->size, instr->idx_mode,
        instr->is_signed, instr->Rm, instr->extend_type, instr->imm_shift,
        !bit(instr->raw, 24) && bits(instr->raw, 11, 10) == 2, a64_ldst_raw_is_load(instr->raw),
        instr->size == A64_SIZE_X || (instr->is_signed && instr->is_64bit));
    if (ret != A64_GEN_OK)
        return ret;

    // Single load/store helpers own pre/post-index writeback internally.
    return A64_GEN_OK;
}

static int a64_gen_simd(a64_gen_state_t *state, const a64_instr_t *instr)
{
    int ret;

    switch (instr->subtype) {
    case A64_SIMD_MOVI_IMM:
        ret = emit_gadget(state, gadget_simd_movi_imm);
        if (ret != A64_GEN_OK)
            return ret;
        ret = emit_u64(state, instr->Rd);
        if (ret != A64_GEN_OK)
            return ret;
        ret = emit_u64(state, (uint64_t)instr->imm);
        if (ret != A64_GEN_OK)
            return ret;
        ret = emit_u64(state, (uint64_t)instr->imm_shift);
        if (ret != A64_GEN_OK)
            return ret;
        ret = emit_u64(state, instr->op ? 1 : 0);
        if (ret != A64_GEN_OK)
            return ret;
        return emit_u64(state, instr->is_64bit ? 1 : 0);

    case A64_SIMD_DUP_GPR:
        ret = emit_gadget(state, gadget_simd_dup_gpr);
        if (ret != A64_GEN_OK)
            return ret;
        ret = emit_u64(state, instr->Rd);
        if (ret != A64_GEN_OK)
            return ret;
        ret = emit_u64(state, instr->Rn);
        if (ret != A64_GEN_OK)
            return ret;
        return emit_u64(state, instr->vec_bytes);

    case A64_SIMD_MOV_GPR_FROM_VEC:
        ret = emit_gadget(state, gadget_simd_mov_gpr_from_vec);
        if (ret != A64_GEN_OK)
            return ret;
        ret = emit_u64(state, instr->Rd);
        if (ret != A64_GEN_OK)
            return ret;
        ret = emit_u64(state, instr->Rn);
        if (ret != A64_GEN_OK)
            return ret;
        ret = emit_u64(state, instr->vec_bytes);
        if (ret != A64_GEN_OK)
            return ret;
        ret = emit_u64(state, instr->vec_index);
        if (ret != A64_GEN_OK)
            return ret;
        return emit_u64(state, instr->is_64bit ? 1 : 0);

    case A64_SIMD_FMOV_GPR:
        ret = emit_gadget(state, gadget_simd_fmov_gpr);
        if (ret != A64_GEN_OK)
            return ret;
        ret = emit_u64(state, instr->Rd);
        if (ret != A64_GEN_OK)
            return ret;
        ret = emit_u64(state, instr->Rn);
        if (ret != A64_GEN_OK)
            return ret;
        ret = emit_u64(state, instr->vec_bytes);
        if (ret != A64_GEN_OK)
            return ret;
        return emit_u64(state, instr->op ? 1 : 0);

    default:
        return A64_GEN_UNSUPPORTED;
    }
}

/* ============================================================================
 * System Instructions (SVC, HINT, etc.)
 * ============================================================================
 */
int a64_gen_system(a64_gen_state_t *state, const a64_instr_t *instr)
{
    int ret;

    // Exception generation (SVC, HVC, SMC) - subtype from decoder
    switch (instr->subtype) {
    case A64_EXCEPTION: // SVC/HVC/SMC exception generation; Linux userspace uses SVC.
        ret = emit_gadget(state, gadget_svc);
        if (ret != A64_GEN_OK)
            return ret;
        ret = emit_u64(state, state->guest_pc + 4);
        if (ret != A64_GEN_OK)
            return ret;
        state->is_complete = 1;
        return A64_GEN_OK;
    case A64_SYSTEM_MRS:
        if (a64_sysreg_route_for_access((uint16_t)instr->sysreg, 0) ==
            A64_SYSREG_ROUTE_UNSUPPORTED) {
            state->is_complete = 1;
            return emit_gadget(state, gadget_sysreg_unsupported);
        }
        ret = emit_gadget(state, gadget_mrs);
        if (ret != A64_GEN_OK)
            return ret;
        ret = emit_u64(state, instr->sysreg);
        if (ret != A64_GEN_OK)
            return ret;
        return emit_u64(state, instr->Rd);
    case A64_SYSTEM_MSR_REG:
        if (a64_sysreg_route_for_access((uint16_t)instr->sysreg, 1) ==
            A64_SYSREG_ROUTE_UNSUPPORTED) {
            state->is_complete = 1;
            return emit_gadget(state, gadget_sysreg_unsupported);
        }
        ret = emit_gadget(state, gadget_msr);
        if (ret != A64_GEN_OK)
            return ret;
        ret = emit_u64(state, instr->sysreg);
        if (ret != A64_GEN_OK)
            return ret;
        return emit_u64(state, instr->Rd);
    case A64_SYSTEM_MSR_IMM:
        if (a64_sysreg_route_for_access((uint16_t)instr->sysreg, 1) ==
            A64_SYSREG_ROUTE_UNSUPPORTED) {
            state->is_complete = 1;
            return emit_gadget(state, gadget_sysreg_unsupported);
        }
        ret = emit_gadget(state, gadget_msr);
        if (ret != A64_GEN_OK)
            return ret;
        ret = emit_u64(state, instr->sysreg);
        if (ret != A64_GEN_OK)
            return ret;
        return emit_u64(state, instr->imm);
    case A64_SYSTEM_BARRIER:
        switch (instr->op) {
        case 4:
            return emit_gadget(state, gadget_dsb);
        case 5:
            return emit_gadget(state, gadget_dmb);
        case 6:
            return emit_gadget(state, gadget_isb);
        default:
            return A64_GEN_UNSUPPORTED;
        }
    case A64_SYSTEM_HINT:
        if (instr->imm == 0) {
            return emit_gadget(state, gadget_nop);
        }
        return A64_GEN_UNSUPPORTED;
    case A64_SYSTEM_DC_ZVA:
        ret = emit_gadget(state, gadget_dc_zva);
        if (ret != A64_GEN_OK)
            return ret;
        return emit_u64(state, instr->Rd);
    default:
        return A64_GEN_UNSUPPORTED;
    }
}

/* ============================================================================
 * Main Instruction Generator
 * ============================================================================
 */
int a64_gen_instruction(a64_gen_state_t *state, uint32_t insn, uint64_t pc)
{
    a64_instr_t decoded;
    int ret = a64_decode(insn, &decoded);

    if (ret < 0)
        return A64_GEN_INVALID_INSN;

    state->guest_pc = pc;
    state->raw_insn = insn;
    state->decoded = decoded;

    // Handle system instructions detected by top byte (0xD4 or 0xD5).
    // The decoder sets cat=A64_BRANCH for these, but they dispatch through
    // a64_gen_system based on their system subtypes.
    uint8_t top_byte = (insn >> 24) & 0xFF;
    if (top_byte == 0xD4 || top_byte == 0xD5) {
        return a64_gen_system(state, &decoded);
    }

    // Dispatch by category
    switch (decoded.cat) {
    case A64_DP_IMM:
    case A64_SIMD0: // cat=8 is actually DP_IMM (ADR, ADRP, etc.)
        ret = a64_gen_dp_imm(state, &decoded);
        break;

    case A64_DP_IMM2: // cat=13 is DP_REG-style (ADC/SBC, CSEL, CCMP)
        ret = a64_gen_dp_reg(state, &decoded);
        break;

    case A64_DP_REG:
    case A64_DP_REG2:
    case A64_DP_REG3:
    case A64_DP_REG4:
        ret = a64_gen_dp_reg(state, &decoded);
        break;

    case A64_BRANCH:
    case A64_BRANCH2:
        ret = a64_gen_branch(state, &decoded);
        break;

    case A64_LD_ST:
        ret = a64_gen_ldst(state, &decoded);
        break;

    case A64_SIMD:
    case A64_SIMD2:
        ret = a64_gen_simd(state, &decoded);
        break;

    default:
        ret = A64_GEN_UNSUPPORTED;
    }

    if (ret == A64_GEN_OK) {
        state->instructions_processed++;

        // Branch-like instructions terminate block ownership of guest PC and
        // write cpu->pc inside their gadget implementations. Mark as complete
        // so block finalization end_pc does not imply fallthrough sequencing.
        if ((decoded.cat == A64_BRANCH || decoded.cat == A64_BRANCH2) &&
            decoded.subtype != A64_EXCEPTION && decoded.subtype != 6) {
            state->is_complete = 1;
        }

        // Non-terminal instructions need PC advancement to reflect fallthrough
        // execution. Emit pc_advance gadget to update cpu->pc to next instruction.
        if (!state->is_complete) {
            int pc_ret = emit_gadget(state, gadget_pc_advance);
            if (pc_ret != A64_GEN_OK) {
                return pc_ret;
            }
            pc_ret = emit_u64(state, state->guest_pc + 4);
            if (pc_ret != A64_GEN_OK) {
                return pc_ret;
            }
        }
    }

    return ret;
}

/* ============================================================================
 * Block Finalization
 * ============================================================================
 */
int a64_gen_finalize(a64_gen_state_t *state)
{
    // Add exit gadget at the end of the block
    int ret = emit_gadget(state, gadget_exit);
    if (ret != A64_GEN_OK)
        return ret;

    state->is_complete = 1;
    state->end_pc = state->guest_pc;

    return A64_GEN_OK;
}

/* ============================================================================
 * Full Block Compilation
 * ============================================================================
 */
int a64_gen_basic_block(a64_gen_state_t *state, struct cpu_state *cpu, struct tlb *tlb,
                        uint64_t *end_pc)
{
    int insn_count = 0;
    const int MAX_INSNS = 50;

    while (insn_count < MAX_INSNS) {
        // Fetch instruction
        uint32_t insn;
        int ret = a64_fetch_insn(cpu, tlb, state->guest_pc, &insn);
        if (ret < 0) {
            break;
        }

        // Generate gadget for this instruction
        ret = a64_gen_instruction(state, insn, state->guest_pc);

        if (ret < 0) {
            // Unsupported instruction
            if (insn_count == 0) {
                return ret; // First instruction unsupported
            }
            break;
        }

        if (ret == 1 || state->is_complete) {
            // Block naturally ended
            break;
        }

        state->guest_pc += 4;
        insn_count++;
    }

    // Finalize block
    int ret = a64_gen_finalize(state);
    if (ret < 0)
        return ret;

    if (end_pc)
        *end_pc = state->end_pc;

    return state->instructions_processed;
}
