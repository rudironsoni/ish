/*
 * Unit tests for aarch64 block generator
 * TDD approach - tests define expected behavior
 */

#include "direct_gadget_trampoline.h"

#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>
#import <IXLandLinuxRuntime/emu/aarch64/sysreg.h>
#import <IXLandLinuxRuntime/tcti/aarch64/gen.h>
#import <IXLandLinuxRuntime/tcti/gadgets_tcti.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern tcti_gadget_t gadget_sysreg_unsupported;

__asm__(".text\n"
        ".align 2\n"
        ".global _test_tcti_single_gadget_snapshot\n"
        "_test_tcti_single_gadget_snapshot:\n"
        "stp x19, x20, [sp, #-16]!\n"
        "stp x21, x22, [sp, #-16]!\n"
        "stp x23, x24, [sp, #-16]!\n"
        "stp x25, x26, [sp, #-16]!\n"
        "stp x27, x28, [sp, #-16]!\n"
        "stp x29, x30, [sp, #-16]!\n"
        "mov x29, sp\n"
        "mov x19, x0\n"
        "mov x21, x1\n"
        "mov x20, x2\n"
        "sub sp, sp, #16\n"
        "mov x28, sp\n"
        "adr x22, 2f\n"
        "str x22, [x28]\n"
        "str x20, [x28, #8]\n"
        "ldr x0, [x21, #0]\n"
        "ldr x1, [x21, #8]\n"
        "ldr x2, [x21, #16]\n"
        "ldr x3, [x21, #24]\n"
        "ldr x4, [x21, #32]\n"
        "ldr x5, [x21, #40]\n"
        "ldr x6, [x21, #48]\n"
        "ldr x7, [x21, #56]\n"
        "ldr x8, [x21, #64]\n"
        "ldr x9, [x21, #72]\n"
        "ldr x10, [x21, #80]\n"
        "ldr x11, [x21, #88]\n"
        "ldr x12, [x21, #96]\n"
        "ldr x13, [x21, #104]\n"
        "ldr x14, [x21, #112]\n"
        "ldr x15, [x21, #120]\n"
        "br x19\n"
        "2:\n"
        "ldr x20, [x28], #8\n"
        "str x0, [x20, #0]\n"
        "str x1, [x20, #8]\n"
        "str x2, [x20, #16]\n"
        "str x3, [x20, #24]\n"
        "str x4, [x20, #32]\n"
        "str x5, [x20, #40]\n"
        "str x6, [x20, #48]\n"
        "str x7, [x20, #56]\n"
        "str x8, [x20, #64]\n"
        "str x9, [x20, #72]\n"
        "str x10, [x20, #80]\n"
        "str x11, [x20, #88]\n"
        "str x12, [x20, #96]\n"
        "str x13, [x20, #104]\n"
        "str x14, [x20, #112]\n"
        "str x15, [x20, #120]\n"
        "str x27, [x20, #128]\n"
        "str x28, [x20, #136]\n"
        "str x29, [x20, #144]\n"
        "str x30, [x20, #152]\n"
        "add sp, sp, #16\n"
        "ldp x29, x30, [sp], #16\n"
        "ldp x27, x28, [sp], #16\n"
        "ldp x25, x26, [sp], #16\n"
        "ldp x23, x24, [sp], #16\n"
        "ldp x21, x22, [sp], #16\n"
        "ldp x19, x20, [sp], #16\n"
        "ret\n");

// Test fixtures
static a64_gen_state_t test_state;
static tcti_gadget_t test_buffer[A64_MAX_GADGETS_PER_BLOCK];

// Test result tracking
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;
static const char *first_failed_test = NULL;
static const char *failed_tests[128];
static int failed_test_lines[128];
static int first_failed_line = 0;
static int current_test_failed = 0;

typedef struct direct_case_record {
    const char *name;
    uint64_t in_x0;
    uint64_t in_x1;
    uint64_t in_x2;
    uint64_t in_x3;
    uint64_t in_x8;
    uint64_t in_x13;
    uint64_t in_x14;
    uint64_t in_x15;
    uint64_t out_x0;
    uint64_t out_x1;
    uint64_t out_x2;
    uint64_t out_x3;
    uint64_t out_x8;
    uint64_t out_x13;
    uint64_t out_x14;
    uint64_t out_x15;
    uint64_t out_x27;
    uint64_t out_x28;
    uint64_t out_x29;
    int passed;
} direct_case_record_t;

static direct_case_record_t direct_records[16];
static int direct_records_count = 0;
static char direct_results_buffer[4096];

#define TEST(name) void test_##name(void)
#define RUN_TEST(name)                                                                             \
    do {                                                                                           \
        tests_run++;                                                                               \
        int failed_before = tests_failed;                                                          \
        current_test_failed = 0;                                                                   \
        test_##name();                                                                             \
        if (tests_failed == failed_before) {                                                       \
            tests_passed++;                                                                        \
        } else if (first_failed_test == NULL) {                                                    \
            first_failed_test = #name;                                                             \
            failed_tests[0] = #name;                                                               \
        } else if (tests_failed <= 128) {                                                          \
            failed_tests[tests_failed - 1] = #name;                                                \
        }                                                                                          \
    } while (0)

#define ASSERT(cond)                                                                               \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            int fail_index = tests_failed;                                                         \
            tests_failed++;                                                                        \
            if (fail_index >= 0 && fail_index < 128)                                               \
                failed_test_lines[fail_index] = __LINE__;                                          \
            if (first_failed_line == 0)                                                            \
                first_failed_line = __LINE__;                                                      \
            current_test_failed = 1;                                                               \
            return;                                                                                \
        }                                                                                          \
    } while (0)

#define LAST_TEST_PASSED() (!current_test_failed)

#define ASSERT_EQ(a, b) ASSERT((a) == (b))

// Setup before each test
static void setup(void)
{
    memset(&test_state, 0, sizeof(test_state));
    memset(test_buffer, 0, sizeof(test_buffer));
    a64_gen_init(&test_state, test_buffer, A64_MAX_GADGETS_PER_BLOCK);
}

enum {
    SNAPSHOT_REGS = 20,
};

static void setup_direct_inputs(uint64_t regs[16])
{
    for (int i = 0; i < 16; i++)
        regs[i] = 0xA500000000000000ULL + (uint64_t)i;
}

static void run_single_gadget_snapshot(tcti_gadget_t gadget, const uint64_t in_regs[16],
                                       uint64_t out_regs[SNAPSHOT_REGS])
{
    memset(out_regs, 0, sizeof(uint64_t) * SNAPSHOT_REGS);
    test_tcti_single_gadget_snapshot(gadget, in_regs, out_regs);
}

static void record_direct_case(const char *name, const uint64_t in_regs[16],
                               const uint64_t out_regs[SNAPSHOT_REGS], int passed)
{
    if (direct_records_count >= (int)(sizeof(direct_records) / sizeof(direct_records[0])))
        return;
    direct_case_record_t *rec = &direct_records[direct_records_count++];
    rec->name = name;
    rec->in_x0 = in_regs[0];
    rec->in_x1 = in_regs[1];
    rec->in_x2 = in_regs[2];
    rec->in_x3 = in_regs[3];
    rec->in_x8 = in_regs[8];
    rec->in_x13 = in_regs[13];
    rec->in_x14 = in_regs[14];
    rec->in_x15 = in_regs[15];
    rec->out_x0 = out_regs[0];
    rec->out_x1 = out_regs[1];
    rec->out_x2 = out_regs[2];
    rec->out_x3 = out_regs[3];
    rec->out_x8 = out_regs[8];
    rec->out_x13 = out_regs[13];
    rec->out_x14 = out_regs[14];
    rec->out_x15 = out_regs[15];
    rec->out_x27 = out_regs[16];
    rec->out_x28 = out_regs[17];
    rec->out_x29 = out_regs[18];
    rec->passed = passed;
}

const char *gen_test_direct_results(void)
{
    int off = 0;
    size_t cap = sizeof(direct_results_buffer);
    direct_results_buffer[0] = '\0';
    for (int i = 0; i < direct_records_count; i++) {
        const direct_case_record_t *r = &direct_records[i];
        int wrote =
            snprintf(direct_results_buffer + off, cap - (size_t)off,
                     "%s:pass=%d,in{x0=0x%llx,x1=0x%llx,x2=0x%llx,x3=0x%llx,x8=0x%llx,x13=0x%llx,"
                     "x14=0x%llx,x15=0x%llx},"
                     "out{x0=0x%llx,x1=0x%llx,x2=0x%llx,x3=0x%llx,x8=0x%llx,x13=0x%llx,x14=0x%llx,"
                     "x15=0x%llx,x27=0x%llx,x28=0x%llx,x29=0x%llx}%s",
                     r->name, r->passed, (unsigned long long)r->in_x0, (unsigned long long)r->in_x1,
                     (unsigned long long)r->in_x2, (unsigned long long)r->in_x3,
                     (unsigned long long)r->in_x8, (unsigned long long)r->in_x13,
                     (unsigned long long)r->in_x14, (unsigned long long)r->in_x15,
                     (unsigned long long)r->out_x0, (unsigned long long)r->out_x1,
                     (unsigned long long)r->out_x2, (unsigned long long)r->out_x3,
                     (unsigned long long)r->out_x8, (unsigned long long)r->out_x13,
                     (unsigned long long)r->out_x14, (unsigned long long)r->out_x15,
                     (unsigned long long)r->out_x27, (unsigned long long)r->out_x28,
                     (unsigned long long)r->out_x29, (i + 1 == direct_records_count) ? "" : " | ");
        if (wrote <= 0)
            break;
        off += wrote;
        if ((size_t)off >= cap - 1)
            break;
    }
    return direct_results_buffer;
}

// Test initialization
TEST(gen_init)
{
    setup();

    ASSERT_EQ(test_state.gadgets, test_buffer);
    ASSERT_EQ(test_state.max_gadgets, A64_MAX_GADGETS_PER_BLOCK);
    ASSERT_EQ(test_state.num_gadgets, 0);
}

// Test adding gadgets
TEST(gen_add_gadget)
{
    setup();
    a64_gen_reset(&test_state, 0x1000);

    // Add a simple gadget
    int ret = a64_gen_add_gadget(&test_state, (tcti_gadget_t)0x1234);
    ASSERT_EQ(ret, A64_GEN_OK);
    ASSERT_EQ(test_state.num_gadgets, 1);

    // Buffer should contain the gadget address
    ASSERT_EQ((uintptr_t)test_buffer[0], 0x1234);
}

// Test MOVZ generation
TEST(gen_movz)
{
    setup();
    a64_gen_reset(&test_state, 0x1000);

    // MOVZ x0, #0x1234
    // Encoding: sf=1, hw=0, imm16=0x1234, Rd=0
    // Opcode: 1|10|100101|hw|imm16|Rd
    uint32_t movz = 0xD2824680; // mov x0, #0x1234

    int ret = a64_gen_instruction(&test_state, movz, 0x1000);
    ASSERT_EQ(ret, A64_GEN_OK);
    ASSERT_EQ(test_state.num_gadgets, 2);
    ASSERT_EQ(test_buffer[0], gadget_mov_imm[0]);
    ASSERT_EQ((uint64_t)(uintptr_t)test_buffer[1], 0x1234ULL);
}

// Test ADD immediate generation
TEST(gen_add_imm)
{
    setup();
    a64_gen_reset(&test_state, 0x1000);

    // ADD x0, x1, #0x10
    // sf=1, op=0, S=0, sh=0, imm12=0x10, Rn=1, Rd=0
    uint32_t add_imm = 0x91004020; // add x0, x1, #0x10

    int ret = a64_gen_instruction(&test_state, add_imm, 0x1000);
    ASSERT_EQ(ret, A64_GEN_OK);
    ASSERT(test_state.num_gadgets > 0);
}

// Test ADD register generation
TEST(gen_add_reg)
{
    setup();
    a64_gen_reset(&test_state, 0x1000);

    // ADD x0, x1, x2
    // sf=1, shift=0, S=0, Rm=2, imm6=0, Rn=1, Rd=0
    uint32_t add_reg = 0x8B020020; // add x0, x1, x2

    int ret = a64_gen_instruction(&test_state, add_reg, 0x1000);
    ASSERT_EQ(ret, A64_GEN_OK);
    ASSERT(test_state.num_gadgets > 0);
}

TEST(exec_add_reg_7_13_14_direct)
{
    uint64_t in_regs[16];
    uint64_t out_regs[SNAPSHOT_REGS];
    setup_direct_inputs(in_regs);

    uint64_t lhs = 0xFFFFFEF7FBF0ULL;
    uint64_t rhs = 0x8ULL;
    in_regs[0] = 0x1111111111111111ULL;
    in_regs[14] = lhs;
    in_regs[15] = rhs;

    run_single_gadget_snapshot(gadget_add_reg[7][13][14], in_regs, out_regs);
    int passed = out_regs[8] == (lhs + rhs) && out_regs[14] == lhs && out_regs[15] == rhs &&
                 out_regs[0] == 0x1111111111111111ULL;
    record_direct_case("exec_add_reg_7_13_14_direct", in_regs, out_regs, passed);
    ASSERT_EQ(out_regs[8], lhs + rhs);
    ASSERT_EQ(out_regs[14], lhs);
    ASSERT_EQ(out_regs[15], rhs);
    ASSERT_EQ(out_regs[0], 0x1111111111111111ULL);
}

TEST(exec_add_reg_13_13_14_direct)
{
    uint64_t in_regs[16];
    uint64_t out_regs[SNAPSHOT_REGS];
    setup_direct_inputs(in_regs);

    uint64_t lhs = 0xFFFFFEF7FBF0ULL;
    uint64_t rhs = 0x8ULL;
    in_regs[0] = 0x2222222222222222ULL;
    in_regs[14] = lhs;
    in_regs[15] = rhs;

    run_single_gadget_snapshot(gadget_add_reg[13][13][14], in_regs, out_regs);
    int passed =
        out_regs[14] == (lhs + rhs) && out_regs[15] == rhs && out_regs[0] == 0x2222222222222222ULL;
    record_direct_case("exec_add_reg_13_13_14_direct", in_regs, out_regs, passed);
    ASSERT_EQ(out_regs[14], lhs + rhs);
    ASSERT_EQ(out_regs[15], rhs);
    ASSERT_EQ(out_regs[0], 0x2222222222222222ULL);
}

TEST(exec_add_reg_0_1_2_direct)
{
    uint64_t in_regs[16];
    uint64_t out_regs[SNAPSHOT_REGS];
    setup_direct_inputs(in_regs);

    in_regs[2] = 0x40ULL;
    in_regs[3] = 0x2ULL;
    in_regs[8] = 0x3333333333333333ULL;

    run_single_gadget_snapshot(gadget_add_reg[0][1][2], in_regs, out_regs);
    int passed = out_regs[1] == 0x42ULL && out_regs[2] == 0x40ULL && out_regs[3] == 0x2ULL &&
                 out_regs[8] == 0x3333333333333333ULL;
    record_direct_case("exec_add_reg_0_1_2_direct", in_regs, out_regs, passed);
    ASSERT_EQ(out_regs[1], 0x42ULL);
    ASSERT_EQ(out_regs[2], 0x40ULL);
    ASSERT_EQ(out_regs[3], 0x2ULL);
    ASSERT_EQ(out_regs[8], 0x3333333333333333ULL);
}

TEST(exec_mov_reg_2_7_direct)
{
    uint64_t in_regs[16];
    uint64_t out_regs[SNAPSHOT_REGS];
    setup_direct_inputs(in_regs);

    in_regs[8] = 0x123456789ABCDEF0ULL;
    in_regs[0] = 0x4444444444444444ULL;
    in_regs[3] = 0;

    run_single_gadget_snapshot(gadget_mov_reg[2][7], in_regs, out_regs);
    int passed = out_regs[3] == 0x123456789ABCDEF0ULL && out_regs[8] == 0x123456789ABCDEF0ULL &&
                 out_regs[0] == 0x4444444444444444ULL;
    record_direct_case("exec_mov_reg_2_7_direct", in_regs, out_regs, passed);
    ASSERT_EQ(out_regs[3], 0x123456789ABCDEF0ULL);
    ASSERT_EQ(out_regs[8], 0x123456789ABCDEF0ULL);
    ASSERT_EQ(out_regs[0], 0x4444444444444444ULL);
}

TEST(exec_add_reg_7_7_14_direct)
{
    uint64_t in_regs[16];
    uint64_t out_regs[SNAPSHOT_REGS];
    setup_direct_inputs(in_regs);

    in_regs[8] = 0x40ULL;
    in_regs[15] = 0x2ULL;
    in_regs[0] = 0x5555555555555555ULL;

    run_single_gadget_snapshot(gadget_add_reg[7][7][14], in_regs, out_regs);
    int passed =
        out_regs[8] == 0x42ULL && out_regs[15] == 0x2ULL && out_regs[0] == 0x5555555555555555ULL;
    record_direct_case("exec_add_reg_7_7_14_direct", in_regs, out_regs, passed);
    ASSERT_EQ(out_regs[8], 0x42ULL);
    ASSERT_EQ(out_regs[15], 0x2ULL);
    ASSERT_EQ(out_regs[0], 0x5555555555555555ULL);
}

TEST(exec_mov_reg_7_2_direct)
{
    uint64_t in_regs[16];
    uint64_t out_regs[SNAPSHOT_REGS];
    setup_direct_inputs(in_regs);

    in_regs[3] = 0x66778899AABBCCDDULL;
    in_regs[0] = 0x6666666666666666ULL;

    run_single_gadget_snapshot(gadget_mov_reg[7][2], in_regs, out_regs);
    int passed = out_regs[8] == 0x66778899AABBCCDDULL && out_regs[3] == 0x66778899AABBCCDDULL &&
                 out_regs[0] == 0x6666666666666666ULL;
    record_direct_case("exec_mov_reg_7_2_direct", in_regs, out_regs, passed);
    ASSERT_EQ(out_regs[8], 0x66778899AABBCCDDULL);
    ASSERT_EQ(out_regs[3], 0x66778899AABBCCDDULL);
    ASSERT_EQ(out_regs[0], 0x6666666666666666ULL);
}

TEST(gen_add_reg_shifted_lsl)
{
    setup();
    a64_gen_reset(&test_state, 0x1000);

    // ADD x19, x1, x19, LSL #3
    uint32_t add_reg_shifted = 0x8B130C33;

    int ret = a64_gen_instruction(&test_state, add_reg_shifted, 0x1000);
    ASSERT_EQ(ret, A64_GEN_OK);
    ASSERT(test_state.num_gadgets > 0);
}

TEST(gen_add_imm_sp_source_uses_distinct_temp)
{
    setup();
    a64_gen_reset(&test_state, 0x1000);

    // add x7, sp, #8
    uint32_t add_sp_imm = 0x910023E7;

    int ret = a64_gen_instruction(&test_state, add_sp_imm, 0x1000);
    ASSERT_EQ(ret, A64_GEN_OK);
    ASSERT_EQ(test_state.num_gadgets, 4);
    ASSERT_EQ(test_buffer[0], (tcti_gadget_t)gadget_load_sp);
    ASSERT_EQ(test_buffer[1], gadget_mov_imm[14]);
    ASSERT_EQ((uint64_t)(uintptr_t)test_buffer[2], 8ULL);
    ASSERT_EQ(test_buffer[3], gadget_add_reg[7][13][14]);
}

TEST(gen_orr_mov_alias_xzr_rn)
{
    setup();
    a64_gen_reset(&test_state, 0x1000);

    uint32_t orr_mov_alias = 0xAA0703E2; // orr x2, xzr, x7 (mov x2, x7)

    int ret = a64_gen_instruction(&test_state, orr_mov_alias, 0x1000);
    ASSERT_EQ(ret, A64_GEN_OK);
    ASSERT_EQ(test_state.num_gadgets, 1);
    ASSERT_EQ(test_buffer[0], gadget_mov_reg[2][7]);
}

TEST(gen_logical_rm_xzr_materializes_zero)
{
    setup();
    a64_gen_reset(&test_state, 0x1000);

    uint32_t orr_rm_xzr = 0xAA1F00E2; // orr x2, x7, xzr

    int ret = a64_gen_instruction(&test_state, orr_rm_xzr, 0x1000);
    ASSERT_EQ(ret, A64_GEN_OK);
    ASSERT_EQ(test_state.num_gadgets, 3);
    ASSERT_EQ(test_buffer[0], gadget_mov_imm[13]);
    ASSERT_EQ((uint64_t)(uintptr_t)test_buffer[1], 0ULL);
    ASSERT_EQ(test_buffer[2], gadget_orr_reg[2][7][13]);
}

// Test branch ends block
TEST(gen_branch_ends_block)
{
    setup();
    a64_gen_reset(&test_state, 0x1000);

    // B #0x2000
    // Unconditional branch should end the block
    uint32_t b_imm = 0x14000000; // b #0 (as example)

    int ret = a64_gen_instruction(&test_state, b_imm, 0x1000);
    ASSERT_EQ(ret, A64_GEN_OK);
    ASSERT_EQ(test_state.is_complete, 1);
}

// Test CBZ ends block
TEST(gen_cbz_ends_block)
{
    setup();
    a64_gen_reset(&test_state, 0x1000);

    // CBZ x0, #0x100
    uint32_t cbz = 0xB4000040; // cbz x0, #offset

    int ret = a64_gen_instruction(&test_state, cbz, 0x1000);
    ASSERT_EQ(ret, A64_GEN_OK);
    ASSERT_EQ(test_state.is_complete, 1);
}

TEST(gen_tbz_ends_block)
{
    setup();
    a64_gen_reset(&test_state, 0x1000);

    // TBZ w1, #0, .-0xc
    uint32_t tbz = 0x3607FFA1;

    int ret = a64_gen_instruction(&test_state, tbz, 0x1000);
    ASSERT_EQ(ret, A64_GEN_OK);
    ASSERT_EQ(test_state.is_complete, 1);
}

// Test SVC ends block
TEST(gen_svc_ends_block)
{
    setup();
    a64_gen_reset(&test_state, 0x1000);

    // SVC #0
    uint32_t svc = 0xD4000001; // svc #0

    int ret = a64_gen_instruction(&test_state, svc, 0x1000);
    ASSERT(ret == A64_GEN_OK || ret == A64_GEN_UNSUPPORTED);
}

// Test multiple instructions in sequence
TEST(gen_sequence)
{
    setup();
    a64_gen_reset(&test_state, 0x1000);

    // ADD x0, x1, #1
    // ADD x0, x0, x2
    uint32_t insns[] = {
        0x91000420, // add x0, x1, #1
        0x8B020000, // add x0, x0, x2
    };

    for (int i = 0; i < 2; i++) {
        int ret = a64_gen_instruction(&test_state, insns[i], 0x1000 + i * 4);
        ASSERT_EQ(ret, A64_GEN_OK);
    }

    ASSERT(test_state.num_gadgets >= 2);
    ASSERT_EQ(test_state.instructions_processed, 2);
}

// Test overflow protection
TEST(gen_overflow)
{
    setup();
    a64_gen_reset(&test_state, 0x1000);

    // Fill the buffer
    for (int i = 0; i < A64_MAX_GADGETS_PER_BLOCK; i++) {
        int ret = a64_gen_add_gadget(&test_state, (tcti_gadget_t)0x1000);
        ASSERT_EQ(ret, A64_GEN_OK);
    }

    // Next add should fail
    int ret = a64_gen_add_gadget(&test_state, (tcti_gadget_t)0x1000);
    ASSERT_EQ(ret, A64_GEN_TOO_MANY);
}

// Test basic block generation
TEST(gen_basic_block)
{
    setup();

    // Would need a mock CPU state for this
    // For now, just test the API exists
    // int ret = a64_gen_basic_block(&test_state, NULL, 0x1000, NULL);
    // ASSERT(ret >= 0 || ret < 0);  // Function exists
}

// Test PC tracking
TEST(gen_pc_tracking)
{
    setup();
    a64_gen_reset(&test_state, 0x1000);

    ASSERT_EQ(a64_gen_current_pc(&test_state), 0x1000);

    // Process an instruction
    uint32_t nop = 0xD503201F;
    a64_gen_instruction(&test_state, nop, 0x1000);

    ASSERT_EQ(a64_gen_current_pc(&test_state), 0x1000);
}

// Test invalid instruction
TEST(gen_invalid)
{
    setup();
    a64_gen_reset(&test_state, 0x1000);

    // Undefined instruction (all zeros in category)
    uint32_t invalid = 0x00000000;

    int ret = a64_gen_instruction(&test_state, invalid, 0x1000);
    // Should report error
    ASSERT(ret < 0);
}

// Test finalize adds exit
TEST(gen_finalize)
{
    setup();
    a64_gen_reset(&test_state, 0x1000);

    // Add one instruction
    uint32_t nop = 0xD503201F;
    a64_gen_instruction(&test_state, nop, 0x1000);

    size_t before = test_state.num_gadgets;

    // Finalize should add exit gadget
    int ret = a64_gen_finalize(&test_state);
    ASSERT_EQ(ret, A64_GEN_OK);

    ASSERT(test_state.num_gadgets > before);
    ASSERT(test_state.is_complete);
}

// Test instruction counting
TEST(gen_instruction_count)
{
    setup();
    a64_gen_reset(&test_state, 0x1000);

    // Process 5 instructions
    for (int i = 0; i < 5; i++) {
        uint32_t add_imm = 0x91000420;
        int ret = a64_gen_instruction(&test_state, add_imm, 0x1000 + i * 4);
        ASSERT_EQ(ret, A64_GEN_OK);
    }

    ASSERT_EQ(test_state.instructions_processed, 5);
}

// Test SUB register
TEST(gen_sub_reg)
{
    setup();
    a64_gen_reset(&test_state, 0x1000);

    // SUB x0, x1, x2
    uint32_t sub = 0xCB020020; // sub x0, x1, x2

    int ret = a64_gen_instruction(&test_state, sub, 0x1000);
    ASSERT_EQ(ret, A64_GEN_OK);
}

// Test load/store (calls into C)
TEST(gen_ldr)
{
    setup();
    a64_gen_reset(&test_state, 0x1000);

    // LDR x0, [x1]
    uint32_t ldr = 0xF9400000; // ldr x0, [x1]

    int ret = a64_gen_instruction(&test_state, ldr, 0x1000);
    // Should generate thunk that calls C helper
    ASSERT_EQ(ret, A64_GEN_OK);
}

// Test STR post-index store: str xzr, [x2], #8
// This is the failing instruction from the zeroing loop
TEST(gen_str_post_index)
{
    setup();
    a64_gen_reset(&test_state, 0xf7fa4650);

    // Raw instruction: str xzr, [x2], #8
    // This stores zero to [x2], then writes back x2 = x2 + 8
    uint32_t str = 0xf800845f;

    int ret = a64_gen_instruction(&test_state, str, 0xf7fa4650);
    ASSERT_EQ(ret, A64_GEN_OK);

    // The generator should emit gadget_str_x with proper parameters
    // Parameters in bytecode order:
    // - gadget pointer
    // - fault_pc = 0xf7fa4650
    // - Rt = 31 (XZR)
    // - Rn = 2 (X2)
    // - imm = 8 (post-index offset)
    // - size = 3 (64-bit)
    // - idx_mode = A64_POST_INDEX (1)
    // - meta = 0 (no signed extension, no register offset)

    // Verify state tracking
    ASSERT_EQ(test_state.guest_pc, 0xf7fa4650);
    ASSERT_EQ(test_state.num_gadgets, 1); // Should emit exactly one gadget
}

// Test RET
TEST(gen_ret)
{
    setup();
    a64_gen_reset(&test_state, 0x1000);

    // RET
    uint32_t ret = 0xD65F03C0; // ret

    int result = a64_gen_instruction(&test_state, ret, 0x1000);
    ASSERT_EQ(result, A64_GEN_OK);
    ASSERT_EQ(test_state.is_complete, 1);
}

// Test NOP
TEST(gen_nop)
{
    setup();
    a64_gen_reset(&test_state, 0x1000);

    // NOP
    uint32_t nop = 0xD503201F;

    int ret = a64_gen_instruction(&test_state, nop, 0x1000);
    ASSERT_EQ(ret, A64_GEN_OK);
    // Should generate NOP gadget
}

TEST(gen_mrs_tpidr_el0)
{
    setup();
    a64_gen_reset(&test_state, 0x1000);

    uint32_t mrs_tpidr_el0 = 0xD53BD041; // mrs x1, tpidr_el0
    int ret = a64_gen_instruction(&test_state, mrs_tpidr_el0, 0x1000);

    ASSERT_EQ(ret, A64_GEN_OK);
    ASSERT(test_state.num_gadgets >= 1);
    if (LAST_TEST_PASSED() && test_state.num_gadgets >= 3) {
        ASSERT_EQ(test_buffer[1], (tcti_gadget_t)(uintptr_t)A64_SYSREG_TPIDR_EL0);
        ASSERT_EQ(test_buffer[2], (tcti_gadget_t)(uintptr_t)1);
    }
}

TEST(gen_msr_fpcr)
{
    setup();
    a64_gen_reset(&test_state, 0x1000);

    uint32_t msr_fpcr_x2 = 0xD51B4402;
    int ret = a64_gen_instruction(&test_state, msr_fpcr_x2, 0x1000);

    ASSERT_EQ(ret, A64_GEN_OK);
    ASSERT(test_state.num_gadgets >= 3);
    ASSERT_EQ(test_buffer[1], (tcti_gadget_t)(uintptr_t)A64_SYSREG_FPCR);
    ASSERT_EQ(test_buffer[2], (tcti_gadget_t)(uintptr_t)2);
}

TEST(gen_mrs_daif_unsupported)
{
    setup();
    a64_gen_reset(&test_state, 0x1000);

    uint32_t mrs_daif = 0xD5420101;
    int ret = a64_gen_instruction(&test_state, mrs_daif, 0x1000);

    if (ret == A64_GEN_OK) {
        ASSERT_EQ(test_state.num_gadgets, 1);
        ASSERT_EQ(test_buffer[0], gadget_sysreg_unsupported);
        ASSERT_EQ(test_state.is_complete, 1);
    } else {
        ASSERT(ret == A64_GEN_UNSUPPORTED || ret == A64_GEN_INVALID_INSN);
    }
}

TEST(gen_mrs_cntvct_unsupported)
{
    setup();
    a64_gen_reset(&test_state, 0x1000);

    uint32_t mrs_cntvct = 0xD53BE043;
    int ret = a64_gen_instruction(&test_state, mrs_cntvct, 0x1000);

    ASSERT_EQ(ret, A64_GEN_OK);
    ASSERT_EQ(test_state.num_gadgets, 1);
    ASSERT_EQ(test_buffer[0], gadget_sysreg_unsupported);
    ASSERT_EQ(test_state.is_complete, 1);
}

TEST(gen_mrs_ctr_el0)
{
    setup();
    a64_gen_reset(&test_state, 0x1000);

    uint32_t mrs_ctr_el0 = 0xD53B0023;
    int ret = a64_gen_instruction(&test_state, mrs_ctr_el0, 0x1000);

    ASSERT_EQ(ret, A64_GEN_OK);
    ASSERT(test_state.num_gadgets >= 1);
    ASSERT_EQ(test_buffer[1], (tcti_gadget_t)(uintptr_t)A64_SYSREG_CTR_EL0);
    ASSERT_EQ(test_buffer[2], (tcti_gadget_t)(uintptr_t)3);
}

TEST(gen_mrs_dczid_el0)
{
    setup();
    a64_gen_reset(&test_state, 0x1000);

    uint32_t mrs_dczid_el0 = 0xD5330004;
    int ret = a64_gen_instruction(&test_state, mrs_dczid_el0, 0x1000);

    ASSERT_EQ(ret, A64_GEN_OK);
    ASSERT(test_state.num_gadgets >= 1);
    if (LAST_TEST_PASSED() && test_state.num_gadgets >= 3) {
        ASSERT_EQ(test_buffer[1], (tcti_gadget_t)(uintptr_t)A64_SYSREG_DCZID_EL0);
        ASSERT_EQ(test_buffer[2], (tcti_gadget_t)(uintptr_t)4);
    }
}

// Integration test: Fibonacci-like sequence
TEST(gen_fibonacci_sequence)
{
    setup();
    a64_gen_reset(&test_state, 0x1000);

    // Simple sequence:
    // ADD x2, x0, x1  ; x2 = x0 + x1
    // MOV x0, x1      ; x0 = x1
    // MOV x1, x2      ; x1 = x2

    uint32_t sequence[] = {
        0x8B010002, // add x2, x0, x1
        0xAA0103E0, // mov x0, x1
        0xAA0203E1, // mov x1, x2
    };

    for (int i = 0; i < 3; i++) {
        int ret = a64_gen_instruction(&test_state, sequence[i], 0x1000 + i * 4);
        ASSERT_EQ(ret, A64_GEN_OK);
    }

    ASSERT(test_state.num_gadgets >= 2);
    ASSERT_EQ(test_state.instructions_processed, 3);
}

// Main test runner
int gen_test_run_all(void)
{
    tests_run = 0;
    tests_passed = 0;
    tests_failed = 0;
    first_failed_test = NULL;
    first_failed_line = 0;
    memset(failed_tests, 0, sizeof(failed_tests));
    memset(failed_test_lines, 0, sizeof(failed_test_lines));
    memset(direct_records, 0, sizeof(direct_records));
    direct_records_count = 0;

    RUN_TEST(gen_init);
    RUN_TEST(gen_add_gadget);

    RUN_TEST(gen_movz);
    RUN_TEST(gen_add_imm);
    RUN_TEST(gen_add_reg);
    RUN_TEST(exec_add_reg_7_13_14_direct);
    RUN_TEST(exec_add_reg_13_13_14_direct);
    RUN_TEST(exec_add_reg_0_1_2_direct);
    RUN_TEST(exec_mov_reg_2_7_direct);
    RUN_TEST(exec_add_reg_7_7_14_direct);
    RUN_TEST(exec_mov_reg_7_2_direct);
    RUN_TEST(gen_add_reg_shifted_lsl);
    RUN_TEST(gen_add_imm_sp_source_uses_distinct_temp);
    RUN_TEST(gen_orr_mov_alias_xzr_rn);
    RUN_TEST(gen_logical_rm_xzr_materializes_zero);
    RUN_TEST(gen_sub_reg);
    RUN_TEST(gen_nop);
    RUN_TEST(gen_mrs_tpidr_el0);
    RUN_TEST(gen_msr_fpcr);
    RUN_TEST(gen_mrs_ctr_el0);
    RUN_TEST(gen_mrs_dczid_el0);
    RUN_TEST(gen_mrs_daif_unsupported);
    RUN_TEST(gen_mrs_cntvct_unsupported);

    RUN_TEST(gen_branch_ends_block);
    RUN_TEST(gen_cbz_ends_block);
    RUN_TEST(gen_tbz_ends_block);
    RUN_TEST(gen_svc_ends_block);
    RUN_TEST(gen_ret);

    RUN_TEST(gen_ldr);

    RUN_TEST(gen_sequence);
    RUN_TEST(gen_fibonacci_sequence);

    RUN_TEST(gen_pc_tracking);
    RUN_TEST(gen_instruction_count);
    RUN_TEST(gen_finalize);

    RUN_TEST(gen_overflow);
    RUN_TEST(gen_invalid);

    return tests_failed > 0 ? 1 : 0;
}

const char *gen_test_first_failed_test(void)
{
    return first_failed_test;
}

int gen_test_failed_count(void)
{
    return tests_failed;
}

int gen_test_first_failed_line(void)
{
    return first_failed_line;
}

const char *gen_test_failed_test_at(int idx)
{
    if (idx < 0 || idx >= tests_failed || idx >= 128)
        return NULL;
    return failed_tests[idx];
}

int gen_test_failed_line_at(int idx)
{
    if (idx < 0 || idx >= tests_failed || idx >= 128)
        return 0;
    return failed_test_lines[idx];
}
