#include "asbestos/aarch64/gadgets.h"
#include "emu/aarch64/decode.h"

// Helper to set NZCV flags based on result
static inline void set_flags_add(struct cpu_state *cpu, uint64_t result,
                                  uint64_t op1, uint64_t op2, int is_64bit) {
    cpu->z = (result == 0);
    cpu->n = is_64bit ? ((result >> 63) & 1) : ((result >> 31) & 1);

    if (is_64bit) {
        // Unsigned overflow (carry): result < op1
        cpu->c = (result < op1);
        // Signed overflow: operands same sign, result different
        cpu->v = ((~(op1 ^ op2) & (op1 ^ result)) >> 63) & 1;
    } else {
        uint32_t r32 = (uint32_t)result;
        uint32_t a32 = (uint32_t)op1;
        cpu->c = (r32 < a32);
        cpu->v = ((~(a32 ^ (uint32_t)op2) & (a32 ^ r32)) >> 31) & 1;
    }
}

static inline void set_flags_sub(struct cpu_state *cpu, uint64_t result,
                                  uint64_t op1, uint64_t op2, int is_64bit) {
    cpu->z = (result == 0);
    cpu->n = is_64bit ? ((result >> 63) & 1) : ((result >> 31) & 1);

    if (is_64bit) {
        // Unsigned underflow (borrow): op1 >= op2 means no borrow
        cpu->c = (op1 >= op2);
        // Signed underflow
        cpu->v = (((op1 ^ op2) & (op1 ^ result)) >> 63) & 1;
    } else {
        uint32_t r32 = (uint32_t)result;
        uint32_t a32 = (uint32_t)op1;
        uint32_t b32 = (uint32_t)op2;
        cpu->c = (a32 >= b32);
        cpu->v = (((a32 ^ b32) & (a32 ^ r32)) >> 31) & 1;
    }
}

// ADD immediate: Rd = Rn + imm
void gadget_add_imm_impl(struct cpu_state *cpu, int Rd, int Rn,
                           int64_t imm, int set_flags, int is_64bit) {
    uint64_t operand = (Rn == 31) ? cpu->sp : cpu->x[Rn];
    uint64_t result = operand + imm;

    if (Rd == 31) {
        if (Rn == 31) {
            // ADD SP, SP, #imm
            cpu->sp = result;
        }
        // else: writes to XZR are discarded
    } else {
        cpu->x[Rd] = is_64bit ? result : (uint32_t)result;
    }

    if (set_flags) {
        set_flags_add(cpu, result, operand, imm, is_64bit);
    }
}

// ADD register: Rd = Rn + Rm
void gadget_add_reg_impl(struct cpu_state *cpu, int Rd, int Rn, int Rm,
                          int shift, int shift_amt, int set_flags, int is_64bit) {
    uint64_t op1 = (Rn == 31) ? cpu->sp : cpu->x[Rn];
    uint64_t op2 = (Rm == 31) ? 0 : cpu->x[Rm];

    // Apply shift
    if (shift == 0) { // LSL
        op2 <<= shift_amt;
    } else if (shift == 1) { // LSR
        op2 = is_64bit ? (op2 >> shift_amt) : ((uint32_t)op2 >> shift_amt);
    } else if (shift == 2) { // ASR
        op2 = is_64bit ? ((int64_t)op2 >> shift_amt) : ((int32_t)op2 >> shift_amt);
    }

    uint64_t result = op1 + op2;

    if (Rd == 31) {
        // Writes to XZR discarded, but if Rn was SP, update SP
        if (Rn == 31) {
            cpu->sp = result;
        }
    } else {
        cpu->x[Rd] = is_64bit ? result : (uint32_t)result;
    }

    if (set_flags) {
        set_flags_add(cpu, result, op1, op2, is_64bit);
    }
}

// SUB immediate: Rd = Rn - imm
void gadget_sub_imm_impl(struct cpu_state *cpu, int Rd, int Rn,
                           int64_t imm, int set_flags, int is_64bit) {
    uint64_t operand = (Rn == 31) ? cpu->sp : cpu->x[Rn];
    uint64_t result = operand - imm;

    if (Rd == 31) {
        if (Rn == 31) {
            cpu->sp = result;
        }
    } else {
        cpu->x[Rd] = is_64bit ? result : (uint32_t)result;
    }

    if (set_flags) {
        set_flags_sub(cpu, result, operand, imm, is_64bit);
    }
}

// SUB register: Rd = Rn - Rm
void gadget_sub_reg_impl(struct cpu_state *cpu, int Rd, int Rn, int Rm,
                          int shift, int shift_amt, int set_flags, int is_64bit) {
    uint64_t op1 = (Rn == 31) ? cpu->sp : cpu->x[Rn];
    uint64_t op2 = (Rm == 31) ? 0 : cpu->x[Rm];

    // Apply shift
    if (shift == 0) { // LSL
        op2 <<= shift_amt;
    } else if (shift == 1) { // LSR
        op2 = is_64bit ? (op2 >> shift_amt) : ((uint32_t)op2 >> shift_amt);
    } else if (shift == 2) { // ASR
        op2 = is_64bit ? ((int64_t)op2 >> shift_amt) : ((int32_t)op2 >> shift_amt);
    }

    uint64_t result = op1 - op2;

    if (Rd == 31) {
        if (Rn == 31) {
            cpu->sp = result;
        }
    } else {
        cpu->x[Rd] = is_64bit ? result : (uint32_t)result;
    }

    if (set_flags) {
        set_flags_sub(cpu, result, op1, op2, is_64bit);
    }
}

// ADC: Rd = Rn + Rm + C
void gadget_adc_impl(struct cpu_state *cpu, int Rd, int Rn, int Rm,
                      int set_flags, int is_64bit) {
    uint64_t op1 = cpu->x[Rn];
    uint64_t op2 = cpu->x[Rm];
    uint64_t carry = cpu->c ? 1 : 0;
    uint64_t result = op1 + op2 + carry;

    cpu->x[Rd] = is_64bit ? result : (uint32_t)result;

    if (set_flags) {
        set_flags_add(cpu, result, op1, op2 + carry, is_64bit);
    }
}

// SBC: Rd = Rn - Rm - ~C
void gadget_sbc_impl(struct cpu_state *cpu, int Rd, int Rn, int Rm,
                      int set_flags, int is_64bit) {
    uint64_t op1 = cpu->x[Rn];
    uint64_t op2 = cpu->x[Rm];
    uint64_t carry = cpu->c ? 0 : 1;  // Inverted carry for subtraction
    uint64_t result = op1 - op2 - carry;

    cpu->x[Rd] = is_64bit ? result : (uint32_t)result;

    if (set_flags) {
        set_flags_sub(cpu, result, op1, op2 + carry, is_64bit);
    }
}

// Generic ADD immediate gadget (decodes instruction from context)
gadget_fn_t gadget_add_imm(struct cpu_state *cpu) {
    // The decoder would have set up the instruction details
    // For now, this is a placeholder
    cpu->pc += 4;
    return NULL;
}

// CMP (compare) - SUBS with Rd=31
gadget_fn_t gadget_cmp_imm(struct cpu_state *cpu) {
    // Implementation
    cpu->pc += 4;
    return NULL;
}

gadget_fn_t gadget_cmp_reg(struct cpu_state *cpu) {
    // Implementation
    cpu->pc += 4;
    return NULL;
}
