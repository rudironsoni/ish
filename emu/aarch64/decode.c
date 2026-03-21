#include "emu/aarch64/decode.h"
#include <string.h>
#include <stdio.h>

/* Mark variables as intentionally unused (for future expansion) */
#define UNUSED(x) ((void)(x))

// Main decode entry point
int a64_decode(uint32_t insn, a64_instr_t *out) {
    memset(out, 0, sizeof(*out));
    out->raw = insn;

    // Check for system instructions first (SVC, HVC, hints, barriers)
    // These have top 8 bits = 0xD4 or 0xD5 (exception and system)
    uint8_t top_byte = (insn >> 24) & 0xFF;
    if (top_byte == 0xD4 || top_byte == 0xD5) {
        return a64_decode_system(insn, out);
    }

    // Get main category from bits 28:25
    a64_category_t cat = a64_get_category(insn);
    out->cat = cat;

    switch (cat) {
        case A64_DP_IMM:      // 0x9
            return a64_decode_dp_imm(insn, out);

        case A64_DP_IMM2:     // 0xD
            // All category 0xD instructions are actually DP_REG instructions
            // (ADC/SBC, 3-source, conditional select, conditional compare)
            return a64_decode_dp_reg(insn, out);

        case A64_DP_REG:      // 0x4
        case A64_DP_REG2:     // 0x5 (most register ops)
        case A64_DP_REG3:     // 0x6
        case A64_DP_REG4:     // 0x7
            return a64_decode_dp_reg(insn, out);

        case A64_BRANCH:      // 0xA
        case A64_BRANCH2:     // 0xB
            return a64_decode_branch(insn, out);

        case A64_LD_ST:       // 0xC
            return a64_decode_ldst(insn, out);

        case A64_SIMD:        // 0xE
        case A64_SIMD2:       // 0xF
            return a64_decode_simd_fp(insn, out);

        case A64_RESERVED:
        case A64_RESERVED1:
        case A64_RESERVED2:
        case A64_RESERVED3:
        case A64_SIMD0:
            // op0 = 0x8 is actually used for Data Processing - Immediate in ARMv8-A
            // This includes: ADR, ADRP, ADD/SUB imm, logical imm, move wide, bitfield
            return a64_decode_dp_imm(insn, out);

        default:
            // Check for system instructions (HINT, barriers, etc.)
            // System instructions: top 8 bits = 0xD5 (11010101)
            if ((insn & 0xFF000000) == 0xD5000000) {
                return a64_decode_system(insn, out);
            }
            // Undefined/unallocated
            return -1;
    }
}

// Data Processing - Immediate
int a64_decode_dp_imm(uint32_t insn, a64_instr_t *out) {
    // Check if this is actually DP_IMM (bit 28 should be 1 for these)
    if (!(insn & (1 << 28)))
        return -1;

    // Get subcategory from bits 25:23
    int op0 = bits(insn, 25, 23);

    // Common: sf bit (bit 31) indicates 64-bit operation
    out->is_64bit = bit(insn, 31);

    switch (op0) {
        case 0: // 000 - PC-rel addressing (ADR, ADRP)
        {
            int op = bit(insn, 31); // 0=ADR, 1=ADRP
            out->Rd = bits(insn, 4, 0);
            // imm = immhi:immlo
            int64_t immhi = bits(insn, 23, 5);
            int immlo = bits(insn, 30, 29);
            out->imm = (immhi << 2) | immlo;
            if (op) {
                // ADRP - page aligned, sign extended
                out->imm = sign_extend(out->imm, 21) << 12;
                out->subtype = 1; // ADRP
            } else {
                // ADR - byte aligned, sign extended
                out->imm = sign_extend(out->imm, 21);
                out->subtype = 0; // ADR
            }
            return 0;
        }

        case 2: // 010 - Add/Subtract immediate OR Move Wide Immediate
        {
            int opc = bits(insn, 30, 29);
            
            // Check if this is MOVZ/MOVN/MOVK (opc = 01, 00, 10)
            if ((opc & 2) != 0) { // bit 29 set = MOVZ/MOVN/MOVK
                // Move Wide Immediate
                int hw = bits(insn, 22, 21);
                uint64_t imm16 = bits(insn, 20, 5);
                out->is_64bit = bit(insn, 31);
                out->Rd = bits(insn, 4, 0);
                out->imm = imm16 << (hw * 16);
                out->subtype = (opc >> 1); // 0=MOVN, 1=MOVZ, 2=MOVK, 3=???(reserved)
                return 0;
            }
            
            // Add/Subtract immediate (opc = 00 or 01 but bit 29 clear)
            int op = bit(insn, 30); // 0=ADD, 1=SUB
            int S = bit(insn, 29);  // Set flags (should be 0 for these)
            out->is_64bit = bit(insn, 31);
            out->Rd = bits(insn, 4, 0);
            out->Rn = bits(insn, 9, 5);
            out->imm = bits(insn, 21, 10);
            out->set_flags = S;
            out->subtype = op ? 1 : 0; // 0=ADD, 1=SUB

            // Check for shift (bit 22 is sh)
            if (bit(insn, 22)) {
                // Shifted by 12
                out->imm <<= 12;
            }

            return 0;
        }
        
        case 3: // 011 - Add/Subtract immediate (alternate encoding with shift)
        {
            int op = bit(insn, 30); // 0=ADD, 1=SUB
            int S = bit(insn, 29);  // Set flags
            out->is_64bit = bit(insn, 31);
            out->Rd = bits(insn, 4, 0);
            out->Rn = bits(insn, 9, 5);
            out->imm = bits(insn, 21, 10);
            out->set_flags = S;
            out->subtype = op ? 1 : 0; // 0=ADD, 1=SUB

            // Check for shift (bit 22 is sh)
            if (bit(insn, 22)) {
                // Shifted by 12
                out->imm <<= 12;
            }

            return 0;
        }

        case 4: // 100 - Logical immediate (AND/ORR/EOR/ANDS)
        case 5: // 101
        {
            int opc = bits(insn, 30, 29);
            out->is_64bit = bit(insn, 31);
            out->Rd = bits(insn, 4, 0);
            out->Rn = bits(insn, 9, 5);
            // immN:imms:immr at bits 22:16, 15:10, 21:16
            int N = bit(insn, 22);
            int immr = bits(insn, 21, 16);
            int imms = bits(insn, 15, 10);
            out->imm = ((uint64_t)N << 13) | ((uint64_t)imms << 6) | immr;
            out->subtype = opc; // 0=AND, 1=ORR, 2=EOR, 3=ANDS
            out->set_flags = (opc == 3);
            return 0;
        }

        case 6: // 110 - Move wide immediate
        {
            int opc = bits(insn, 30, 29);
            out->is_64bit = bit(insn, 31);
            out->Rd = bits(insn, 4, 0);
            uint64_t imm16 = bits(insn, 20, 5);
            int hw = bits(insn, 22, 21); // shift: 0=0, 1=16, 2=32, 3=48
            out->imm = imm16 << (hw * 16);
            out->subtype = opc; // 0=MOVN, 1=MOVZ, 2=MOVK
            return 0;
        }

        case 7: // 111 - Bitfield move
        {
            int opc = bits(insn, 30, 29);
            out->is_64bit = bit(insn, 31);
            out->Rd = bits(insn, 4, 0);
            out->Rn = bits(insn, 9, 5);
            int immr = bits(insn, 21, 16);
            int imms = bits(insn, 15, 10);
            int N = bit(insn, 22);
            UNUSED(N); // N is implicit in instruction encoding
            out->imm = immr;
            out->imm_shift = imms;
            out->subtype = opc; // 0=SBFM, 1=BFM, 2=UBFM
            return 0;
        }

        case 1: // 001 - Extract
        {
            int op21 = bits(insn, 30, 29);
            out->is_64bit = bit(insn, 31);
            out->Rd = bits(insn, 4, 0);
            out->Rn = bits(insn, 9, 5);
            out->Rm = bits(insn, 20, 16);
            int imms = bits(insn, 15, 10);
            (void)bit(insn, 22);  // N bit - implicit in encoding
            out->imm = imms; // lsb position
            out->subtype = op21; // 0=EXTR
            return 0;
        }

        default:
            return -1;
    }
}

// Data Processing - Register
int a64_decode_dp_reg(uint32_t insn, a64_instr_t *out) {
    // DP_REG categories: op0 = bits 28:25 = 0100-0111 (0x4-0x7) or 1101 (0xD for ADC/SBC)
    // For categorization within DP_REG, use op2 = bits 24:21
    out->is_64bit = bit(insn, 31);

    // Check if this is ADC/SBC (category 0xD) which needs special handling
    a64_category_t cat = a64_get_category(insn);
    if (cat == A64_DP_IMM2) {
        // ADC/SBC with carry - op2 = bits 24:21 should be 0-2
        int op2 = bits(insn, 24, 21);
        if (op2 <= 2) {
            int op = bit(insn, 30); // 0=ADC, 1=SBC
            int S = bit(insn, 29);
            out->Rd = bits(insn, 4, 0);
            out->Rn = bits(insn, 9, 5);
            out->Rm = bits(insn, 20, 16);
            out->set_flags = S;
            out->subtype = op ? 3 : 2; // 2=ADC, 3=SBC
            return 0;
        }
        // Fall through for other category 0xD instructions
    }

    // Standard DP_REG processing based on op2 = bits 24:21
    int op2 = bits(insn, 24, 21);

    switch (op2) {
        case 0: // Logical shifted register
        case 1:
        case 2:
        case 3:
        {
                    int opc = bits(insn, 30, 29);
                int shift = bits(insn, 23, 22);
                (void)bit(insn, 21);  // N bit - not used in this encoding
                out->Rd = bits(insn, 4, 0);
                out->Rn = bits(insn, 9, 5);
                out->Rm = bits(insn, 20, 16);
                out->imm_shift = bits(insn, 15, 10);
                out->shift_type = shift;
                out->subtype = opc; // 0=AND, 1=BIC, 2=ORR, 3=ORN, 4=EOR, 5=EON, 6=ANDS, 7=BICS
                out->set_flags = (opc == 6 || opc == 7);
                return 0;
            }

        case 4: // Add/subtract (shifted register)
        case 5:
        case 6:
        case 7:
        {
                int op = bit(insn, 30); // 0=ADD, 1=SUB
                int S = bit(insn, 29);
                int shift = bits(insn, 23, 22);
                out->Rd = bits(insn, 4, 0);
                out->Rn = bits(insn, 9, 5);
                out->Rm = bits(insn, 20, 16);
                out->imm_shift = bits(insn, 15, 10);
                out->shift_type = shift;
                out->set_flags = S;
                out->subtype = op ? 1 : 0;
                return 0;
            }

        case 8: // Add/subtract (extended register) OR Conditional select
        case 9:
        case 10:
        case 11:
        {
                // Check if this is conditional select (cat=0xD, op2=8-11)
                a64_category_t cat = a64_get_category(insn);
                if (cat == A64_DP_IMM2) {
                    // Conditional select: CSEL, CSINC, CSINV, CSNEG
                    int op = bit(insn, 30); // 0=CSEL/CSINC, 1=CSINV/CSNEG
                    int S = bit(insn, 29); // 0 for conditional select
                    int cond = bits(insn, 15, 12);
                    int o2 = bit(insn, 10); // 0 for CSEL/CSINV, 1 for CSINC/CSNEG
                    out->Rd = bits(insn, 4, 0);
                    out->Rn = bits(insn, 9, 5);
                    out->Rm = bits(insn, 20, 16);
                    out->cond = cond;
                    out->set_flags = S;
                    // subtype: 0=CSEL, 1=CSINC, 2=CSINV, 3=CSNEG
                    out->subtype = (op << 1) | o2;
                    return 0;
                }
                // Add/subtract (extended register)
                int op = bit(insn, 30);
                int S = bit(insn, 29);
                int opt = bits(insn, 23, 22); // option for extension
                int imm3 = bits(insn, 15, 10);
                out->Rd = bits(insn, 4, 0);
                out->Rn = bits(insn, 9, 5);
                out->Rm = bits(insn, 20, 16);
                out->extend_type = opt;
                out->imm_shift = imm3;
                out->set_flags = S;
                out->subtype = op ? 1 : 0;
                return 0;
            }

        case 12: // Add/subtract (with carry)
        case 13:
        {
                int op = bit(insn, 30);
                int S = bit(insn, 29);
                out->Rd = bits(insn, 4, 0);
                out->Rn = bits(insn, 9, 5);
                out->Rm = bits(insn, 20, 16);
                out->set_flags = S;
                out->subtype = op ? 3 : 2; // 2=ADC, 3=SBC
                return 0;
            }

        case 14: // Conditional compare (register)
        case 15:
        {
                int op = bit(insn, 30);
                (void)bit(insn, 29);  // S bit - always 1 for CCMN/CCMP
                (void)bit(insn, 10);  // o2 bit - part of encoding
                (void)bit(insn, 4);   // o3 bit - part of encoding
                int cond = bits(insn, 15, 12);
                int nzcv = bits(insn, 3, 0);
                out->Rn = bits(insn, 9, 5);
                out->Rm = bits(insn, 20, 16);
                out->cond = cond;
                out->imm = nzcv;
                out->subtype = op ? 5 : 4; // 4=CCMN, 5=CCMP
                return 0;
            }

            default:
            return -1;
    }
}

// Branch instructions
// Based on ARMv8-A encoding: op0 = bits 31:29
int a64_decode_branch(uint32_t insn, a64_instr_t *out) {
    (void)bits(insn, 28, 25);  // op1 - always 10 or 11 for branch instructions
    int op0 = bits(insn, 31, 29);  // Changed from 30:29 to 31:29

    switch (op0) {
        case 0: // 000 - Unconditional branch (immediate)
        case 4: // 100 - Also unconditional branch
        {
            int op = bit(insn, 31); // 0=B, 1=BL
            int64_t imm26 = bits(insn, 25, 0);
            out->imm = sign_extend(imm26, 26) << 2;
            out->subtype = op; // 0=B, 1=BL
            return 0;
        }

        case 2: // 010 - Conditional branch (immediate)
        {
            int o1 = bit(insn, 24);
            if (o1 == 0) {
                // B.cond
                int64_t imm19 = bits(insn, 23, 5);
                int cond = bits(insn, 3, 0);
                out->imm = sign_extend(imm19, 19) << 2;
                out->cond = cond;
                out->subtype = A64_BRANCH_COND;
                return 0;
            }
            break;
        }

        case 1: // 001 - Compare and branch (immediate) - 32-bit
        case 5: // 101 - Compare and branch (immediate) - 64-bit
        {
            int op = bit(insn, 24); // 0=CBZ, 1=CBNZ
            out->is_64bit = bit(insn, 31);
            int64_t imm19 = bits(insn, 23, 5);
            out->Rd = bits(insn, 4, 0);
            out->imm = sign_extend(imm19, 19) << 2;
            out->subtype = op ? 1 : 0; // 0=CBZ, 1=CBNZ
            return 0;
        }

        case 3: // 011 - Test and branch (immediate)
        {
            int op = bit(insn, 24); // 0=TBZ, 1=TBNZ
            int imm14 = bits(insn, 18, 5);
            int bit_pos = (bit(insn, 31) << 5) | bits(insn, 23, 19);
            out->Rd = bits(insn, 4, 0);
            out->imm = sign_extend(imm14, 14) << 2;
            out->imm_shift = bit_pos;
            out->subtype = op ? 1 : 0; // 0=TBZ, 1=TBNZ
            return 0;
        }

        case 6: // 110 - Unconditional branch (register)
        case 7: // 111 - Also branch register
        {
            // BR, BLR, RET - handled below
            break;
        }
    }

    // Check for branch register / exception generation
    int op3 = bits(insn, 30, 25);
    // 1101011 = 0x6B for BR/BLR/RET (bits 31:25)
    // So bits 30:25 = 0b101011 = 0x2B
    if (op3 == 0x2B) { // 101011 - BR/BLR/RET
        // Unconditional branch (register)
        int opc = bits(insn, 24, 21);
        int op2 = bits(insn, 20, 16);
        int op3_low = bits(insn, 15, 10);
        int Rn = bits(insn, 9, 5);
        int op4 = bits(insn, 4, 0);
        (void)op2; (void)op3_low; (void)op4;  // Used in validation below

        if (op2 == 0x1F && op3_low == 0 && op4 == 0) {
            out->Rn = Rn;
            switch (opc) {
                case 0: // BR
                case 1: // BLR
                case 2: // RET
                    out->subtype = A64_BRANCH_REG;
                    return 0;
            }
        }
    }

    // Exception generation
    if ((insn & 0xFF000000) == 0xD4000000) {
        (void)bits(insn, 23, 21);     // opc - distinguishes SVC/HVC/SMC
        (void)bits(insn, 20, 16);     // op2 - reserved
        (void)bits(insn, 1, 0);       // LL - instruction level
        uint16_t imm16 = bits(insn, 15, 0);
        out->imm = imm16;
        out->subtype = A64_EXCEPTION;
        return 0;
    }

    return -1;
}

// Load/Store instructions
int a64_decode_ldst(uint32_t insn, a64_instr_t *out) {
    int op0 = bits(insn, 31, 30);
    int op1 = bit(insn, 28);
    int op2 = bits(insn, 23, 21);
    int op3 = bits(insn, 15, 12);
    int op4 = bits(insn, 11, 10);

    out->is_64bit = op0 == 3;
    out->is_vector = bit(insn, 26);

    // Size encoding: 0=B, 1=H, 2=W, 3=X (or S for FP)
    if (out->is_vector) {
        out->size = op0; // Vector size
    } else {
        out->size = op0;
    }

    if (op1 == 0) {
        // Load/store unscaled immediate (LDUR/STUR)
        if (op2 == 0) {
            (void)bit(insn, 26);  // V bit - vector flag, extracted above
            int imm9 = bits(insn, 20, 12);
            (void)bit(insn, 10);  // post-index bit
            (void)bit(insn, 22);  // L bit - load/store flag
            out->Rd = bits(insn, 4, 0);
            out->Rn = bits(insn, 9, 5);
            out->imm = sign_extend(imm9, 9);
            out->is_signed = false;
            return 0;
        }
    }

    // Load/store (unsigned immediate) - handles both op1=0 (op2>=2) and op1=1
    // op1=0, op2>=2: unsigned immediate with size-based scaling
    // op1=1: unsigned immediate (bit 24 = 1 distinguishes from unscaled)
    if (!out->is_vector && (op1 == 1 || (op1 == 0 && op2 >= 2))) {
        (void)bit(insn, 22);  // L bit - load/store flag
        uint64_t imm12 = bits(insn, 21, 10);
        out->Rd = bits(insn, 4, 0);
        out->Rn = bits(insn, 9, 5);
        // Scale immediate by size
        int scale = out->is_64bit ? 3 : out->size;
        out->imm = imm12 << scale;
        return 0;
    }

    // Load/store register pair
    if (op2 == 1) {
        (void)bit(insn, 22);  // L bit - load/store flag
        int imm7 = bits(insn, 21, 15);
        int Rt2 = bits(insn, 14, 10);
        (void)bits(insn, 24, 23);  // mode - indexing mode

        out->Rd = bits(insn, 4, 0);
        out->Rn = bits(insn, 9, 5);
        out->Rm = Rt2; // Second register
        out->is_pair = true;

        // Scale by size
        int scale = 2 + out->size;
        out->pair_offset = sign_extend(imm7, 7) << scale;

        return 0;
    }

    // Load literal
    if (op2 == 0 && bit(insn, 27) && !bit(insn, 24)) {
        (void)bit(insn, 26);  // V bit - vector flag
        int64_t imm19 = bits(insn, 23, 5);
        out->Rd = bits(insn, 4, 0);
        out->imm = sign_extend(imm19, 19) << 2;
        out->subtype = A64_LDST_LITERAL;
        return 0;
    }

    // Load/store register (register offset)
    if (op4 == 2) {
        int S = bit(insn, 12);
        int opt = bits(insn, 15, 13);
        out->Rd = bits(insn, 4, 0);
        out->Rn = bits(insn, 9, 5);
        out->Rm = bits(insn, 20, 16);
        out->extend_type = opt;
        out->imm_shift = S ? (out->size) : 0;
        return 0;
    }

    // Atomic operations
    if (op3 == 0xE || op3 == 0xF) {
        out->is_pair = false;
        out->subtype = A64_LDST_ATOMIC;
        return 0;
    }

    return -1;
}

// SIMD and FP instructions
int a64_decode_simd_fp(uint32_t insn, a64_instr_t *out) {
    int op0 = bits(insn, 28, 25);

    // Floating point data processing (scalar)
    if (op0 == 0xE || op0 == 0xF) {
        (void)bit(insn, 31);  // M bit - part of encoding
        (void)bit(insn, 29);  // S bit - part of encoding
        int ptype = bits(insn, 23, 22); // 00=H, 01=S, 10=D
        (void)bits(insn, 15, 12);  // opcode - part of encoding
        int Rn = bits(insn, 9, 5);
        int Rd = bits(insn, 4, 0);

        // Size from precision type
        switch (ptype) {
            case 1: out->size = 2; break; // Single
            case 2: out->size = 3; break; // Double
            default: out->size = 1; break; // Half
        }

        out->Rd = Rd;
        out->Rn = Rn;
        out->Rm = bits(insn, 20, 16);
        out->Ra = bits(insn, 14, 10); // For FMA

        return 0;
    }

    // SIMD vector operations
    if (op0 >= 0x8 && op0 <= 0xB) {
        out->is_vector = true;
        out->Rd = bits(insn, 4, 0);
        out->Rn = bits(insn, 9, 5);
        out->Rm = bits(insn, 20, 16);
        return 0;
    }

    return -1;
}

// System instructions (SVC, MRS, MSR, barriers, hints)
int a64_decode_system(uint32_t insn, a64_instr_t *out) {
    // HINT instructions (NOP, YIELD, WFE, WFI, SEV, etc.)
    // HINT encoding: bits 31:20 = 1101 0101 0000 (0xD50), CRm varies, op2=0, Rt=31
    // Base pattern: 0xD503201F = NOP (CRm=0)
    // The only variable part is CRm (bits 11:8) for different hint types
    if ((insn & 0xFFFFF01F) == 0xD503201F) {
        out->subtype = 6; // HINT
        out->imm = bits(insn, 11, 8); // CRm field selects hint type
        return 0;
    }

    // SVC - System call
    if ((insn & 0xFFC00000) == 0xD4000000) {
        int op = bits(insn, 23, 21);
        if (op == 1) { // SVC
            uint16_t imm16 = bits(insn, 15, 0);
            out->imm = imm16;
            out->subtype = 0; // SVC
            return 0;
        }
        if (op == 0) { // HVC
            uint16_t imm16 = bits(insn, 15, 0);
            out->imm = imm16;
            out->subtype = 1; // HVC
            return 0;
        }
    }

    // MRS/MSR
    // MRS encoding: 110101010011xxxxxxxxxxxxxxxxxxx (0xD5300000)
    // MSR (imm): 110101010001xxxxxxxxxxxxxxxxxxx (0xD5100000)
    // MSR (reg): 110101010001xxxxxxxxxxxxxxxxxxx (0xD5100000)
    // The L bit at position 21 distinguishes MRS (1) from MSR (0)
    if ((insn & 0xFFF00000) == 0xD5300000) { // MRS
        int Rt = bits(insn, 4, 0);
        int sysreg = bits(insn, 19, 5);
        out->Rd = Rt;
        out->sysreg = sysreg;
        out->subtype = 2; // MRS
        return 0;
    }

    if ((insn & 0xFFD80000) == 0xD5100000) { // MSR (imm)
        int op1 = bits(insn, 18, 16);
        (void)bits(insn, 11, 8);   // CRm - sysreg field
        (void)bits(insn, 7, 5);    // op2 - sysreg field
        int imm = bits(insn, 4, 0);
        out->op = op1;
        out->imm = imm;
        out->subtype = 3; // MSR (imm)
        return 0;
    }

    if ((insn & 0xFFD00000) == 0xD5100000) { // MSR (reg)
        int Rt = bits(insn, 4, 0);
        int sysreg = bits(insn, 19, 5);
        out->Rd = Rt;
        out->sysreg = sysreg;
        out->subtype = 4; // MSR (reg)
        return 0;
    }

    // System instructions (hint, barriers, etc.)
    // ISB/DSB/DMB barriers: bits 31:20 = 0xD50, CRn=3 (bits 15:12), CRm varies
    // Pattern: 0xD50xx020 where xx encodes the barrier type
    if ((insn & 0xFFF00020) == 0xD5000020) {
        // ISB/DSB/DMB - bits 15:12 = CRn=3 for barriers
        if (bits(insn, 15, 12) == 3) {
            out->imm = bits(insn, 11, 8); // CRm
            out->subtype = 5; // barriers
            return 0;
        }
    }

    return -1;
}

// String helpers
const char *a64_category_name(a64_category_t cat) {
    switch (cat) {
        case A64_RESERVED:  return "RESERVED(0)";
        case A64_RESERVED1: return "RESERVED(1)";
        case A64_RESERVED2: return "RESERVED(2)";
        case A64_RESERVED3: return "RESERVED(3)";
        case A64_DP_REG:    return "DP_REG(4)";
        case A64_DP_REG2:   return "DP_REG(5)";
        case A64_DP_REG3:   return "DP_REG(6)";
        case A64_DP_REG4:   return "DP_REG(7)";
        case A64_SIMD0:     return "SIMD(8)";
        case A64_DP_IMM:    return "DP_IMM(9)";
        case A64_BRANCH:    return "BRANCH(A)";
        case A64_BRANCH2:   return "BRANCH(B)";
        case A64_LD_ST:     return "LD_ST(C)";
        case A64_DP_IMM2:   return "DP_IMM(D)";
        case A64_SIMD:      return "SIMD(E)";
        case A64_SIMD2:     return "SIMD(F)";
        default:            return "UNKNOWN";
    }
}

const char *a64_cond_name(a64_cond_t cond) {
    switch (cond) {
        case A64_EQ: return "eq";
        case A64_NE: return "ne";
        case A64_CS: return "cs";
        case A64_CC: return "cc";
        case A64_MI: return "mi";
        case A64_PL: return "pl";
        case A64_VS: return "vs";
        case A64_VC: return "vc";
        case A64_HI: return "hi";
        case A64_LS: return "ls";
        case A64_GE: return "ge";
        case A64_LT: return "lt";
        case A64_GT: return "gt";
        case A64_LE: return "le";
        case A64_AL: return "al";
        case A64_NV: return "nv";
        default: return "?";
    }
}
