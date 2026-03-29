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
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

// Raw C helper: write marker file, no Foundation, no trace
static void write_marker_raw(const char *name, const char *content) {
    const char *home = getenv("HOME");
    if (!home) {
        home = "/tmp";
    }
    char path[1024];
    int n = snprintf(path, sizeof(path), "%s/Library/Caches/%s", home, name);
    if (n < 0 || n >= (int)sizeof(path)) {
        return;
    }
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return;
    }
    size_t len = strlen(content);
    write(fd, content, len);
    fsync(fd);
    close(fd);
}

int main(int argc, char * argv[]) {
    // ============================================================
    // EARLIEST POSSIBLE MARKER: Before ANY Foundation/Objective-C
    // ============================================================
    write_marker_raw("MAIN_PRE_AUTORELEASEPOOL", "REACHED\n");
    
    // ============================================================
    // STAGE 0: Minimal trace bootstrap (NOP backend only)
    // ============================================================
    trace_config_t trace_config;
    memset(&trace_config, 0, sizeof(trace_config));
    trace_config.backend = TRACE_BACKEND_NOP;
    trace_config.level = TRACE_LEVEL_SUMMARY;
    trace_config.ring_size = 256;
    trace_config.category_mask = 0xFF;
    trace_config.event_mask = ~0ULL;
    trace_init(&trace_config);
    
    // Marker after minimal Stage 0 bootstrap
    write_marker_raw("MAIN_POST_MINIMAL_STAGE0", "REACHED\n");
    
    // ============================================================
    // NOW safe to use Foundation
    // ============================================================
    @autoreleasepool {
        // Get caches directory via Foundation for later use
        NSString *cachesDir = [NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES) firstObject];
        const char *cachesPath = cachesDir.UTF8String;
        
        const char *markerPath = [[[NSString stringWithUTF8String:cachesPath] stringByAppendingPathComponent:@"ish_run_marker"] UTF8String];
        const char *ringPath = [[[NSString stringWithUTF8String:cachesPath] stringByAppendingPathComponent:@"ish_crash_trace.ring"] UTF8String];
        
        // Set global path for die() to use same location
        extern const char *g_crash_ring_path;
        g_crash_ring_path = ringPath;
        
        // Recovery check (after trace init, uses trace_emit internally)
        extern int trace_recover_previous_run(const char *marker_path, const char *ring_path);
        int recovered = trace_recover_previous_run(markerPath, ringPath);
        
        // Bootstrap proof
        char bootstrapProofPath[1024];
        snprintf(bootstrapProofPath, sizeof(bootstrapProofPath), "%s/ish_bootstrap_proof", cachesPath);
        FILE *proofFp = fopen(bootstrapProofPath, "w");
        if (proofFp) {
            fprintf(proofFp, "BOOTSTRAP_REACHED\n");
            fprintf(proofFp, "timestamp: %lu\n", (unsigned long)time(NULL));
            fflush(proofFp);
            fsync(fileno(proofFp));
            fclose(proofFp);
        }
        
        // Run marker
        extern int trace_mark_run_started(const char *path);
        trace_mark_run_started(markerPath);
        
        if (recovered) {
            NSLog(@"[iSH] Recovered from previous crash - ring available at: %s", ringPath);
        }
        
        trace_emit(TRACE_EVENT_APP_TRACE_BOOTSTRAP_READY, 0);
        extern void trace_flush(void);
        trace_flush();
        
        NSSetUncaughtExceptionHandler(iSHExceptionHandler);
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([AppDelegate class]));
    }
}
