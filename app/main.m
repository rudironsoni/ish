//
//  main.m
//  iSH
//
//  Created by Theodore Dubois on 10/17/17.
//

#import <UIKit/UIKit.h>
#import "AppDelegate.h"
#import "ExceptionExfiltrator.h"

// Trace system bootstrap at app entry point
#include "trace/trace.h"
#include <stdio.h>
#include <time.h>
#include <unistd.h>

int main(int argc, char * argv[]) {
    // ULTRA-EARLY: Write diagnostic before anything else
    NSString *tmpDir = NSTemporaryDirectory();
    NSString *diagPath = [tmpDir stringByAppendingPathComponent:@"main_entry.diag"];
    [@"main: ENTRY\n" writeToFile:diagPath atomically:NO encoding:NSUTF8StringEncoding error:nil];
    
    // Use iOS Caches directory for stable persistence across launches
    NSString *cachesDir = [NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES) firstObject];
    const char *markerPath = [[cachesDir stringByAppendingPathComponent:@"ish_run_marker"] UTF8String];
    const char *ringPath = [[cachesDir stringByAppendingPathComponent:@"ish_crash_trace.ring"] UTF8String];
    
    // Set global path for die() to use same location
    extern const char *g_crash_ring_path;
    g_crash_ring_path = ringPath;
    
    [@"main: before trace_init\n" writeToFile:diagPath atomically:NO encoding:NSUTF8StringEncoding error:nil];
    
    // APP-FIRST: Bootstrap trace system at earliest app boundary
    // MUST initialize BEFORE any trace calls (including trace_recover_previous_run)
    trace_config_t trace_config;
    trace_config_from_env(&trace_config);
    trace_init(&trace_config);
    
    [@"main: after trace_init\n" writeToFile:diagPath atomically:NO encoding:NSUTF8StringEncoding error:nil];
    
    // NEXT-LAUNCH RECOVERY: Check for previous crash after trace init
    // Marker existing means previous run started but didn't complete cleanly (crash)
    extern int trace_recover_previous_run(const char *marker_path, const char *ring_path);
    int recovered = trace_recover_previous_run(markerPath, ringPath);
    
    [@"main: after trace_recover\n" writeToFile:diagPath atomically:NO encoding:NSUTF8StringEncoding error:nil];
    
    // BOOTSTRAP PROOF: Synchronously write proof that main() was reached
    // This is distinct from the run marker - it proves bootstrap executed
    char bootstrapProofPath[1024];
    snprintf(bootstrapProofPath, sizeof(bootstrapProofPath), "%s/ish_bootstrap_proof", cachesDir.UTF8String);
    FILE *proofFp = fopen(bootstrapProofPath, "w");
    if (proofFp) {
        fprintf(proofFp, "BOOTSTRAP_REACHED\n");
        fprintf(proofFp, "timestamp: %lu\n", (unsigned long)time(NULL));
        fflush(proofFp);
        fsync(fileno(proofFp)); // Ensure durable commit
        fclose(proofFp);
    }
    
    [@"main: after bootstrap_proof\n" writeToFile:diagPath atomically:NO encoding:NSUTF8StringEncoding error:nil];
    
    // RUN STATE MARKER: Mark this run as started (for crash detection)
    // If we crash, this marker persists and next launch will detect it
    extern int trace_mark_run_started(const char *path);
    trace_mark_run_started(markerPath);
    
    [@"main: after run_marker\n" writeToFile:diagPath atomically:NO encoding:NSUTF8StringEncoding error:nil];
    
    // If we recovered from previous crash, log it
    if (recovered) {
        NSLog(@"[iSH] Recovered from previous crash - ring available at: %s", ringPath);
    }
    
    // Earliest guaranteed app bootstrap event
    trace_emit(TRACE_EVENT_APP_TRACE_BOOTSTRAP_READY, 0);
    
    // SYNC FLUSH: Ensure trace events are committed before continuing
    extern void trace_flush(void);
    trace_flush();
    
    [@"main: before UIApplicationMain\n" writeToFile:diagPath atomically:NO encoding:NSUTF8StringEncoding error:nil];
    
    NSSetUncaughtExceptionHandler(iSHExceptionHandler);
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([AppDelegate class]));
    }
}
