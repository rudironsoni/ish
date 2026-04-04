/*
 * test_trace_basic.c
 * Basic trace subsystem unit tests.
 */

#import <IXLandInstrumentationTracing/trace.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_count = 0;
static int fail_count = 0;

#define TEST(name) void test_##name(void)
#define RUN_TEST(name)                                                                             \
    do {                                                                                           \
        test_count++;                                                                              \
        printf("Running test_%s... ", #name);                                                      \
        test_##name();                                                                             \
        printf("PASS\n");                                                                          \
    } while (0)

#define ASSERT(cond)                                                                               \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "\nASSERTION FAILED: %s at %s:%d\n", #cond, __FILE__, __LINE__);       \
            fail_count++;                                                                          \
            return;                                                                                \
        }                                                                                          \
    } while (0)

/* Test 1: Backend selection via environment */
TEST(backend_selection)
{
    trace_config_t config;

    /* Test NOP backend */
    setenv("ISH_TRACE_BACKEND", "nop", 1);
    trace_config_from_env(&config);
    ASSERT(config.backend == TRACE_BACKEND_NOP);
    unsetenv("ISH_TRACE_BACKEND");

    /* Test RING backend (default) */
    memset(&config, 0, sizeof(config));
    trace_config_from_env(&config);
    ASSERT(config.backend == TRACE_BACKEND_RING);

    /* Test STDERR backend */
    setenv("ISH_TRACE_BACKEND", "stderr", 1);
    memset(&config, 0, sizeof(config));
    trace_config_from_env(&config);
    ASSERT(config.backend == TRACE_BACKEND_STDERR);
    unsetenv("ISH_TRACE_BACKEND");
}

/* Test 2: Trace level parsing */
TEST(level_parsing)
{
    trace_config_t config;

    setenv("ISH_TRACE_LEVEL", "0", 1);
    trace_config_from_env(&config);
    ASSERT(config.level == TRACE_LEVEL_OFF);

    setenv("ISH_TRACE_LEVEL", "2", 1);
    trace_config_from_env(&config);
    ASSERT(config.level == TRACE_LEVEL_BOUNDARY);

    setenv("ISH_TRACE_LEVEL", "5", 1);
    trace_config_from_env(&config);
    ASSERT(config.level == TRACE_LEVEL_FORENSIC);

    unsetenv("ISH_TRACE_LEVEL");
}

/* Test 3: Event filtering */
TEST(event_filtering)
{
    trace_config_t config;

    /* Test all events */
    setenv("ISH_TRACE_EVENTS", "all", 1);
    trace_config_from_env(&config);
    ASSERT(config.event_mask == ~0ULL);

    /* Test specific events */
    setenv("ISH_TRACE_EVENTS", "fault,syscall", 1);
    trace_config_from_env(&config);
    ASSERT(config.event_mask & (1ULL << TRACE_EVENT_FAULT));
    ASSERT(config.event_mask & (1ULL << TRACE_EVENT_SYSCALL_ENTER));
    ASSERT(config.event_mask & (1ULL << TRACE_EVENT_SYSCALL_RETURN));

    /* Test block event family */
    setenv("ISH_TRACE_EVENTS", "block.entry,block.exit", 1);
    trace_config_from_env(&config);
    ASSERT(config.event_mask & (1ULL << TRACE_EVENT_BLOCK_ENTRY));
    ASSERT(config.event_mask & (1ULL << TRACE_EVENT_BLOCK_EXIT));

    unsetenv("ISH_TRACE_EVENTS");
}

/* Test 4: Ring buffer initialization and emission */
TEST(ring_buffer_basic)
{
    trace_config_t config = {
        .backend = TRACE_BACKEND_RING,
        .level = TRACE_LEVEL_BLOCK,
        .ring_size = 100,
        .event_mask = ~0ULL,
        .category_mask = 0xFF,
    };

    int ret = trace_init(&config);
    ASSERT(ret == 0);
    ASSERT(trace_is_enabled());

    /* Emit some events */
    trace_emit_block_compile_start(0x1000);
    trace_emit_block_compile_end(0x1000, 0x1100, 10);
    trace_emit_block_entry(0x1000, 5);

    trace_ctx_t *ctx = trace_get_global();
    ASSERT(ctx != NULL);
    ASSERT(ctx->emitted_count == 3);

    trace_shutdown();
    ASSERT(!trace_is_enabled());
}

/* Test 5: Ring buffer wraparound */
TEST(ring_wraparound)
{
    /* Note: Minimum ring size is 1024, so use that */
    trace_config_t config = {
        .backend = TRACE_BACKEND_RING,
        .level = TRACE_LEVEL_INSTR,
        .ring_size = 1024,
        .event_mask = ~0ULL,
        .category_mask = 0xFF,
    };

    int ret = trace_init(&config);
    ASSERT(ret == 0);

    /* Emit 1500 events to overflow 1024-slot ring */
    for (int i = 0; i < 1500; i++) {
        trace_emit(TRACE_EVENT_BLOCK_ENTRY, 0x1000 + i);
    }

    trace_ctx_t *ctx = trace_get_global();
    ASSERT(ctx->emitted_count == 1500);
    ASSERT(ctx->ring != NULL);
    /* Ring should have wrapped after 1024 events */
    ASSERT(ctx->ring->wrapped);

    trace_shutdown();
}

/* Test 6: PC range filtering */
TEST(pc_range_filter)
{
    trace_config_t config = {
        .backend = TRACE_BACKEND_RING,
        .level = TRACE_LEVEL_BLOCK,
        .ring_size = 100,
        .event_mask = ~0ULL,
        .category_mask = 0xFF,
        .pc_start = 0x1000,
        .pc_end = 0x2000,
    };

    int ret = trace_init(&config);
    ASSERT(ret == 0);

    /* Event inside range should be enabled */
    ASSERT(trace_event_enabled(TRACE_EVENT_BLOCK_ENTRY, 0x1500));

    /* Event outside range should be disabled */
    ASSERT(!trace_event_enabled(TRACE_EVENT_BLOCK_ENTRY, 0x500));
    ASSERT(!trace_event_enabled(TRACE_EVENT_BLOCK_ENTRY, 0x2500));

    trace_shutdown();
}

/* Test 7: Max events limit */
TEST(max_events_limit)
{
    trace_config_t config = {
        .backend = TRACE_BACKEND_RING,
        .level = TRACE_LEVEL_INSTR,
        .ring_size = 100,
        .event_mask = ~0ULL,
        .category_mask = 0xFF,
        .max_events = 5,
    };

    int ret = trace_init(&config);
    ASSERT(ret == 0);

    /* Emit up to limit */
    for (int i = 0; i < 10; i++) {
        trace_emit(TRACE_EVENT_BLOCK_ENTRY, 0x1000 + i);
    }

    /* Should have stopped at limit */
    trace_ctx_t *ctx = trace_get_global();
    ASSERT(ctx->emitted_count == 5);
    ASSERT(!ctx->enabled); /* Disabled after limit */

    trace_shutdown();
}

/* Test 8: Event names and descriptions */
TEST(event_names)
{
    const char *name;

    name = trace_event_name(TRACE_EVENT_BLOCK_ENTRY);
    ASSERT(name != NULL);
    ASSERT(strcmp(name, "BLOCK_ENTRY") == 0);

    name = trace_event_name(TRACE_EVENT_FAULT);
    ASSERT(name != NULL);
    ASSERT(strcmp(name, "FAULT") == 0);

    /* Invalid event */
    name = trace_event_name(TRACE_EVENT_MAX);
    ASSERT(strcmp(name, "UNKNOWN") == 0);
}

/* Test 9: Level-based filtering */
TEST(level_filtering)
{
    trace_config_t config = {
        .backend = TRACE_BACKEND_RING,
        .level = TRACE_LEVEL_BOUNDARY,
        .ring_size = 100,
        .event_mask = ~0ULL,
        .category_mask = 0xFF,
    };

    int ret = trace_init(&config);
    ASSERT(ret == 0);

    /* Level 2 events should be enabled */
    ASSERT(trace_event_enabled(TRACE_EVENT_BLOCK_ENTRY, 0));
    ASSERT(trace_event_enabled(TRACE_EVENT_BLOCK_EXIT, 0));
    ASSERT(trace_event_enabled(TRACE_EVENT_FAULT, 0));

    /* Level 3 events should be disabled at level 2 */
    ASSERT(!trace_event_enabled(TRACE_EVENT_REGISTER_SNAPSHOT, 0));

    trace_shutdown();
}

/* Test 10: Block sidecar creation */
TEST(block_sidecar)
{
    trace_config_t config = {
        .backend = TRACE_BACKEND_RING,
        .level = TRACE_LEVEL_BLOCK,
        .ring_size = 100,
        .event_mask = ~0ULL,
        .category_mask = 0xFF,
    };

    int ret = trace_init(&config);
    ASSERT(ret == 0);

    /* Sidecar should be enabled at level 3 */
    ASSERT(trace_sidecar_enabled());

    /* Create a sidecar */
    trace_block_sidecar_t *sidecar = trace_sidecar_create(0x1000, 0x1100);
    ASSERT(sidecar != NULL);
    ASSERT(sidecar->start_pc == 0x1000);
    ASSERT(sidecar->end_pc == 0x1100);
    ASSERT(sidecar->insn_count == 0);

    /* Add instructions */
    trace_sidecar_add_insn(sidecar, 0x1000, 0xD10043FF, "sub sp, sp, #0x10");
    ASSERT(sidecar->insn_count == 1);
    ASSERT(sidecar->insns[0].pc == 0x1000);
    ASSERT(sidecar->insns[0].raw_insn == 0xD10043FF);

    /* Set gadget count */
    trace_sidecar_set_gadget_count(sidecar, 5);
    ASSERT(sidecar->gadget_count == 5);

    trace_shutdown();
}

/* Main test runner */
int main(void)
{
    printf("=== iSH Trace Subsystem Tests ===\n\n");

    RUN_TEST(backend_selection);
    RUN_TEST(level_parsing);
    RUN_TEST(event_filtering);
    RUN_TEST(ring_buffer_basic);
    RUN_TEST(ring_wraparound);
    RUN_TEST(pc_range_filter);
    RUN_TEST(max_events_limit);
    RUN_TEST(event_names);
    RUN_TEST(level_filtering);
    RUN_TEST(block_sidecar);

    printf("\n=== Results ===\n");
    printf("Tests run: %d\n", test_count);
    printf("Passed: %d\n", test_count - fail_count);
    printf("Failed: %d\n", fail_count);

    return fail_count > 0 ? 1 : 0;
}
