//
//  ISHInstrumentationSinkApple.m
//  iSH
//
//  Apple sink implementation using os_log and signposts
//

#import "ISHInstrumentationSinkApple.h"
#import <os/log.h>

#if defined(__IPHONE_12_0) || defined(__MAC_10_14)
#import <os/signpost.h>
#endif

// Static log handles
static os_log_t g_ish_log = NULL;
static os_log_t g_ish_signpost_log = NULL;

// File-based trace output for direct evidence capture
static NSFileHandle *g_ish_trace_file = NULL;
static dispatch_queue_t g_ish_trace_queue = NULL;

// Event name mapping
static inline const char *ISHInstrumentationEventName(ISHInstrumentationEvent event) {
    switch (event) {
        case ISHInstrumentationEventBootstrapReady:
            return "app.bootstrap.ready";
        case ISHInstrumentationEventLaunchBegan:
            return "app.launch.began";
        case ISHInstrumentationEventLaunchReady:
            return "app.launch.ready";
        case ISHInstrumentationEventSceneConnected:
            return "app.scene.connected";
        case ISHInstrumentationEventSessionStarted:
            return "session.started";
        case ISHInstrumentationEventSessionReady:
            return "session.ready";
        case ISHInstrumentationEventSessionBootstrapDeferred:
            return "session.bootstrap.deferred";
        case ISHInstrumentationEventRecoveryDetected:
            return "recovery.detected";
        case ISHInstrumentationEventShutdownClean:
            return "shutdown.clean";
        case ISHInstrumentationEventSessionBootstrapReady:
            return "session.bootstrap.ready";
        case ISHInstrumentationEventSessionExecReady:
            return "session.exec.ready";
        case ISHInstrumentationEventGuestThreadStart:
            return "guest.thread.start";
        // Proof point events
        case ISHInstrumentationEventTaskProofStartEnter:
            return "task.proof.start_enter";
        case ISHInstrumentationEventTaskProofBeforePthread:
            return "task.proof.before_pthread";
        case ISHInstrumentationEventTaskProofAfterPthread:
            return "task.proof.after_pthread";
        case ISHInstrumentationEventTaskProofThreadEntry:
            return "task.proof.thread_entry";
        case ISHInstrumentationEventTaskProofAfterCurrentSet:
            return "task.proof.after_current_set";
        case ISHInstrumentationEventTaskProofRunCurrentEnter:
            return "task.proof.run_current_enter";
        case ISHInstrumentationEventTaskProofBeforeGuestCpu:
            return "task.proof.before_guest_cpu";
        // Paired diagnostic proof points
        case ISHInstrumentationEventTaskProofAfterThreadEntry:
            return "task.proof.after_thread_entry";
        case ISHInstrumentationEventTaskProofBeforeTaskRunCurrent:
            return "task.proof.before_task_run_current";
        case ISHInstrumentationEventTaskProofTaskRunCurrentEntry:
            return "task.proof.task_run_current_entry";
        // APPSIM-004 Stage 3A: Guest exec continuity and first output
        case ISHInstrumentationEventGuestExecTarget:
            return "task.proof.guest.exec.target";
        case ISHInstrumentationEventGuestExecSuccess:
            return "task.proof.guest.exec.success";
        case ISHInstrumentationEventGuestPidAliveAfterExec:
            return "task.proof.guest.pid.alive_after_exec";
        case ISHInstrumentationEventGuestWriteAttempt:
            return "task.proof.guest.write.attempt";
        case ISHInstrumentationEventGuestIoctlAttempt:
            return "task.proof.guest.ioctl.attempt";
        case ISHInstrumentationEventGuestReadAttempt:
            return "task.proof.guest.read.attempt";
        // APPSIM-004 Stage 3B: stdio wiring proof
        case ISHInstrumentationEventStdioFd0Target:
            return "task.proof.stdio.fd0.target";
        case ISHInstrumentationEventStdioFd1Target:
            return "task.proof.stdio.fd1.target";
        case ISHInstrumentationEventStdioFd2Target:
            return "task.proof.stdio.fd2.target";
        case ISHInstrumentationEventStdioPtySlaveBound:
            return "task.proof.stdio.pty.slave.bound";
        case ISHInstrumentationEventStdioTtySessionState:
            return "task.proof.stdio.tty.session.state";
        default:
            return "unknown";
    }
}

@implementation ISHInstrumentationSinkApple

+ (void)setup {
    // Create log handles with subsystem com.rudironsoni.ish
    g_ish_log = os_log_create("com.rudironsoni.ish", "instrumentation");
    g_ish_signpost_log = os_log_create("com.rudironsoni.ish", "intervals");
    
    // Create serial queue for file operations
    g_ish_trace_queue = dispatch_queue_create("com.rudironsoni.ish.trace", DISPATCH_QUEUE_SERIAL);
    
    // Setup file-based trace in tmp directory (writeable in simulator)
    dispatch_async(g_ish_trace_queue, ^{
        NSString *tmpDir = NSTemporaryDirectory();
        NSString *tracePath = [tmpDir stringByAppendingPathComponent:@"ish_trace.txt"];
        
        // Create/truncate file
        [[NSFileManager defaultManager] createFileAtPath:tracePath
                                                contents:nil
                                              attributes:nil];
        g_ish_trace_file = [NSFileHandle fileHandleForWritingAtPath:tracePath];
        
        if (g_ish_trace_file) {
            NSString *header = @"=== iSH Trace Log ===\n";
            [g_ish_trace_file writeData:[header dataUsingEncoding:NSUTF8StringEncoding]];
        }
    });
}

+ (void)activate {
    // Sink is ready to receive events
    os_log(g_ish_log, "ISHInstrumentationSinkApple activated");
}

+ (void)recordEvent:(ISHInstrumentationEvent)event {
    const char *eventName = ISHInstrumentationEventName(event);

    os_log(g_ish_log, "Event: %{public}s", eventName);

    // Write to file for direct evidence capture (proof points must be visible)
    dispatch_async(g_ish_trace_queue, ^{
        if (g_ish_trace_file) {
            NSString *line = [NSString stringWithFormat:@"[EVENT] %s\n", eventName];
            [g_ish_trace_file writeData:[line dataUsingEncoding:NSUTF8StringEncoding]];
            [g_ish_trace_file synchronizeFile];
        }
    });

#if defined(__IPHONE_12_0) || defined(__MAC_10_14)
    if (@available(iOS 12.0, macOS 10.14, *)) {
        os_signpost_event_emit(g_ish_signpost_log,
                               OS_SIGNPOST_ID_EXCLUSIVE,
                               "event",
                               "Event: %{public}s", eventName);
    }
#endif
}

+ (void)beginInterval:(NSString *)name attributes:(nullable NSDictionary *)attributes {
    os_log(g_ish_log, "Begin interval: %{public}@", name);
    
    // Write to file for direct evidence capture
    dispatch_async(g_ish_trace_queue, ^{
        if (g_ish_trace_file) {
            NSMutableString *line = [NSMutableString stringWithFormat:@"[TRACE] %@", name];
            if (attributes.count > 0) {
                [attributes enumerateKeysAndObjectsUsingBlock:^(id key, id value, BOOL *stop) {
                    [line appendFormat:@" | %@=%@", key, value];
                }];
            }
            [line appendString:@"\n"];
            [g_ish_trace_file writeData:[line dataUsingEncoding:NSUTF8StringEncoding]];
            [g_ish_trace_file synchronizeFile];
        }
    });

#if defined(__IPHONE_12_0) || defined(__MAC_10_14)
    if (@available(iOS 12.0, macOS 10.14, *)) {
        os_signpost_id_t signpostID = os_signpost_id_generate(g_ish_signpost_log);
        os_signpost_interval_begin(g_ish_signpost_log,
                                   signpostID,
                                   "interval",
                                   "Begin: %{public}@", name);
    }
#endif

    // Log attributes if present
    if (attributes.count > 0) {
        [attributes enumerateKeysAndObjectsUsingBlock:^(id key, id value, BOOL *stop) {
            os_log(g_ish_log, "  %{public}@: %{public}@", key, value);
        }];
    }
}

+ (void)endInterval:(NSString *)name attributes:(nullable NSDictionary *)attributes {
    os_log(g_ish_log, "End interval: %{public}@", name);

#if defined(__IPHONE_12_0) || defined(__MAC_10_14)
    if (@available(iOS 12.0, macOS 10.14, *)) {
        // Note: In a full implementation, we'd look up the stored signpostID
        os_signpost_id_t signpostID = OS_SIGNPOST_ID_NULL;
        os_signpost_interval_end(g_ish_signpost_log,
                                 signpostID,
                                 "interval",
                                 "End: %{public}@", name);
    }
#endif

    // Log attributes if present
    if (attributes.count > 0) {
        [attributes enumerateKeysAndObjectsUsingBlock:^(id key, id value, BOOL *stop) {
            os_log(g_ish_log, "  %{public}@: %{public}@", key, value);
        }];
    }
}

@end
