/*
 * Math and Logic Gadgets for TCTI - Part 2
 *
 * Additional arithmetic and logical operations:
 * - Compare operations (CMP, CMN, TST)
 * - Multiplication (MUL, MNEG)
 * - Division (UDIV, SDIV) - software implementation
 * - Bitwise operations (LSL, LSR, ASR, ROR)
 * - Bit manipulation (CLZ, RBIT, REV)
 */

#include "asbestos/aarch64/gadgets_tcti.h"
#include "emu/aarch64/cpu.h"

// ============================================================================
// Compare Operations (set flags, discard result)
// ============================================================================

// CMP Rn, Rm - Compare (subtract and set flags)
__attribute__((naked)) void gadget_cmp_0_1(void) {
    // Compare x0 (host x1) with x1 (host x2)
    asm volatile(
        "subs xzr, x1, x2\n\t"      // Subtract to zero register, set flags
        GADGET_EPILOGUE
    );
}

// CMN Rn, Rm - Compare negative (add and set flags)
__attribute__((naked)) void gadget_cmn_0_1(void) {
    asm volatile(
        "adds xzr, x1, x2\n\t"      // Add to zero register, set flags
        GADGET_EPILOGUE
    );
}

// TST Rn, Rm - Test (AND and set flags)
__attribute__((naked)) void gadget_tst_0_1(void) {
    asm volatile(
        "ands xzr, x1, x2\n\t"      // AND to zero register, set flags
        GADGET_EPILOGUE
    );
}

// ============================================================================
// Multiplication
// ============================================================================

// MUL Rd, Rn, Rm - Multiply (low 64 bits)
__attribute__((naked)) void gadget_mul_0_1_2(void) {
    // x0 = x1 * x2
    asm volatile(
        "mul x1, x2, x3\n\t"
        GADGET_EPILOGUE
    );
}

// MNEG Rd, Rn, Rm - Multiply and negate
__attribute__((naked)) void gadget_mneg_0_1_2(void) {
    asm volatile(
        "mneg x1, x2, x3\n\t"
        GADGET_EPILOGUE
    );
}

// SMULH Rd, Rn, Rm - Signed multiply high
__attribute__((naked)) void gadget_smulh_0_1_2(void) {
    asm volatile(
        "smulh x1, x2, x3\n\t"
        GADGET_EPILOGUE
    );
}

// UMULH Rd, Rn, Rm - Unsigned multiply high
__attribute__((naked)) void gadget_umulh_0_1_2(void) {
    asm volatile(
        "umulh x1, x2, x3\n\t"
        GADGET_EPILOGUE
    );
}

// ============================================================================
// Division (synthesized - aarch64 doesn't have hardware divide in all cores)
// ============================================================================

// External C helper for division
extern uint64_t a64_udiv_helper(uint64_t dividend, uint64_t divisor);
extern int64_t a64_sdiv_helper(int64_t dividend, int64_t divisor);

// UDIV Rd, Rn, Rm - Unsigned divide
__attribute__((naked)) void gadget_udiv_0_1_2(void) {
    // x0 = x1 / x2 (unsigned)
    asm volatile(
        "stp x19, x20, [sp, #-16]!\n\t"
        "mov x0, x2\n\t"           // Dividend
        "mov x1, x3\n\t"           // Divisor
        "bl a64_udiv_helper\n\t"   // Call C helper
        "mov x1, x0\n\t"           // Result to x1 (guest x0)
        "ldp x19, x20, [sp], #16\n\t"
        GADGET_EPILOGUE
    );
}

// SDIV Rd, Rn, Rm - Signed divide
__attribute__((naked)) void gadget_sdiv_0_1_2(void) {
    asm volatile(
        "stp x19, x20, [sp, #-16]!\n\t"
        "mov x0, x2\n\t"
        "mov x1, x3\n\t"
        "bl a64_sdiv_helper\n\t"
        "mov x1, x0\n\t"
        "ldp x19, x20, [sp], #16\n\t"
        GADGET_EPILOGUE
    );
}

// ============================================================================
// Bit Shifts
// ============================================================================

// LSL Rd, Rn, Rm - Logical shift left by register
__attribute__((naked)) void gadget_lsl_0_1_2(void) {
    // x0 = x1 << (x2 & 63)
    asm volatile(
        "lsl x1, x2, x3\n\t"
        GADGET_EPILOGUE
    );
}

// LSR Rd, Rn, Rm - Logical shift right
__attribute__((naked)) void gadget_lsr_0_1_2(void) {
    asm volatile(
        "lsr x1, x2, x3\n\t"
        GADGET_EPILOGUE
    );
}

// ASR Rd, Rn, Rm - Arithmetic shift right
__attribute__((naked)) void gadget_asr_0_1_2(void) {
    asm volatile(
        "asr x1, x2, x3\n\t"
        GADGET_EPILOGUE
    );
}

// ROR Rd, Rn, Rm - Rotate right
__attribute__((naked)) void gadget_ror_0_1_2(void) {
    asm volatile(
        "ror x1, x2, x3\n\t"
        GADGET_EPILOGUE
    );
}

// Shift by immediate (common cases)
__attribute__((naked)) void gadget_lsl_imm_0_1_16(void) {
    // x0 = x1 << 16
    asm volatile(
        "lsl x1, x2, #16\n\t"
        GADGET_EPILOGUE
    );
}

__attribute__((naked)) void gadget_lsr_imm_0_1_16(void) {
    asm volatile(
        "lsr x1, x2, #16\n\t"
        GADGET_EPILOGUE
    );
}

// ============================================================================
// Bit Manipulation
// ============================================================================

// CLZ Rd, Rn - Count leading zeros
__attribute__((naked)) void gadget_clz_0_1(void) {
    asm volatile(
        "clz x1, x2\n\t"
        GADGET_EPILOGUE
    );
}

// RBIT Rd, Rn - Reverse bits
__attribute__((naked)) void gadget_rbit_0_1(void) {
    asm volatile(
        "rbit x1, x2\n\t"
        GADGET_EPILOGUE
    );
}

// REV Rd, Rn - Reverse bytes (64-bit)
__attribute__((naked)) void gadget_rev_0_1(void) {
    asm volatile(
        "rev x1, x2\n\t"
        GADGET_EPILOGUE
    );
}

// REV16 Rd, Rn - Reverse bytes in each 16-bit halfword
__attribute__((naked)) void gadget_rev16_0_1(void) {
    asm volatile(
        "rev16 x1, x2\n\t"
        GADGET_EPILOGUE
    );
}

// REV32 Rd, Rn - Reverse bytes in each 32-bit word
__attribute__((naked)) void gadget_rev32_0_1(void) {
    asm volatile(
        "rev32 x1, x2\n\t"
        GADGET_EPILOGUE
    );
}

// ============================================================================
// Conditional Select
// ============================================================================

// CSEL Rd, Rn, Rm, cond - Conditional select
__attribute__((naked)) void gadget_csel_eq_0_1_2(void) {
    // x0 = (flags.EQ) ? x1 : x2
    // Flags are in PSTATE which we'd need to track
    // For now, simplified version
    asm volatile(
        "csel x1, x2, x3, eq\n\t"
        GADGET_EPILOGUE
    );
}

__attribute__((naked)) void gadget_csel_ne_0_1_2(void) {
    asm volatile(
        "csel x1, x2, x3, ne\n\t"
        GADGET_EPILOGUE
    );
}

__attribute__((naked)) void gadget_csel_cs_0_1_2(void) {
    asm volatile(
        "csel x1, x2, x3, cs\n\t"
        GADGET_EPILOGUE
    );
}

__attribute__((naked)) void gadget_csel_cc_0_1_2(void) {
    asm volatile(
        "csel x1, x2, x3, cc\n\t"
        GADGET_EPILOGUE
    );
}

// CSINC - Conditional select and increment
__attribute__((naked)) void gadget_csinc_eq_0_1_2(void) {
    asm volatile(
        "csinc x1, x2, x3, eq\n\t"
        GADGET_EPILOGUE
    );
}

// CSNEG - Conditional select and negate
__attribute__((naked)) void gadget_csneg_eq_0_1_2(void) {
    asm volatile(
        "csneg x1, x2, x3, eq\n\t"
        GADGET_EPILOGUE
    );
}

// CSINV - Conditional select and invert
__attribute__((naked)) void gadget_csinv_eq_0_1_2(void) {
    asm volatile(
        "csinv x1, x2, x3, eq\n\t"
        GADGET_EPILOGUE
    );
}

// ============================================================================
// C Implementation of division helpers
// ============================================================================

uint64_t a64_udiv_helper(uint64_t dividend, uint64_t divisor) {
    if (divisor == 0) {
        // Division by zero - would trap in real hardware
        // Return 0 or handle as needed
        return 0;
    }
    return dividend / divisor;
}

int64_t a64_sdiv_helper(int64_t dividend, int64_t divisor) {
    if (divisor == 0) {
        return 0;
    }
    // Handle special case: LLONG_MIN / -1 overflows
    if (dividend == (1LL << 63) && divisor == -1) {
        return dividend; // Keep as-is per ARM spec
    }
    return dividend / divisor;
}

// ============================================================================
// Lookup Tables for Math Gadgets
// ============================================================================

// Note: Full tables would be generated by tcti-gadget-gen.py
// These are the key entries for common operations

// Multiplication table (simplified - just showing structure)
const tcti_gadget_t gadget_mul_reg[16][16][16] = {
    // Populated by gadget_add_mul_reg_0_0_0, etc.
    [0] = { [1] = { [2] = gadget_mul_0_1_2 } }
};

// Division table
const tcti_gadget_t gadget_udiv_reg[16][16][16] = {
    [0] = { [1] = { [2] = gadget_udiv_0_1_2 } }
};

const tcti_gadget_t gadget_sdiv_reg[16][16][16] = {
    [0] = { [1] = { [2] = gadget_sdiv_0_1_2 } }
};

// Shift table
const tcti_gadget_t gadget_lsl_reg[16][16][16] = {
    [0] = { [1] = { [2] = gadget_lsl_0_1_2 } }
};

const tcti_gadget_t gadget_lsr_reg[16][16][16] = {
    [0] = { [1] = { [2] = gadget_lsr_0_1_2 } }
};

const tcti_gadget_t gadget_asr_reg[16][16][16] = {
    [0] = { [1] = { [2] = gadget_asr_0_1_2 } }
};

const tcti_gadget_t gadget_ror_reg[16][16][16] = {
    [0] = { [1] = { [2] = gadget_ror_0_1_2 } }
};

// Bit manipulation
const tcti_gadget_t gadget_clz_reg[16] = {
    [0] = gadget_clz_0_1
};

const tcti_gadget_t gadget_rbit_reg[16] = {
    [0] = gadget_rbit_0_1
};

const tcti_gadget_t gadget_rev_reg[16] = {
    [0] = gadget_rev_0_1
};
