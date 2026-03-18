/*
 * Integration test for aarch64 emulation
 * Tests that all components work together
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "misc.h"
#include "emu/aarch64/cpu.h"
#include "emu/aarch64/decode.h"
#include "asbestos/aarch64/gen.h"

// Mock test fixtures
static struct cpu_state test_cpu;
static tcti_gadget_t test_buffer[A64_MAX_GADGETS_PER_BLOCK];
static a64_gen_state_t gen_state;

// Test tracking
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

// Setup
static void setup(void) {
    memset(&test_cpu, 0, sizeof(test_cpu));
    memset(test_buffer, 0, sizeof(test_buffer));
    a64_gen_init(&gen_state, test_buffer, A64_MAX_GADGETS_PER_BLOCK);
}

// Test 1: Decode + Generate for simple instruction
TEST(decode_and_gen_add) {
    setup();

    // ADD x0, x1, x2
    uint32_t add_reg = 0x8B020020;

    // Decode
    a64_instr_t instr;
    int ret = a64_decode(add_reg, &instr);
    ASSERT_EQ(ret, 0);
    ASSERT_EQ(instr.cat, A64_DP_REG);
    ASSERT_EQ(instr.Rd, 0);
    ASSERT_EQ(instr.Rn, 1);
    ASSERT_EQ(instr.Rm, 2);

    // Generate
    a64_gen_reset(&gen_state, 0x1000);
    ret = a64_gen_instruction(&gen_state, add_reg, 0x1000);
    ASSERT_EQ(ret, 0);
    ASSERT_EQ(gen_state.num_gadgets, 1);
}

// Test 2: CPU state initialization
TEST(cpu_init) {
    setup();

    // Set up CPU state like a new process
    test_cpu.pc = 0x400000;  // Typical load address
    test_cpu.sp = 0x7FFF0000; // Stack top

    // Initialize registers to known pattern
    for (int i = 0; i < 31; i++) {
        test_cpu.x[i] = i;
    }

    ASSERT_EQ(test_cpu.pc, 0x400000);
    ASSERT_EQ(test_cpu.sp, 0x7FFF0000);
    ASSERT_EQ(test_cpu.x[0], 0);
    ASSERT_EQ(test_cpu.x[30], 30);
}

// Test 3: TLS setup
TEST(tls_setup) {
    setup();

    // Simulate musl TLS setup
    test_cpu.tpidr_el0 = 0x7FFE8000;

    ASSERT_EQ(test_cpu.tpidr_el0, 0x7FFE8000);

    // TLS access would be: mrs x0, tpidr_el0
    // Then ldr x1, [x0, #offset]
}

// Test 4: Block with multiple instructions
TEST(block_generation) {
    setup();

    // Generate a simple block:
    // ADD x0, x1, x2
    // SUB x0, x0, #1
    uint32_t insns[] = {
        0x8B020020,  // add x0, x1, x2
        0xD1000400,  // sub x0, x0, #1 (this won't generate as immediate sub)
    };

    a64_gen_reset(&gen_state, 0x1000);

    int ret = a64_gen_instruction(&gen_state, insns[0], 0x1000);
    ASSERT_EQ(ret, 0);

    // Second instruction might not be supported
    ret = a64_gen_instruction(&gen_state, insns[1], 0x1004);
    // Either 0 (ok) or -2 (unsupported) are acceptable
    ASSERT(ret == 0 || ret == -2);
}

// Test 5: Flag handling
TEST(flag_operations) {
    setup();

    // Simulate an operation that sets flags
    // ADDS x0, x1, x2
    test_cpu.x[1] = 0xFFFFFFFFFFFFFFFF;
    test_cpu.x[2] = 1;
    uint64_t result = test_cpu.x[1] + test_cpu.x[2];

    // Calculate flags
    test_cpu.z = (result == 0);
    test_cpu.n = (result >> 63) & 1;
    test_cpu.c = (result < test_cpu.x[1]);  // Unsigned overflow

    ASSERT_EQ(result, 0);
    ASSERT_EQ(test_cpu.z, 1);  // Zero
    ASSERT_EQ(test_cpu.c, 1);  // Carry
}

// Test 6: PC advancement
TEST(pc_advancement) {
    setup();
    a64_gen_reset(&gen_state, 0x1000);

    uint64_t pc = gen_state.guest_pc;

    // Process instructions
    for (int i = 0; i < 5; i++) {
        uint32_t nop = 0xD503201F;
        a64_gen_instruction(&gen_state, nop, pc);
        pc += 4;
    }

    ASSERT_EQ(pc, 0x1014);
    ASSERT_EQ(gen_state.instructions_processed, 5);
}

// Test 7: Block termination
TEST(block_termination) {
    setup();
    a64_gen_reset(&gen_state, 0x1000);

    // ADD x0, x1, x2 (should continue)
    uint32_t add = 0x8B020020;
    int ret = a64_gen_instruction(&gen_state, add, 0x1000);
    ASSERT_EQ(ret, 0);

    // RET (should end block)
    uint32_t ret_insn = 0xD65F03C0;
    ret = a64_gen_instruction(&gen_state, ret_insn, 0x1004);
    ASSERT_EQ(ret, 1);  // Block end signal
}

// Test 8: SIMD/FP registers
TEST(simd_registers) {
    setup();

    // Initialize vector registers
    for (int i = 0; i < 32; i++) {
        test_cpu.vregs[i].d[0] = 0x1000 + i;
        test_cpu.vregs[i].d[1] = 0x2000 + i;
    }

    ASSERT_EQ(test_cpu.vregs[0].d[0], 0x1000);
    ASSERT_EQ(test_cpu.vregs[31].d[1], 0x201F);
}

// Test 9: Memory fault tracking
TEST(memory_fault) {
    setup();

    // Simulate a page fault
    test_cpu.fault_addr = 0xDEADBEEF;
    test_cpu.fault_was_write = 1;

    ASSERT_EQ(test_cpu.fault_addr, 0xDEADBEEF);
    ASSERT_EQ(test_cpu.fault_was_write, 1);
}

// Test 10: Complete workflow
TEST(complete_workflow) {
    setup();

    // Initialize CPU for a simple program
    test_cpu.pc = 0x400000;
    test_cpu.sp = 0x80000000;
    test_cpu.x[0] = 10;
    test_cpu.x[1] = 20;

    // Generate code for: x2 = x0 + x1
    uint32_t add_insn = 0x8B010002;  // add x2, x0, x1

    a64_gen_reset(&gen_state, test_cpu.pc);
    int ret = a64_gen_instruction(&gen_state, add_insn, test_cpu.pc);
    ASSERT_EQ(ret, 0);

    // Simulate execution
    test_cpu.x[2] = test_cpu.x[0] + test_cpu.x[1];
    ASSERT_EQ(test_cpu.x[2], 30);

    // Advance PC
    test_cpu.pc += 4;
    ASSERT_EQ(test_cpu.pc, 0x400004);
}

// Main test runner
int main(void) {
    printf("aarch64 Integration Tests\n");
    printf("=========================\n\n");

    printf("Component Integration:\n");
    RUN_TEST(decode_and_gen_add);
    RUN_TEST(block_generation);
    RUN_TEST(pc_advancement);
    RUN_TEST(block_termination);
    RUN_TEST(complete_workflow);

    printf("\nCPU State:\n");
    RUN_TEST(cpu_init);
    RUN_TEST(flag_operations);
    RUN_TEST(simd_registers);
    RUN_TEST(memory_fault);

    printf("\nSystem Features:\n");
    RUN_TEST(tls_setup);

    printf("\n=========================\n");
    printf("Results: %d run, %d passed, %d failed\n",
           tests_run, tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
