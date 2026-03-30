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

    ISHInstrumentationEvent event = ISHInstrumentationEventBootstrapReady; // default

    if (strcmp(event_name, "task_proof_start_enter") == 0) {
        event = ISHInstrumentationEventTaskProofStartEnter;
    } else if (strcmp(event_name, "task_proof_before_pthread") == 0) {
        event = ISHInstrumentationEventTaskProofBeforePthread;
    } else if (strcmp(event_name, "task_proof_after_pthread") == 0) {
        event = ISHInstrumentationEventTaskProofAfterPthread;
    } else if (strcmp(event_name, "task_proof_thread_entry") == 0) {
        event = ISHInstrumentationEventTaskProofThreadEntry;
    } else if (strcmp(event_name, "task_proof_after_current_set") == 0) {
        event = ISHInstrumentationEventTaskProofAfterCurrentSet;
    } else if (strcmp(event_name, "task_proof_run_current_enter") == 0) {
        event = ISHInstrumentationEventTaskProofRunCurrentEnter;
    } else if (strcmp(event_name, "task_proof_before_guest_cpu") == 0) {
        event = ISHInstrumentationEventTaskProofBeforeGuestCpu;
    } else if (strcmp(event_name, "session.bootstrap.ready") == 0) {
        event = ISHInstrumentationEventSessionBootstrapReady;
    } else if (strcmp(event_name, "session.exec.ready") == 0) {
        event = ISHInstrumentationEventSessionExecReady;
    } else if (strcmp(event_name, "guest.thread.start") == 0) {
        event = ISHInstrumentationEventGuestThreadStart;
    }
    // Add other event mappings as needed

    [ISHInstrumentation recordEvent:event];
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
