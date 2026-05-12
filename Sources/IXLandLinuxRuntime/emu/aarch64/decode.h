/*
 * AArch64 Instruction Decoder Header
 * Compatible with original iSH decode.h structure
 */

#ifndef EMU_AARCH64_DECODE_H
#define EMU_AARCH64_DECODE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Instruction categories based on op0 = bits 28:25 */
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

/* Subcategories for Data Processing - Immediate */
typedef enum {
    A64_DP_IMM_UNALLOC = 0,  // 00 - Unallocated
    A64_DP_IMM_PC_REL = 1,   // 01 - PC-rel addressing (ADR, ADRP)
    A64_DP_IMM_ADD_SUB = 2,  // 10 - Add/Subtract immediate
    A64_DP_IMM_BITFIELD = 3, // 11 - Bitfield move
    A64_DP_IMM_LOGIC = 5,    // 101 - Logical immediate (moved from here in v8)
    A64_DP_IMM_MOVEW = 6,    // 110 - Move wide immediate
    // Decoder subtypes are also generator dispatch keys. Keep EXTR out of the
    // shared ADR/MOVN bucket so full AArch64 extract encodings stay distinct.
    A64_DP_IMM_EXTRACT = 14, // Extract
} a64_dp_imm_subtype_t;

/* Subcategories for Branches */
typedef enum {
    A64_BRANCH_COND = 0,     // Conditional branch
    A64_BRANCH_UNCOND = 1,   // Unconditional branch (immediate)
    A64_BRANCH_CMP = 2,      // Compare and branch (immediate)
    A64_BRANCH_TEST = 3,     // Test and branch
    A64_BRANCH_REG = 5,      // Unconditional branch (register)
    A64_EXCEPTION = 7,       // Exception generation
} a64_branch_subtype_t;

typedef enum {
    A64_SYSTEM_MRS = 2,
    A64_SYSTEM_MSR_IMM = 3,
    A64_SYSTEM_MSR_REG = 4,
    A64_SYSTEM_BARRIER = 5,
    A64_SYSTEM_HINT = 6,
    A64_SYSTEM_DC_ZVA = 8,
    A64_SYSTEM_CLREX = 9,
} a64_system_subtype_t;

/* Subcategories for Loads and Stores */
typedef enum {
    A64_LDST_SINGLE = 4,     // Load/store single (unscaled, immediate, etc.)
    A64_LDST_PAIR = 5,       // Load/store register pair
    A64_LDST_LITERAL = 3,    // Load register (literal)
    A64_LDST_ATOMIC = 0,     // Atomic operations
} a64_ldst_subtype_t;

/* Subcategories for SIMD/FP instructions */
typedef enum {
    A64_SIMD_DUP_GPR = 1,        // DUP vector element from general register
    A64_SIMD_MOV_GPR_FROM_VEC = 2, // UMOV/MOV general register from vector element
    A64_SIMD_MOVI_IMM = 3,       // MOVI/MVNI vector modified immediate
    A64_SIMD_FMOV_GPR = 4,       // FMOV scalar FP <-> general register
    A64_SIMD_SCALAR_FADD = 5,    // FADD scalar FP
    A64_SIMD_EXT = 6,            // EXT vector extract
    A64_SIMD_CNT = 7,            // CNT population count per byte
    A64_SIMD_INS_GPR = 8,        // INS vector element from general register
    A64_SIMD_TBL = 9,            // TBL table lookup (one-register form)
    A64_SIMD_TBX = 10,           // TBX table lookup with destination preserve
    A64_SIMD_XTN = 11,           // XTN narrow vector elements into low half
    A64_SIMD_ZIP1 = 12,          // ZIP1 interleave low halves of two vectors
    A64_SIMD_TRN1 = 13,          // TRN1 transpose even lanes from two vectors
    A64_SIMD_UZP1 = 14,          // UZP1 pack even lanes from two vectors
    A64_SIMD_BIC = 15,           // BIC bit clear vector logical op
    A64_SIMD_AND = 16,           // AND bitwise vector logical op
    A64_SIMD_ORR = 17,           // ORR bitwise vector logical op
    A64_SIMD_EOR = 18,           // EOR bitwise vector logical op
    A64_SIMD_ADD = 19,           // ADD per-lane vector arithmetic op
    A64_SIMD_SUB = 20,           // SUB per-lane vector arithmetic op
    A64_SIMD_MUL = 21,           // MUL per-lane vector arithmetic op
    A64_SIMD_ORN = 22,           // ORN bitwise OR with inverted vector mask
    A64_SIMD_BSL = 23,           // BSL bitwise select using destination mask
    A64_SIMD_BIT = 24,           // BIT bitwise insert where mask bits are set
    A64_SIMD_BIF = 25,           // BIF bitwise insert where mask bits are clear
    A64_SIMD_CMEQ = 26,          // CMEQ per-lane equality mask
    A64_SIMD_CMGT = 27,          // CMGT signed per-lane greater-than mask
    A64_SIMD_CMGE = 28,          // CMGE signed per-lane greater-or-equal mask
    A64_SIMD_CMHI = 29,          // CMHI unsigned per-lane greater-than mask
    A64_SIMD_CMHS = 30,          // CMHS unsigned per-lane greater-or-equal mask
    A64_SIMD_CMLE = 31,          // CMLE signed per-lane less-or-equal-zero mask
    A64_SIMD_CMLT = 32,          // CMLT signed per-lane less-than-zero mask
    A64_SIMD_CMTST = 33,         // CMTST nonzero bit-intersection mask
    A64_SIMD_MLA = 34,           // MLA multiply-accumulate into destination
    A64_SIMD_MLS = 35,           // MLS multiply-subtract from destination
    A64_SIMD_XTN2 = 36,          // XTN2 narrow vector elements into upper half
    A64_SIMD_ZIP2 = 37,          // ZIP2 interleave high halves of two vectors
    A64_SIMD_TRN2 = 38,          // TRN2 transpose odd lanes from two vectors
    A64_SIMD_UZP2 = 39,          // UZP2 pack odd lanes from two vectors
    A64_SIMD_SCALAR_FP_GENERIC = 40, // Scalar FP helper family beyond FMOV/FADD
} a64_simd_subtype_t;

/* Indexing modes for load/store */
typedef enum {
    A64_INDEX_OFFSET = 0,    // Offset addressing (base + offset, no writeback)
    A64_POST_INDEX = 1,      // Post-indexed (load/store, then base += offset)
    A64_PRE_INDEX = 2,       // Pre-indexed (base += offset, then load/store)
} a64_index_mode_t;

/* Data processing register subcategories */
typedef enum {
    A64_DP_REG_LOGICAL = 0,     // Logical (shifted register)
    A64_DP_REG_ADD_SUB = 1,     // Add/Subtract (shifted register)
    A64_DP_REG_ADD_SUB_C = 2,   // Add/Subtract (with carry)
    A64_DP_REG_COND = 3,        // Conditional compare
    A64_DP_REG_COND2 = 4,       // Conditional select
    A64_DP_REG_SHIFT = 5,       // Data processing (1 source/2 source)
    A64_DP_REG_3SRC = 6,        // Data processing (3 source)
    A64_DP_REG_MADD = 32,       // MADD
    A64_DP_REG_MSUB = 33,       // MSUB
    A64_DP_REG_SMADDL = 34,     // SMADDL / SMULL alias when Ra == XZR
    A64_DP_REG_SMSUBL = 35,     // SMSUBL / SMNEGL alias when Ra == XZR
    A64_DP_REG_UMADDL = 36,     // UMADDL / UMULL alias when Ra == XZR
    A64_DP_REG_UMSUBL = 37,     // UMSUBL / UMNEGL alias when Ra == XZR
    A64_DP_REG_CCMN = 38,       // Conditional compare negative (register)
    A64_DP_REG_CCMP = 39,       // Conditional compare (register)
    A64_DP_REG_CCMN_IMM = 40,   // Conditional compare negative (immediate)
    A64_DP_REG_CCMP_IMM = 41,   // Conditional compare (immediate)
    A64_DP_REG_UDIV = 42,       // Unsigned divide
    A64_DP_REG_SDIV = 43,       // Signed divide
    A64_DP_REG_RBIT = 44,       // Reverse bits
    A64_DP_REG_CLZ = 45,        // Count leading zeros
    A64_DP_REG_UMULH = 46,      // Unsigned multiply high
    A64_DP_REG_SMULH = 47,      // Signed multiply high
    A64_DP_REG_REV = 48,        // Reverse bytes
    A64_DP_REG_REV16 = 49,      // Reverse bytes in 16-bit halfwords
    A64_DP_REG_REV32 = 50,      // Reverse bytes in 32-bit words
} a64_dp_reg_subtype_t;

/* Condition codes for conditional instructions */
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

/* Extended register types for load/store */
typedef enum {
    A64_EXT_UXTW = 0,   // Unsigned extend word
    A64_EXT_UXTX = 1,   // Unsigned extend doubleword
    A64_EXT_SXTW = 2,   // Signed extend word
    A64_EXT_SXTX = 3,   // Signed extend doubleword
    A64_EXT_LSL = 4,    // Logical shift left (no extend)
    A64_EXT_UXTB = 5,   // Unsigned extend byte
    A64_EXT_UXTH = 6,   // Unsigned extend halfword
    A64_EXT_SXTB = 7,   // Signed extend byte
    A64_EXT_SXTH = 8,   // Signed extend halfword
} a64_extend_t;

/* Shift types */
typedef enum {
    A64_SHIFT_LSL = 0,
    A64_SHIFT_LSR = 1,
    A64_SHIFT_ASR = 2,
    A64_SHIFT_ROR = 3,
} a64_shift_t;

/* Size field for loads/stores */
typedef enum {
    A64_SIZE_B = 0,   // Byte
    A64_SIZE_H = 1,   // Halfword
    A64_SIZE_W = 2,   // Word (32-bit)
    A64_SIZE_X = 3,   // Doubleword (64-bit)
} a64_size_t;

/* Decoded instruction structure */
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
    int vec_bytes;          // Vector element/register memory width in bytes
    int vec_index;          // Vector lane index for copy instructions
    int pair_offset;        // Offset for pair
    a64_index_mode_t idx_mode;  // Indexing mode (offset, post, pre)

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

/* Bit manipulation helpers */
static inline uint32_t bits(uint32_t val, int hi, int lo) {
    return (val >> lo) & ((1U << (hi - lo + 1)) - 1);
}

static inline uint32_t bit(uint32_t val, int n) {
    return (val >> n) & 1;
}

static inline bool a64_ldst_raw_is_load(uint32_t insn) {
    uint32_t opc = bits(insn, 23, 22);
    return opc != 0;
}

static inline int64_t sign_extend(uint64_t val, int bits) {
    int64_t sign_bit = 1LL << (bits - 1);
    return (val ^ sign_bit) - sign_bit;
}

/* Category detection based on ARMv8-A architecture */
// op0 = bits 28:25 for Data Processing instructions
// For branches, the encoding is different (bits 31:26 determine type)
static inline a64_category_t a64_get_category(uint32_t insn) {
    uint32_t top = (insn >> 25) & 0xF;

    // Check branch encodings with the architectural masks used by the
    // branch decoder. Broad top-bit checks alias valid data-processing
    // instructions such as CMP/SUBS register.
    if ((insn & 0x7c000000) == 0x14000000) { // B/BL
        return A64_BRANCH;
    } else if ((insn & 0xff000000) == 0x54000000) { // B.cond / BC.cond
        return A64_BRANCH;
    } else if ((insn & 0x7e000000) == 0x34000000) { // CBZ/CBNZ
        return A64_BRANCH;
    } else if ((insn & 0x7e000000) == 0x36000000) { // TBZ/TBNZ
        return A64_BRANCH;
    }

    // Load/store pair: bits 29:25 = 10100 (0x14)
    // Distinguishes from logical register ops (AND/ORR/EOR) which have
    // bits 29:25 = 10101 (0x15) when sf=1.
    if ((((insn >> 25) & 0x1F) & 0x1D) == 0x14) {
        return A64_LD_ST;
    }

    if (((insn >> 25) & 0x1F) == 0x1C || ((insn >> 25) & 0x1F) == 0x1E) {
        return A64_LD_ST;
    }

    // Load/store exclusive and ordered atomic forms use top-level bits 28:25
    // that otherwise look like data-processing register. Classify them as
    // load/store before the generic category fallback.
    uint32_t atomic_class = (insn >> 24) & 0x3F;
    if (atomic_class == 0x08 || atomic_class == 0x38) {
        uint32_t op3 = (insn >> 12) & 0xF;
        if ((atomic_class == 0x08 && (op3 == 0x7 || op3 == 0xE || op3 == 0xF)) ||
            ((insn >> 21) & 1))
            return A64_LD_ST;
    }

    if ((insn & 0xbfe0fc00) == 0x0e000c00 || // DUP Vd.T, Rn
        (insn & 0xbfe0fc00) == 0x0e003c00) { // UMOV/MOV Rd, Vn.T[index]
        return A64_SIMD;
    }

    // Otherwise, use standard category from bits 28:25
    return (a64_category_t)top;
}

/* Main decode function */
// Returns 0 on success, -1 on undefined instruction
int a64_decode(uint32_t insn, a64_instr_t *out);

/* Decode helpers for specific categories */
int a64_decode_dp_imm(uint32_t insn, a64_instr_t *out);
int a64_decode_dp_reg(uint32_t insn, a64_instr_t *out);
int a64_decode_branch(uint32_t insn, a64_instr_t *out);
int a64_decode_ldst(uint32_t insn, a64_instr_t *out);
int a64_decode_simd_fp(uint32_t insn, a64_instr_t *out);
int a64_decode_system(uint32_t insn, a64_instr_t *out);

/* Instruction printing for debugging */
const char *a64_category_name(a64_category_t cat);
const char *a64_cond_name(a64_cond_t cond);
void a64_print_instr(const a64_instr_t *instr, char *buf, size_t size);

/* Common register names */
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

/* Vector register names */
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

#endif /* EMU_AARCH64_DECODE_H */
