/*
 * Functional decoder tests - no dependencies on generator
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "misc.h"
#include "emu/aarch64/cpu.h"
#include "emu/aarch64/decode.h"

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

    ASSERT(sizeof(cpu) > 0);
    ASSERT(sizeof(cpu) < 10000);

    cpu.tpidr_el0 = 0xDEADBEEFCAFEBABEULL;
    ASSERT_EQ(cpu.tpidr_el0, 0xDEADBEEFCAFEBABEULL);
}

TEST(cpu_registers) {
    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));

    for (int i = 0; i < 31; i++) {
        cpu.x[i] = 0x1000 + i;
    }

    for (int i = 0; i < 31; i++) {
        ASSERT_EQ(cpu.x[i], 0x1000 + i);
    }

    cpu.pc = 0x400000;
    cpu.sp = 0x7FFF0000;
    ASSERT_EQ(cpu.pc, 0x400000);
    ASSERT_EQ(cpu.sp, 0x7FFF0000);
}

TEST(cpu_flags) {
    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));

    // Set flags via bitfields
    cpu.n = 1;
    cpu.z = 0;
    cpu.c = 1;
    cpu.v = 0;

    ASSERT_EQ(cpu.n, 1);
    ASSERT_EQ(cpu.z, 0);
    ASSERT_EQ(cpu.c, 1);
    ASSERT_EQ(cpu.v, 0);

    // Save flag values before clearing pstate (they share memory via union)
    int n_val = cpu.n;
    int z_val = cpu.z;
    int c_val = cpu.c;
    int v_val = cpu.v;

    cpu.pstate = 0;
    cpu.pstate |= ((uint64_t)n_val << 31);
    cpu.pstate |= ((uint64_t)z_val << 30);
    cpu.pstate |= ((uint64_t)c_val << 29);
    cpu.pstate |= ((uint64_t)v_val << 28);

    ASSERT((cpu.pstate >> 31) & 1);
    ASSERT(!((cpu.pstate >> 30) & 1));
}

TEST(vector_registers) {
    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));

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
// Decoder Tests
// ============================================================================

TEST(decode_add_register) {
    uint32_t insn = 0x8B020020;  // ADD x0, x1, x2

    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);

    ASSERT_EQ(ret, 0);
    ASSERT_EQ(instr.Rd, 0);
    ASSERT_EQ(instr.Rn, 1);
    ASSERT_EQ(instr.Rm, 2);
    ASSERT_EQ(instr.is_64bit, 1);
}

TEST(decode_add_register_32bit) {
    uint32_t insn = 0x0B020020;  // ADD w0, w1, w2

    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);

    ASSERT_EQ(ret, 0);
    ASSERT_EQ(instr.Rd, 0);
    ASSERT_EQ(instr.Rn, 1);
    ASSERT_EQ(instr.Rm, 2);
    ASSERT_EQ(instr.is_64bit, 0);
}

TEST(decode_sub_register) {
    uint32_t insn = 0xCB020020;  // SUB x0, x1, x2

    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);

    ASSERT_EQ(ret, 0);
    ASSERT_EQ(instr.Rd, 0);
    ASSERT_EQ(instr.Rn, 1);
    ASSERT_EQ(instr.Rm, 2);
}

TEST(decode_branch_unconditional) {
    // B .+0x100 (256 bytes forward, encoded as 64 instructions)
    // imm26 = 64 = 0x40, byte offset = 0x40 << 2 = 0x100
    uint32_t insn = 0x14000040;

    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);

    ASSERT_EQ(ret, 0);
    ASSERT_EQ(instr.imm, 0x100);  // Byte offset, not instruction count
}

TEST(decode_branch_conditional) {
    uint32_t insn = 0x54000400;  // B.EQ .+0x80

    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);

    ASSERT_EQ(ret, 0);
    ASSERT_EQ(instr.cond, A64_EQ);
}

TEST(decode_branch_conditional_ne) {
    // B.NE: op0=00 (bits 29:28), op1=0 (bit 25), o1=0 (bit 24)
    // cond = 0001 (NE), imm19 = 0
    // Encoding: 0101 0100 0000 0000 0000 0000 0000 0001 = 0x54000001
    // But this is actually category 0x5 (DP_SCALAR), not BRANCH!

    // Correct B.NE encoding: op0=0110 (BRANCH category)
    // 54000001 in binary: 0101 0100 0000 0000 0000 0000 0000 0001
    // Bits 28:25 = 0101 = 0x5 (DP_SCALAR), NOT BRANCH

    // The issue is the test instruction encoding. Let me use a real B.NE.
    // B.NE: 01010100 xx00 0000 0000 0000 0001
    // But we need: 0101 0100 0000 ...

    // Actually 0x54000001 decodes as:
    // op0 = bits 28:25 = 0101 = 5
    // This is DP_SCALAR, not BRANCH

    // Real B.NE should be in category 6 (0110)
    // Let me recalculate: category 6 means bits 28:25 = 0110 = 0x6
    // 0110 0100 ... = 0x64xxxxxx

    // A proper B.NE: 0x54000001 was incorrect test data
    // Correct: cond=NE (1), imm19=0
    // op0=01 (conditional branch), op1=0 (B.cond)
    // Bits 31:26 = 010101 (0x15)
    // Wait, let me look at actual ARM encoding...

    // B.cond: 01010100 xx00 0000 0000 0000 cccc cccc
    // Where cccc = cond = 0001 for NE
    // xx = imm19

    // So B.NE with imm19=0, cond=1:
    // 0101 0100 0000 0000 0000 0000 0000 0001 = 0x54000001
    // But this is category 5!

    // I see the issue - my category detection was wrong.
    // 0x54000001 with fixed category detection should now be BRANCH.

    uint32_t insn = 0x54000001;  // B.NE (after category fix)

    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);

    ASSERT_EQ(ret, 0);
    ASSERT_EQ(instr.cond, A64_NE);
}

TEST(decode_svc) {
    uint32_t insn = 0xD4000001;  // SVC #0

    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);

    ASSERT_EQ(ret, 0);
}

TEST(decode_ret) {
    uint32_t insn = 0xD65F03C0;  // RET

    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);

    ASSERT_EQ(ret, 0);
}

TEST(decode_nop) {
    uint32_t insn = 0xD503201F;  // NOP

    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);

    ASSERT_EQ(ret, 0);
}

TEST(decode_movz) {
    uint32_t insn = 0xD2802460;  // MOVZ x0, #0x1234

    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);

    ASSERT_EQ(ret, 0);
    ASSERT_EQ(instr.Rd, 0);
}

TEST(decode_movn) {
    uint32_t insn = 0x92800000;  // MOVN x0, #0

    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);

    ASSERT_EQ(ret, 0);
    ASSERT_EQ(instr.Rd, 0);
}

TEST(decode_cbz) {
    uint32_t insn = 0xB4000040;  // CBZ x0, .+0x80

    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);

    ASSERT_EQ(ret, 0);
    ASSERT_EQ(instr.Rd, 0);
}

TEST(decode_cbnz) {
    uint32_t insn = 0xB5000040;  // CBNZ x0, .+0x80

    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);

    ASSERT_EQ(ret, 0);
    ASSERT_EQ(instr.Rd, 0);
}

TEST(decode_ldr) {
    uint32_t insn = 0xF9400000;  // LDR x0, [x0]

    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);

    ASSERT_EQ(ret, 0);
    ASSERT_EQ(instr.Rd, 0);
    ASSERT_EQ(instr.Rn, 0);
}

TEST(decode_str) {
    uint32_t insn = 0xF9000000;  // STR x0, [x0]

    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);

    ASSERT_EQ(ret, 0);
    ASSERT_EQ(instr.Rd, 0);
    ASSERT_EQ(instr.Rn, 0);
}

TEST(decode_cmp) {
    uint32_t insn = 0xEB02001F;  // CMP x0, x2

    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);

    ASSERT_EQ(ret, 0);
    ASSERT_EQ(instr.Rn, 0);
    ASSERT_EQ(instr.Rm, 2);
}

TEST(decode_undefined) {
    uint32_t insn = 0x00000000;

    a64_instr_t instr;
    int ret = a64_decode(insn, &instr);

    ASSERT(ret != 0);
}

// ============================================================================
// Syscall Number Tests
// ============================================================================

#include "kernel/aarch64/calls.h"

TEST(syscall_numbers_basic) {
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

TEST(syscall_range) {
    // Verify syscall numbers are in reasonable range
    ASSERT(A64_SYS_read < 500);
    ASSERT(A64_SYS_write < 500);
    ASSERT(A64_SYS_exit < 500);
}

// ============================================================================
// Signal Context Tests
// ============================================================================

struct test_sigcontext {
    uint64_t fault_address;
    uint64_t regs[31];
    uint64_t sp;
    uint64_t pc;
    uint64_t pstate;
    uint8_t __reserved[4096];
};

TEST(sigcontext_size) {
    ASSERT(sizeof(struct test_sigcontext) >= 4096);
}

TEST(sigcontext_fields) {
    struct test_sigcontext sc;

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

// ============================================================================
// Instruction Encoding/Decoding Round-trip
// ============================================================================

TEST(encode_decode_roundtrip) {
    // Test that we can encode and decode ADD
    for (int rd = 0; rd < 5; rd++) {
        for (int rn = 0; rn < 5; rn++) {
            for (int rm = 0; rm < 5; rm++) {
                uint32_t insn = 0x8B000000 | (rm << 16) | (rn << 5) | rd;

                a64_instr_t instr;
                int ret = a64_decode(insn, &instr);

                if (ret == 0) {
                    ASSERT_EQ(instr.Rd, rd);
                    ASSERT_EQ(instr.Rn, rn);
                    ASSERT_EQ(instr.Rm, rm);
                }
            }
        }
    }
}

// ============================================================================
// Main
// ============================================================================

int main(void) {
    printf("aarch64 Decoder Functional Tests\n");
    printf("================================\n\n");

    printf("CPU State:\n");
    RUN_TEST(cpu_state_layout);
    RUN_TEST(cpu_registers);
    RUN_TEST(cpu_flags);
    RUN_TEST(vector_registers);

    printf("\nDecoder:\n");
    RUN_TEST(decode_add_register);
    RUN_TEST(decode_add_register_32bit);
    RUN_TEST(decode_sub_register);
    RUN_TEST(decode_branch_unconditional);
    RUN_TEST(decode_branch_conditional);
    RUN_TEST(decode_branch_conditional_ne);
    RUN_TEST(decode_svc);
    RUN_TEST(decode_ret);
    RUN_TEST(decode_nop);
    RUN_TEST(decode_movz);
    RUN_TEST(decode_movn);
    RUN_TEST(decode_cbz);
    RUN_TEST(decode_cbnz);
    RUN_TEST(decode_ldr);
    RUN_TEST(decode_str);
    RUN_TEST(decode_cmp);
    RUN_TEST(decode_undefined);

    printf("\nSyscall Numbers:\n");
    RUN_TEST(syscall_numbers_basic);
    RUN_TEST(syscall_numbers_file);
    RUN_TEST(syscall_numbers_memory);
    RUN_TEST(syscall_range);

    printf("\nSignal Context:\n");
    RUN_TEST(sigcontext_size);
    RUN_TEST(sigcontext_fields);

    printf("\nRound-trip:\n");
    RUN_TEST(encode_decode_roundtrip);

    printf("\n================================\n");
    printf("Results: %d run, %d passed, %d failed\n",
           tests_run, tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
