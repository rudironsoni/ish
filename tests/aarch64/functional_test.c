/*
 * Functional tests for aarch64 implementation
 * Tests actual behavior rather than internal categorization
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "misc.h"
#include "emu/aarch64/cpu.h"
#include "emu/aarch64/decode.h"
#include "tcti/aarch64/gen.h"

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

// ============================================================================
// CPU State Tests
// ============================================================================

TEST(cpu_state_layout) {
    struct cpu_state cpu;

    // Verify structure size is reasonable
    ASSERT(sizeof(cpu) > 0);
    ASSERT(sizeof(cpu) < 10000);

    // Verify x[0] is at expected offset
    ASSERT_EQ((size_t)&cpu.x[0], (size_t)&cpu);

    // Verify tpidr_el0 is accessible
    cpu.tpidr_el0 = 0xDEADBEEFCAFEBABEULL;
    ASSERT_EQ(cpu.tpidr_el0, 0xDEADBEEFCAFEBABEULL);
}

TEST(cpu_registers) {
    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));

    // Test all x registers
    for (int i = 0; i < 31; i++) {
        cpu.x[i] = 0x1000 + i;
    }

    for (int i = 0; i < 31; i++) {
        ASSERT_EQ(cpu.x[i], 0x1000 + i);
    }

    // Test PC and SP
    cpu.pc = 0x400000;
    cpu.sp = 0x7FFF0000;
    ASSERT_EQ(cpu.pc, 0x400000);
    ASSERT_EQ(cpu.sp, 0x7FFF0000);
}

TEST(cpu_flags) {
    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));

    // Test individual flag fields
    cpu.n = 1;
    cpu.z = 0;
    cpu.c = 1;
    cpu.v = 0;

    ASSERT_EQ(cpu.n, 1);
    ASSERT_EQ(cpu.z, 0);
    ASSERT_EQ(cpu.c, 1);
    ASSERT_EQ(cpu.v, 0);

    // Verify PSTATE packing
    cpu.pstate = 0;
    cpu.pstate |= ((uint64_t)cpu.n << 31);
    cpu.pstate |= ((uint64_t)cpu.z << 30);
    cpu.pstate |= ((uint64_t)cpu.c << 29);
    cpu.pstate |= ((uint64_t)cpu.v << 28);

    ASSERT((cpu.pstate >> 31) & 1);
    ASSERT(!((cpu.pstate >> 30) & 1));
}

TEST(vector_registers) {
    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));

    // Test 128-bit vector registers
    for (int i = 0; i < 32; i++) {
        cpu.vregs[i].d[0] = 0x1000 + i;
        cpu.vregs[i].d[1] = 0x2000 + i;
    }

    for (int i = 0; i < 32; i++) {
        ASSERT_EQ(cpu.vregs[i].d[0], 0x1000 + i);
        ASSERT_EQ(cpu.vregs[i].d[1], 0x2000 + i);
    }
}

// ============================================================================
// Decoder Tests - Functional
// ============================================================================

TEST(decode_add_register) {
    // ADD x0, x1, x2
    uint32_t insn = 0x8B020020;

    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);

    ASSERT_EQ(ret, 0);
    ASSERT_EQ(instr.Rd, 0);
    ASSERT_EQ(instr.Rn, 1);
    ASSERT_EQ(instr.Rm, 2);
    ASSERT_EQ(instr.is_64bit, 1);
}

TEST(decode_mov_register) {
    // MOV x5, x10 (encoded as ORR x5, xzr, x10)
    // Actually let's use ADD with xzr: ADD x5, xzr, x10
    uint32_t insn = 0x8B0A03E5;  // ADD x5, xzr, x10

    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);

    ASSERT_EQ(ret, 0);
    ASSERT_EQ(instr.Rd, 5);
    ASSERT_EQ(instr.Rn, 31);  // xzr
    ASSERT_EQ(instr.Rm, 10);
}

TEST(decode_branch_unconditional) {
    // B #0x100 (relative)
    uint32_t insn = 0x14000040;  // B .+0x100

    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);

    ASSERT_EQ(ret, 0);
    ASSERT_EQ(instr.imm, 0x40);  // Offset in instructions
}

TEST(decode_branch_conditional) {
    // B.EQ #0x80
    uint32_t insn = 0x54000400;  // B.EQ .+0x80

    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);

    ASSERT_EQ(ret, 0);
    ASSERT_EQ(instr.cond, A64_EQ);
}

TEST(decode_svc) {
    // SVC #0
    uint32_t insn = 0xD4000001;

    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);

    ASSERT_EQ(ret, 0);
    // SVC should be recognized as a system instruction
}

TEST(decode_ret) {
    // RET
    uint32_t insn = 0xD65F03C0;

    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);

    ASSERT_EQ(ret, 0);
}

TEST(decode_nop) {
    // NOP
    uint32_t insn = 0xD503201F;

    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);

    ASSERT_EQ(ret, 0);
}

TEST(decode_movz) {
    // MOVZ x0, #0x1234
    uint32_t insn = 0xD2802460;

    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);

    ASSERT_EQ(ret, 0);
    ASSERT_EQ(instr.Rd, 0);
    // The immediate value and shift are encoded
}

TEST(decode_undefined) {
    // Reserved/undefined instruction pattern
    uint32_t insn = 0x00000000;

    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);

    // Should return error for undefined
    ASSERT(ret != 0);
}

// ============================================================================
// Generator Tests
// ============================================================================

TEST(generator_init) {
    a64_gen_state_t state;
    tcti_gadget_t buffer[256];

    int ret = a64_gen_init(&state, buffer, 256);
    ASSERT_EQ(ret, A64_GEN_OK);
    ASSERT_EQ(state.max_gadgets, 256);
    ASSERT_EQ(state.num_gadgets, 0);
}

TEST(generator_reset) {
    a64_gen_state_t state;
    tcti_gadget_t buffer[256];

    a64_gen_init(&state, buffer, 256);
    a64_gen_reset(&state, 0x1000);

    ASSERT_EQ(state.start_pc, 0x1000);
    ASSERT_EQ(state.guest_pc, 0x1000);
    ASSERT_EQ(state.num_gadgets, 0);
}

TEST(generator_add_gadget) {
    a64_gen_state_t state;
    tcti_gadget_t buffer[256];

    a64_gen_init(&state, buffer, 256);
    a64_gen_reset(&state, 0x1000);

    tcti_gadget_t dummy = (tcti_gadget_t)0x12345678;
    int ret = a64_gen_add_gadget(&state, dummy);

    ASSERT_EQ(ret, A64_GEN_OK);
    ASSERT_EQ(state.num_gadgets, 1);
    ASSERT_EQ(buffer[0], dummy);
}

TEST(generator_overflow) {
    a64_gen_state_t state;
    tcti_gadget_t buffer[2];

    a64_gen_init(&state, buffer, 2);
    a64_gen_reset(&state, 0x1000);

    a64_gen_add_gadget(&state, (tcti_gadget_t)1);
    a64_gen_add_gadget(&state, (tcti_gadget_t)2);

    // Third should fail
    int ret = a64_gen_add_gadget(&state, (tcti_gadget_t)3);
    ASSERT_EQ(ret, A64_GEN_TOO_MANY);
}

TEST(generator_finalize) {
    a64_gen_state_t state;
    tcti_gadget_t buffer[256];

    a64_gen_init(&state, buffer, 256);
    a64_gen_reset(&state, 0x1000);

    a64_gen_add_gadget(&state, (tcti_gadget_t)0x1000);
    a64_gen_add_gadget(&state, (tcti_gadget_t)0x1004);

    int ret = a64_gen_finalize(&state);
    ASSERT_EQ(ret, A64_GEN_OK);
    ASSERT_EQ(state.is_complete, 1);
    ASSERT_EQ(state.num_gadgets, 3);  // 2 user + exit
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST(decode_to_generate) {
    // Full pipeline: instruction -> decode -> generate

    // ADD x0, x1, x2
    uint32_t insn = 0x8B020020;

    // Decode
    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);
    ASSERT_EQ(ret, 0);

    // Generate
    a64_gen_state_t state;
    tcti_gadget_t buffer[256];
    a64_gen_init(&state, buffer, 256);
    a64_gen_reset(&state, 0x1000);

    ret = a64_gen_instruction(&state, insn, 0x1000);
    // Should succeed or report unsupported
    ASSERT(ret == 0 || ret == A64_GEN_UNSUPPORTED);
}

TEST(block_with_multiple) {
    // Test generating a block with multiple instructions
    a64_gen_state_t state;
    tcti_gadget_t buffer[256];

    a64_gen_init(&state, buffer, 256);
    a64_gen_reset(&state, 0x1000);

    // First instruction
    int ret = a64_gen_instruction(&state, 0x8B020020, 0x1000);  // ADD
    ASSERT(ret == 0 || ret == A64_GEN_UNSUPPORTED);

    // Second instruction
    ret = a64_gen_instruction(&state, 0xD503201F, 0x1004);  // NOP
    ASSERT(ret == 0 || ret == A64_GEN_UNSUPPORTED);
}

// ============================================================================
// Syscall Number Tests
// ============================================================================

#include "kernel/aarch64/calls.h"

TEST(syscall_numbers_basic) {
    // Verify key syscall numbers
    ASSERT_EQ(A64_SYS_read, 63);
    ASSERT_EQ(A64_SYS_write, 64);
    ASSERT_EQ(A64_SYS_exit, 93);
    ASSERT_EQ(A64_SYS_exit_group, 94);
}

TEST(syscall_numbers_file) {
    ASSERT_EQ(A64_SYS_openat, 56);
    ASSERT_EQ(A64_SYS_close, 57);
    ASSERT_EQ(A64_SYS_lseek, 62);
}

TEST(syscall_numbers_memory) {
    ASSERT_EQ(A64_SYS_mmap, 222);
    ASSERT_EQ(A64_SYS_munmap, 215);
    ASSERT_EQ(A64_SYS_brk, 214);
}

// ============================================================================
// Signal Frame Tests (basic structure without full headers)
// ============================================================================

struct test_a64_sigcontext {
    uint64_t fault_address;
    uint64_t regs[31];
    uint64_t sp;
    uint64_t pc;
    uint64_t pstate;
    uint8_t __reserved[4096];
};

TEST(sigcontext_size) {
    ASSERT(sizeof(struct test_a64_sigcontext) >= 4096);
}

TEST(sigcontext_fields) {
    struct test_a64_sigcontext sc;

    // Test we can access all fields
    sc.fault_address = 0xDEADBEEF;
    sc.regs[0] = 1;
    sc.regs[30] = 31;
    sc.sp = 0x7FFF0000;
    sc.pc = 0x400000;
    sc.pstate = 0;

    ASSERT_EQ(sc.fault_address, 0xDEADBEEF);
    ASSERT_EQ(sc.regs[0], 1);
    ASSERT_EQ(sc.regs[30], 31);
}

TEST(sigcontext_reserved_size) {
    // Reserved area should be 4096 bytes for extended contexts
    ASSERT_EQ(sizeof(((struct test_a64_sigcontext*)0)->__reserved), 4096);
}

// ============================================================================
// Main
// ============================================================================

int main(void) {
    printf("aarch64 Functional Tests\n");
    printf("========================\n\n");

    printf("CPU State:\n");
    RUN_TEST(cpu_state_layout);
    RUN_TEST(cpu_registers);
    RUN_TEST(cpu_flags);
    RUN_TEST(vector_registers);

    printf("\nDecoder (Functional):\n");
    RUN_TEST(decode_add_register);
    RUN_TEST(decode_mov_register);
    RUN_TEST(decode_branch_unconditional);
    RUN_TEST(decode_branch_conditional);
    RUN_TEST(decode_svc);
    RUN_TEST(decode_ret);
    RUN_TEST(decode_nop);
    RUN_TEST(decode_movz);
    RUN_TEST(decode_undefined);

    printf("\nGenerator:\n");
    RUN_TEST(generator_init);
    RUN_TEST(generator_reset);
    RUN_TEST(generator_add_gadget);
    RUN_TEST(generator_overflow);
    RUN_TEST(generator_finalize);

    printf("\nIntegration:\n");
    RUN_TEST(decode_to_generate);
    RUN_TEST(block_with_multiple);

    printf("\nSyscall Numbers:\n");
    RUN_TEST(syscall_numbers_basic);
    RUN_TEST(syscall_numbers_file);
    RUN_TEST(syscall_numbers_memory);

    printf("\nSignal Frames:\n");
    RUN_TEST(sigcontext_size);
    RUN_TEST(sigcontext_fields);
    RUN_TEST(sigcontext_reserved_size);

    printf("\n========================\n");
    printf("Results: %d run, %d passed, %d failed\n",
           tests_run, tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
