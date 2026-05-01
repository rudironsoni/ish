/*
 * trace_config.c
 *
 * RETIRED: This legacy config bridge is kept only for source compatibility.
 * Runtime trace-level ownership lives in trace.c, including the compile-level
 * default and the supported environment override parser.
 *
 * No code should call trace_config_from_env(). The function signature is
 * retained below for source compatibility, but the implementation is a no-op.
 */

#include "trace.h"

#include <stdlib.h>
#include <string.h>

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

/* TOMBSTONE: parse_event_mask - removed, no longer used */
/* TOMBSTONE: parse_regs_mask - removed, no longer used */
/* TOMBSTONE: parse_pc_range - removed, no longer used */

/* RETIRED: This function returns a minimal backend config and ignores
 * all trace-level policy. trace.c is the single owner for that policy.
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
    config->level = TRACE_LEVEL_NONE; /* Trace level is owned by trace.c. */
    config->ring_size = 0;
    config->category_mask = 0;
    config->event_mask = 0;

    return 0;
}

/* RETIRED: Configuration printing is no longer supported.
 * The new framework uses os_log and signposts directly.
 */
void trace_config_print(trace_config_t *config)
{
    (void)config;
}
