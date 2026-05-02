//
//  ISHInstrumentationSinkApple.m
//  iSH
//
//  Concrete Apple Unified Logging sink for ISHInstrumentation.
//  This is the single authoritative external logging sink for normal runtime observability.
//

#import "ISHInstrumentationSinkApple.h"
#import <os/log.h>

// MARK: - Subsystem and Categories
// One stable subsystem: app.ixland.terminal
// Categories: app, session, kernel, task, exec, emulator, tcti, terminal, crash

static const char *const kISHOSLogSubsystem = "app.ixland.terminal";

static os_log_t g_log_app;
static os_log_t g_log_session;
static os_log_t g_log_kernel;
static os_log_t g_log_task;
static os_log_t g_log_exec;
static os_log_t g_log_emulator;
static os_log_t g_log_tcti;
static os_log_t g_log_terminal;
static os_log_t g_log_crash;

// MARK: - Interval Storage
// Real interval correlation with stored metadata

@interface ISHIntervalRecord : NSObject
@property (nonatomic, strong) NSString *name;
@property (nonatomic, strong) NSDictionary *attributes;
@property (nonatomic, assign) uint64_t intervalId;
@property (nonatomic, assign) CFAbsoluteTime startTime;
@end

@implementation ISHIntervalRecord
@end

static NSMutableDictionary<NSNumber *, ISHIntervalRecord *> *g_intervalStore;
static uint64_t g_nextIntervalId = 1;
static dispatch_queue_t g_intervalQueue;

// MARK: - Category Resolution

static os_log_t log_for_category(NSString *category) {
    if (!category || category.length == 0) {
        return g_log_app;
    }
    NSString *cat = [category lowercaseString];
    if ([cat hasPrefix:@"session"] || [cat hasPrefix:@"bootstrap"]) {
        return g_log_session;
    } else if ([cat hasPrefix:@"kernel"]) {
        return g_log_kernel;
    } else if ([cat hasPrefix:@"task"]) {
        return g_log_task;
    } else if ([cat hasPrefix:@"exec"]) {
        return g_log_exec;
    } else if ([cat hasPrefix:@"emulator"] || [cat hasPrefix:@"emu"]) {
        return g_log_emulator;
    } else if ([cat hasPrefix:@"tcti"]) {
        return g_log_tcti;
    } else if ([cat hasPrefix:@"terminal"] || [cat hasPrefix:@"tty"]) {
        return g_log_terminal;
    } else if ([cat hasPrefix:@"crash"] || [cat hasPrefix:@"fault"]) {
        return g_log_crash;
    }
    return g_log_app;
}

static os_log_t log_for_event_name(const char *eventName) {
    if (!eventName) return g_log_app;
    NSString *name = [[NSString stringWithUTF8String:eventName] lowercaseString];
    if ([name hasPrefix:@"session."] || [name hasPrefix:@"bootstrap."]) {
        return g_log_session;
    } else if ([name hasPrefix:@"kernel."]) {
        return g_log_kernel;
    } else if ([name hasPrefix:@"task."]) {
        return g_log_task;
    } else if ([name hasPrefix:@"exec."]) {
        return g_log_exec;
    } else if ([name hasPrefix:@"emulator."] || [name hasPrefix:@"emu."]) {
        return g_log_emulator;
    } else if ([name hasPrefix:@"tcti."]) {
        return g_log_tcti;
    } else if ([name hasPrefix:@"terminal."] || [name hasPrefix:@"tty."]) {
        return g_log_terminal;
    } else if ([name hasPrefix:@"crash."] || [name hasPrefix:@"fault."]) {
        return g_log_crash;
    }
    return g_log_app;
}

// MARK: - Level Detection

typedef NS_ENUM(NSInteger, ISHLogLevel) {
    ISHLogLevelDebug,
    ISHLogLevelInfo,
    ISHLogLevelDefault,
    ISHLogLevelError,
    ISHLogLevelFault
};

static ISHLogLevel level_for_event_name(const char *eventName) {
    if (!eventName) return ISHLogLevelDefault;
    NSString *name = [NSString stringWithUTF8String:eventName];
    NSString *lower = [name lowercaseString];
    if ([lower containsString:@"fault"] || [lower containsString:@"crash"] ||
        [lower containsString:@"error_fatal"]) {
        return ISHLogLevelFault;
    } else if ([lower containsString:@"error"] || [lower containsString:@"fail"]) {
        return ISHLogLevelError;
    } else if ([lower hasPrefix:@"tcti."] || [lower hasPrefix:@"gadget."] ||
               [lower hasPrefix:@"mem."] || [lower hasPrefix:@"guest."] ||
               [lower hasPrefix:@"task.proof."] || [lower hasPrefix:@"boot.generic_openat."] ||
               [lower hasPrefix:@"boot.mount_find."] || [lower hasPrefix:@"boot.construct_task."]) {
        return ISHLogLevelDebug;
    } else if ([lower containsString:@"debug"] || [lower hasPrefix:@"trace."]) {
        return ISHLogLevelDebug;
    } else if ([lower containsString:@"ready"] || [lower containsString:@"complete"] ||
               [lower containsString:@"start"] || [lower containsString:@"enter"]) {
        return ISHLogLevelInfo;
    }
    return ISHLogLevelDefault;
}

static void log_with_level(os_log_t log, ISHLogLevel level, const char *eventName, NSDictionary *attrs) {
    NSString *nameStr = eventName ? [NSString stringWithUTF8String:eventName] : @"unknown";
    NSString *attrStr = attrs.count > 0 ? [attrs description] : @"";
    
    switch (level) {
        case ISHLogLevelDebug:
#if defined(NDEBUG)
            os_log_debug(log, "%{public}s: %{public}@", eventName ?: "unknown", attrStr);
#else
            /*
             * The trace level remains TRACE_LEVEL_DEBUG. In Debug app builds, publish
             * admitted Debug events as Info so simulator log capture can reliably
             * retrieve full emulator traces without per-event probes.
             */
            os_log_info(log, "%{public}s: %{public}@", eventName ?: "unknown", attrStr);
#endif
            break;
        case ISHLogLevelInfo:
            os_log_info(log, "%{public}s: %{public}@", eventName ?: "unknown", attrStr);
            break;
        case ISHLogLevelDefault:
            os_log(log, "%{public}s: %{public}@", eventName ?: "unknown", attrStr);
            break;
        case ISHLogLevelError:
            os_log_error(log, "%{public}s: %{public}@", eventName ?: "unknown", attrStr);
            break;
        case ISHLogLevelFault:
            os_log_fault(log, "%{public}s: %{public}@", eventName ?: "unknown", attrStr);
            break;
    }
}

@implementation ISHInstrumentationSinkApple

+ (void)initialize {
    if (self == [ISHInstrumentationSinkApple class]) {
        // Initialize OSLog loggers with subsystem and categories
        os_log_t base = os_log_create(kISHOSLogSubsystem, "app");
        g_log_app = base;
        g_log_session = os_log_create(kISHOSLogSubsystem, "session");
        g_log_kernel = os_log_create(kISHOSLogSubsystem, "kernel");
        g_log_task = os_log_create(kISHOSLogSubsystem, "task");
        g_log_exec = os_log_create(kISHOSLogSubsystem, "exec");
        g_log_emulator = os_log_create(kISHOSLogSubsystem, "emulator");
        g_log_tcti = os_log_create(kISHOSLogSubsystem, "tcti");
        g_log_terminal = os_log_create(kISHOSLogSubsystem, "terminal");
        g_log_crash = os_log_create(kISHOSLogSubsystem, "crash");
        
        // Initialize interval storage
        g_intervalQueue = dispatch_queue_create("app.ixland.terminal.intervals", DISPATCH_QUEUE_SERIAL);
        g_intervalStore = [NSMutableDictionary dictionary];
    }
}

+ (void)setup {
    // Sink is ready - OSLog initialized in +initialize
    os_log_info(g_log_session, "ISHInstrumentationSinkApple: setup complete");
}

+ (void)activate {
    os_log_info(g_log_session, "ISHInstrumentationSinkApple: activated");
}

+ (void)recordEvent:(NSString *)eventName attributes:(NSDictionary *)attributes {
    if (!eventName || eventName.length == 0) {
        os_log_error(g_log_app, "recordEvent called with empty event name");
        return;
    }
    
    const char *nameC = [eventName UTF8String];
    os_log_t log = log_for_event_name(nameC);
    ISHLogLevel level = level_for_event_name(nameC);
    
    log_with_level(log, level, nameC, attributes);
}

+ (uint64_t)beginInterval:(NSString *)intervalName attributes:(NSDictionary *)attributes {
    if (!intervalName || intervalName.length == 0) {
        os_log_error(g_log_app, "beginInterval called with empty name");
        return 0;
    }
    
    __block uint64_t intervalId = 0;
    dispatch_sync(g_intervalQueue, ^{
        intervalId = g_nextIntervalId++;
        
        ISHIntervalRecord *record = [[ISHIntervalRecord alloc] init];
        record.name = intervalName;
        record.attributes = attributes ?: @{};
        record.intervalId = intervalId;
        record.startTime = CFAbsoluteTimeGetCurrent();
        
        g_intervalStore[@(intervalId)] = record;
    });
    
    os_log_t log = log_for_category([intervalName componentsSeparatedByString:@"."].firstObject);
    NSString *beginAttrs = attributes.count > 0 ? [attributes description] : @"";
    os_log_info(log, "BEGIN interval %{public}@ (id=%llu) %{public}@", intervalName, intervalId,
                beginAttrs);
    
    return intervalId;
}

+ (void)endInterval:(uint64_t)intervalId attributes:(NSDictionary *)attributes {
    if (intervalId == 0) {
        os_log_error(g_log_app, "endInterval called with invalid id=0");
        return;
    }
    
    __block ISHIntervalRecord *record = nil;
    dispatch_sync(g_intervalQueue, ^{
        record = g_intervalStore[@(intervalId)];
        if (record) {
            [g_intervalStore removeObjectForKey:@(intervalId)];
        }
    });
    
    if (!record) {
        os_log_error(g_log_app, "endInterval: unknown interval id=%llu", intervalId);
        return;
    }
    
    CFAbsoluteTime endTime = CFAbsoluteTimeGetCurrent();
    NSTimeInterval duration = endTime - record.startTime;
    
    NSMutableDictionary *mergedAttrs = [record.attributes mutableCopy] ?: [NSMutableDictionary dictionary];
    if (attributes.count > 0) {
        [mergedAttrs addEntriesFromDictionary:attributes];
    }
    NSString *endAttrs = mergedAttrs.count > 0 ? [mergedAttrs description] : @"";

    os_log_t log = log_for_category([record.name componentsSeparatedByString:@"."].firstObject);
    os_log_info(log, "END interval %{public}@ (id=%llu) duration=%.3fs %{public}@", 
                record.name, intervalId, duration, endAttrs);
}

+ (void)recordEventWithName:(const char *)eventName {
    [self recordEvent:@(eventName ?: "unknown") attributes:nil];
}

@end
