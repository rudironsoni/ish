/*
 * trace_backends.c
 * Thin bridge-only forwarding layer to ISHInstrumentation.
 *
 * This file has been rewritten as a thin forwarding layer.
 * All backend implementations, startup logic, recovery, ring persistence,
 * policy/config logic, proof markers, and low-level startup I/O have been
 * removed.
 *
 * The trace layer now forwards to the app-owned ISHInstrumentation bridge
 * which assumes app activation has already happened.
 */

#include "trace/trace.h"
#include "trace/trace_types.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================
 * Bridge Forwarding to ISHInstrumentationBridge
 * ============================================ */

#include "app/Instrumentation/ISHInstrumentationBridge.h"

/* ============================================
 * Thin Forwarding Backend
 * ============================================ */

static int bridge_init(void **ctx, trace_config_t *config)
{
    (void)ctx;
    (void)config;
    /* Bootstrap and activation are owned by the app layer */
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
    if (!record) return;

    /* Forward to the app-owned instrumentation bridge */
    if (ish_instrumentation_is_active()) {
        const char *event_name = trace_event_name(record->header.event_id);
        ish_instrumentation_record_event(ISH_INSTRUMENTATION_ORIGIN_EMULATOR, event_name);
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
    /* All backends now route through the thin bridge */
    return &bridge_ops;
}
