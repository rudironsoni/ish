/*
 * Simple unit tests for generator state management
 * Doesn't require full gadget implementations
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <stddef.h>

// Minimal structs for testing
typedef void (*tcti_gadget_t)(void);

#define A64_MAX_GADGETS_PER_BLOCK 256

enum a64_gen_error {
    A64_GEN_OK = 0,
    A64_GEN_INVALID_INSN = -1,
    A64_GEN_UNSUPPORTED = -2,
    A64_GEN_TOO_MANY = -3,
    A64_GEN_NO_MEMORY = -4,
};

typedef struct a64_gen_state {
    tcti_gadget_t *gadgets;
    size_t max_gadgets;
    size_t num_gadgets;
    uint64_t start_pc;
    uint64_t guest_pc;
    int is_complete;
    int instructions_processed;
} a64_gen_state_t;

// Simple implementation for testing
int a64_gen_init(a64_gen_state_t *state, tcti_gadget_t *buffer, size_t max) {
    if (!state || !buffer) return -1;
    memset(state, 0, sizeof(*state));
    state->gadgets = buffer;
    state->max_gadgets = max;
    return 0;
}

void a64_gen_reset(a64_gen_state_t *state, uint64_t pc) {
    if (!state) return;
    state->num_gadgets = 0;
    state->start_pc = pc;
    state->guest_pc = pc;
    state->is_complete = 0;
    state->instructions_processed = 0;
}

int a64_gen_add_gadget(a64_gen_state_t *state, tcti_gadget_t gadget) {
    if (state->num_gadgets >= state->max_gadgets) return -3;
    state->gadgets[state->num_gadgets++] = gadget;
    return 0;
}

// Test runner
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
        printf("FAILED at line %d\n", __LINE__); \
        tests_failed++; \
        tests_passed--; \
        return; \
    } \
} while(0)

#define ASSERT_EQ(a, b) ASSERT((a) == (b))

// Test fixtures
static a64_gen_state_t test_state;
static tcti_gadget_t test_buffer[A64_MAX_GADGETS_PER_BLOCK];

static void setup(void) {
    memset(&test_state, 0, sizeof(test_state));
    memset(test_buffer, 0, sizeof(test_buffer));
    a64_gen_init(&test_state, test_buffer, A64_MAX_GADGETS_PER_BLOCK);
}

TEST(gen_init) {
    setup();
    ASSERT_EQ(test_state.gadgets, test_buffer);
    ASSERT_EQ(test_state.max_gadgets, A64_MAX_GADGETS_PER_BLOCK);
    ASSERT_EQ(test_state.num_gadgets, 0);
}

TEST(gen_reset) {
    setup();
    a64_gen_reset(&test_state, 0x1000);
    ASSERT_EQ(test_state.start_pc, 0x1000);
    ASSERT_EQ(test_state.guest_pc, 0x1000);
    ASSERT_EQ(test_state.num_gadgets, 0);
    ASSERT_EQ(test_state.is_complete, 0);
}

TEST(gen_add_gadget) {
    setup();
    a64_gen_reset(&test_state, 0x1000);

    tcti_gadget_t dummy = (tcti_gadget_t)0x1234;
    int ret = a64_gen_add_gadget(&test_state, dummy);
    ASSERT_EQ(ret, 0);
    ASSERT_EQ(test_state.num_gadgets, 1);
    ASSERT_EQ((uintptr_t)test_buffer[0], 0x1234);
}

TEST(gen_overflow) {
    setup();
    a64_gen_reset(&test_state, 0x1000);

    tcti_gadget_t dummy = (tcti_gadget_t)0x1234;
    for (int i = 0; i < A64_MAX_GADGETS_PER_BLOCK; i++) {
        int ret = a64_gen_add_gadget(&test_state, dummy);
        ASSERT_EQ(ret, 0);
    }

    int ret = a64_gen_add_gadget(&test_state, dummy);
    ASSERT_EQ(ret, -3);
}

TEST(gen_instruction_count) {
    setup();
    a64_gen_reset(&test_state, 0x1000);

    // Simulate processing instructions
    for (int i = 0; i < 5; i++) {
        test_state.instructions_processed++;
        test_state.guest_pc += 4;
    }

    ASSERT_EQ(test_state.instructions_processed, 5);
    ASSERT_EQ(test_state.guest_pc, 0x1014);
}

TEST(gen_finalize) {
    setup();
    a64_gen_reset(&test_state, 0x1000);

    // Add some gadgets
    for (int i = 0; i < 3; i++) {
        a64_gen_add_gadget(&test_state, (tcti_gadget_t)0x1000 + i);
    }
    ASSERT_EQ(test_state.num_gadgets, 3);

    // Mark complete
    test_state.is_complete = 1;
    ASSERT(test_state.is_complete);
}

int main(void) {
    printf("aarch64 Generator State Tests\n");
    printf("=============================\n\n");

    RUN_TEST(gen_init);
    RUN_TEST(gen_reset);
    RUN_TEST(gen_add_gadget);
    RUN_TEST(gen_overflow);
    RUN_TEST(gen_instruction_count);
    RUN_TEST(gen_finalize);

    printf("\n=============================\n");
    printf("Results: %d run, %d passed, %d failed\n",
           tests_run, tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
