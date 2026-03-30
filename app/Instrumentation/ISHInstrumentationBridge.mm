/*
 * ISHInstrumentationBridge.m
 * Objective-C bridge implementing C API for ISHInstrumentation
 */

#import "ISHInstrumentationBridge.h"
#import "ISHInstrumentation.h"

#include <stdatomic.h>

// C-level atomic flags for startup safety
// These are checked BEFORE any Objective-C calls to prevent crashes
// when the bridge is called before the Objective-C runtime is ready
static atomic_bool instrumentation_bootstrapped = ATOMIC_VAR_INIT(false);
static atomic_bool instrumentation_active = ATOMIC_VAR_INIT(false);

void ish_instrumentation_bootstrap(void) {
    atomic_store(&instrumentation_bootstrapped, true);
    [ISHInstrumentation bootstrap];
}

void ish_instrumentation_activate(void) {
    atomic_store(&instrumentation_active, true);
    [ISHInstrumentation activate];
}

bool ish_instrumentation_is_active(void) {
    return atomic_load(&instrumentation_active);
}

void ish_instrumentation_record_event(ish_instrumentation_origin_t origin, const char *event_name) {
    (void)origin;

    // CRITICAL: Check C-level atomic flag FIRST before any Objective-C calls
    // This prevents crashes when the bridge is called before:
    //   - The Objective-C runtime is fully initialized
    //   - ISHInstrumentation is bootstrapped/activated
    //   - The app layer is ready
    if (!atomic_load(&instrumentation_active)) {
        return;
    }

    ISHInstrumentationEvent event = ISHInstrumentationEventBootstrapReady; // default

    if (strcmp(event_name, "task_proof_start_enter") == 0) {
        event = ISHInstrumentationEventTaskProofStartEnter;
    } else if (strcmp(event_name, "task_proof_before_pthread") == 0) {
        event = ISHInstrumentationEventTaskProofBeforePthread;
    } else if (strcmp(event_name, "task_proof_after_pthread") == 0) {
        event = ISHInstrumentationEventTaskProofAfterPthread;
    } else if (strcmp(event_name, "task_proof_thread_entry") == 0) {
        event = ISHInstrumentationEventTaskProofThreadEntry;
    } else if (strcmp(event_name, "task_proof_before_current_set") == 0) {
        event = ISHInstrumentationEventTaskProofBeforeCurrentSet;
    } else if (strcmp(event_name, "task_proof_after_current_set") == 0) {
        event = ISHInstrumentationEventTaskProofAfterCurrentSet;
    } else if (strcmp(event_name, "task_proof_run_current_enter") == 0) {
        event = ISHInstrumentationEventTaskProofRunCurrentEnter;
    } else if (strcmp(event_name, "task_proof_before_guest_cpu") == 0) {
        event = ISHInstrumentationEventTaskProofBeforeGuestCpu;
    } else if (strcmp(event_name, "task_proof_after_thread_entry") == 0) {
        event = ISHInstrumentationEventTaskProofAfterThreadEntry;
    } else if (strcmp(event_name, "task_proof_before_task_run_current") == 0) {
        event = ISHInstrumentationEventTaskProofBeforeTaskRunCurrent;
    } else if (strcmp(event_name, "task_proof_task_run_current_entry") == 0) {
        event = ISHInstrumentationEventTaskProofTaskRunCurrentEntry;
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

    // CRITICAL: Check C-level atomic flag FIRST before any Objective-C calls
    if (!atomic_load(&instrumentation_active)) {
        return 0;
    }

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

    // CRITICAL: Check C-level atomic flag FIRST before any Objective-C calls
    if (!atomic_load(&instrumentation_active)) {
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
    [ISHInstrumentation endInterval:@"" attributes:attributes];
}
