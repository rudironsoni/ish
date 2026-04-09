/*
 * trace_backends.c
 * Minimal bridge forwarding backend.
 */

#include "trace.h"
#include "trace_types.h"

#include <stdbool.h>

/* ============================================
 * Instrumentation API (provided by IXLandInstrumentation package)
 * ============================================ */

#include <IXLandInstrumentation/IXLandInstrumentation.h>

/* ============================================
 * Thin Forwarding Backend
 * ============================================ */

static int bridge_init(void **ctx, trace_config_t *config)
{
    (void)ctx;
    (void)config;
    return 0;
}

static void bridge_shutdown(void *ctx)
{
    (void)ctx;
    /* No-op: shutdown is owned by the app layer */
}

static void bridge_emit(void *ctx, trace_record_t *record)
{
    (void)ctx;
    if (!record)
        return;

    /* Forward to the app-owned instrumentation bridge if active */
    if (ixland_instrumentation_is_active()) {
        const char *event_name = trace_event_name(record->header.event_id);
        ixland_instrumentation_record_event(IXLAND_INSTRUMENTATION_ORIGIN_EMULATOR, event_name);
    }
}

static void bridge_flush(void *ctx)
{
    (void)ctx;
    /* No-op: flushing is owned by the app layer */
}

static int bridge_dump(void *ctx, const char *path)
{
    (void)ctx;
    (void)path;
    /* Dump is owned by the app layer */
    return 0;
}

static const trace_backend_ops_t bridge_ops = {
    .init = bridge_init,
    .shutdown = bridge_shutdown,
    .emit = bridge_emit,
    .flush = bridge_flush,
    .dump = bridge_dump,
};

/* ============================================
 * Backend Selection
 * ============================================ */

const trace_backend_ops_t *trace_backend_get_ops(trace_backend_t backend)
{
    (void)backend;
    return &bridge_ops;
}
