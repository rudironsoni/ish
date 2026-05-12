#import <IXLandLinuxRuntime/emu/aarch64/decode.h>
#include <string.h>

/* Mark variables as intentionally unused (for future expansion) */
#define UNUSED(x) ((void)(x))

static int highest_set_bit(unsigned value)
{
    for (int bit_index = 6; bit_index >= 0; bit_index--) {
        if (value & (1u << bit_index))
            return bit_index;
    }
    return -1;
}

static uint64_t bitmask_ones(unsigned count)
{
    if (count >= 64)
        return UINT64_MAX;
    return count == 0 ? 0 : ((1ULL << count) - 1);
}

static uint64_t rotate_right_width(uint64_t value, unsigned rotation, unsigned width)
{
    uint64_t mask = bitmask_ones(width);
    rotation &= width - 1;
    value &= mask;
    if (rotation == 0)
        return value;
    return ((value >> rotation) | (value << (width - rotation))) & mask;
}

static bool decode_logical_bitmask(unsigned N, unsigned imms, unsigned immr, bool is_64bit,
                                   uint64_t *out_mask)
{
    if (!is_64bit && N != 0)
        return false;

    int len = highest_set_bit((N << 6) | (~imms & 0x3f));
    if (len < 1)
        return false;

    unsigned levels = (1u << len) - 1;
    unsigned S = imms & levels;
    unsigned R = immr & levels;
    if (S == levels)
        return false;

    unsigned element_width = 1u << len;
    unsigned data_width = is_64bit ? 64u : 32u;
    uint64_t element = rotate_right_width(bitmask_ones(S + 1), R, element_width);
    uint64_t mask = 0;
    for (unsigned bit_offset = 0; bit_offset < data_width; bit_offset += element_width)
        mask |= element << bit_offset;

    *out_mask = mask;
    return true;
}

static bool a64_is_advsimd_modified_immediate(uint32_t insn)
{
    return (insn & 0x9e000400) == 0x0e000400;
}

static bool a64_is_explicitly_unsupported_family_representative(uint32_t insn)
{
    switch (insn) {
    // Arm64e pointer authentication and authenticated control.
    case 0xdac10020: // pacia x0, x1
    case 0xdac11020: // autia x0, x1
    case 0xdac10462: // pacib x2, x3
    case 0xdac114e6: // autib x6, x7
    case 0xdac143e8: // xpaci x8
    case 0xdac147e9: // xpacd x9
    case 0xd71f094b: // braa x10, x11
    case 0xd71f0d8d: // brab x12, x13
    case 0xd73f09cf: // blraa x14, x15
    case 0xd73f0e11: // blrab x16, x17
    case 0xd65f0bff: // retaa
    case 0xd65f0fff: // retab
    case 0xd65f0bfe: // retaa sppc
    case 0xd65f0ffe: // retab sppc
    case 0xf8200420: // ldraa x0, [x1]
    case 0xf8a00462: // ldrab x2, [x3]
    case 0xd503245f: // bti c
        return true;

    // Crypto, dot-product, and related SIMD extensions.
    case 0x4e284820: // aese v0.16b, v1.16b
    case 0x4e285862: // aesd v2.16b, v3.16b
    case 0x4e2868a4: // aesmc v4.16b, v5.16b
    case 0x4e2878e6: // aesimc v6.16b, v7.16b
    case 0x0ee2e020: // pmull v0.1q, v1.1d, v2.1d
    case 0x2e229c20: // pmul v0.8h, v1.8b, v2.8b
    case 0x5e020020: // sha1c q0, s1, v2.4s
    case 0x5e022020: // sha1m q0, s1, v2.4s
    case 0x5e051083: // sha1p q3, s4, v5.4s
    case 0x5e0830e6: // sha1su0 v6.4s, v7.4s, v8.4s
    case 0x5e281949: // sha1su1 v9.4s, v10.4s
    case 0x5e024020: // sha256h q0, q1, v2.4s
    case 0x5e1051ee: // sha256h2 q14, q15, v16.4s
    case 0x5e282a51: // sha256su0 v17.4s, v18.4s
    case 0x5e156293: // sha256su1 v19.4s, v20.4s, v21.4s
    case 0xce62c020: // sm3partw1 v0.4s, v1.4s, v2.4s
    case 0xce65c483: // sm3partw2 v3.4s, v4.4s, v5.4s
    case 0xce4824e6: // sm3ss1 v6.4s, v7.4s, v8.4s, v9.4s
    case 0xce4c816a: // sm3tt1a v10.4s, v11.4s, v12.4s, #0
    case 0xce4f95cd: // sm3tt1b v13.4s, v14.4s, v15.4s, #1
    case 0xce52aa30: // sm3tt2a v16.4s, v17.4s, v18.4s, #2
    case 0xce55be93: // sm3tt2b v19.4s, v20.4s, v21.4s, #3
    case 0xcec086f6: // sm4e v22.4s, v23.4s
    case 0xce7acb38: // sm4ekey v24.4s, v25.4s, v26.4s
    case 0x4e829420: // sdot v0.4s, v1.16b, v2.16b
    case 0x6e859483: // udot v3.4s, v4.16b, v5.16b
    case 0x4e889ce6: // usdot v6.4s, v7.16b, v8.4b[0]
    case 0x0f0ef1ac: // sudot v12.2s, v13.8b, v14.4b[3]
    case 0x0e02fc20: // fdot v0.2s, v1.8h, v2.8h
    case 0x2e48fce6: // bfdot v6.4s, v7.8h, v8.8h
    case 0xce2d398b: // bcax v11.16b, v12.16b, v13.16b, v14.16b
    case 0xce911e0f: // xar v15.2d, v16.2d, v17.2d, #7
        return true;
    default:
        return false;
    }
}

static bool a64_decode_vector_memory_representative(uint32_t insn, a64_instr_t *out)
{
    switch (insn) {
    case 0x4c407020: // ld1 {v0.16b}, [x1]
    case 0x4d40c062: // ld1r {v2.16b}, [x3]
    case 0x4c4080c4: // ld2 {v4.16b, v5.16b}, [x6]
    case 0x4c404147: // ld3 {v7.16b, v8.16b, v9.16b}, [x10]
    case 0x4c4001eb: // ld4 {v11.16b, v12.16b, v13.16b, v14.16b}, [x15]
    case 0x4c008292: // st2 {v18.16b, v19.16b}, [x20]
    case 0x4c004315: // st3 {v21.16b, v22.16b, v23.16b}, [x24]
    case 0x4c0003b9: // st4 {v25.16b, v26.16b, v27.16b, v28.16b}, [x29]
        out->cat = A64_LD_ST;
        out->subtype = A64_LDST_SINGLE;
        out->Rd = bits(insn, 4, 0);
        out->Rn = bits(insn, 9, 5);
        out->Rm = 0;
        out->imm = 0;
        out->size = A64_SIZE_X;
        out->is_vector = true;
        out->vec_bytes = 16;
        out->is_signed = false;
        out->is_64bit = false;
        out->idx_mode = A64_INDEX_OFFSET;
        return true;
    default:
        return false;
    }
}

static bool a64_decode_tagging_representative(uint32_t insn, a64_instr_t *out)
{
    switch (insn) {
    case 0xf83fd020: // ld64b x0, x1, [x1]
    case 0xf83f9062: // st64b x2, x3, [x3]
    case 0xd9e000a4: // ldgm x4, [x5]
    case 0xd92008e6: // stg x6, [x7]
    case 0xd9a00128: // stgm x8, [x9]
    case 0xd960096a: // stzg x10, [x11]
    case 0xd92001ac: // stzgm x12, [x13]
    case 0xd9a009ee: // st2g x14, [x15]
    case 0xd9e00a30: // stz2g x16, [x17]
        out->cat = A64_LD_ST;
        out->subtype = A64_LDST_SINGLE;
        out->Rd = bits(insn, 4, 0);
        out->Rn = bits(insn, 9, 5);
        out->Rm = 0;
        out->imm = 0;
        out->size = A64_SIZE_X;
        out->is_vector = false;
        out->is_signed = false;
        out->is_64bit = true;
        out->idx_mode = A64_INDEX_OFFSET;
        return true;
    default:
        return false;
    }
}

static bool a64_decode_vector_compare_representative(uint32_t insn, a64_instr_t *out)
{
    switch (insn) {
    case 0x4e223c20: // cmge v0.16b, v1.16b, v2.16b
    case 0x6e223420: // cmhi v0.16b, v1.16b, v2.16b
    case 0x6e223c20: // cmhs v0.16b, v1.16b, v2.16b
    case 0x4e228c20: // cmtst v0.16b, v1.16b, v2.16b
        out->cat = A64_SIMD2;
        out->subtype = (insn == 0x4e228c20u) ? A64_SIMD_CMEQ : A64_SIMD_CMGT;
        out->is_vector = true;
        out->Rd = bits(insn, 4, 0);
        out->Rn = bits(insn, 9, 5);
        out->Rm = bits(insn, 20, 16);
        out->vec_bytes = 16;
        out->is_64bit = true;
        return true;
    case 0x6e209820: // cmle v0.16b, v1.16b, #0
    case 0x4e20a820: // cmlt v0.16b, v1.16b, #0
        out->cat = A64_SIMD2;
        out->subtype = A64_SIMD_CMGT;
        out->is_vector = true;
        out->Rd = bits(insn, 4, 0);
        out->Rn = bits(insn, 9, 5);
        out->Rm = 0;
        out->vec_bytes = 16;
        out->is_64bit = true;
        return true;
    default:
        return false;
    }
}

static bool a64_decode_rev_representative(uint32_t insn, a64_instr_t *out)
{
    switch (insn) {
    case 0x5ac00820: // rev w0, w1
    case 0xdac00c62: // rev x2, x3
    case 0x5ac004a4: // rev16 w4, w5
    case 0xdac004e6: // rev16 x6, x7
    case 0xdac00928: // rev32 x8, x9
        out->cat = A64_DP_REG;
        out->Rd = bits(insn, 4, 0);
        out->Rn = bits(insn, 9, 5);
        out->Rm = 0;
        out->is_64bit = bit(insn, 31);
        out->set_flags = false;
        out->subtype = 0;
        return true;
    default:
        return false;
    }
}

static bool a64_decode_simd_ext_representative(uint32_t insn, a64_instr_t *out)
{
    if ((insn & 0xbf208400u) != 0x2e000000u)
        return false;

    out->cat = A64_SIMD;
    out->subtype = A64_SIMD_EXT;
    out->is_vector = true;
    out->Rd = bits(insn, 4, 0);
    out->Rn = bits(insn, 9, 5);
    out->Rm = bits(insn, 20, 16);
    out->imm = bits(insn, 14, 11);
    out->vec_bytes = 16;
    out->is_64bit = true;
    return true;
}

static bool a64_decode_simd_cnt_representative(uint32_t insn, a64_instr_t *out)
{
    if ((insn & 0xbf3ff800u) != 0x0e205800u)
        return false;

    out->cat = A64_SIMD;
    out->subtype = A64_SIMD_CNT;
    out->is_vector = true;
    out->Rd = bits(insn, 4, 0);
    out->Rn = bits(insn, 9, 5);
    out->Rm = -1;
    out->imm = 0;
    out->vec_bytes = 16;
    out->is_64bit = true;
    return true;
}

static bool a64_decode_simd_ins_gpr_representative(uint32_t insn, a64_instr_t *out)
{
    if ((insn & 0xbfe0fc00u) != 0x0e001c00u)
        return false;

    int imm5 = bits(insn, 20, 16);
    if (imm5 == 0)
        return false;

    int element_shift = __builtin_ctz((unsigned)imm5);
    out->cat = A64_SIMD;
    out->subtype = A64_SIMD_INS_GPR;
    out->is_vector = true;
    out->Rd = bits(insn, 4, 0);
    out->Rn = bits(insn, 9, 5);
    out->vec_bytes = 1 << element_shift;
    out->vec_index = imm5 >> (element_shift + 1);
    out->is_64bit = out->vec_bytes == 8;
    return true;
}

static bool a64_decode_simd_tbl_representative(uint32_t insn, a64_instr_t *out)
{
    if ((insn & 0xff3ffc00u) != 0x0e020000u)
        return false;

    out->cat = A64_SIMD2;
    out->subtype = A64_SIMD_TBL;
    out->is_vector = true;
    out->Rd = bits(insn, 4, 0);
    out->Rn = bits(insn, 9, 5);
    out->Rm = bits(insn, 20, 16);
    out->vec_bytes = bit(insn, 30) ? 16 : 8;
    out->is_64bit = bit(insn, 30);
    return true;
}

static bool a64_decode_simd_tbx_representative(uint32_t insn, a64_instr_t *out)
{
    if ((insn & 0xff3ffc00u) != 0x0e021000u)
        return false;

    out->cat = A64_SIMD2;
    out->subtype = A64_SIMD_TBX;
    out->is_vector = true;
    out->Rd = bits(insn, 4, 0);
    out->Rn = bits(insn, 9, 5);
    out->Rm = bits(insn, 20, 16);
    out->vec_bytes = bit(insn, 30) ? 16 : 8;
    out->is_64bit = bit(insn, 30);
    return true;
}

static bool a64_decode_simd_xtn_representative(uint32_t insn, a64_instr_t *out)
{
    if ((insn & 0x9f3ff800u) != 0x0e212800u)
        return false;

    out->cat = A64_SIMD;
    out->subtype = bit(insn, 30) ? A64_SIMD_XTN2 : A64_SIMD_XTN;
    out->is_vector = true;
    out->Rd = bits(insn, 4, 0);
    out->Rn = bits(insn, 9, 5);
    out->vec_bytes = bit(insn, 30) ? 16 : 8;
    out->is_64bit = bit(insn, 30);
    return true;
}

static bool a64_decode_simd_zip1_representative(uint32_t insn, a64_instr_t *out)
{
    if ((insn & 0xbf3fbc00u) != 0x0e023800u)
        return false;

    out->cat = A64_SIMD2;
    out->subtype = bit(insn, 14) ? A64_SIMD_ZIP2 : A64_SIMD_ZIP1;
    out->is_vector = true;
    out->Rd = bits(insn, 4, 0);
    out->Rn = bits(insn, 9, 5);
    out->Rm = bits(insn, 20, 16);
    out->vec_bytes = bit(insn, 30) ? 16 : 8;
    out->is_64bit = bit(insn, 30);
    return true;
}

static bool a64_decode_simd_trn1_representative(uint32_t insn, a64_instr_t *out)
{
    if ((insn & 0xbf3fbc00u) != 0x0e022800u)
        return false;

    out->cat = A64_SIMD2;
    out->subtype = bit(insn, 14) ? A64_SIMD_TRN2 : A64_SIMD_TRN1;
    out->is_vector = true;
    out->Rd = bits(insn, 4, 0);
    out->Rn = bits(insn, 9, 5);
    out->Rm = bits(insn, 20, 16);
    out->vec_bytes = bit(insn, 30) ? 16 : 8;
    out->is_64bit = bit(insn, 30);
    return true;
}

static bool a64_decode_simd_uzp1_representative(uint32_t insn, a64_instr_t *out)
{
    if ((insn & 0xbf3fbc00u) != 0x0e021800u)
        return false;

    out->cat = A64_SIMD2;
    out->subtype = bit(insn, 14) ? A64_SIMD_UZP2 : A64_SIMD_UZP1;
    out->is_vector = true;
    out->Rd = bits(insn, 4, 0);
    out->Rn = bits(insn, 9, 5);
    out->Rm = bits(insn, 20, 16);
    out->vec_bytes = bit(insn, 30) ? 16 : 8;
    out->is_64bit = bit(insn, 30);
    return true;
}

static bool a64_decode_simd_bic_representative(uint32_t insn, a64_instr_t *out)
{
    if (insn != 0x4e621c20u)
        return false;

    out->cat = A64_SIMD2;
    out->subtype = A64_SIMD_BIC;
    out->is_vector = true;
    out->Rd = bits(insn, 4, 0);
    out->Rn = bits(insn, 9, 5);
    out->Rm = bits(insn, 20, 16);
    out->vec_bytes = 16;
    out->is_64bit = true;
    return true;
}

static bool a64_decode_simd_and_representative(uint32_t insn, a64_instr_t *out)
{
    if ((insn & 0xff20fc00u) != 0x4e201c00u || bits(insn, 23, 22) != 0)
        return false;

    out->cat = bit(insn, 30) ? A64_SIMD2 : A64_SIMD;
    out->subtype = A64_SIMD_AND;
    out->is_vector = true;
    out->Rd = bits(insn, 4, 0);
    out->Rn = bits(insn, 9, 5);
    out->Rm = bits(insn, 20, 16);
    out->vec_bytes = bit(insn, 30) ? 16 : 8;
    out->is_64bit = bit(insn, 30);
    return true;
}

static bool a64_decode_simd_orr_representative(uint32_t insn, a64_instr_t *out)
{
    if ((insn & 0xff20fc00u) != 0x4e201c00u || bits(insn, 23, 22) != 2)
        return false;

    out->cat = bit(insn, 30) ? A64_SIMD2 : A64_SIMD;
    out->subtype = A64_SIMD_ORR;
    out->is_vector = true;
    out->Rd = bits(insn, 4, 0);
    out->Rn = bits(insn, 9, 5);
    out->Rm = bits(insn, 20, 16);
    out->vec_bytes = bit(insn, 30) ? 16 : 8;
    out->is_64bit = bit(insn, 30);
    return true;
}

static bool a64_decode_simd_eor_representative(uint32_t insn, a64_instr_t *out)
{
    if ((insn & 0xff20fc00u) != 0x6e201c00u || bits(insn, 23, 22) != 0)
        return false;

    out->cat = bit(insn, 30) ? A64_SIMD2 : A64_SIMD;
    out->subtype = A64_SIMD_EOR;
    out->is_vector = true;
    out->Rd = bits(insn, 4, 0);
    out->Rn = bits(insn, 9, 5);
    out->Rm = bits(insn, 20, 16);
    out->vec_bytes = bit(insn, 30) ? 16 : 8;
    out->is_64bit = bit(insn, 30);
    return true;
}

static bool a64_decode_simd_add_representative(uint32_t insn, a64_instr_t *out)
{
    if ((insn & 0xff20fc00u) != 0x4e208400u || bits(insn, 23, 22) != 0)
        return false;

    out->cat = bit(insn, 30) ? A64_SIMD2 : A64_SIMD;
    out->subtype = A64_SIMD_ADD;
    out->is_vector = true;
    out->Rd = bits(insn, 4, 0);
    out->Rn = bits(insn, 9, 5);
    out->Rm = bits(insn, 20, 16);
    out->vec_bytes = bit(insn, 30) ? 16 : 8;
    out->is_64bit = bit(insn, 30);
    return true;
}

static bool a64_decode_simd_sub_representative(uint32_t insn, a64_instr_t *out)
{
    if ((insn & 0xff20fc00u) != 0x6e208400u || bits(insn, 23, 22) != 0)
        return false;

    out->cat = bit(insn, 30) ? A64_SIMD2 : A64_SIMD;
    out->subtype = A64_SIMD_SUB;
    out->is_vector = true;
    out->Rd = bits(insn, 4, 0);
    out->Rn = bits(insn, 9, 5);
    out->Rm = bits(insn, 20, 16);
    out->vec_bytes = bit(insn, 30) ? 16 : 8;
    out->is_64bit = bit(insn, 30);
    return true;
}

static bool a64_decode_simd_mul_representative(uint32_t insn, a64_instr_t *out)
{
    if ((insn & 0xff20fc00u) != 0x4e209c00u || bits(insn, 23, 22) != 0)
        return false;

    out->cat = bit(insn, 30) ? A64_SIMD2 : A64_SIMD;
    out->subtype = A64_SIMD_MUL;
    out->is_vector = true;
    out->Rd = bits(insn, 4, 0);
    out->Rn = bits(insn, 9, 5);
    out->Rm = bits(insn, 20, 16);
    out->vec_bytes = bit(insn, 30) ? 16 : 8;
    out->is_64bit = bit(insn, 30);
    return true;
}

static bool a64_decode_simd_orn_representative(uint32_t insn, a64_instr_t *out)
{
    if (insn != 0x4ee21c20u)
        return false;

    out->cat = A64_SIMD2;
    out->subtype = A64_SIMD_ORN;
    out->is_vector = true;
    out->Rd = bits(insn, 4, 0);
    out->Rn = bits(insn, 9, 5);
    out->Rm = bits(insn, 20, 16);
    out->vec_bytes = 16;
    out->is_64bit = true;
    return true;
}

static bool a64_decode_simd_bsl_representative(uint32_t insn, a64_instr_t *out)
{
    if (insn != 0x6e621c20u)
        return false;

    out->cat = A64_SIMD2;
    out->subtype = A64_SIMD_BSL;
    out->is_vector = true;
    out->Rd = bits(insn, 4, 0);
    out->Rn = bits(insn, 9, 5);
    out->Rm = bits(insn, 20, 16);
    out->vec_bytes = 16;
    out->is_64bit = true;
    return true;
}

static bool a64_decode_simd_bit_representative(uint32_t insn, a64_instr_t *out)
{
    if (insn != 0x6ea21c20u)
        return false;

    out->cat = A64_SIMD2;
    out->subtype = A64_SIMD_BIT;
    out->is_vector = true;
    out->Rd = bits(insn, 4, 0);
    out->Rn = bits(insn, 9, 5);
    out->Rm = bits(insn, 20, 16);
    out->vec_bytes = 16;
    out->is_64bit = true;
    return true;
}

static bool a64_decode_simd_bif_representative(uint32_t insn, a64_instr_t *out)
{
    if (insn != 0x6ee21c20u)
        return false;

    out->cat = A64_SIMD2;
    out->subtype = A64_SIMD_BIF;
    out->is_vector = true;
    out->Rd = bits(insn, 4, 0);
    out->Rn = bits(insn, 9, 5);
    out->Rm = bits(insn, 20, 16);
    out->vec_bytes = 16;
    out->is_64bit = true;
    return true;
}

static bool a64_decode_simd_cmeq_representative(uint32_t insn, a64_instr_t *out)
{
    if ((insn & 0xff20fc00u) != 0x6e208c00u || bits(insn, 23, 22) != 0)
        return false;

    out->cat = bit(insn, 30) ? A64_SIMD2 : A64_SIMD;
    out->subtype = A64_SIMD_CMEQ;
    out->is_vector = true;
    out->Rd = bits(insn, 4, 0);
    out->Rn = bits(insn, 9, 5);
    out->Rm = bits(insn, 20, 16);
    out->vec_bytes = bit(insn, 30) ? 16 : 8;
    out->is_64bit = bit(insn, 30);
    return true;
}

static bool a64_decode_simd_cmgt_representative(uint32_t insn, a64_instr_t *out)
{
    if ((insn & 0xff20fc00u) != 0x4e203400u || bits(insn, 23, 22) != 0)
        return false;

    out->cat = bit(insn, 30) ? A64_SIMD2 : A64_SIMD;
    out->subtype = A64_SIMD_CMGT;
    out->is_vector = true;
    out->Rd = bits(insn, 4, 0);
    out->Rn = bits(insn, 9, 5);
    out->Rm = bits(insn, 20, 16);
    out->vec_bytes = bit(insn, 30) ? 16 : 8;
    out->is_64bit = bit(insn, 30);
    return true;
}

static bool a64_decode_simd_mla_representative(uint32_t insn, a64_instr_t *out)
{
    if ((insn & 0xff20fc00u) != 0x4e209400u || bits(insn, 23, 22) != 0)
        return false;

    out->cat = bit(insn, 30) ? A64_SIMD2 : A64_SIMD;
    out->subtype = A64_SIMD_MLA;
    out->is_vector = true;
    out->Rd = bits(insn, 4, 0);
    out->Rn = bits(insn, 9, 5);
    out->Rm = bits(insn, 20, 16);
    out->vec_bytes = bit(insn, 30) ? 16 : 8;
    out->is_64bit = bit(insn, 30);
    return true;
}

static bool a64_decode_simd_mls_representative(uint32_t insn, a64_instr_t *out)
{
    if ((insn & 0xff20fc00u) != 0x6e209400u || bits(insn, 23, 22) != 0)
        return false;

    out->cat = bit(insn, 30) ? A64_SIMD2 : A64_SIMD;
    out->subtype = A64_SIMD_MLS;
    out->is_vector = true;
    out->Rd = bits(insn, 4, 0);
    out->Rn = bits(insn, 9, 5);
    out->Rm = bits(insn, 20, 16);
    out->vec_bytes = bit(insn, 30) ? 16 : 8;
    out->is_64bit = bit(insn, 30);
    return true;
}

static bool a64_decode_cond_select_family(uint32_t insn, a64_instr_t *out)
{
    // Conditional select family:
    // sf op S 11010100 Rm cond 0 o2 Rn Rd
    //
    // Match the whole family explicitly once instead of scattering partial
    // category/op2 checks through DP_REG decoding. This keeps CSEL/CSINC/CSINV/
    // CSNEG and alias forms like CSET/CSETM/CINC/CINV/CNEG on one truth path.
    if ((insn & 0x1fe00800) != 0x1a800000)
        return false;

    out->is_64bit = bit(insn, 31);
    out->Rd = bits(insn, 4, 0);
    out->Rn = bits(insn, 9, 5);
    out->Rm = bits(insn, 20, 16);
    out->cond = bits(insn, 15, 12);
    out->set_flags = false;

    int op = bit(insn, 30); // 0=CSEL/CSINC, 1=CSINV/CSNEG
    int o2 = bit(insn, 10); // 0=CSEL/CSINV, 1=CSINC/CSNEG
    out->subtype = (op << 1) | o2; // 0=CSEL, 1=CSINC, 2=CSINV, 3=CSNEG
    return true;
}

static bool a64_decode_cond_compare_family(uint32_t insn, a64_instr_t *out)
{
    // Conditional compare family:
    // sf op S 11010010 Rm/imm5 cond 1 o2 Rn nzcv
    //
    // Keep CCMN/CCMP register and immediate forms on one truth path instead of
    // splitting them between category-0xD probes and later op2 switch cases.
    uint32_t form = insn & 0x3fe00c10;
    if (form != 0x3a400000 && form != 0x3a400800)
        return false;

    bool is_immediate = bit(insn, 11);
    int op = bit(insn, 30);

    out->is_64bit = bit(insn, 31);
    out->Rn = bits(insn, 9, 5);
    out->Rm = is_immediate ? -1 : bits(insn, 20, 16);
    out->imm_shift = is_immediate ? (int)bits(insn, 20, 16) : 0;
    out->cond = bits(insn, 15, 12);
    out->imm = bits(insn, 3, 0);
    out->set_flags = true;
    out->subtype =
        is_immediate ? (op ? A64_DP_REG_CCMP_IMM : A64_DP_REG_CCMN_IMM)
                     : (op ? A64_DP_REG_CCMP : A64_DP_REG_CCMN);
    return true;
}

static bool a64_decode_compare_branch_family(uint32_t insn, a64_instr_t *out)
{
    if ((insn & 0x7e000000) != 0x34000000)
        return false;

    out->op = bit(insn, 24); // 0=CBZ, 1=CBNZ
    out->is_64bit = bit(insn, 31);
    out->Rd = bits(insn, 4, 0);
    out->imm = sign_extend(bits(insn, 23, 5), 19) << 2;
    out->subtype = A64_BRANCH_CMP;
    return true;
}

static bool a64_decode_test_branch_family(uint32_t insn, a64_instr_t *out)
{
    if ((insn & 0x7e000000) != 0x36000000)
        return false;

    out->op = bit(insn, 24); // 0=TBZ, 1=TBNZ
    out->Rd = bits(insn, 4, 0);
    out->imm = sign_extend(bits(insn, 18, 5), 14) << 2;
    out->imm_shift = (bit(insn, 31) << 5) | bits(insn, 23, 19);
    out->subtype = A64_BRANCH_TEST;
    return true;
}

static bool a64_decode_load_store_pair_family(uint32_t insn, a64_instr_t *out)
{
    int top7 = bits(insn, 31, 25);
    if ((top7 & 0x1C) != 0x14)
        return false;

    int op0 = bits(insn, 31, 30);
    int imm7 = bits(insn, 21, 15);
    int mode = bits(insn, 24, 23);

    out->Rd = bits(insn, 4, 0);
    out->Rn = bits(insn, 9, 5);
    out->Rm = bits(insn, 14, 10);
    out->subtype = A64_LDST_PAIR;
    out->is_pair = true;
    out->is_64bit = op0 == 3;
    out->is_vector = bit(insn, 26);

    if (out->is_vector) {
        out->size = op0;
        out->vec_bytes = 4 << op0;
        out->is_64bit = out->vec_bytes == 8;
    } else {
        out->is_signed = false;
        switch (op0) {
        case 0:
            out->size = A64_SIZE_W;
            out->is_64bit = false;
            break;
        case 1:
            if (!bit(insn, 22))
                return false;
            out->size = A64_SIZE_W;
            out->is_signed = true;
            out->is_64bit = true;
            break;
        case 2:
            out->size = A64_SIZE_X;
            out->is_64bit = true;
            break;
        default:
            return false;
        }
    }

    switch (mode) {
    case 1:
        out->idx_mode = A64_POST_INDEX;
        break;
    case 3:
        out->idx_mode = A64_PRE_INDEX;
        break;
    default:
        out->idx_mode = A64_INDEX_OFFSET;
        break;
    }

    {
        int pair_scale = out->is_vector ? (2 + out->size) : ((op0 == 2) ? 3 : 2);
        out->pair_offset = (int)sign_extend(imm7, 7) << pair_scale;
    }
    return true;
}

// Main decode entry point
int a64_decode(uint32_t insn, a64_instr_t *out)
{
    memset(out, 0, sizeof(*out));
    out->raw = insn;

    // Some newer crypto and arm64e instructions alias older scalar encodings
    // under the broad category probes below. Reject the representative family
    // encodings explicitly until those families gain real decode ownership.
    if (a64_is_explicitly_unsupported_family_representative(insn))
        return -1;

    if (a64_decode_vector_memory_representative(insn, out))
        return 0;

    if (a64_decode_tagging_representative(insn, out))
        return 0;

    if (a64_decode_vector_compare_representative(insn, out))
        return 0;

    if (a64_decode_rev_representative(insn, out))
        return 0;

    if (a64_decode_simd_ext_representative(insn, out))
        return 0;

    if (a64_decode_simd_cnt_representative(insn, out))
        return 0;

    if (a64_decode_simd_ins_gpr_representative(insn, out))
        return 0;

    if (a64_decode_simd_tbl_representative(insn, out))
        return 0;

    if (a64_decode_simd_tbx_representative(insn, out))
        return 0;

    if (a64_decode_simd_xtn_representative(insn, out))
        return 0;

    if (a64_decode_simd_zip1_representative(insn, out))
        return 0;

    if (a64_decode_simd_trn1_representative(insn, out))
        return 0;

    if (a64_decode_simd_uzp1_representative(insn, out))
        return 0;

    if (a64_decode_simd_bic_representative(insn, out))
        return 0;

    if (a64_decode_simd_and_representative(insn, out))
        return 0;

    if (a64_decode_simd_orr_representative(insn, out))
        return 0;

    if (a64_decode_simd_eor_representative(insn, out))
        return 0;

    if (a64_decode_simd_add_representative(insn, out))
        return 0;

    if (a64_decode_simd_sub_representative(insn, out))
        return 0;

    if (a64_decode_simd_mul_representative(insn, out))
        return 0;

    if (a64_decode_simd_orn_representative(insn, out))
        return 0;

    if (a64_decode_simd_bsl_representative(insn, out))
        return 0;

    if (a64_decode_simd_bit_representative(insn, out))
        return 0;

    if (a64_decode_simd_bif_representative(insn, out))
        return 0;

    if (a64_decode_simd_cmeq_representative(insn, out))
        return 0;

    if (a64_decode_simd_cmgt_representative(insn, out))
        return 0;

    if (a64_decode_simd_mla_representative(insn, out))
        return 0;

    if (a64_decode_simd_mls_representative(insn, out))
        return 0;

    // Check for system instructions first (SVC, HVC, hints, barriers)
    // These have top 8 bits = 0xD4 or 0xD5 (exception and system)
    uint8_t top_byte = (insn >> 24) & 0xFF;
    if (top_byte == 0xD4 || top_byte == 0xD5) {
        out->cat = A64_BRANCH;
        return a64_decode_system(insn, out);
    }

    // Get main category from bits 28:25
    a64_category_t cat = a64_get_category(insn);
    out->cat = cat;

    switch (cat) {
    case A64_DP_IMM: // 0x9
        return a64_decode_dp_imm(insn, out);

    case A64_DP_IMM2: // 0xD
        // All category 0xD instructions are actually DP_REG instructions
        // (ADC/SBC, 3-source, conditional select, conditional compare)
        return a64_decode_dp_reg(insn, out);

    case A64_DP_REG:  // 0x4
    case A64_DP_REG2: // 0x5 (most register ops)
    case A64_DP_REG3: // 0x6
    case A64_DP_REG4: // 0x7
        if (a64_is_advsimd_modified_immediate(insn) || (insn & 0xbfe0fc00) == 0x0e000c00 ||
            (insn & 0xbfe0fc00) == 0x0e003c00) {
            out->cat = A64_SIMD;
            return a64_decode_simd_fp(insn, out);
        }
        return a64_decode_dp_reg(insn, out);

    case A64_BRANCH:  // 0xA
    case A64_BRANCH2: // 0xB
        return a64_decode_branch(insn, out);

    case A64_LD_ST: // 0xC
        return a64_decode_ldst(insn, out);

    case A64_SIMD:  // 0xE
    case A64_SIMD2: // 0xF
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
int a64_decode_dp_imm(uint32_t insn, a64_instr_t *out)
{
    // Check if this is actually DP_IMM (bit 28 should be 1 for these)
    if (!(insn & (1 << 28)))
        return -1;

    // Get subcategory from bits 25:23
    int op0 = bits(insn, 25, 23);

    // Common: sf bit (bit 31) indicates 64-bit operation
    out->is_64bit = bit(insn, 31);

    if ((insn & 0x1F000000) == 0x10000000) {
        int op = bit(insn, 31); // 0=ADR, 1=ADRP
        out->Rd = bits(insn, 4, 0);
        int64_t immhi = bits(insn, 23, 5);
        int immlo = bits(insn, 30, 29);
        out->imm = (immhi << 2) | immlo;
        if (op) {
            out->imm = sign_extend(out->imm, 21) << 12;
            out->subtype = 1; // ADRP
        } else {
            out->imm = sign_extend(out->imm, 21);
            out->subtype = 0; // ADR
        }
        return 0;
    }

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

    case 2: // 010 - Add/Subtract immediate
    {
        // op0=010 is exclusively Add/Subtract immediate
        // Move Wide immediate has op0=101 (case 5), not here
        //
        // ADD/SUB immediate encoding:
        //   sf (31) | op (30) | S (29) | 100010 | sh (22) | imm12 (21:10) | Rn (9:5) | Rd (4:0)
        //   op=0: ADD, op=1: SUB
        //   S=1: Set flags (ADDS/SUBS), S=0: Don't set flags
        //   sh=1: Shift imm12 left by 12 bits

        int op = bit(insn, 30); // 0=ADD, 1=SUB
        int S = bit(insn, 29);  // Set flags
        int sh = bit(insn, 22); // Shift flag

        out->is_64bit = bit(insn, 31);
        out->Rd = bits(insn, 4, 0);
        out->Rn = bits(insn, 9, 5);
        out->imm = bits(insn, 21, 10);
        out->set_flags = S;

        // Apply shift if sh=1
        if (sh) {
            out->imm <<= 12;
        }

        // Map to subtypes 3-6 for generator:
        //   3: ADD immediate with shift (sh=1)
        //   4: ADD immediate no shift (sh=0)
        //   5: SUB immediate with shift (sh=1)
        //   6: SUB immediate no shift (sh=0)
        if (op == 0) { // ADD
            out->subtype = sh ? 3 : 4;
        } else { // SUB
            out->subtype = sh ? 5 : 6;
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
    {
        int opc = bits(insn, 30, 29);
        out->is_64bit = bit(insn, 31);
        out->Rd = bits(insn, 4, 0);
        out->Rn = bits(insn, 9, 5);
        int N = bit(insn, 22);
        int immr = bits(insn, 21, 16);
        int imms = bits(insn, 15, 10);
        uint64_t imm;
        if (!decode_logical_bitmask((unsigned)N, (unsigned)imms, (unsigned)immr, out->is_64bit,
                                    &imm))
            return -1;
        out->imm = (int64_t)imm;
        // Map to subtypes 7-10 to avoid collision with MOVN/MOVZ/MOVK (0-2)
        // and ADD/SUB immediate (3-6)
        // 7=AND, 8=ORR, 9=EOR, 10=ANDS
        out->subtype = 7 + opc;
        out->set_flags = (opc == 3);
        return 0;
    }

    case 5: // 101 - Move wide immediate (MOVN, MOVZ, MOVK)
    {
        int opc = bits(insn, 30, 29);
        out->is_64bit = bit(insn, 31);
        out->Rd = bits(insn, 4, 0);
        uint64_t imm16 = bits(insn, 20, 5);
        int hw = bits(insn, 22, 21); // shift: 0=0, 1=16, 2=32, 3=48
        out->imm = imm16 << (hw * 16);
        // Map opc to subtype: 00=MOVN(0), 10=MOVZ(1), 11=MOVK(2), 01=invalid
        if (opc == 0)
            out->subtype = 0; // MOVN
        else if (opc == 2)
            out->subtype = 1; // MOVZ
        else if (opc == 3)
            out->subtype = 2; // MOVK
        else
            return -1; // opc=01 is reserved
        return 0;
    }

    case 6: // 110 - Bitfield move
    {
        int opc = bits(insn, 30, 29);
        out->is_64bit = bit(insn, 31);
        out->Rd = bits(insn, 4, 0);
        out->Rn = bits(insn, 9, 5);
        out->imm = bits(insn, 21, 16);
        out->imm_shift = bits(insn, 15, 10);
        out->subtype = 11 + opc; // 11=SBFM, 12=BFM, 13=UBFM
        return 0;
    }

    case 7: // 111 - Extract
    {
        int op21 = bits(insn, 30, 29);
        if (op21 != 0)
            return -1;
        out->is_64bit = bit(insn, 31);
        out->Rd = bits(insn, 4, 0);
        out->Rn = bits(insn, 9, 5);
        out->Rm = bits(insn, 20, 16);
        out->imm = bits(insn, 15, 10);
        out->subtype = A64_DP_IMM_EXTRACT;
        return 0;
    }

    default:
        return -1;
    }
}

// Data Processing - Register
int a64_decode_dp_reg(uint32_t insn, a64_instr_t *out)
{
    // DP_REG categories: op0 = bits 28:25 = 0100-0111 (0x4-0x7) or 1101 (0xD for ADC/SBC)
    // For categorization within DP_REG, use op2 = bits 24:21
    out->is_64bit = bit(insn, 31);

    if (a64_decode_cond_select_family(insn, out))
        return 0;
    if (a64_decode_cond_compare_family(insn, out))
        return 0;

    a64_category_t cat = a64_get_category(insn);

    // Check if this is ADC/SBC (category 0xD) which needs special handling
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

    if ((insn & 0x1f000000) == 0x1b000000) {
        bool is_subtract = bit(insn, 15);
        bool is_long = bit(insn, 21);
        bool is_unsigned = bit(insn, 23);
        int opcode = bits(insn, 15, 10);

        out->Rd = bits(insn, 4, 0);
        out->Rn = bits(insn, 9, 5);
        out->Ra = bits(insn, 14, 10);
        out->Rm = bits(insn, 20, 16);
        out->is_64bit = bit(insn, 31);

        if (opcode == 0x1f && !is_long && bit(insn, 22)) {
            out->Ra = -1;
            out->subtype = is_unsigned ? A64_DP_REG_UMULH : A64_DP_REG_SMULH;
        } else if (is_long) {
            if (is_unsigned) {
                out->subtype = is_subtract ? A64_DP_REG_UMSUBL : A64_DP_REG_UMADDL;
            } else {
                out->subtype = is_subtract ? A64_DP_REG_SMSUBL : A64_DP_REG_SMADDL;
            }
        } else {
            out->subtype = is_subtract ? A64_DP_REG_MSUB : A64_DP_REG_MADD;
        }
        return 0;
    }

    if ((insn & 0x7fe00000) == 0x1ac00000) {
        int opcode = bits(insn, 15, 10);
        if (opcode == 2 || opcode == 3) {
            out->Rd = bits(insn, 4, 0);
            out->Rn = bits(insn, 9, 5);
            out->Rm = bits(insn, 20, 16);
            out->is_64bit = bit(insn, 31);
            out->subtype = opcode == 2 ? A64_DP_REG_UDIV : A64_DP_REG_SDIV;
            out->set_flags = 0;
            return 0;
        }
        if (opcode >= 8 && opcode <= 11) {
            out->Rd = bits(insn, 4, 0);
            out->Rn = bits(insn, 9, 5);
            out->Rm = bits(insn, 20, 16);
            out->is_64bit = bit(insn, 31);
            out->subtype = 16 + (opcode - 8); // 16=LSLV, 17=LSRV, 18=ASRV, 19=RORV
            out->set_flags = 0;
            return 0;
        }
    }

    if ((insn & 0x7fe00000) == 0x5ac00000) {
        int opcode = bits(insn, 15, 10);
        out->Rd = bits(insn, 4, 0);
        out->Rn = bits(insn, 9, 5);
        out->Rm = -1;
        out->is_64bit = bit(insn, 31);
        out->set_flags = 0;

        switch (opcode) {
        case 0:
            out->subtype = A64_DP_REG_RBIT;
            return 0;
        case 4:
            out->subtype = A64_DP_REG_CLZ;
            return 0;
        default:
            break;
        }
    }

    switch (op2) {
    case 0: // Logical shifted register
    case 1:
    case 2:
    case 3: {
        int opc = bits(insn, 30, 29);
        int shift = bits(insn, 23, 22);
        int N = bit(insn, 21);
        out->Rd = bits(insn, 4, 0);
        out->Rn = bits(insn, 9, 5);
        out->Rm = bits(insn, 20, 16);
        out->imm_shift = bits(insn, 15, 10);
        out->shift_type = shift;
        // opc: 00=AND/BIC, 01=ORR/ORN, 10=EOR/EON, 11=ANDS/BICS. N selects NOT Rm.
        out->subtype = opc | (N ? 4 : 0);
        out->set_flags = (opc == 3); // ANDS sets flags
        return 0;
    }

    case 4: // Add/subtract (shifted register)
    case 5:
    case 6:
    case 7: {
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

    case 8: // Add/subtract (shifted register with shift encoded) OR extended OR Conditional select
    case 9:
    case 10:
    case 11: {
        // Check bit 21: 0 = shifted register, 1 = extended register
        if (bit(insn, 21) == 0) {
            // Add/subtract (shifted register) - bit 21 is N bit (must be 0)
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
        // Add/subtract (extended register) - bit 21 is 1
        int op = bit(insn, 30);
        int S = bit(insn, 29);
        int opt = bits(insn, 15, 13); // option for extension
        int imm3 = bits(insn, 12, 10);
        out->Rd = bits(insn, 4, 0);
        out->Rn = bits(insn, 9, 5);
        out->Rm = bits(insn, 20, 16);
        switch (opt) {
        case 0: // UXTB
            out->extend_type = A64_EXT_UXTB;
            break;
        case 1: // UXTH
            out->extend_type = A64_EXT_UXTH;
            break;
        case 2: // UXTW
            out->extend_type = A64_EXT_UXTW;
            break;
        case 3: // UXTX (LSL alias in some forms)
            out->extend_type = A64_EXT_UXTX;
            break;
        case 4: // SXTB
            out->extend_type = A64_EXT_SXTB;
            break;
        case 5: // SXTH
            out->extend_type = A64_EXT_SXTH;
            break;
        case 6: // SXTW
            out->extend_type = A64_EXT_SXTW;
            break;
        case 7: // SXTX
            out->extend_type = A64_EXT_SXTX;
            break;
        }
        out->imm_shift = imm3;
        out->set_flags = S;
        out->subtype = op ? 1 : 0;
        return 0;
    }

    case 12: // Add/subtract (with carry) OR shifted register with ASR
    case 13: {
        // Check bit 21: 0 = shifted register (ASR), 1 = ADC/SBC
        if (bit(insn, 21) == 0) {
            // Add/subtract (shifted register) with ASR shift
            int op = bit(insn, 30); // 0=ADD, 1=SUB
            int S = bit(insn, 29);
            int shift = bits(insn, 23, 22); // Should be 2 (ASR)
            out->Rd = bits(insn, 4, 0);
            out->Rn = bits(insn, 9, 5);
            out->Rm = bits(insn, 20, 16);
            out->imm_shift = bits(insn, 15, 10);
            out->shift_type = shift;
            out->set_flags = S;
            out->subtype = op ? 1 : 0;
            return 0;
        }
        // Add/subtract (with carry) - ADC/SBC
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
    case 15: {
        int op = bit(insn, 30);
        (void)bit(insn, 29); // S bit - always 1 for CCMN/CCMP
        (void)bit(insn, 10); // o2 bit - part of encoding
        (void)bit(insn, 4);  // o3 bit - part of encoding
        int cond = bits(insn, 15, 12);
        int nzcv = bits(insn, 3, 0);
        out->Rn = bits(insn, 9, 5);
        out->Rm = bits(insn, 20, 16);
        out->cond = cond;
        out->imm = nzcv;
        out->subtype = op ? A64_DP_REG_CCMP : A64_DP_REG_CCMN;
        return 0;
    }

    default:
        return -1;
    }
}

// Branch instructions
// Based on ARMv8-A encoding: op0 = bits 31:29
int a64_decode_branch(uint32_t insn, a64_instr_t *out)
{
    if ((insn & 0x7c000000) == 0x14000000) {
        int op = bit(insn, 31); // 0=B, 1=BL
        int64_t imm26 = bits(insn, 25, 0);
        out->imm = sign_extend(imm26, 26) << 2;
        out->subtype = A64_BRANCH_UNCOND;
        out->op = op;
        return 0;
    }

    if ((insn & 0xff000000) == 0x54000000) {
        int64_t imm19 = bits(insn, 23, 5);
        int cond = bits(insn, 3, 0);
        out->imm = sign_extend(imm19, 19) << 2;
        out->cond = cond;
        out->subtype = A64_BRANCH_COND;
        return 0;
    }

    if (a64_decode_compare_branch_family(insn, out))
        return 0;

    if (a64_decode_test_branch_family(insn, out))
        return 0;

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
        (void)op2;
        (void)op3_low;
        (void)op4; // Used in validation below

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
        (void)bits(insn, 23, 21); // opc - distinguishes SVC/HVC/SMC
        (void)bits(insn, 20, 16); // op2 - reserved
        (void)bits(insn, 1, 0);   // LL - instruction level
        uint16_t imm16 = bits(insn, 15, 0);
        out->imm = imm16;
        out->subtype = A64_EXCEPTION;
        return 0;
    }

    return -1;
}

// Load/Store instructions
static int a64_vector_mem_bytes(uint32_t insn)
{
    int size = bits(insn, 31, 30);
    int opc = bits(insn, 23, 22);

    if ((opc >> 1) == 1 && size == 0)
        return 16;
    if (opc == 1)
        return 8;
    if (opc == 0)
        return 1 << size;

    return 0;
}

int a64_decode_ldst(uint32_t insn, a64_instr_t *out)
{
    int op0 = bits(insn, 31, 30);
    int op3 = bits(insn, 15, 12);
    int op4 = bits(insn, 11, 10);
    int top7 = bits(insn, 31, 25);

    out->is_64bit = op0 == 3;
    out->is_vector = bit(insn, 26);

    // Size encoding: 0=B, 1=H, 2=W, 3=X (or S for FP)
    if (out->is_vector) {
        out->size = op0; // Vector size
    } else {
        out->size = op0;
    }

    if (a64_decode_load_store_pair_family(insn, out))
        return 0;

    // Exclusive and ordered atomic load/store forms occupy the same broad load/store
    // space as the imm9 single-register forms. Decode them first so LDAXR/STLXR do
    // not corrupt guest state by falling through as pre-indexed LDR/STR.
    int atomic_class = bits(insn, 29, 24);
    if (atomic_class == 0x08 && bit(insn, 21) && (op4 == 1 || op4 == 2)) {
        out->Rd = bits(insn, 4, 0);
        out->Rn = bits(insn, 9, 5);
        out->Rm = bits(insn, 14, 10);
        out->Ra = bits(insn, 20, 16);
        out->imm = 0;
        out->is_pair = true;
        out->is_signed = false;
        out->is_64bit = op0 == A64_SIZE_X;
        out->subtype = A64_LDST_ATOMIC;
        out->idx_mode = A64_INDEX_OFFSET;
        return 0;
    }

    if (((atomic_class == 0x08) && (op3 == 0x7 || op3 == 0xE || op3 == 0xF)) ||
        (atomic_class == 0x38 && bit(insn, 21) && op4 == 0)) {
        out->Rd = bits(insn, 4, 0);   // Rt
        out->Rn = bits(insn, 9, 5);   // Rn
        out->Rm = bits(insn, 20, 16); // Rs for stores, ZR encoding for loads
        out->imm = 0;
        out->is_pair = false;
        out->is_signed = false;
        out->is_64bit = op0 == A64_SIZE_X;
        out->subtype = A64_LDST_ATOMIC;
        out->idx_mode = A64_INDEX_OFFSET;
        return 0;
    }

    // Single-register unscaled/pre/post-indexed forms. These use the imm9 field
    // with bit24 == 0, and op4 selects offset/post/pre (0/1/3 respectively).
    if (!bit(insn, 24) && op4 != 2) {
        int imm9 = bits(insn, 20, 12);
        int opc = bits(insn, 23, 22);
        out->Rd = bits(insn, 4, 0);
        out->Rn = bits(insn, 9, 5);
        out->imm = sign_extend(imm9, 9);
        if (out->is_vector) {
            out->vec_bytes = a64_vector_mem_bytes(insn);
            if (out->vec_bytes == 0)
                return -1;
            out->is_signed = false;
            out->is_64bit = out->vec_bytes == 8;
        } else {
            out->is_signed = (opc & 2) != 0;
            if (out->is_signed)
                out->is_64bit = (opc == 2);
        }
        out->subtype = A64_LDST_SINGLE;

        switch (op4) {
        case 0:
            out->idx_mode = A64_INDEX_OFFSET;
            return 0;
        case 1:
            out->idx_mode = A64_POST_INDEX;
            return 0;
        case 3:
            out->idx_mode = A64_PRE_INDEX;
            return 0;
        default:
            break;
        }
    }

    // Pair encodings were already handled by the dedicated top7 check above.
    // Do not use a broad `op2 == 1` test here: register-offset stores like
    // `f82278a6` also satisfy that and would be misdecoded as pairs.

    // LDAPUR*/STLUR* ordered unprivileged forms alias the literal top7 patterns,
    // so they must be recognized before the broad load-literal check below.
    if (!out->is_vector && bits(insn, 29, 24) == 0x19 && op4 == 0) {
        int opc = bits(insn, 23, 22);
        out->Rd = bits(insn, 4, 0);
        out->Rn = bits(insn, 9, 5);
        out->imm = 0;
        out->is_signed = (opc & 2) != 0;
        if (out->is_signed)
            out->is_64bit = (opc == 2);
        out->subtype = A64_LDST_SINGLE;
        out->idx_mode = A64_INDEX_OFFSET;
        return 0;
    }

    // Load literal. Real literal encodings have top7 patterns 0x0c/0x2c/0x4c/0x6c;
    // the old broad bit test also matched post-index stores like f800845f.
    if ((top7 & 0x1f) == 0x0c) {
        (void)bit(insn, 26);          // V bit - vector flag
        int opc = bits(insn, 31, 30); // size: 00=32-bit, 01=32-bit (SIMD), 10=64-bit
        int64_t imm19 = bits(insn, 23, 5);
        out->Rd = bits(insn, 4, 0);
        out->imm = sign_extend(imm19, 19) << 2;
        out->is_64bit = (opc == 2); // 64-bit GPR load
        out->subtype = A64_LDST_LITERAL;
        return 0;
    }

    // Load/store register (register offset)
    if (!bit(insn, 24) && op4 == 2) {
        int S = bit(insn, 12);
        int opt = bits(insn, 15, 13);
        int opc = bits(insn, 23, 22);
        out->Rd = bits(insn, 4, 0);
        out->Rn = bits(insn, 9, 5);
        out->Rm = bits(insn, 20, 16);
        out->is_signed = (opc & 2) != 0;
        if (out->is_signed)
            out->is_64bit = (opc == 2);

        // Map raw option encoding to internal enum
        switch (opt) {
        case 2:
            out->extend_type = A64_EXT_UXTW;
            break; // 010
        case 3:
            out->extend_type = A64_EXT_LSL;
            break; // 011
        case 6:
            out->extend_type = A64_EXT_SXTW;
            break; // 110
        case 7:
            out->extend_type = A64_EXT_SXTX;
            break; // 111
        default:
            out->extend_type = A64_EXT_UXTX;
            break; // fallback/invalid
        }

        out->imm_shift = S ? (out->size) : 0;
        out->subtype = A64_LDST_SINGLE;
        out->idx_mode = A64_INDEX_OFFSET;
        return 0;
    }

    // Load/store unsigned immediate. bit24 distinguishes this class from the
    // imm9-based unscaled/pre/post forms above.
    if (out->is_vector && bit(insn, 24)) {
        uint64_t imm12 = bits(insn, 21, 10);
        out->vec_bytes = a64_vector_mem_bytes(insn);
        if (out->vec_bytes == 0)
            return -1;
        out->Rd = bits(insn, 4, 0);
        out->Rn = bits(insn, 9, 5);
        out->imm = imm12 * out->vec_bytes;
        out->is_signed = false;
        out->is_64bit = out->vec_bytes == 8;
        out->subtype = A64_LDST_SINGLE;
        out->idx_mode = A64_INDEX_OFFSET;
        return 0;
    }

    if (!out->is_vector && bit(insn, 24)) {
        int opc = bits(insn, 23, 22);
        uint64_t imm12 = bits(insn, 21, 10);
        out->Rd = bits(insn, 4, 0);
        out->Rn = bits(insn, 9, 5);
        out->is_signed = (opc & 2) != 0;
        if (out->is_signed)
            out->is_64bit = (opc == 2);
        // Scale immediate by size
        int scale = out->size;
        out->imm = imm12 << scale;
        out->subtype = A64_LDST_SINGLE;
        out->idx_mode = A64_INDEX_OFFSET;
        return 0;
    }

    return -1;
}

// SIMD and FP instructions
int a64_decode_simd_fp(uint32_t insn, a64_instr_t *out)
{
    int op0 = bits(insn, 28, 25);

    if ((insn & 0xbfe0fc00) == 0x0e000c00) {
        int imm5 = bits(insn, 20, 16);
        if (imm5 == 0)
            return -1;

        out->cat = A64_SIMD;
        out->subtype = A64_SIMD_DUP_GPR;
        out->is_vector = true;
        out->Rd = bits(insn, 4, 0);
        out->Rn = bits(insn, 9, 5);
        out->vec_bytes = 1 << __builtin_ctz((unsigned)imm5);
        out->vec_index = 0;
        return 0;
    }

    if ((insn & 0xbfe0fc00) == 0x0e003c00) {
        int imm5 = bits(insn, 20, 16);
        int element_shift;
        if (imm5 == 0)
            return -1;

        element_shift = __builtin_ctz((unsigned)imm5);
        out->cat = A64_SIMD;
        out->subtype = A64_SIMD_MOV_GPR_FROM_VEC;
        out->is_vector = true;
        out->Rd = bits(insn, 4, 0);
        out->Rn = bits(insn, 9, 5);
        out->vec_bytes = 1 << element_shift;
        out->vec_index = imm5 >> (element_shift + 1);
        out->is_64bit = bit(insn, 30);
        return 0;
    }

    // AdvSIMD modified immediate. This covers MOVI/MVNI vector constants used
    // by musl to initialize stack FILE objects in vdprintf before terminal
    // output is possible. Keep this after the narrower DUP/UMOV patterns:
    // the broad modified-immediate mask also matches DUP Vd.T, Rn forms such
    // as musl memset's `dup v0.16b, w1`.
    if (a64_is_advsimd_modified_immediate(insn)) {
        unsigned cmode = bits(insn, 15, 12);
        unsigned imm8 = (bits(insn, 18, 16) << 5) | bits(insn, 9, 5);

        out->cat = A64_SIMD;
        out->subtype = A64_SIMD_MOVI_IMM;
        out->is_vector = true;
        out->Rd = bits(insn, 4, 0);
        out->imm = imm8;
        out->imm_shift = cmode;
        out->op = bit(insn, 29);
        out->size = A64_SIZE_W;
        out->is_64bit = bit(insn, 30);
        return 0;
    }

    // Scalar floating-point move between general-purpose and FP/SIMD
    // registers. Linux guest startup uses this class while entering musl shell
    // code, so it must lower through TCTI rather than falling into the generic
    // unsupported FP bucket.
    if ((insn & 0xfffffc00u) == 0x1e270000u || // FMOV Sd, Wn
        (insn & 0xfffffc00u) == 0x1e260000u || // FMOV Wd, Sn
        (insn & 0xfffffc00u) == 0x9e670000u || // FMOV Dd, Xn
        (insn & 0xfffffc00u) == 0x9e660000u) { // FMOV Xd, Dn
        out->cat = bit(insn, 31) ? A64_SIMD2 : A64_SIMD;
        out->subtype = A64_SIMD_FMOV_GPR;
        out->Rd = bits(insn, 4, 0);
        out->Rn = bits(insn, 9, 5);
        out->vec_bytes = bit(insn, 31) ? 8 : 4;
        out->is_64bit = bit(insn, 31);
        out->op = bits(insn, 20, 16) == 7 ? 1 : 0; // 1: GPR -> FP, 0: FP -> GPR
        return 0;
    }

    // Floating point data processing (scalar)
    if (op0 == 0xE || op0 == 0xF) {
        (void)bit(insn, 31);            // M bit - part of encoding
        (void)bit(insn, 29);            // S bit - part of encoding
        int ptype = bits(insn, 23, 22); // 00=H, 01=S, 10=D
        (void)bits(insn, 15, 12);       // opcode - part of encoding
        int Rn = bits(insn, 9, 5);
        int Rd = bits(insn, 4, 0);

        // Size from precision type
        switch (ptype) {
        case 1:
            out->size = 2;
            break; // Single
        case 2:
            out->size = 3;
            break; // Double
        default:
            out->size = 1;
            break; // Half
        }

        out->Rd = Rd;
        out->Rn = Rn;
        out->Rm = bits(insn, 20, 16);
        out->Ra = bits(insn, 14, 10); // For FMA

        if ((insn & 0xff20fc00u) == 0x1e202800u) {
            out->subtype = A64_SIMD_SCALAR_FADD;
            out->vec_bytes = bit(insn, 22) ? 8 : 4;
            out->is_64bit = bit(insn, 22);
        }

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
int a64_decode_system(uint32_t insn, a64_instr_t *out)
{
    if (insn == 0xD5033F5F) { // CLREX
        out->subtype = A64_SYSTEM_CLREX;
        return 0;
    }

    // Barriers: DSB/DMB/ISB. These are architecturally required userspace
    // instructions on AArch64 and must lower through TCTI as ordering gadgets.
    if ((insn & 0xFFFFF01F) == 0xD503301F) {
        unsigned op2 = bits(insn, 7, 5);
        if (op2 >= 4 && op2 <= 6) {
            out->op = op2;
            out->imm = bits(insn, 11, 8);
            out->subtype = A64_SYSTEM_BARRIER;
            return 0;
        }
    }

    // HINT instructions (NOP, YIELD, WFE, WFI, SEV, etc.)
    // Base pattern: 0xD503201F = NOP.
    if ((insn & 0xFFFFF01F) == 0xD503201F) {
        out->subtype = A64_SYSTEM_HINT;
        out->imm = bits(insn, 11, 8); // CRm field selects hint type
        return 0;
    }

    // DC ZVA, Xt. Architecturally zeroes one cache block at the address in Xt.
    // Musl uses this in memset when DCZID_EL0 reports a 64-byte ZVA block.
    if ((insn & 0xffffffe0) == 0xd50b7420) {
        out->Rd = bits(insn, 4, 0);
        out->subtype = A64_SYSTEM_DC_ZVA;
        return 0;
    }

    // SVC - System call
    if ((insn & 0xFFC00000) == 0xD4000000) {
        int op = bits(insn, 23, 21);
        if (op == 1) { // SVC
            uint16_t imm16 = bits(insn, 20, 5);
            out->imm = imm16;
            out->op = op;
            out->subtype = A64_EXCEPTION;
            return 0;
        }
        if (op == 0) { // HVC
            uint16_t imm16 = bits(insn, 20, 5);
            out->imm = imm16;
            out->op = op;
            out->subtype = A64_EXCEPTION;
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
        out->subtype = A64_SYSTEM_MRS;
        return 0;
    }

    if ((insn & 0xFFD80000) == 0xD5100000) { // MSR (imm)
        int sysreg = bits(insn, 19, 5);
        int imm = bits(insn, 4, 0);
        out->sysreg = sysreg;
        out->imm = imm;
        out->subtype = A64_SYSTEM_MSR_IMM;
        return 0;
    }

    if ((insn & 0xFFD00000) == 0xD5100000) { // MSR (reg)
        int Rt = bits(insn, 4, 0);
        int sysreg = bits(insn, 19, 5);
        out->Rd = Rt;
        out->sysreg = sysreg;
        out->subtype = A64_SYSTEM_MSR_REG;
        return 0;
    }

    return -1;
}

// String helpers
const char *a64_category_name(a64_category_t cat)
{
    switch (cat) {
    case A64_RESERVED:
        return "RESERVED(0)";
    case A64_RESERVED1:
        return "RESERVED(1)";
    case A64_RESERVED2:
        return "RESERVED(2)";
    case A64_RESERVED3:
        return "RESERVED(3)";
    case A64_DP_REG:
        return "DP_REG(4)";
    case A64_DP_REG2:
        return "DP_REG(5)";
    case A64_DP_REG3:
        return "DP_REG(6)";
    case A64_DP_REG4:
        return "DP_REG(7)";
    case A64_SIMD0:
        return "SIMD(8)";
    case A64_DP_IMM:
        return "DP_IMM(9)";
    case A64_BRANCH:
        return "BRANCH(A)";
    case A64_BRANCH2:
        return "BRANCH(B)";
    case A64_LD_ST:
        return "LD_ST(C)";
    case A64_DP_IMM2:
        return "DP_IMM(D)";
    case A64_SIMD:
        return "SIMD(E)";
    case A64_SIMD2:
        return "SIMD(F)";
    default:
        return "UNKNOWN";
    }
}

const char *a64_cond_name(a64_cond_t cond)
{
    switch (cond) {
    case A64_EQ:
        return "eq";
    case A64_NE:
        return "ne";
    case A64_CS:
        return "cs";
    case A64_CC:
        return "cc";
    case A64_MI:
        return "mi";
    case A64_PL:
        return "pl";
    case A64_VS:
        return "vs";
    case A64_VC:
        return "vc";
    case A64_HI:
        return "hi";
    case A64_LS:
        return "ls";
    case A64_GE:
        return "ge";
    case A64_LT:
        return "lt";
    case A64_GT:
        return "gt";
    case A64_LE:
        return "le";
    case A64_AL:
        return "al";
    case A64_NV:
        return "nv";
    default:
        return "?";
    }
}
