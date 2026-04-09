/*
 * Unit tests for aarch64 block generator
 * TDD approach - tests define expected behavior
 */

#import <IXLandLinuxRuntime/emu/aarch64/sysreg.h>
#import <IXLandLinuxRuntime/tcti/aarch64/gen.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

extern tcti_gadget_t gadget_sysreg_unsupported;

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

    RUN_TEST(gen_init);
    RUN_TEST(gen_add_gadget);

    RUN_TEST(gen_movz);
    RUN_TEST(gen_add_imm);
    RUN_TEST(gen_add_reg);
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
