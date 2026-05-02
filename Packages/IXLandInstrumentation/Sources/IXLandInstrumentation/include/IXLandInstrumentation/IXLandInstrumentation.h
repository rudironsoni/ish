#ifndef IXLandInstrumentation_h
#define IXLandInstrumentation_h

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IXLAND_INSTRUMENTATION_ORIGIN_APP = 0,
    IXLAND_INSTRUMENTATION_ORIGIN_UI,
    IXLAND_INSTRUMENTATION_ORIGIN_SESSION,
    IXLAND_INSTRUMENTATION_ORIGIN_KERNEL,
    IXLAND_INSTRUMENTATION_ORIGIN_TASK,
    IXLAND_INSTRUMENTATION_ORIGIN_EXEC,
    IXLAND_INSTRUMENTATION_ORIGIN_EMULATOR,
    IXLAND_INSTRUMENTATION_ORIGIN_TCTI
} ixland_instrumentation_origin_t;

typedef struct {
    const char *key;
    const char *value;
} ixland_instrumentation_attribute_t;

typedef struct {
    void (*bootstrap)(void);
    void (*activate)(void);
    bool (*is_active)(void);
    void (*record_event)(ixland_instrumentation_origin_t origin, const char *event_name);
    void (*record_event_attrs)(ixland_instrumentation_origin_t origin, const char *event_name,
                               const ixland_instrumentation_attribute_t *attrs,
                               uint32_t attr_count);
    uint64_t (*begin_interval)(ixland_instrumentation_origin_t origin, const char *interval_name,
                               const ixland_instrumentation_attribute_t *attrs,
                               uint32_t attr_count);
    void (*end_interval)(uint64_t interval_id, const ixland_instrumentation_attribute_t *attrs,
                         uint32_t attr_count);
} ixland_instrumentation_sink_t;

void ixland_instrumentation_register_sink(const ixland_instrumentation_sink_t *sink);

void ixland_instrumentation_bootstrap(void);
void ixland_instrumentation_activate(void);
bool ixland_instrumentation_is_active(void);
void ixland_instrumentation_record_event(ixland_instrumentation_origin_t origin,
                                         const char *event_name);
void ixland_instrumentation_record_event_attrs(ixland_instrumentation_origin_t origin,
                                               const char *event_name,
                                               const ixland_instrumentation_attribute_t *attrs,
                                               uint32_t attr_count);
uint64_t ixland_instrumentation_begin_interval(ixland_instrumentation_origin_t origin,
                                               const char *interval_name,
                                               const ixland_instrumentation_attribute_t *attrs,
                                               uint32_t attr_count);
void ixland_instrumentation_end_interval(uint64_t interval_id,
                                         const ixland_instrumentation_attribute_t *attrs,
                                         uint32_t attr_count);

#ifdef __cplusplus
}
#endif

#endif
