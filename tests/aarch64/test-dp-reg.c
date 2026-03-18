/*
 * Data processing - register tests
 * Tests: ADD, SUB, AND, ORR, EOR, CMP
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "test.h"
#include "misc.h"
#include "emu/aarch64/cpu.h"
#include "emu/aarch64/decode.h"

/* Test ADD Xd, Xn, Xm (64-bit) */
static int test_add_reg_64(void)
{
    TEST_START("add_reg_64");

    uint32_t insn = 0x8B020020;  // ADD X0, X1, X2
    a64_instr_t instr;

    int ret = a64_decode(insn, &instr);
    ASSERT_EQ(ret, 0);
    ASSERT_EQ(instr.Rd, 0);
    ASSERT_EQ(instr.Rn, 1);
    ASSERT_EQ(instr.Rm, 2);
    ASSERT_EQ(instr.is_64bit, 1);

    TEST_PASS();
}

/* Test ADD Wd, Wn, Wm (32-bit) */
static int test_add_reg_32(void)
{
    TEST_START("add_reg_32");

    uint32_t insn = 0x0B020020;  // ADD W0, W1, W2
    a64_instr_t instr;

    int ret = a64_decode(insn, &instr);
    ASSERT_EQ(ret, 0);
    ASSERT_EQ(instr.is_64bit, 0);

    TEST_PASS();
}

/* Test ADD with LSL shift */
static int test_add_lsl(void)
{
    TEST_START("add_lsl");

    uint32_t insn = 0x8B020820;  // ADD X0, X1, X2, LSL #2
    a64_instr_t instr;

    int ret = a64_decode(insn, &instr);
    ASSERT_EQ(ret, 0);

    TEST_PASS();
}

/* Test SUB Xd, Xn, Xm */
static int test_sub_reg_64(void)
{
    TEST_START("sub_reg_64");

    uint32_t insn = 0xCB020020;  // SUB X0, X1, X2
    a64_instr_t instr;

    int ret = a64_decode(insn, &instr);
    ASSERT_EQ(ret, 0);
    ASSERT_EQ(instr.Rd, 0);
    ASSERT_EQ(instr.Rn, 1);
    ASSERT_EQ(instr.Rm, 2);

    TEST_PASS();
}

/* Test CMP (compare = SUBS with XZR destination) */
static int test_cmp_reg(void)
{
    TEST_START("cmp_reg");

    uint32_t insn = 0xEB02001F;  // CMP X0, X2
    a64_instr_t instr;

    int ret = a64_decode(insn, &instr);
    ASSERT_EQ(ret, 0);
    ASSERT_EQ(instr.Rn, 0);
    ASSERT_EQ(instr.Rm, 2);

    TEST_PASS();
}

/* Test AND Xd, Xn, Xm */
static int test_and_reg(void)
{
    TEST_START("and_reg");

    uint32_t insn = 0x8A020020;  // AND X0, X1, X2
    a64_instr_t instr;

    int ret = a64_decode(insn, &instr);
    ASSERT_EQ(ret, 0);

    TEST_PASS();
}

/* Test ORR Xd, Xn, Xm */
static int test_orr_reg(void)
{
    TEST_START("orr_reg");

    uint32_t insn = 0xAA020020;  // ORR X0, X1, X2
    a64_instr_t instr;

    int ret = a64_decode(insn, &instr);
    ASSERT_EQ(ret, 0);

    TEST_PASS();
}

/* Test EOR Xd, Xn, Xm */
static int test_eor_reg(void)
{
    TEST_START("eor_reg");

    uint32_t insn = 0xCA020020;  // EOR X0, X1, X2
    a64_instr_t instr;

    int ret = a64_decode(insn, &instr);
    ASSERT_EQ(ret, 0);

    TEST_PASS();
}

/* Test ADD all register combinations */
static int test_add_all_regs(void)
{
    TEST_START("add_all_regs");

    for (int rd = 0; rd < 31; rd++) {
        for (int rn = 0; rn < 5; rn++) {
            uint32_t insn = 0x8B000020 | (rn << 5) | rd;
            a64_instr_t instr;
            int ret = a64_decode(insn, &instr);
            if (ret != 0) {
                TEST_FAIL("decode failed");
            }
        }
    }

    TEST_PASS();
}

int main(void)
{
    int total = 0;
    int failures = 0;

    printf("Data Processing - Register Tests\n");
    printf("================================\n");

    RUN_TEST(test_add_reg_64);
    RUN_TEST(test_add_reg_32);
    RUN_TEST(test_add_lsl);
    RUN_TEST(test_sub_reg_64);
    RUN_TEST(test_cmp_reg);
    RUN_TEST(test_and_reg);
    RUN_TEST(test_orr_reg);
    RUN_TEST(test_eor_reg);
    RUN_TEST(test_add_all_regs);

    PRINT_SUMMARY("test-dp-reg");
}
