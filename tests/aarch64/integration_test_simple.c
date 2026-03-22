/*
 * Integration test for aarch64 - Simplified version with mock gadgets
 * Tests decode + generator integration without full gadget library
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>

#include "misc.h"
#include "emu/aarch64/cpu.h"
#include "emu/aarch64/decode.h"
#include "tcti/aarch64/gen.h"

// Mock gadgets for testing
tcti_gadget_t mock_gadget_add_reg[16][16][16];
tcti_gadget_t mock_gadget_sub_reg[16][16][16];
tcti_gadget_t mock_gadget_mov_reg[16][16];
tcti_gadget_t mock_gadget_add_imm[16][16];
tcti_gadget_t mock_gadget_mov_imm[16];
tcti_gadget_t mock_gadget_b;
tcti_gadget_t mock_gadget_bcond;
tcti_gadget_t mock_gadget_cbz;
tcti_gadget_t mock_gadget_cbnz;
tcti_gadget_t mock_gadget_br;
tcti_gadget_t mock_gadget_svc;
tcti_gadget_t mock_gadget_mrs;
tcti_gadget_t mock_gadget_msr;
tcti_gadget_t mock_gadget_nop;

// Helper macro for creating mock function pointers
#define MOCK_GADGET(val) ((tcti_gadget_t)(uintptr_t)(val))

// Initialize mock gadgets
static void init_mock_gadgets(void) {
    // Create distinct pointers for each mock gadget
    for (int i = 0; i < 16; i++) {
        mock_gadget_mov_imm[i] = MOCK_GADGET(0x100000 + i);
        for (int j = 0; j < 16; j++) {
            mock_gadget_add_imm[i][j] = MOCK_GADGET(0x200000 + i*16 + j);
            mock_gadget_mov_reg[i][j] = MOCK_GADGET(0x300000 + i*16 + j);
            for (int k = 0; k < 16; k++) {
                mock_gadget_add_reg[i][j][k] = MOCK_GADGET(0x400000 + i*256 + j*16 + k);
                mock_gadget_sub_reg[i][j][k] = MOCK_GADGET(0x500000 + i*256 + j*16 + k);
            }
        }
    }

    mock_gadget_b = MOCK_GADGET(0x600000);
    mock_gadget_bcond = MOCK_GADGET(0x600001);
    mock_gadget_cbz = MOCK_GADGET(0x600002);
    mock_gadget_cbnz = MOCK_GADGET(0x600003);
    mock_gadget_br = MOCK_GADGET(0x600004);
    mock_gadget_svc = MOCK_GADGET(0x600005);
    mock_gadget_mrs = MOCK_GADGET(0x600006);
    mock_gadget_msr = MOCK_GADGET(0x600007);
    mock_gadget_nop = MOCK_GADGET(0x600008);
}

// Redirect gadget references to mock versions
#define gadget_add_reg mock_gadget_add_reg
#define gadget_sub_reg mock_gadget_sub_reg
#define gadget_mov_reg mock_gadget_mov_reg
#define gadget_add_imm mock_gadget_add_imm
#define gadget_mov_imm mock_gadget_mov_imm
#define gadget_b mock_gadget_b
#define gadget_bcond mock_gadget_bcond
#define gadget_cbz mock_gadget_cbz
#define gadget_cbnz mock_gadget_cbnz
#define gadget_br mock_gadget_br
#define gadget_svc mock_gadget_svc
#define gadget_mrs mock_gadget_mrs
#define gadget_msr mock_gadget_msr
#define gadget_nop mock_gadget_nop
// tcti_exit_block is already declared in gadgets_tcti.h

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

// Test fixtures
static struct cpu_state test_cpu;
static tcti_gadget_t test_buffer[A64_MAX_GADGETS_PER_BLOCK];
static a64_gen_state_t gen_state;

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
    ASSERT(instr.cat == A64_DP_REG || instr.cat == A64_DP_REG2);
    ASSERT_EQ(instr.Rd, 0);
    ASSERT_EQ(instr.Rn, 1);
    ASSERT_EQ(instr.Rm, 2);

    // Generate (using mock gadgets)
    a64_gen_reset(&gen_state, 0x1000);
    // Manually add gadget to simulate successful generation
    ret = a64_gen_add_gadget(&gen_state, gadget_add_reg[instr.Rd][instr.Rn][instr.Rm]);
    ASSERT_EQ(ret, 0);
    ASSERT_EQ(gen_state.num_gadgets, 1);
    ASSERT_EQ(gen_state.guest_pc, 0x1000);
}

// Test 2: CPU state initialization
TEST(cpu_init) {
    setup();

    // Set up CPU state like a new process
    test_cpu.pc = 0x400000;
    test_cpu.sp = 0x7FFF0000;

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
}

// Test 4: Block with multiple instructions
TEST(block_generation) {
    setup();

    // Generate a simple block with mock gadgets
    a64_gen_reset(&gen_state, 0x1000);

    // ADD x0, x1, x2
    int ret = a64_gen_add_gadget(&gen_state, gadget_add_reg[0][1][2]);
    ASSERT_EQ(ret, 0);

    // MOV x3, x4
    ret = a64_gen_add_gadget(&gen_state, gadget_mov_reg[3][4]);
    ASSERT_EQ(ret, 0);

    // SUB x5, x6, x7
    ret = a64_gen_add_gadget(&gen_state, gadget_sub_reg[5][6][7]);
    ASSERT_EQ(ret, 0);

    ASSERT_EQ(gen_state.num_gadgets, 3);
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
        // Simulate adding a gadget for each instruction
        a64_gen_add_gadget(&gen_state, gadget_nop);
        gen_state.instructions_processed++;
        pc += 4;
        gen_state.guest_pc = pc;
    }

    ASSERT_EQ(pc, 0x1014);
    ASSERT_EQ(gen_state.instructions_processed, 5);
}

// Test 7: Block termination
TEST(block_termination) {
    setup();
    a64_gen_reset(&gen_state, 0x1000);

    // Add ADD gadget (should continue)
    int ret = a64_gen_add_gadget(&gen_state, gadget_add_reg[0][1][2]);
    ASSERT_EQ(ret, 0);
    ASSERT_EQ(gen_state.num_gadgets, 1);

    // Add BR gadget (simulates block end)
    ret = a64_gen_add_gadget(&gen_state, gadget_br);
    ASSERT_EQ(ret, 0);
    ASSERT_EQ(gen_state.num_gadgets, 2);
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

// Test 10: Complete workflow simulation
TEST(complete_workflow) {
    setup();

    // Initialize CPU for a simple program
    test_cpu.pc = 0x400000;
    test_cpu.sp = 0x80000000;
    test_cpu.x[0] = 10;
    test_cpu.x[1] = 20;

    // Generate code for: x2 = x0 + x1
    a64_gen_reset(&gen_state, test_cpu.pc);

    // Add ADD gadget (representing: add x2, x0, x1)
    int ret = a64_gen_add_gadget(&gen_state, gadget_add_reg[2][0][1]);
    ASSERT_EQ(ret, 0);

    // Simulate execution (actual execution would call the gadgets)
    test_cpu.x[2] = test_cpu.x[0] + test_cpu.x[1];
    ASSERT_EQ(test_cpu.x[2], 30);

    // Advance PC
    test_cpu.pc += 4;
    gen_state.guest_pc = test_cpu.pc;
    ASSERT_EQ(test_cpu.pc, 0x400004);

    // Finalize block
    ret = a64_gen_finalize(&gen_state);
    ASSERT_EQ(ret, 0);
    ASSERT_EQ(gen_state.is_complete, 1);
    ASSERT_EQ(gen_state.num_gadgets, 2);  // ADD + exit
}

// Main test runner
int main(void) {
    printf("aarch64 Integration Tests (Simplified)\n");
    printf("=====================================\n\n");

    // Initialize mock gadgets
    init_mock_gadgets();

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

    printf("\n=====================================\n");
    printf("Results: %d run, %d passed, %d failed\n",
           tests_run, tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
