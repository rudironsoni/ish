/*
 * trace_backends.c
 * Bridge forwarding layer with ring buffer pre-crash capture.
 *
 * This file now maintains a ring buffer that captures events even before
 * the bridge is activated. On crash, the ring buffer can be dumped to
 * provide forensic evidence of the events leading to the crash.
 */

#include "trace.h"
#include "trace_types.h"

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================
 * Instrumentation API (provided by IXLandInstrumentation package)
 * ============================================ */

#include <IXLandInstrumentation/IXLandInstrumentation.h>

/* ============================================
 * Ring Buffer State - Always Active
 * ============================================ */

static trace_ring_t g_ring_buffer = { 0 };
static int g_ring_initialized = 0;

/* Initialize ring buffer early - can be called before trace_init */
static int ensure_ring_buffer(void)
{
    if (g_ring_initialized)
        return 0;

    /* Default capacity: 16384 records (enough for pre-crash capture) */
    size_t capacity = 16384;

    g_ring_buffer.records = calloc(capacity, sizeof(trace_record_t));
    if (!g_ring_buffer.records) {
        fprintf(stderr, "[RING] Failed to allocate ring buffer\n");
        return -1;
    }

    g_ring_buffer.capacity = capacity;
    g_ring_buffer.head = 0;
    g_ring_buffer.seq = 0;
    g_ring_buffer.dropped = 0;
    g_ring_buffer.wrapped = false;
    g_ring_initialized = 1;

    fprintf(stderr, "[RING-INIT] capacity=%zu (pre-crash capture enabled)\n", capacity);
    return 0;
}

/* Write record to ring buffer - always active regardless of bridge state */
static void ring_buffer_write(trace_record_t *record)
{
    if (!g_ring_initialized) {
        ensure_ring_buffer();
    }
    if (!g_ring_buffer.records || !record)
        return;

    /* Calculate write position */
    size_t idx = g_ring_buffer.head % g_ring_buffer.capacity;

    /* Copy record to ring */
    memcpy(&g_ring_buffer.records[idx], record, sizeof(trace_record_t));

    /* Update sequence */
    g_ring_buffer.head++;
    g_ring_buffer.seq++;

    /* Mark as wrapped if we've filled buffer */
    if (g_ring_buffer.head >= g_ring_buffer.capacity) {
        g_ring_buffer.wrapped = true;
        g_ring_buffer.dropped++;
    }
}

/* Get global ring buffer for crash dump */
trace_ring_t *trace_get_global_ring(void)
{
    if (!g_ring_initialized) {
        ensure_ring_buffer();
    }
    return g_ring_initialized ? &g_ring_buffer : NULL;
}

/* ============================================
 * Thin Forwarding Backend
 * ============================================ */

static int bridge_init(void **ctx, trace_config_t *config)
{
    (void)ctx;
    (void)config;
    /* Initialize ring buffer early for pre-crash capture */
    ensure_ring_buffer();
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
    if (!record)
        return;

    /* ALWAYS write to ring buffer first - this captures pre-crash events */
    ring_buffer_write(record);

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
    /* All backends now route through the thin bridge */
    return &bridge_ops;
}
