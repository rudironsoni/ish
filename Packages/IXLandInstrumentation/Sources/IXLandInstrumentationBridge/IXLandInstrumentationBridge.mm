/*
 * IXLandInstrumentationBridge.mm
 * Apple-side sink implementation for IXLandInstrumentation.
 * Translates C instrumentation calls to ISHInstrumentation Objective-C API.
 */

#import "IXLandInstrumentationBridge.h"
#import "IXLandInstrumentation/IXLandInstrumentation.h"
#import "ISHInstrumentation.h"

#include <stdatomic.h>
#include <string.h>

static atomic_bool bridge_initialized = ATOMIC_VAR_INIT(false);
static atomic_bool bridge_active = ATOMIC_VAR_INIT(false);

static void bridge_bootstrap(void) {
    atomic_store(&bridge_initialized, true);
    [ISHInstrumentation bootstrap];
}

static void bridge_activate(void) {
    atomic_store(&bridge_active, true);
    [ISHInstrumentation activate];
}

static bool bridge_is_active(void) {
    return atomic_load(&bridge_active);
}

static void bridge_record_event(ixland_instrumentation_origin_t origin, const char *event_name) {
    (void)origin;

    if (!atomic_load(&bridge_active)) {
        return;
    }

    if (!event_name || strlen(event_name) == 0) {
        return;
    }

    // Forward semantic event name directly to ISHInstrumentation
    // NO remapping to enum - original semantic names preserved
    NSString *name = [NSString stringWithUTF8String:event_name];
    [ISHInstrumentation recordEvent:name];
}

static void bridge_record_event_attrs(ixland_instrumentation_origin_t origin,
                                      const char *event_name,
                                      const ixland_instrumentation_attribute_t *attrs,
                                      uint32_t attr_count) {
    (void)origin;

    if (!atomic_load(&bridge_active)) {
        return;
    }

    if (!event_name || strlen(event_name) == 0) {
        return;
    }

    NSString *name = [NSString stringWithUTF8String:event_name];
    NSMutableDictionary *attributes = nil;
    if (attrs && attr_count > 0) {
        attributes = [NSMutableDictionary dictionaryWithCapacity:attr_count];
        for (uint32_t i = 0; i < attr_count; i++) {
            NSString *key = attrs[i].key ? [NSString stringWithUTF8String:attrs[i].key] : @"";
            NSString *value = attrs[i].value ? [NSString stringWithUTF8String:attrs[i].value] : @"";
            attributes[key] = value;
        }
    }

    [ISHInstrumentation recordEvent:name attributes:attributes];
}

static uint64_t bridge_begin_interval(ixland_instrumentation_origin_t origin, const char *interval_name,
                                      const ixland_instrumentation_attribute_t *attrs, uint32_t attr_count) {
    (void)origin;

    if (!atomic_load(&bridge_active)) {
        return 0;
    }

    if (!interval_name || strlen(interval_name) == 0) {
        return 0;
    }

    NSString *name = [NSString stringWithUTF8String:interval_name];
    NSMutableDictionary *attributes = nil;
    if (attrs && attr_count > 0) {
        attributes = [NSMutableDictionary dictionaryWithCapacity:attr_count];
        for (uint32_t i = 0; i < attr_count; i++) {
            NSString *key = attrs[i].key ? [NSString stringWithUTF8String:attrs[i].key] : @"";
            NSString *value = attrs[i].value ? [NSString stringWithUTF8String:attrs[i].value] : @"";
            attributes[key] = value;
        }
    }
    
    // ISHInstrumentation returns a real interval ID from the sink
    return [ISHInstrumentation beginInterval:name attributes:attributes];
}

static void bridge_end_interval(uint64_t interval_id, const ixland_instrumentation_attribute_t *attrs, uint32_t attr_count) {
    if (!atomic_load(&bridge_active)) {
        return;
    }

    if (interval_id == 0) {
        return;
    }

    NSMutableDictionary *attributes = nil;
    if (attrs && attr_count > 0) {
        attributes = [NSMutableDictionary dictionaryWithCapacity:attr_count];
        for (uint32_t i = 0; i < attr_count; i++) {
            NSString *key = attrs[i].key ? [NSString stringWithUTF8String:attrs[i].key] : @"";
            NSString *value = attrs[i].value ? [NSString stringWithUTF8String:attrs[i].value] : @"";
            attributes[key] = value;
        }
    }
    
    // Pass the actual interval ID for proper correlation
    // The sink looks up the interval name from its storage
    [ISHInstrumentation endInterval:interval_id attributes:attributes];
}

static const ixland_instrumentation_sink_t bridge_sink = {
    .bootstrap = bridge_bootstrap,
    .activate = bridge_activate,
    .is_active = bridge_is_active,
    .record_event = bridge_record_event,
    .record_event_attrs = bridge_record_event_attrs,
    .begin_interval = bridge_begin_interval,
    .end_interval = bridge_end_interval,
};

void ixland_instrumentation_bridge_register(void) {
    ixland_instrumentation_register_sink(&bridge_sink);
}
