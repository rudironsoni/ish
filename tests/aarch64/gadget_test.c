/*
 * Unit tests for aarch64 TCTI gadgets
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "misc.h"
#include "emu/aarch64/cpu.h"
#include "tcti/aarch64/gadgets_tcti.h"

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
 * Test fixture - create CPU state
 */
static struct cpu_state test_cpu;

static void setup_cpu(void) {
    memset(&test_cpu, 0, sizeof(test_cpu));
    test_cpu.pc = 0x1000;
    test_cpu.sp = 0x8000;
}

/*
 * Test CPU state structure
 */
TEST(cpu_state_size) {
    // Ensure struct is reasonable size
    ASSERT(sizeof(struct cpu_state) < 0x10000);
}

TEST(cpu_state_alignment) {
    // Check that key members have proper alignment
    ASSERT(((uintptr_t)&test_cpu.x[0] % 8) == 0);
    ASSERT(((uintptr_t)&test_cpu.sp % 8) == 0);
    ASSERT(((uintptr_t)&test_cpu.pc % 8) == 0);
}

/*
 * Test register access helpers
 */
TEST(register_access) {
    setup_cpu();

    // Set x0-x30
    for (int i = 0; i < 31; i++) {
        test_cpu.x[i] = 0x1000 + i;
    }

    // Verify
    for (int i = 0; i < 31; i++) {
        ASSERT_EQ(test_cpu.x[i], 0x1000 + i);
    }

    // Test 32-bit access
    test_cpu.x[0] = 0xDEADBEEFCAFEBABEULL;
    ASSERT_EQ((uint32_t)test_cpu.x[0], 0xCAFEBABE);  // Truncated

    // Test w0 accessor (would be implemented as macro/inline)
    // set_wn(&test_cpu, 0, 0x12345678);
    // ASSERT_EQ((uint32_t)test_cpu.x[0], 0x12345678);
}

/*
 * Test flag operations
 */
TEST(flag_operations) {
    setup_cpu();

    // Test NZCV flag setting
    test_cpu.n = 1;
    test_cpu.z = 0;
    test_cpu.c = 1;
    test_cpu.v = 0;

    ASSERT_EQ(test_cpu.n, 1);
    ASSERT_EQ(test_cpu.z, 0);
    ASSERT_EQ(test_cpu.c, 1);
    ASSERT_EQ(test_cpu.v, 0);

    // Pack into pstate
    test_cpu.pstate = 0;
    test_cpu.pstate |= ((uint64_t)test_cpu.n << 31);
    test_cpu.pstate |= ((uint64_t)test_cpu.z << 30);
    test_cpu.pstate |= ((uint64_t)test_cpu.c << 29);
    test_cpu.pstate |= ((uint64_t)test_cpu.v << 28);

    ASSERT_EQ((test_cpu.pstate >> 31) & 1, 1);
    ASSERT_EQ((test_cpu.pstate >> 30) & 1, 0);
    ASSERT_EQ((test_cpu.pstate >> 29) & 1, 1);
    ASSERT_EQ((test_cpu.pstate >> 28) & 1, 0);
}

/*
 * Test gadget lookup tables
 */
TEST(gadget_tables_exist) {
    // Test that lookup tables are properly defined

    // MOV reg table should exist
    extern const tcti_gadget_t gadget_mov_reg[16][16];
    ASSERT(gadget_mov_reg != NULL);

    // ADD reg table should exist
    extern const tcti_gadget_t gadget_add_reg[16][16][16];
    ASSERT(gadget_add_reg != NULL);

    // SUB reg table should exist
    extern const tcti_gadget_t gadget_sub_reg[16][16][16];
    ASSERT(gadget_sub_reg != NULL);
}

/*
 * Test gadget function pointers
 */
TEST(gadget_functions_valid) {
    // Check that some key gadgets have valid function pointers

    // MOV x1, x2 should have a valid function
    ASSERT(gadget_mov_reg[1][2] != NULL);

    // ADD x0, x1, x2 should have a valid function
    ASSERT(gadget_add_reg[0][1][2] != NULL);

    // SUB x15, x15, x15 should have a valid function (clear register)
    ASSERT(gadget_sub_reg[15][15][15] != NULL);
}

/*
 * Test SIMD/FP register access
 */
TEST(simd_registers) {
    setup_cpu();

    // Set vector registers
    for (int i = 0; i < 32; i++) {
        test_cpu.vregs[i].q = ((__int128)(0x100 + i) << 64) | (0x200 + i);
    }

    // Verify
    for (int i = 0; i < 32; i++) {
        ASSERT_EQ(test_cpu.vregs[i].d[0], 0x200 + i);
        ASSERT_EQ(test_cpu.vregs[i].d[1], 0x100 + i);
    }

    // Test accessing as floats
    test_cpu.vregs[0].f32[0] = 1.5f;
    ASSERT(test_cpu.vregs[0].f32[0] == 1.5f);
}

/*
 * Test TLS register
 */
TEST(tls_register) {
    setup_cpu();

    // TPIDR_EL0
    test_cpu.tpidr_el0 = 0x7FFF0000;
    ASSERT_EQ(test_cpu.tpidr_el0, 0x7FFF0000);

    // This is the thread pointer for musl/glibc
}

/*
 * Test memory fault tracking
 */
TEST(memory_fault) {
    setup_cpu();

    test_cpu.fault_addr = 0xDEADBEEF;
    test_cpu.fault_was_write = 1;

    ASSERT_EQ(test_cpu.fault_addr, 0xDEADBEEF);
    ASSERT_EQ(test_cpu.fault_was_write, 1);
}

/*
 * Test PSTATE NZCV calculations
 */
TEST(nzcv_arithmetic) {
    setup_cpu();

    // Test addition with carry
    uint64_t a = 0xFFFFFFFFFFFFFFFF;
    uint64_t b = 1;
    uint64_t result = a + b;

    // Should overflow, set carry
    int c = (result < a);  // Unsigned overflow
    int v = ((~(a ^ b) & (a ^ result)) >> 63) & 1;  // Signed overflow
    int n = (result >> 63) & 1;
    int z = (result == 0);

    ASSERT_EQ(result, 0);
    ASSERT_EQ(z, 1);  // Result is zero
    ASSERT_EQ(c, 1);  // Carry was set
    ASSERT_EQ(v, 0);  // No signed overflow ( -1 + 1 = 0, correct)
}

/*
 * Test condition codes
 */
TEST(condition_codes) {
    // EQ: Z==1
    // NE: Z==0
    // CS/HS: C==1
    // CC/LO: C==0
    // MI: N==1
    // PL: N==0
    // VS: V==1
    // VC: V==0
    // HI: C==1 && Z==0
    // LS: C==0 || Z==1
    // GE: N==V
    // LT: N!=V
    // GT: Z==0 && N==V
    // LE: Z==1 || N!=V

    // Test GE: N==V
    ASSERT_EQ(A64_GE, 0xA);  // 1010

    // Test LT: N!=V
    ASSERT_EQ(A64_LT, 0xB);  // 1011
}

/*
 * Main test runner
 */
int main(void) {
    printf("aarch64 Gadget Unit Tests\n");
    printf("=========================\n\n");

    printf("CPU State:\n");
    RUN_TEST(cpu_state_size);
    RUN_TEST(cpu_state_alignment);
    RUN_TEST(register_access);
    RUN_TEST(flag_operations);

    printf("\nSIMD/FP State:\n");
    RUN_TEST(simd_registers);

    printf("\nSystem State:\n");
    RUN_TEST(tls_register);
    RUN_TEST(memory_fault);

    printf("\nArithmetic:\n");
    RUN_TEST(nzcv_arithmetic);
    RUN_TEST(condition_codes);

    printf("\nGadget Tables:\n");
    RUN_TEST(gadget_tables_exist);
    RUN_TEST(gadget_functions_valid);

    printf("\n=========================\n");
    printf("Results: %d run, %d passed, %d failed\n",
           tests_run, tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
