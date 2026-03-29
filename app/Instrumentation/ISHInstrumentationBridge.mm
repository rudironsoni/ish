/*
 * ISHInstrumentationBridge.m
 * Objective-C bridge implementing C API for ISHInstrumentation
 */

#import "ISHInstrumentationBridge.h"
#import "ISHInstrumentation.h"

void ish_instrumentation_bootstrap(void) {
    [ISHInstrumentation bootstrap];
}

void ish_instrumentation_activate(void) {
    [ISHInstrumentation activate];
}

bool ish_instrumentation_is_active(void) {
    return [ISHInstrumentation isActive];
}

void ish_instrumentation_record_event(ish_instrumentation_origin_t origin, const char *event_name) {
    (void)origin;
    (void)event_name;
    // Map event name string to ISHInstrumentationEvent enum
    // For now, record all events as BootstrapReady (minimal implementation)
    [ISHInstrumentation recordEvent:ISHInstrumentationEventBootstrapReady];
}

uint64_t ish_instrumentation_begin_interval(ish_instrumentation_origin_t origin, const char *interval_name,
                                             const ish_instrumentation_attribute_t *attrs, uint32_t attr_count) {
    (void)origin;
    NSString *name = interval_name ? [NSString stringWithUTF8String:interval_name] : @"";
    NSMutableDictionary *attributes = nil;
    if (attrs && attr_count > 0) {
        attributes = [NSMutableDictionary dictionaryWithCapacity:attr_count];
        for (uint32_t i = 0; i < attr_count; i++) {
            NSString *key = attrs[i].key ? [NSString stringWithUTF8String:attrs[i].key] : @"";
            NSString *value = attrs[i].value ? [NSString stringWithUTF8String:attrs[i].value] : @"";
            attributes[key] = value;
        }
    }
    [ISHInstrumentation beginInterval:name attributes:attributes];
    // Return a simple hash of the name as interval ID
    return (uint64_t)[name hash];
}

void ish_instrumentation_end_interval(uint64_t interval_id, const ish_instrumentation_attribute_t *attrs, uint32_t attr_count) {
    (void)interval_id;
    NSMutableDictionary *attributes = nil;
    if (attrs && attr_count > 0) {
        attributes = [NSMutableDictionary dictionaryWithCapacity:attr_count];
        for (uint32_t i = 0; i < attr_count; i++) {
            NSString *key = attrs[i].key ? [NSString stringWithUTF8String:attrs[i].key] : @"";
            NSString *value = attrs[i].value ? [NSString stringWithUTF8String:attrs[i].value] : @"";
            attributes[key] = value;
        }
    }
    [ISHInstrumentation endInterval:@"" attributes:attributes];
}
