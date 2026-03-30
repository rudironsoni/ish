/*
 * trace.h
 * Minimal semantic API for the iSH tracing subsystem.
 *
 * This header provides ONLY the minimal semantic surface that forwards to the
 * ISHInstrumentation framework. New code MUST use only these 6 functions.
 *
 * Legacy event-specific APIs are preserved in trace_internal.h for kernel
 * compatibility (these call sites in kernel/task.c, kernel/exec.c, kernel/mmap.c
 * cannot be modified per mission constraints). They are deprecated and should
 * not be used in new code.
 */

#ifndef TRACE_H
#define TRACE_H

#include "trace/trace_types.h"
#include "trace/trace_internal.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================
 * Minimal Semantic API (PUBLIC)
 * ============================================
 *
 * These 6 functions provide the public interface to the tracing subsystem.
 * They forward to the ISHInstrumentation framework via the C bridge.
 * NEW CODE MUST USE ONLY THESE FUNCTIONS.
 */

/*
 * Bootstrap the tracing system.
 * Called once during app startup before any events are recorded.
 */
void trace_bootstrap(void);

/*
 * Activate tracing after bootstrap is complete.
 * Tracing will be enabled after this call returns.
 */
void trace_activate(void);

/*
 * Check if tracing is currently active.
 * Returns true if bootstrap and activation have completed.
 */
bool trace_is_active(void);

/*
 * Record a semantic event.
 *
 * @param origin    The component that originated the event (from ISHInstrumentationBridge.h)
 * @param event_name The semantic event name (dotted notation, e.g., "task.created")
 */
void trace_record_event(int origin, const char *event_name);

/*
 * Begin a timed interval.
 *
 * @param origin        The component that originated the interval
 * @param interval_name The semantic interval name (dotted notation)
 * @param attrs         Optional array of key-value attribute pairs (may be NULL)
 * @param attr_count    Number of attributes in attrs array
 * @return              An interval ID that must be passed to trace_end_interval
 */
uint64_t trace_begin_interval(int origin, const char *interval_name,
                               const void *attrs, uint32_t attr_count);

/*
 * End a timed interval.
 *
 * @param interval_id   The ID returned by trace_begin_interval
 * @param attrs         Optional additional attributes to record at end (may be NULL)
 * @param attr_count    Number of attributes in attrs array
 */
void trace_end_interval(uint64_t interval_id, const void *attrs, uint32_t attr_count);

#ifdef __cplusplus
}
#endif

#endif /* TRACE_H */
