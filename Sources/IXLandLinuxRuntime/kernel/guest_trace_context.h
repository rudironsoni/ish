#ifndef IXLAND_GUEST_TRACE_CONTEXT_H
#define IXLAND_GUEST_TRACE_CONTEXT_H

#include <stdbool.h>
#include <stdint.h>

#include <IXLandInstrumentation/IXLandInstrumentation.h>

#ifdef __cplusplus
extern "C" {
#endif

void ixland_guest_trace_set_context(int64_t attempt_id,
                                    int64_t guest_pid,
                                    int64_t ui_pid,
                                    bool is_restart_path,
                                    bool has_terminal);

void ixland_guest_trace_set_guest_pid(int64_t guest_pid);

void ixland_guest_trace_emit(ixland_instrumentation_origin_t origin,
                             const char *event_name);

void ixland_guest_trace_emit_int(ixland_instrumentation_origin_t origin,
                                 const char *event_name,
                                 const char *key,
                                 int64_t value);

void ixland_guest_trace_emit_int2(ixland_instrumentation_origin_t origin,
                                  const char *event_name,
                                  const char *key1,
                                  int64_t value1,
                                  const char *key2,
                                  int64_t value2);

#ifdef __cplusplus
}
#endif

#endif
