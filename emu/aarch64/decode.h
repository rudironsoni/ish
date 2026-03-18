#ifndef AARCH64_DECODE_H
#define AARCH64_DECODE_H

#include "misc.h"
#include "emu/aarch64/cpu.h"

// aarch64 instruction size is always 4 bytes
#define A64_INSTR_SIZE 4

// Instruction categories based on op0 = bits 28:25
// Based on ARMv8-A Reference Manual
// Note: These values are the ACTUAL op0 encodings, not sequential
typedef enum {
    // op0 = 0x0, 0x1, 0x2, 0x3: Reserved/Unallocated in standard encoding
    A64_RESERVED = 0x0,
    A64_RESERVED1 = 0x1,
    A64_RESERVED2 = 0x2,
    A64_RESERVED3 = 0x3,

    // op0 = 0x4, 0x5, 0x6, 0x7: Data Processing - Register
    A64_DP_REG = 0x4,        // 0100
    A64_DP_REG2 = 0x5,        // 0101 (ADD, SUB, AND, ORR, etc.)
    A64_DP_REG3 = 0x6,        // 0110
    A64_DP_REG4 = 0x7,        // 0111

    // op0 = 0x8: Reserved/SIMD
    A64_SIMD0 = 0x8,

    // op0 = 0x9: Data Processing - Immediate
    A64_DP_IMM = 0x9,         // 1001 (MOVZ, ADDI, etc.)

    // op0 = 0xA: Branches, Exception Generating, System
    A64_BRANCH = 0xA,         // 1010 (B, B.cond, SVC, HVC, SMC, etc.)

    // op0 = 0xB: Branches/Exception (unconditional branch reg)
    A64_BRANCH2 = 0xB,        // 1011 (BR, BLR, RET)

    // op0 = 0xC: Loads and Stores
    A64_LD_ST = 0xC,          // 1100 (LDR, STR, LDP, STP, etc.)

    // op0 = 0xD: Data Processing - Immediate (continued)
    A64_DP_IMM2 = 0xD,        // 1101

    // op0 = 0xE: SIMD/FP
    A64_SIMD = 0xE,           // 1110

    // op0 = 0xF: SIMD/FP
    A64_SIMD2 = 0xF,          // 1111
} a64_category_t;

// Subcategories for Data Processing - Immediate
typedef enum {
    A64_DP_IMM_UNALLOC = 0,  // 00 - Unallocated
    A64_DP_IMM_PC_REL = 1,   // 01 - PC-rel addressing (ADR, ADRP)
    A64_DP_IMM_ADD_SUB = 2,  // 10 - Add/Subtract immediate
    A64_DP_IMM_BITFIELD = 3, // 11 - Bitfield move
    A64_DP_IMM_EXTRACT = 4,  // 100 - Extract
    A64_DP_IMM_LOGIC = 5,    // 101 - Logical immediate (moved from here in v8)
    A64_DP_IMM_MOVEW = 6,    // 110 - Move wide immediate
} a64_dp_imm_subtype_t;

// Subcategories for Branches
typedef enum {
    A64_BRANCH_COND = 0,     // Conditional branch
    A64_BRANCH_UNCOND = 1,   // Unconditional branch (immediate)
    A64_BRANCH_CMP = 2,      // Compare and branch (immediate)
    A64_BRANCH_TEST = 3,     // Test and branch
    A64_BRANCH_REG = 5,      // Unconditional branch (register)
    A64_EXCEPTION = 7,       // Exception generation
} a64_branch_subtype_t;

// Subcategories for Loads and Stores
typedef enum {
    A64_LDST_SINGLE = 4,     // Load/store single (unscaled, immediate, etc.)
    A64_LDST_PAIR = 5,       // Load/store register pair
    A64_LDST_LITERAL = 3,    // Load register (literal)
    A64_LDST_ATOMIC = 0,     // Atomic operations
} a64_ldst_subtype_t;

// Data processing register subcategories
typedef enum {
    A64_DP_REG_LOGICAL = 0,     // Logical (shifted register)
    A64_DP_REG_ADD_SUB = 1,     // Add/Subtract (shifted register)
    A64_DP_REG_ADD_SUB_C = 2,   // Add/Subtract (with carry)
    A64_DP_REG_COND = 3,        // Conditional compare
    A64_DP_REG_COND2 = 4,       // Conditional select
    A64_DP_REG_SHIFT = 5,       // Data processing (1 source/2 source)
    A64_DP_REG_3SRC = 6,        // Data processing (3 source)
} a64_dp_reg_subtype_t;

// Condition codes for conditional instructions
typedef enum {
    A64_EQ = 0x0,  // Equal
    A64_NE = 0x1,  // Not equal
    A64_CS = 0x2,  // Carry set (HS)
    A64_CC = 0x3,  // Carry clear (LO)
    A64_MI = 0x4,  // Minus/negative
    A64_PL = 0x5,  // Plus/positive
    A64_VS = 0x6,  // Overflow set
    A64_VC = 0x7,  // Overflow clear
    A64_HI = 0x8,  // Unsigned higher
    A64_LS = 0x9,  // Unsigned lower or same
    A64_GE = 0xA,  // Signed greater or equal
    A64_LT = 0xB,  // Signed less than
    A64_GT = 0xC,  // Signed greater than
    A64_LE = 0xD,  // Signed less or equal
    A64_AL = 0xE,  // Always
    A64_NV = 0xF,  // Always (contradicts condition)
} a64_cond_t;

// Extended register types for load/store
typedef enum {
    A64_EXT_UXTW = 0,   // Unsigned extend word
    A64_EXT_UXTX = 1,   // Unsigned extend doubleword
    A64_EXT_SXTW = 2,   // Signed extend word
    A64_EXT_SXTX = 3,   // Signed extend doubleword
    A64_EXT_LSL = 4,    // Logical shift left (no extend)
} a64_extend_t;

// Shift types
typedef enum {
    A64_SHIFT_LSL = 0,
    A64_SHIFT_LSR = 1,
    A64_SHIFT_ASR = 2,
    A64_SHIFT_ROR = 3,
} a64_shift_t;

// Size field for loads/stores
typedef enum {
    A64_SIZE_B = 0,   // Byte
    A64_SIZE_H = 1,   // Halfword
    A64_SIZE_W = 2,   // Word (32-bit)
    A64_SIZE_X = 3,   // Doubleword (64-bit)
} a64_size_t;

// Decoded instruction structure
typedef struct {
    uint32_t raw;           // Raw instruction
    a64_category_t cat;     // Main category
    int subtype;            // Sub-category

    // Common fields
    int Rd;                 // Destination register (0-30, 31=SP/XZR)
    int Rn;                 // First source register
    int Rm;                 // Second source register
    int Ra;                 // Third source register (multiply-add)

    // Immediate values
    int64_t imm;            // Immediate (signed, up to 64-bit)
    int imm_shift;          // Immediate shift amount
    int shift_type;         // LSL, LSR, ASR, ROR
    int extend_type;        // Extension type for indexed addressing

    // Condition codes
    a64_cond_t cond;        // Condition for branches

    // Memory access
    int size;               // Load/store size (0=B, 1=H, 2=W, 3=X)
    bool is_signed;         // Signed extend on load
    bool is_vector;         // Vector/SIMD load/store
    bool is_pair;           // Load/store pair
    int pair_offset;        // Offset for pair

    // Misc
    bool is_64bit;          // 64-bit vs 32-bit operation (sf bit)
    bool set_flags;         // Update condition flags (S bit)
    bool is_prefetch;       // Prefetch instruction

    // Target for branches
    uint64_t target;

    // System instructions
    uint16_t sysreg;        // System register number
    uint8_t op;             // Opcode for system instructions
} a64_instr_t;

// Bit manipulation helpers
static inline uint32_t bits(uint32_t val, int hi, int lo) {
    return (val >> lo) & ((1U << (hi - lo + 1)) - 1);
}

static inline uint32_t bit(uint32_t val, int n) {
    return (val >> n) & 1;
}

static inline int64_t sign_extend(uint64_t val, int bits) {
    int64_t sign_bit = 1LL << (bits - 1);
    return (val ^ sign_bit) - sign_bit;
}

// Category detection based on ARMv8-A architecture
// op0 = bits 28:25
static inline a64_category_t a64_get_category(uint32_t insn) {
    return (a64_category_t)((insn >> 25) & 0xF);
}

// Main decode function
// Returns 0 on success, -1 on undefined instruction
int a64_decode(uint32_t insn, a64_instr_t *out);

// Decode helpers for specific categories
int a64_decode_dp_imm(uint32_t insn, a64_instr_t *out);
int a64_decode_dp_reg(uint32_t insn, a64_instr_t *out);
int a64_decode_branch(uint32_t insn, a64_instr_t *out);
int a64_decode_ldst(uint32_t insn, a64_instr_t *out);
int a64_decode_simd_fp(uint32_t insn, a64_instr_t *out);
int a64_decode_system(uint32_t insn, a64_instr_t *out);

// Instruction printing for debugging
const char *a64_category_name(a64_category_t cat);
const char *a64_cond_name(a64_cond_t cond);
void a64_print_instr(const a64_instr_t *instr, char *buf, size_t size);

// Common register names
static inline const char *a64_reg_name(int reg, int is_64bit) {
    if (reg == 31)
        return is_64bit ? "sp" : "wsp";
    static const char *names64[] = {
        "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7",
        "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15",
        "x16", "x17", "x18", "x19", "x20", "x21", "x22", "x23",
        "x24", "x25", "x26", "x27", "x28", "x29", "x30"
    };
    static const char *names32[] = {
        "w0", "w1", "w2", "w3", "w4", "w5", "w6", "w7",
        "w8", "w9", "w10", "w11", "w12", "w13", "w14", "w15",
        "w16", "w17", "w18", "w19", "w20", "w21", "w22", "w23",
        "w24", "w25", "w26", "w27", "w28", "w29", "w30"
    };
    if (reg >= 0 && reg < 31)
        return is_64bit ? names64[reg] : names32[reg];
    return "?";
}

// Vector register names
static inline const char *a64_vec_reg_name(int reg, int is_128bit) {
    static const char *names128[] = {
        "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7",
        "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15",
        "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23",
        "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31"
    };
    static const char *names64[] = {
        "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7",
        "d8", "d9", "d10", "d11", "d12", "d13", "d14", "d15",
        "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23",
        "d24", "d25", "d26", "d27", "d28", "d29", "d30", "d31"
    };
    if (reg >= 0 && reg < 32)
        return is_128bit ? names128[reg] : names64[reg];
    return "?";
}

#endif
