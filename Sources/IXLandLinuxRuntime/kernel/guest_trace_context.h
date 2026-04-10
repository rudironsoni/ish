#ifndef IXLAND_GUEST_TRACE_CONTEXT_H
#define IXLAND_GUEST_TRACE_CONTEXT_H

#include <IXLandInstrumentation/IXLandInstrumentation.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void ixland_guest_trace_set_context(int64_t attempt_id, int64_t guest_pid, int64_t ui_pid,
                                    bool is_restart_path, bool has_terminal);

void ixland_guest_trace_set_guest_pid(int64_t guest_pid);

void ixland_guest_trace_emit(ixland_instrumentation_origin_t origin, const char *event_name);

void ixland_guest_trace_emit_int(ixland_instrumentation_origin_t origin, const char *event_name,
                                 const char *key, int64_t value);

void ixland_guest_trace_emit_int2(ixland_instrumentation_origin_t origin, const char *event_name,
                                  const char *key1, int64_t value1, const char *key2,
                                  int64_t value2);

void ixland_guest_trace_emit_attrs(ixland_instrumentation_origin_t origin, const char *event_name,
                                   const ixland_instrumentation_attribute_t *attrs,
                                   uint32_t attr_count);

typedef enum {
    IXLAND_GUEST_TRACE_FIELD_STRING = 0,
    IXLAND_GUEST_TRACE_FIELD_I64_DEC = 1,
    IXLAND_GUEST_TRACE_FIELD_U64_DEC = 2,
    IXLAND_GUEST_TRACE_FIELD_U64_HEX = 3,
} ixland_guest_trace_field_kind_t;

typedef struct {
    const char *key;
    ixland_guest_trace_field_kind_t kind;
    const char *string_value;
    int64_t i64_value;
    uint64_t u64_value;
} ixland_guest_trace_field_t;

void ixland_guest_trace_emit_structured(ixland_instrumentation_origin_t origin,
                                        const char *event_name,
                                        const ixland_guest_trace_field_t *fields,
                                        uint32_t field_count);

#ifdef __cplusplus
}
#endif

#endif
