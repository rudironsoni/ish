#include "IXLandInstrumentation/IXLandInstrumentation.h"

#include <stdatomic.h>

static const ixland_instrumentation_sink_t *registered_sink = NULL;

void ixland_instrumentation_register_sink(const ixland_instrumentation_sink_t *sink)
{
    if (sink != NULL) {
        registered_sink = sink;
    }
}

void ixland_instrumentation_bootstrap(void)
{
    if (registered_sink && registered_sink->bootstrap) {
        registered_sink->bootstrap();
    }
}

void ixland_instrumentation_activate(void)
{
    if (registered_sink && registered_sink->activate) {
        registered_sink->activate();
    }
}

bool ixland_instrumentation_is_active(void)
{
    if (registered_sink && registered_sink->is_active) {
        return registered_sink->is_active();
    }
    return false;
}

void ixland_instrumentation_record_event(ixland_instrumentation_origin_t origin,
                                         const char *event_name)
{
    if (registered_sink && registered_sink->record_event) {
        registered_sink->record_event(origin, event_name);
    }
}

uint64_t ixland_instrumentation_begin_interval(ixland_instrumentation_origin_t origin,
                                               const char *interval_name,
                                               const ixland_instrumentation_attribute_t *attrs,
                                               uint32_t attr_count)
{
    if (registered_sink && registered_sink->begin_interval) {
        return registered_sink->begin_interval(origin, interval_name, attrs, attr_count);
    }
    return 0;
}

void ixland_instrumentation_end_interval(uint64_t interval_id,
                                         const ixland_instrumentation_attribute_t *attrs,
                                         uint32_t attr_count)
{
    if (registered_sink && registered_sink->end_interval) {
        registered_sink->end_interval(interval_id, attrs, attr_count);
    }
}