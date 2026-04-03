/*
 * ISHInstrumentationBridge.h
 * C bridge to ISHInstrumentation for trace layer forwarding
 */

#ifndef ISH_INSTRUMENTATION_BRIDGE_H
#define ISH_INSTRUMENTATION_BRIDGE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Origin enum for event attribution */
typedef enum {
    ISH_INSTRUMENTATION_ORIGIN_APP = 0,
    ISH_INSTRUMENTATION_ORIGIN_UI,
    ISH_INSTRUMENTATION_ORIGIN_SESSION,
    ISH_INSTRUMENTATION_ORIGIN_KERNEL,
    ISH_INSTRUMENTATION_ORIGIN_TASK,
    ISH_INSTRUMENTATION_ORIGIN_EXEC,
    ISH_INSTRUMENTATION_ORIGIN_EMULATOR,
    ISH_INSTRUMENTATION_ORIGIN_TCTI
} ish_instrumentation_origin_t;

/* Attribute structure for contextual data */
typedef struct {
    const char *key;
    const char *value;
} ish_instrumentation_attribute_t;

/* Bootstrap the instrumentation system */
void ish_instrumentation_bootstrap(void);

/* Activate instrumentation after bootstrap */
void ish_instrumentation_activate(void);

/* Check if instrumentation is active */
bool ish_instrumentation_is_active(void);

/* Record a semantic event */
void ish_instrumentation_record_event(ish_instrumentation_origin_t origin, const char *event_name);

/* Begin a timed interval */
uint64_t ish_instrumentation_begin_interval(ish_instrumentation_origin_t origin, const char *interval_name,
                                             const ish_instrumentation_attribute_t *attrs, uint32_t attr_count);

/* End a timed interval */
void ish_instrumentation_end_interval(uint64_t interval_id, const ish_instrumentation_attribute_t *attrs, uint32_t attr_count);

#ifdef __cplusplus
}
#endif

#endif /* ISH_INSTRUMENTATION_BRIDGE_H */
