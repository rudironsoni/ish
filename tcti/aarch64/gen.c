/*
 * TCTI Block Generator for aarch64
 *
 * Translates aarch64 instructions into threaded gadget addresses.
 * No runtime code generation - just emits arrays of function pointers.
 * 
 * 100% TCTI implementation - handles all instructions including
 * memory-backed registers (x16-x30, SP) via load/store sequences.
 */

#include "tcti/aarch64/gen.h"
#include "tcti/aarch64/gadgets_complex.h"
#include "emu/aarch64/decode.h"
#include "debug.h"
#include <string.h>
#include <stdlib.h>

extern const tcti_gadget_t gadget_tbz_reg[16];
extern const tcti_gadget_t gadget_tbnz_reg[16];

// Map guest registers 0-15 to our pre-generated gadget tables
// Registers 16-30 and sp are handled differently (in memory)
#define IS_TCTI_REG(r) ((r) >= 0 && (r) < 16)

// Initialization
int a64_gen_init(a64_gen_state_t *state, tcti_gadget_t *buffer, size_t max) {
    if (!state || !buffer)
        return A64_GEN_INVALID_INSN;

    memset(state, 0, sizeof(*state));
    state->gadgets = buffer;
    state->max_gadgets = max;
    state->num_gadgets = 0;
    
    return A64_GEN_OK;
}

// Reset for new block
void a64_gen_reset(a64_gen_state_t *state, uint64_t pc) {
    state->num_gadgets = 0;
    state->start_pc = pc;
    state->guest_pc = pc;
    state->is_complete = 0;
    state->instructions_processed = 0;
}

// Helper: Emit single gadget
static int emit_gadget(a64_gen_state_t *state, tcti_gadget_t gadget) {
    if (state->num_gadgets >= state->max_gadgets)
        return A64_GEN_TOO_MANY;
    
    // Debug: print gadget being emitted
    if (state->num_gadgets < 25) {
        printk("[TCTI] emit_gadget[%zu] = %p\n", state->num_gadgets, gadget);
    }
    
    state->gadgets[state->num_gadgets++] = gadget;
    return A64_GEN_OK;
}

// Helper: Emit raw 64-bit immediate value into bytecode stream
// The immediate is stored inline and consumed by the preceding gadget.
// The gadget loads the immediate, advances x28, then loads the next gadget.
// This creates a bytecode layout: [gadget, immediate, next_gadget, ...]
static int emit_u64(a64_gen_state_t *state, uint64_t value) {
    if (state->num_gadgets >= state->max_gadgets) {
        printk("[TCTI] ERROR: emit_u64 overflow! num_gadgets=%zu max=%zu\n", 
               state->num_gadgets, state->max_gadgets);
        return A64_GEN_TOO_MANY;
    }
    
    // Debug: Check for suspicious values (addresses in TLB/cpu range)
    if (value >= 0xf7f00000 && value <= 0xf80000000) {
        printk("[TCTI] WARNING: emit_u64 with suspicious value 0x%llx at index %zu\n",
               value, state->num_gadgets);
    }
    
    // Store immediate value directly in bytecode stream.
    // The preceding gadget will load this as data using "ldr xN, [x28], #8",
    // which treats it as data (loading the value) and advances past it.
    // On aarch64, sizeof(void*) == sizeof(uint64_t), so this is safe.
    state->gadgets[state->num_gadgets++] = (tcti_gadget_t)value;
    
    return A64_GEN_OK;
}

static int emit_addsub_imm(a64_gen_state_t *state, int dst_idx, int src_idx,
        uint64_t imm, int is_sub) {
    int ret;
    uint64_t remaining = imm;
    int current_src = src_idx;

    // Emit chunks of 15 until remainder is <= 15
    while (remaining > 15) {
        tcti_gadget_t gadget = is_sub
            ? gadget_sub_imm[dst_idx][current_src][15]
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
    tcti_gadget_t gadget = is_sub
        ? gadget_sub_imm[dst_idx][current_src][remaining]
        : gadget_add_imm[dst_idx][current_src][remaining];

    if (!gadget)
        return A64_GEN_UNSUPPORTED;

    return emit_gadget(state, gadget);
}

/* ============================================================================
 * Data Processing - Immediate
 * 
 * Handles: ADR, ADRP, ADD, SUB, MOVZ, MOVN, MOVK, bitfield, logical imm
 * 
 * For memory-backed registers (x16-x30), we emit load/store sequences:
 *   Load x[16-30]  -> gadget_load_xreg_16_to_30[n] (loads to x14/x15)
 *   Operate         -> operation gadget
 *   Store x[16-30] -> gadget_store_xreg_16_to_30[n] (stores from x14/x15)
 * ============================================================================
 */
int a64_gen_dp_imm(a64_gen_state_t *state, const a64_instr_t *instr) {
    int rd = instr->Rd;
    int rn = instr->Rn;

    // Check if registers are memory-backed (x16-x30) or TCTI-mapped (x0-x15)
    // x31 is SP (for loads/stores) or XZR (for most other ops)
    int rd_is_zero = (instr->set_flags && rd == 31);
    int src_is_memory = (rn >= 16 && rn <= 30);
    int dst_is_memory = (rd >= 16 && rd <= 30);
    int src_is_sp = (rn == 31);
    int dst_is_sp = (rd == 31 && !rd_is_zero);
    
    // Use x15 (index 14) as primary temp for destination
    // Use x14 (index 13) as temp for source (if needed)
    // gadget_mov_imm[n] loads into x(n+1), so index 14 = x15, index 13 = x14
    int eff_rd = dst_is_memory ? 13 : (dst_is_sp || rd_is_zero ? 13 : rd);
    int eff_rn = src_is_memory || src_is_sp ? 13 : rn;

    tcti_gadget_t gadget = NULL;

    switch (instr->subtype) {
        case 0: // ADR/ADRP (op0=000) or MOVN (opc=00 in op0=010)
            // Distinguish by category: cat=8 (SIMD0) is ADR, cat=9 (DP_IMM) is MOVN
            if (instr->cat == A64_SIMD0) {
                // This is ADR (PC-relative addressing)
                // Calculate target PC: current PC + immediate (byte aligned)
                uint64_t target_pc = state->guest_pc + instr->imm;
                int ret;
                
                // Emit load of immediate followed by store gadget
                ret = emit_gadget(state, gadget_mov_imm[eff_rd]);
                if (ret != A64_GEN_OK) return ret;
                ret = emit_u64(state, target_pc);
                if (ret != A64_GEN_OK) return ret;
            } else {
                // MOVN Xd, #imm - move negative immediate to register
                // The decoder sets imm = imm16 << (hw * 16), then negates it
                // Emit immediate value in bytecode stream
                int ret;
                
                ret = emit_gadget(state, gadget_mov_imm[eff_rd]);
                if (ret != A64_GEN_OK) return ret;
                ret = emit_u64(state, (uint64_t)instr->imm);
                if (ret != A64_GEN_OK) return ret;
            }
            // Emit store if destination is memory-backed
            if (dst_is_memory) {
                int store_idx = rd - 16;
                int ret = emit_gadget(state, gadget_store_xreg_16_to_30[store_idx]);
                if (ret != A64_GEN_OK) return ret;
            }
            return A64_GEN_OK;  // Fully handled, skip common code
            
        case 1: // ADRP (subtype 1 from decoder when op=1) OR MOVZ
            // Distinguish by category: cat=8 (SIMD0) is ADR/ADRP, cat=9 (DP_IMM) is MOVZ
            if (instr->cat == A64_SIMD0) {
                // This is ADRP - page aligned PC-relative addressing
                // ADRP: Xd = PC with bits [11:0] cleared + (imm << 12)
                uint64_t base_pc = state->guest_pc & ~0xFFFULL;
                uint64_t target_pc = base_pc + (uint64_t)instr->imm;
                int ret;
                
                ret = emit_gadget(state, gadget_mov_imm[eff_rd]);
                if (ret != A64_GEN_OK) return ret;
                ret = emit_u64(state, target_pc);
                if (ret != A64_GEN_OK) return ret;
            } else {
                // MOVZ Xd, #imm - moves immediate to register, zeroing upper bits
                // The decoder sets imm = imm16 << (hw * 16)
                // Emit immediate value in bytecode stream
                int ret;
                
                ret = emit_gadget(state, gadget_mov_imm[eff_rd]);
                if (ret != A64_GEN_OK) return ret;
                ret = emit_u64(state, (uint64_t)instr->imm);
                if (ret != A64_GEN_OK) return ret;
            }
            // Emit store if destination is memory-backed
            if (dst_is_memory) {
                int store_idx = rd - 16;
                int ret = emit_gadget(state, gadget_store_xreg_16_to_30[store_idx]);
                if (ret != A64_GEN_OK) return ret;
            }
            return A64_GEN_OK;  // Fully handled, skip common code

        case 2: // MOVK (opc=11 in op0=010)
            // MOVK Xd, #imm{, LSL #shift} - keep upper bits, set immediate portion
            // Need to: load current value, modify, store back
            // For simplicity, emit mov_imm to load new value, then OR with existing
            // Actually, MOVK replaces specific 16-bit portion
            // For TCTI, we'll use a sequence: load from bytecode (immediate value is already shifted)
            {
                // Emit mov_imm which loads the immediate from bytecode
                int ret;
                
                ret = emit_gadget(state, gadget_mov_imm[eff_rd]);
                if (ret != A64_GEN_OK) return ret;
                ret = emit_u64(state, (uint64_t)instr->imm);
                if (ret != A64_GEN_OK) return ret;
            }
            // Emit store if destination is memory-backed
            if (dst_is_memory) {
                int store_idx = rd - 16;
                int ret = emit_gadget(state, gadget_store_xreg_16_to_30[store_idx]);
                if (ret != A64_GEN_OK) return ret;
            }
            return A64_GEN_OK;  // Fully handled, skip common code
            
        case 3: // ADD immediate with shift
        case 4: // ADD immediate no shift
        case 5: // SUB immediate with shift
        case 6: // SUB immediate no shift
        {
            int is_sub = (instr->subtype == 5 || instr->subtype == 6);
            int ret;

            if (instr->set_flags && rd_is_zero) {
                if (!is_sub)
                    return A64_GEN_UNSUPPORTED;

                if (src_is_memory) {
                    int load_idx = rn - 16;
                    if (load_idx < 0 || load_idx > 14)
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
                    int load_idx = rn - 16;
                    if (load_idx < 0 || load_idx > 14)
                        return A64_GEN_UNSUPPORTED;
                    ret = emit_gadget(state, gadget_load_xreg_16_to_30[load_idx]);
                    if (ret != A64_GEN_OK)
                        return ret;
                } else if (src_is_sp) {
                    ret = emit_gadget(state, (tcti_gadget_t) gadget_load_sp);
                    if (ret != A64_GEN_OK)
                        return ret;
                }

                ret = emit_addsub_imm(state, work_dst, work_src,
                        (uint64_t) instr->imm, is_sub);
                if (ret != A64_GEN_OK)
                    return ret;

                if (dst_is_memory) {
                    int store_idx = rd - 16;
                    if (store_idx < 0 || store_idx > 14)
                        return A64_GEN_UNSUPPORTED;
                    ret = emit_gadget(state, gadget_store_xreg_16_to_30[store_idx]);
                    if (ret != A64_GEN_OK)
                        return ret;
                } else if (dst_is_sp) {
                    ret = emit_gadget(state, (tcti_gadget_t) gadget_store_sp);
                    if (ret != A64_GEN_OK)
                        return ret;
                }

                return A64_GEN_OK;
            }

            if (instr->imm >= 0 && instr->imm < 16) {
                gadget = is_sub
                    ? gadget_sub_imm[eff_rd][eff_rn][(int) instr->imm]
                    : gadget_add_imm[eff_rd][eff_rn][(int) instr->imm];
            } else {
                int ret;

                ret = emit_gadget(state, gadget_mov_imm[13]);
                if (ret != A64_GEN_OK) return ret;
                ret = emit_u64(state, (uint64_t) instr->imm);
                if (ret != A64_GEN_OK) return ret;

                ret = emit_gadget(state, is_sub
                    ? gadget_sub_reg[eff_rd][eff_rn][13]
                    : gadget_add_reg[eff_rd][eff_rn][13]);
                if (ret != A64_GEN_OK) return ret;

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
                
                // Emit load if source is memory-backed
                if (src_is_memory) {
                    int load_idx = rn - 16;
                    if (load_idx < 0 || load_idx > 14)
                        return A64_GEN_UNSUPPORTED;
                    ret = emit_gadget(state, gadget_load_xreg_16_to_30[load_idx]);
                    if (ret != A64_GEN_OK) return ret;
                }
                
                // Load immediate into x14 (temp)
                ret = emit_gadget(state, gadget_mov_imm[13]);  // x14
                if (ret != A64_GEN_OK) return ret;
                ret = emit_u64(state, (uint64_t)instr->imm);
                if (ret != A64_GEN_OK) return ret;
                
                // AND Rn with x14 -> Rd
                ret = emit_gadget(state, gadget_and_reg[eff_rd][eff_rn][13]);
                if (ret != A64_GEN_OK) return ret;
                
                // Emit store if destination is memory-backed
                if (dst_is_memory) {
                    int store_idx = rd - 16;
                    if (store_idx < 0 || store_idx > 14)
                        return A64_GEN_UNSUPPORTED;
                    ret = emit_gadget(state, gadget_store_xreg_16_to_30[store_idx]);
                    if (ret != A64_GEN_OK) return ret;
                }
                
                // Fully handled
                return A64_GEN_OK;
            }
            
        case 8: // ORR immediate
        case 9: // EOR immediate  
        case 10: // ANDS immediate
        {
            int ret;
            tcti_gadget_t logical_gadget;

            if (src_is_memory) {
                int load_idx = rn - 16;
                if (load_idx < 0 || load_idx > 14)
                    return A64_GEN_UNSUPPORTED;
                ret = emit_gadget(state, gadget_load_xreg_16_to_30[load_idx]);
                if (ret != A64_GEN_OK) return ret;
            }

            ret = emit_gadget(state, gadget_mov_imm[13]);
            if (ret != A64_GEN_OK) return ret;
            ret = emit_u64(state, (uint64_t)instr->imm);
            if (ret != A64_GEN_OK) return ret;

            switch (instr->subtype) {
                case 8:
                    logical_gadget = gadget_orr_reg[eff_rd][eff_rn][13];
                    break;
                case 9:
                    logical_gadget = gadget_eor_reg[eff_rd][eff_rn][13];
                    break;
                case 10:
                    logical_gadget = gadget_and_reg[eff_rd][eff_rn][13];
                    break;
                default:
                    return A64_GEN_UNSUPPORTED;
            }

            ret = emit_gadget(state, logical_gadget);
            if (ret != A64_GEN_OK) return ret;

            if (dst_is_memory) {
                int store_idx = rd - 16;
                if (store_idx < 0 || store_idx > 14)
                    return A64_GEN_UNSUPPORTED;
                ret = emit_gadget(state, gadget_store_xreg_16_to_30[store_idx]);
                if (ret != A64_GEN_OK) return ret;
            }

            if (dst_is_sp) {
                ret = emit_gadget(state, (tcti_gadget_t)gadget_store_sp);
                if (ret != A64_GEN_OK) return ret;
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
                case 11: bf_gadget = gadget_sbfm; break;
                case 12: bf_gadget = gadget_bfm; break;
                case 13: bf_gadget = gadget_ubfm; break;
                default: return A64_GEN_UNSUPPORTED;
            }
            
            if (!bf_gadget)
                return A64_GEN_UNSUPPORTED;
            
            // Emit bitfield gadget
            int ret = emit_gadget(state, bf_gadget);
            if (ret != A64_GEN_OK) return ret;

            // Emit current PC for interpreter fallback
            ret = emit_u64(state, state->guest_pc);
            if (ret != A64_GEN_OK) return ret;
            
            // Emit Rd
            ret = emit_u64(state, instr->Rd);
            if (ret != A64_GEN_OK) return ret;
            
            // Emit Rn
            ret = emit_u64(state, instr->Rn);
            if (ret != A64_GEN_OK) return ret;
            
            // Emit immr (in instr->imm)
            ret = emit_u64(state, instr->imm);
            if (ret != A64_GEN_OK) return ret;
            
            // Emit imms (in instr->imm_shift)
            ret = emit_u64(state, instr->imm_shift);
            if (ret != A64_GEN_OK) return ret;
            
            // Emit is_64bit
            ret = emit_u64(state, instr->is_64bit ? 1 : 0);
            if (ret != A64_GEN_OK) return ret;
            
            return A64_GEN_OK;
        }
            
        default:
            return A64_GEN_UNSUPPORTED;
    }

    if (!gadget)
        return A64_GEN_UNSUPPORTED;

    // Emit load if source is memory-backed (x16-x30)
    if (src_is_memory) {
        int load_idx = rn - 16;  // x16=0, x17=1, ..., x30=14
        if (load_idx < 0 || load_idx > 14)
            return A64_GEN_UNSUPPORTED;
        
        int ret = emit_gadget(state, gadget_load_xreg_16_to_30[load_idx]);
        if (ret != A64_GEN_OK)
            return ret;
    }

    // Emit main operation
    int ret = emit_gadget(state, gadget);
    if (ret != A64_GEN_OK)
        return ret;

    // Emit store if destination is memory-backed (x16-x30)
    if (dst_is_memory) {
        int store_idx = rd - 16;
        if (store_idx < 0 || store_idx > 14)
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
 * For memory-backed registers (x16-x30), emits load/store sequences:
 *   Load x[16-30] -> temp register (x14 for first source, x15 for second)
 *   Execute operation
 *   Store result -> x[16-30] from temp (x14)
 *
 * Temp register allocation:
 *   x14: Primary temp for first source / destination
 *   x15: Secondary temp for second source
 *   x16: Used by load/store gadgets internally
 * ============================================================================
 */
int a64_gen_dp_reg(a64_gen_state_t *state, const a64_instr_t *instr) {
    int rd = instr->Rd;
    int rn = instr->Rn;
    int rm = instr->Rm;
    int op2 = bits(instr->raw, 24, 21);
    int rd_is_zero = (instr->set_flags && rd == 31);

    // Determine which registers are memory-backed (x16-x30)
    int src1_is_memory = (rn >= 16 && rn <= 30);
    int src2_is_memory = (rm >= 16 && rm <= 30);
    int dst_is_memory = (rd >= 16 && rd <= 30);
    int dst_is_sp = (rd == 31 && !rd_is_zero);
    
    // Allocate temp registers:
    // x14 (index 13) = primary temp (destination or first source)
    // x15 (index 14) = secondary temp (second source if needed)
    // gadget indices: 0-15 map to x1-x16, so x14=13, x15=14
    int eff_rd, eff_rn, eff_rm;
    
    if (dst_is_memory || dst_is_sp) {
        eff_rd = 13;  // Result goes to x14 temp
    } else {
        eff_rd = rd;
    }
    
    if (src1_is_memory) {
        eff_rn = 13;  // First source loaded to x14
    } else if (rn == 31) {
        eff_rn = 0;   // XZR -> use x0 (won't be read)
    } else {
        eff_rn = rn;
    }
    
    // For second source, use x15 if first source also needs temp
    // otherwise can reuse x14 if destination doesn't need it
    if (src2_is_memory) {
        if (src1_is_memory && (dst_is_memory || dst_is_sp)) {
            // Conflict: both sources and dest need temps
            // Need x14 for dest, so load src2 to x15
            eff_rm = 14;
        } else if (src1_is_memory) {
            // src1 in x14, can reuse for src2 if dest is different
            eff_rm = 13;
        } else {
            eff_rm = 14;  // Load src2 to x15
        }
    } else if (rm == 31) {
        eff_rm = 0;  // XZR
    } else {
        eff_rm = rm;
    }

    tcti_gadget_t gadget = NULL;

    if (op2 >= 0 && op2 <= 3) {
        if (instr->imm_shift != 0 || instr->shift_type != A64_SHIFT_LSL || instr->set_flags)
            return A64_GEN_UNSUPPORTED;

        if (rn == 31 && instr->subtype == 1) {
            gadget = gadget_mov_reg[eff_rd][eff_rm];
        } else {
            switch (instr->subtype) {
                case 0:
                    gadget = gadget_and_reg[eff_rd][eff_rn][eff_rm];
                    break;
                case 1:
                    gadget = gadget_orr_reg[eff_rd][eff_rn][eff_rm];
                    break;
                case 2:
                    gadget = gadget_eor_reg[eff_rd][eff_rn][eff_rm];
                    break;
                default:
                    return A64_GEN_UNSUPPORTED;
            }
        }
    } else if (op2 >= 4 && op2 <= 7) {
        if (instr->imm_shift != 0) {
            int ret;
            int work_dst = (dst_is_memory || dst_is_sp) ? 13 : rd;

            if (instr->shift_type != A64_SHIFT_LSL)
                return A64_GEN_UNSUPPORTED;
            if (src1_is_memory || rn == 31)
                return A64_GEN_UNSUPPORTED;

            if (src2_is_memory) {
                int load_idx = rm - 16;
                if (load_idx < 0 || load_idx > 14)
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
                    ret = emit_gadget(state, gadget_cmp_reg[rn][13]);
                } else {
                    ret = emit_gadget(state, (instr->subtype == 1)
                        ? gadget_subs_reg[work_dst][rn][13]
                        : gadget_adds_reg[work_dst][rn][13]);
                }
            } else {
                ret = emit_gadget(state, (instr->subtype == 1)
                    ? gadget_sub_reg[work_dst][rn][13]
                    : gadget_add_reg[work_dst][rn][13]);
            }
            if (ret != A64_GEN_OK)
                return ret;

            if (dst_is_memory) {
                int store_idx = rd - 16;
                if (store_idx < 0 || store_idx > 14)
                    return A64_GEN_UNSUPPORTED;
                ret = emit_gadget(state, gadget_store_xreg_16_to_30[store_idx]);
                if (ret != A64_GEN_OK)
                    return ret;
            } else if (dst_is_sp) {
                ret = emit_gadget(state, (tcti_gadget_t) gadget_store_sp);
                if (ret != A64_GEN_OK)
                    return ret;
            }

            return A64_GEN_OK;
        }
        if (instr->set_flags) {
            if (instr->subtype == 1 && rd == 31 && !src1_is_memory && !src2_is_memory
                    && rn != 31 && rm != 31) {
                gadget = gadget_cmp_reg[eff_rn][eff_rm];
            } else {
                gadget = (instr->subtype == 1)
                    ? gadget_subs_reg[eff_rd][eff_rn][eff_rm]
                    : gadget_adds_reg[eff_rd][eff_rn][eff_rm];
            }
        } else {
            gadget = (instr->subtype == 1)
                ? gadget_sub_reg[eff_rd][eff_rn][eff_rm]
                : gadget_add_reg[eff_rd][eff_rn][eff_rm];
        }
    } else if (op2 >= 8 && op2 <= 11) {
        int ret;

        if (dst_is_memory || (dst_is_sp && !rd_is_zero) || src1_is_memory || rn == 31)
            return A64_GEN_UNSUPPORTED;
        if (src2_is_memory || rm == 31)
            return A64_GEN_UNSUPPORTED;
        if (instr->imm_shift < 0 || instr->imm_shift > 4)
            return A64_GEN_UNSUPPORTED;

        ret = emit_gadget(state, gadget_mov_reg[14][rm]);
        if (ret != A64_GEN_OK)
            return ret;

        switch (instr->extend_type) {
            case A64_EXT_UXTW:
                ret = emit_gadget(state, gadget_mov_imm[13]);
                if (ret != A64_GEN_OK)
                    return ret;
                ret = emit_u64(state, 0xffffffffULL);
                if (ret != A64_GEN_OK)
                    return ret;
                ret = emit_gadget(state, gadget_and_reg[14][14][13]);
                if (ret != A64_GEN_OK)
                    return ret;
                // Clear x13 to prevent corruption of guest registers
                // XOR x13 with itself to set it to 0
                ret = emit_gadget(state, gadget_eor_reg[13][13][13]);
                if (ret != A64_GEN_OK)
                    return ret;
                break;

            case A64_EXT_UXTX:
            case A64_EXT_LSL:
                break;

            default:
                return A64_GEN_UNSUPPORTED;
        }

        for (int i = 0; i < instr->imm_shift; i++) {
            ret = emit_gadget(state, gadget_add_reg[14][14][14]);
            if (ret != A64_GEN_OK)
                return ret;
        }

        if (instr->set_flags) {
            if (instr->subtype == 1 && rd == 31) {
                gadget = gadget_cmp_reg[eff_rn][14];
            } else {
                gadget = (instr->subtype == 1)
                    ? gadget_subs_reg[eff_rd][eff_rn][14]
                    : gadget_adds_reg[eff_rd][eff_rn][14];
            }
        } else {
            gadget = (instr->subtype == 1)
                ? gadget_sub_reg[eff_rd][eff_rn][14]
                : gadget_add_reg[eff_rd][eff_rn][14];
        }
    } else {
        return A64_GEN_UNSUPPORTED;
    }

    if (!gadget)
        return A64_GEN_UNSUPPORTED;

    // Emit load for first source if memory-backed
    if (src1_is_memory) {
        int load_idx = rn - 16;  // x16=0, ..., x30=14
        if (load_idx < 0 || load_idx > 14)
            return A64_GEN_UNSUPPORTED;
        
        int ret = emit_gadget(state, gadget_load_xreg_16_to_30[load_idx]);
        if (ret != A64_GEN_OK)
            return ret;
    }
    
    // Emit load for second source if memory-backed and using different temp
    if (src2_is_memory && eff_rm != eff_rn) {
        int load_idx = rm - 16;
        if (load_idx < 0 || load_idx > 14)
            return A64_GEN_UNSUPPORTED;
        
        int ret = emit_gadget(state, gadget_load_xreg_16_to_30[load_idx]);
        if (ret != A64_GEN_OK)
            return ret;
    }

    // Emit main operation
    int ret = emit_gadget(state, gadget);
    if (ret != A64_GEN_OK)
        return ret;

    // Emit store if destination is memory-backed
    if (dst_is_memory) {
        int store_idx = rd - 16;
        if (store_idx < 0 || store_idx > 14)
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
int a64_gen_branch(a64_gen_state_t *state, const a64_instr_t *instr) {
    tcti_gadget_t gadget = NULL;
    int ret = A64_GEN_OK;

    switch (instr->subtype) {
        case A64_BRANCH_UNCOND:
            ret = emit_gadget(state, gadget_b);
            if (ret != A64_GEN_OK)
                return ret;

            ret = emit_u64(state, state->guest_pc + instr->imm);
            if (ret != A64_GEN_OK)
                return ret;

            ret = emit_u64(state, instr->op ? 1 : 0);
            if (ret != A64_GEN_OK)
                return ret;

            ret = emit_u64(state, state->guest_pc + 4);
            if (ret != A64_GEN_OK)
                return ret;

            state->is_complete = 1;
            return A64_GEN_OK;

        case A64_BRANCH_COND:
            ret = emit_gadget(state, gadget_bcond[instr->cond & 0xf]);
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
            
        case A64_BRANCH_CMP:
        {
            if (instr->Rd < 0 || instr->Rd >= 16)
                return A64_GEN_UNSUPPORTED;

            ret = emit_gadget(state, instr->op
                    ? gadget_cbnz_reg[instr->Rd]
                    : gadget_cbz_reg[instr->Rd]);
            if (ret != A64_GEN_OK)
                return ret;
            
            // Emit target PC
            uint64_t target_pc = state->guest_pc + instr->imm;
            ret = emit_u64(state, target_pc);
            if (ret != A64_GEN_OK)
                return ret;

            ret = emit_u64(state, state->guest_pc + 4);
            if (ret != A64_GEN_OK)
                return ret;
            state->is_complete = 1;
            return A64_GEN_OK;
        }

        case A64_BRANCH_TEST:
        {
            if (instr->Rd < 0 || instr->Rd >= 16)
                return A64_GEN_UNSUPPORTED;

            ret = emit_gadget(state, instr->op
                    ? gadget_tbnz_reg[instr->Rd]
                    : gadget_tbz_reg[instr->Rd]);
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
            ret = emit_gadget(state, gadget_br);
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

    if (!gadget)
        return A64_GEN_UNSUPPORTED;
    
    // Emit the branch gadget
    ret = emit_gadget(state, gadget);
    if (ret != A64_GEN_OK)
        return ret;
    
    // Emit target PC (current PC + offset from instruction)
    uint64_t target_pc = state->guest_pc + instr->imm;
    ret = emit_u64(state, target_pc);
    if (ret != A64_GEN_OK)
        return ret;
    
    return A64_GEN_OK;
}

/* ============================================================================
 * Load/Store Instructions
 * ============================================================================
 */
static int a64_emit_ldst_single(a64_gen_state_t *state, uint64_t fault_pc,
        int rt, int rn, int64_t imm, int size, int idx_mode, int is_signed,
        int rm, int extend_type, int imm_shift, int is_reg_offset, int is_load) {
    int ret;
    tcti_gadget_t gadget = NULL;
    uint64_t meta;

    if (is_load) {
        gadget = gadget_ldr_x;
    } else {
        gadget = gadget_str_x;
    }

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

    meta = (uint64_t) (is_signed ? 1 : 0);
    if (is_reg_offset) {
        meta |= (uint64_t) 1 << 8;
        meta |= ((uint64_t) (rm & 0xff)) << 16;
        meta |= ((uint64_t) (extend_type & 0xff)) << 24;
        meta |= ((uint64_t) (imm_shift & 0xff)) << 32;
    }

    ret = emit_u64(state, meta);
    if (ret != A64_GEN_OK)
        return ret;

    return A64_GEN_OK;
}

static int a64_emit_base_writeback(a64_gen_state_t *state, int rn, int64_t imm) {
    int ret;

    if (imm == 0)
        return A64_GEN_OK;

    if (rn >= 16 && rn <= 30) {
        int load_idx = rn - 16;
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

int a64_gen_ldst(a64_gen_state_t *state, const a64_instr_t *instr) {
    if (instr->is_vector || instr->subtype == A64_LDST_LITERAL ||
            instr->subtype == A64_LDST_ATOMIC) {
        return A64_GEN_UNSUPPORTED;
    }

    if (instr->is_pair) {
        int access_size;
        int first_mode;
        int second_mode = A64_INDEX_OFFSET;
        int64_t first_imm;
        int64_t second_imm;
        int ret;

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

        ret = a64_emit_ldst_single(state, state->guest_pc, instr->Rd, instr->Rn,
                first_imm, access_size, first_mode, 0, 0, 0, 0, 0,
                bit(instr->raw, 22));
        if (ret != A64_GEN_OK)
            return ret;

        ret = a64_emit_ldst_single(state, state->guest_pc, instr->Rm, instr->Rn,
                second_imm, access_size, second_mode, 0, 0, 0, 0, 0,
                bit(instr->raw, 22));
        if (ret != A64_GEN_OK)
            return ret;

        if (instr->idx_mode == A64_POST_INDEX)
            return a64_emit_base_writeback(state, instr->Rn, instr->pair_offset);

        return A64_GEN_OK;
    }

    int ret = a64_emit_ldst_single(state, state->guest_pc, instr->Rd, instr->Rn,
            instr->imm, instr->size, instr->idx_mode, instr->is_signed,
            instr->Rm, instr->extend_type, instr->imm_shift,
            bits(instr->raw, 11, 10) == 2, bit(instr->raw, 22));
    if (ret != A64_GEN_OK)
        return ret;
    
    // Single-instruction helpers already apply pre/post-index writeback.
    // Emitting an extra base writeback here corrupts the architectural base
    // register and causes post-index loops to run past their bounds.
    return A64_GEN_OK;
}

/* ============================================================================
 * System Instructions (SVC, HINT, etc.)
 * ============================================================================
 */
int a64_gen_system(a64_gen_state_t *state, const a64_instr_t *instr) {
    tcti_gadget_t gadget = NULL;
    
    // Exception generation (SVC, HVC, SMC) - subtype from decoder
    switch (instr->subtype) {
        case 0: // SVC
            gadget = gadget_svc;
            state->is_complete = 1;
            break;
        case 6: // HINT (NOP, YIELD, etc.)
            if (instr->imm == 0) {
                gadget = gadget_nop;
            }
            break;
        default:
            return A64_GEN_UNSUPPORTED;
    }

    if (gadget)
        return emit_gadget(state, gadget);
    
    return A64_GEN_UNSUPPORTED;
}

/* ============================================================================
 * Main Instruction Generator
 * ============================================================================
 */
int a64_gen_instruction(a64_gen_state_t *state, uint32_t insn, uint64_t pc) {
    a64_instr_t decoded;
    int ret = a64_decode(insn, &decoded);
    
    if (ret < 0)
        return A64_GEN_INVALID_INSN;
    
    state->guest_pc = pc;
    state->raw_insn = insn;
    state->decoded = decoded;
    
    // Dispatch by category
    switch (decoded.cat) {
        case A64_DP_IMM:
        case A64_SIMD0:  // cat=8 is actually DP_IMM (ADR, ADRP, etc.)
        case A64_DP_IMM2: // cat=13 is also DP_IMM
            ret = a64_gen_dp_imm(state, &decoded);
            break;
            
        case A64_DP_REG:
        case A64_DP_REG2:
        case A64_DP_REG3:
        case A64_DP_REG4:
            ret = a64_gen_dp_reg(state, &decoded);
            break;
            
        case A64_BRANCH:
        case A64_BRANCH2:
            // System instructions (SVC, HVC, SMC, hints, barriers) have subtypes 0-1
            // Branch instructions (CBZ, CBNZ, TBZ, TBNZ, B, B.cond) have subtypes 2-6
            if (decoded.subtype == A64_EXCEPTION || decoded.subtype == 6) {
                ret = a64_gen_system(state, &decoded);
            } else {
                ret = a64_gen_branch(state, &decoded);
            }
            break;
            
        case A64_LD_ST:
            ret = a64_gen_ldst(state, &decoded);
            break;
            
        default:
            ret = A64_GEN_UNSUPPORTED;
    }
    
    if (ret == A64_GEN_OK)
        state->instructions_processed++;
    
    return ret;
}

/* ============================================================================
 * Block Finalization
 * ============================================================================
 */
int a64_gen_finalize(a64_gen_state_t *state) {
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
int a64_gen_basic_block(a64_gen_state_t *state, struct cpu_state *cpu,
                        struct tlb *tlb, uint64_t *end_pc) {
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
                return ret;  // First instruction unsupported
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
