/*
 * Unit tests for aarch64 instruction decoder
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "emu/aarch64/decode.h"

// Test result tracking
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    tests_run++; \
    test_##name(); \
    tests_passed++; \
    printf("OK\n"); \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAILED: %s at line %d\n", #cond, __LINE__); \
        tests_failed++; \
        tests_passed--; \
        return; \
    } \
} while(0)

#define ASSERT_EQ(a, b) ASSERT((a) == (b))

/*
 * Helper to encode instructions
 */
static uint32_t encode_movz(int rd, uint16_t imm, int hw, int sf) {
    // MOVZ: sf|1|0|1|0|0|1|0|1|hw|imm16|Rd
    return (sf << 31) | (0x5 << 26) | (0x2 << 23) |
           ((hw & 3) << 21) | ((imm & 0xFFFF) << 5) | (rd & 0x1F);
}

static uint32_t encode_movk(int rd, uint16_t imm, int hw, int sf) {
    // MOVK: sf|1|1|1|0|0|1|0|1|hw|imm16|Rd
    return (sf << 31) | (0x7 << 26) | (0x2 << 23) |
           ((hw & 3) << 21) | ((imm & 0xFFFF) << 5) | (rd & 0x1F);
}

static uint32_t encode_add_imm(int rd, int rn, int imm, int shift, int sf) {
    // ADD immediate: sf|0|0|1|0|0|0|1|sh|0|imm12|Rn|Rd
    return (sf << 31) | (0x11 << 24) | ((shift & 1) << 22) |
           ((imm & 0xFFF) << 10) | ((rn & 0x1F) << 5) | (rd & 0x1F);
}

static uint32_t encode_add_reg(int rd, int rn, int rm, int shift, int imm6, int sf) {
    // ADD register: sf|0|0|0|1|0|1|1|shift|0|Rm|imm6|Rn|Rd
    return (sf << 31) | (0xB << 24) | ((shift & 3) << 22) |
           ((rm & 0x1F) << 16) | ((imm6 & 0x3F) << 10) |
           ((rn & 0x1F) << 5) | (rd & 0x1F);
}

static uint32_t encode_b_imm(int64_t imm26) {
    // B unconditional: 0|0|0|1|0|1|imm26
    return (0x5 << 26) | ((imm26 >> 2) & 0x3FFFFFF);
}

static uint32_t encode_b_cond(int64_t imm19, int cond) {
    // B conditional: 0|1|0|1|0|1|0|0|imm19|0|cond
    return (0x54 << 24) | (((imm19 >> 2) & 0x7FFFF) << 5) | (cond & 0xF);
}

static uint32_t encode_cbz(int rt, int64_t imm19, int sf, int nz) {
    // CBZ/CBNZ: sf|0|1|1|0|1|0|nz|imm19|Rt
    return (sf << 31) | (0x34 << 24) | ((nz & 1) << 24) |
           (((imm19 >> 2) & 0x7FFFF) << 5) | (rt & 0x1F);
}

static uint32_t encode_ldr_imm(int rt, int rn, int imm12, int size, int is_vector) {
    // LDR (unsigned immediate): size|1|1|1|0|0|1|0|1|imm12|Rn|Rt
    return ((size & 3) << 30) | (0x39 << 24) | (0x1 << 22) |
           ((imm12 & 0xFFF) << 10) | ((rn & 0x1F) << 5) | (rt & 0x1F);
}

static uint32_t encode_svc(uint16_t imm) {
    // SVC: 1|1|0|1|0|1|0|0|0|0|0|0|0|0|0|0|0|0|0|0|0|imm16
    return (0xD4000001) | ((imm & 0xFFFF) << 5);
}

static uint32_t encode_nop(void) {
    // NOP: hint #0 encoded as MSR (system register)
    return 0xD503201F;
}

static uint32_t encode_ret(int rn) {
    // RET: 1|1|0|1|0|1|1|0|0|1|0|1|1|1|1|1|0|0|0|0|0|Rn|0|0|0|0|0
    return (0xD65F << 16) | 0x03C0 | ((rn & 0x1F) << 5);
}

/*
 * Test MOVZ/MOVN/MOVK decoding
 */
TEST(mov_immediate) {
    a64_instr_t instr;

    // Test MOVZ x5, #0x1234
    uint32_t insn = encode_movz(5, 0x1234, 0, 1);
    ASSERT_EQ(a64_decode(insn, &instr), 0);
    ASSERT_EQ(instr.cat, A64_DP_IMM);
    ASSERT_EQ(instr.Rd, 5);
    ASSERT_EQ(instr.imm, 0x1234);
    ASSERT_EQ(instr.is_64bit, 1);

    // Test MOVZ w3, #0xABCD, LSL #16
    insn = encode_movz(3, 0xABCD, 1, 0);
    ASSERT_EQ(a64_decode(insn, &instr), 0);
    ASSERT_EQ(instr.Rd, 3);
    ASSERT_EQ(instr.imm, 0xABCD0000);
    ASSERT_EQ(instr.is_64bit, 0);

    // Test MOVZ x0, #0xFFFF, LSL #48
    insn = encode_movz(0, 0xFFFF, 3, 1);
    ASSERT_EQ(a64_decode(insn, &instr), 0);
    ASSERT_EQ(instr.Rd, 0);
    ASSERT_EQ(instr.imm, 0xFFFF000000000000ULL);
}

/*
 * Test ADD immediate decoding
 */
TEST(add_immediate) {
    a64_instr_t instr;

    // ADD x1, x2, #0x100
    uint32_t insn = encode_add_imm(1, 2, 0x100, 0, 1);
    ASSERT_EQ(a64_decode(insn, &instr), 0);
    ASSERT_EQ(instr.cat, A64_DP_IMM);
    ASSERT_EQ(instr.Rd, 1);
    ASSERT_EQ(instr.Rn, 2);
    ASSERT_EQ(instr.imm, 0x100);
    ASSERT_EQ(instr.is_64bit, 1);

    // ADD x3, x4, #0x1000, LSL #12
    insn = encode_add_imm(3, 4, 0x1, 1, 1);
    ASSERT_EQ(a64_decode(insn, &instr), 0);
    ASSERT_EQ(instr.Rd, 3);
    ASSERT_EQ(instr.Rn, 4);
    ASSERT_EQ(instr.imm, 0x1000);  // 1 << 12
}

/*
 * Test ADD register decoding
 */
TEST(add_register) {
    a64_instr_t instr;

    // ADD x1, x2, x3
    uint32_t insn = encode_add_reg(1, 2, 3, 0, 0, 1);
    ASSERT_EQ(a64_decode(insn, &instr), 0);
    ASSERT_EQ(instr.cat, A64_DP_REG);
    ASSERT_EQ(instr.Rd, 1);
    ASSERT_EQ(instr.Rn, 2);
    ASSERT_EQ(instr.Rm, 3);
    ASSERT_EQ(instr.is_64bit, 1);

    // ADD x4, x5, x6, LSL #2
    insn = encode_add_reg(4, 5, 6, 0, 2, 1);
    ASSERT_EQ(a64_decode(insn, &instr), 0);
    ASSERT_EQ(instr.Rd, 4);
    ASSERT_EQ(instr.Rn, 5);
    ASSERT_EQ(instr.Rm, 6);
    ASSERT_EQ(instr.shift_type, 0);  // LSL
    ASSERT_EQ(instr.imm_shift, 2);
}

/*
 * Test branch decoding
 */
TEST(branch_unconditional) {
    a64_instr_t instr;

    // B #0x1000 (forward 4096 bytes)
    uint32_t insn = encode_b_imm(0x1000);
    ASSERT_EQ(a64_decode(insn, &instr), 0);
    ASSERT_EQ(instr.cat, A64_BRANCH);
    ASSERT_EQ(instr.subtype, A64_BRANCH_UNCOND);
    ASSERT_EQ(instr.imm, 0x1000);
}

TEST(branch_conditional) {
    a64_instr_t instr;

    // B.EQ #0x400
    uint32_t insn = encode_b_cond(0x400, A64_EQ);
    ASSERT_EQ(a64_decode(insn, &instr), 0);
    ASSERT_EQ(instr.cat, A64_BRANCH);
    ASSERT_EQ(instr.subtype, A64_BRANCH_COND);
    ASSERT_EQ(instr.cond, A64_EQ);

    // B.GT #0x800
    insn = encode_b_cond(0x800, A64_GT);
    ASSERT_EQ(a64_decode(insn, &instr), 0);
    ASSERT_EQ(instr.cond, A64_GT);
}

TEST(compare_and_branch) {
    a64_instr_t instr;

    // CBZ x5, #0x200
    uint32_t insn = encode_cbz(5, 0x200, 1, 0);
    ASSERT_EQ(a64_decode(insn, &instr), 0);
    ASSERT_EQ(instr.cat, A64_BRANCH);
    ASSERT_EQ(instr.subtype, A64_BRANCH_CMP);
    ASSERT_EQ(instr.Rd, 5);
    ASSERT_EQ(instr.imm, 0x200);
    ASSERT_EQ(instr.is_64bit, 1);

    // CBNZ w3, #0x100
    insn = encode_cbz(3, 0x100, 0, 1);
    ASSERT_EQ(a64_decode(insn, &instr), 0);
    ASSERT_EQ(instr.Rd, 3);
    ASSERT_EQ(instr.is_64bit, 0);
}

/*
 * Test load/store decoding
 */
TEST(load_immediate) {
    a64_instr_t instr;

    // LDR x1, [x2, #0x80]
    uint32_t insn = encode_ldr_imm(1, 2, 0x80, 3, 0);  // size=3 for 64-bit
    ASSERT_EQ(a64_decode(insn, &instr), 0);
    ASSERT_EQ(instr.cat, A64_LD_ST);
    ASSERT_EQ(instr.Rd, 1);
    ASSERT_EQ(instr.Rn, 2);
    ASSERT_EQ(instr.size, 3);  // 64-bit
}

/*
 * Test system instructions
 */
TEST(system_svc) {
    a64_instr_t instr;

    // SVC #0
    uint32_t insn = encode_svc(0);
    ASSERT_EQ(a64_decode(insn, &instr), 0);
    ASSERT_EQ(instr.cat, A64_BRANCH);
    ASSERT_EQ(instr.imm, 0);

    // SVC #93 (exit syscall on aarch64)
    insn = encode_svc(93);
    ASSERT_EQ(a64_decode(insn, &instr), 0);
    ASSERT_EQ(instr.imm, 93);
}

/*
 * Test undefined instruction handling
 */
TEST(undefined_instructions) {
    a64_instr_t instr;

    // Reserved encoding (all zeros in category field)
    uint32_t insn = 0x00000000;
    ASSERT_EQ(a64_decode(insn, &instr), -1);

    // Another undefined encoding
    insn = 0xFFFFFFFF;
    ASSERT_EQ(a64_decode(insn, &instr), -1);
}

/*
 * Test NOP
 */
TEST(nop_instruction) {
    a64_instr_t instr;

    uint32_t insn = encode_nop();
    ASSERT_EQ(a64_decode(insn, &instr), 0);
    // NOP is a system instruction with specific encoding
    ASSERT_EQ(instr.cat, A64_BRANCH);
}

/*
 * Test RET
 */
TEST(ret_instruction) {
    a64_instr_t instr;

    // RET
    uint32_t insn = encode_ret(30);  // RET using x30 (lr)
    ASSERT_EQ(a64_decode(insn, &instr), 0);
    ASSERT_EQ(instr.cat, A64_BRANCH);
    ASSERT_EQ(instr.subtype, A64_BRANCH_REG);
    ASSERT_EQ(instr.Rn, 30);
}

/*
 * Fuzz test - ensure decoder doesn't crash on random inputs
 */
TEST(fuzz_random) {
    a64_instr_t instr;
    srand(12345);  // Deterministic seed

    for (int i = 0; i < 10000; i++) {
        uint32_t random_insn = (uint32_t)rand();
        // Should either decode or return -1, never crash
        a64_decode(random_insn, &instr);
    }
}

/*
 * Test specific real instruction encodings
 */
TEST(real_instructions) {
    a64_instr_t instr;

    // Examples from actual aarch64 code

    // d503201f = NOP
    ASSERT_EQ(a64_decode(0xd503201f, &instr), 0);

    // d50330df = ISB
    ASSERT_EQ(a64_decode(0xd50330df, &instr), 0);

    // d5033fdf = DSB SY
    ASSERT_EQ(a64_decode(0xd5033fdf, &instr), 0);

    // d5033bdf = DMB SY
    ASSERT_EQ(a64_decode(0xd5033bdf, &instr), 0);

    // d50342df = MSR DAZEL, #0 (hint)
    ASSERT_EQ(a64_decode(0xd50342df, &instr), 0);

    // d65f03c0 = RET
    ASSERT_EQ(a64_decode(0xd65f03c0, &instr), 0);

    // d2800000 = MOVZ x0, #0
    ASSERT_EQ(a64_decode(0xd2800000, &instr), 0);
    ASSERT_EQ(instr.Rd, 0);
    ASSERT_EQ(instr.imm, 0);

    // 94000000 = BL #0 (offset 0)
    ASSERT_EQ(a64_decode(0x94000000, &instr), 0);

    // b5000000 = CBNZ x0, #0
    ASSERT_EQ(a64_decode(0xb5000000, &instr), 0);
}

/*
 * Main test runner
 */
int main(void) {
    printf("aarch64 Decoder Unit Tests\n");
    printf("==========================\n\n");

    printf("Data Processing - Immediate:\n");
    RUN_TEST(mov_immediate);
    RUN_TEST(add_immediate);

    printf("\nData Processing - Register:\n");
    RUN_TEST(add_register);

    printf("\nBranch Instructions:\n");
    RUN_TEST(branch_unconditional);
    RUN_TEST(branch_conditional);
    RUN_TEST(compare_and_branch);

    printf("\nLoad/Store Instructions:\n");
    RUN_TEST(load_immediate);

    printf("\nSystem Instructions:\n");
    RUN_TEST(system_svc);
    RUN_TEST(nop_instruction);
    RUN_TEST(ret_instruction);

    printf("\nError Handling:\n");
    RUN_TEST(undefined_instructions);

    printf("\nReal Instruction Encodings:\n");
    RUN_TEST(real_instructions);

    printf("\nFuzz Testing:\n");
    RUN_TEST(fuzz_random);

    printf("\n==========================\n");
    printf("Results: %d run, %d passed, %d failed\n",
           tests_run, tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
