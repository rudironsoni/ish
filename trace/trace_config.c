/*
 * trace_config.c
 *
 * RETIRED: This file has been retired as part of the ISHInstrumentation
 * framework migration (Task 1.2). Environment-driven trace configuration is
 * no longer supported. The new instrumentation framework uses compile-time
 * flags and explicit API calls instead of environment variables.
 *
 * No code should call trace_config_from_env(). The function signature is
 * retained below for source compatibility, but the implementation is a no-op.
 */

#include "trace/trace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

/* TOMBSTONE: parse_event_mask - removed, no longer used */
/* TOMBSTONE: parse_regs_mask - removed, no longer used */
/* TOMBSTONE: parse_pc_range - removed, no longer used */

/* RETIRED: Environment-driven configuration is no longer supported.
 * This function now returns a minimal default configuration and ignores
 * all environment variables. The new ISHInstrumentation framework
 * replaces this with compile-time flags and explicit API initialization.
 */
int trace_config_from_env(trace_config_t *config)
{
    if (!config) {
        return -1;
    }

    /* Initialize with minimal defaults - instrumentation now owned by app layer */
    memset(config, 0, sizeof(trace_config_t));
#if defined(__APPLE__) && (TARGET_OS_IOS || TARGET_OS_SIMULATOR)
    config->backend = TRACE_BACKEND_NOP; /* Backend selection now in ISHInstrumentation */
#else
    config->backend = TRACE_BACKEND_NOP;
#endif
    config->level = TRACE_LEVEL_NONE; /* Tracing disabled by default - use ISHInstrumentation */
    config->ring_size = 0;
    config->category_mask = 0;
    config->event_mask = 0;

    /* NOTE: All environment variable parsing has been removed.
     * Environment-driven trace policy is retired. Use ISHInstrumentation
     * framework for app-level observability configuration.
     */

    return 0;
}

/* RETIRED: Configuration printing is no longer supported.
 * The new framework uses os_log and signposts directly.
 */
void trace_config_print(trace_config_t *config)
{
    (void)config;
    fprintf(stderr, "[TRACE] trace_config_print: RETIRED - use ISHInstrumentation instead\n");
}
