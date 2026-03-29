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
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

// Get Caches path using raw C API - works before autoreleasepool
static int get_caches_path(char *buf, size_t buflen) {
    // Use NSTemporaryDirectory which works without autoreleasepool in main()
    // but wrap in autoreleasepool to be safe
    @autoreleasepool {
        NSString *cachesDir = [NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES) firstObject];
        if (!cachesDir) return -1;
        const char *path = cachesDir.UTF8String;
        if (!path) return -1;
        size_t len = strlen(path);
        if (len >= buflen) return -1;
        memcpy(buf, path, len + 1);
        return 0;
    }
}

int main(int argc, char * argv[]) {
    // Use autoreleasepool for ALL Foundation operations
    @autoreleasepool {
        // Get caches directory
        char cachesPath[1024];
        if (get_caches_path(cachesPath, sizeof(cachesPath)) < 0) {
            // Fallback: use tmpdir
            const char *tmpdir = getenv("TMPDIR");
            if (!tmpdir) tmpdir = "/tmp";
            snprintf(cachesPath, sizeof(cachesPath), "%s", tmpdir);
        }
        
        const char *markerPath = [[[NSString stringWithUTF8String:cachesPath] stringByAppendingPathComponent:@"ish_run_marker"] UTF8String];
        const char *ringPath = [[[NSString stringWithUTF8String:cachesPath] stringByAppendingPathComponent:@"ish_crash_trace.ring"] UTF8String];
        
        // Set global path for die() to use same location
        extern const char *g_crash_ring_path;
        g_crash_ring_path = ringPath;
        
        // BOOTSTRAP CRASH REDUCTION: Write durable markers around trace_init
        // These use raw C file operations only, no trace system
        char preTracePath[1024];
        char postTracePath[1024];
        snprintf(preTracePath, sizeof(preTracePath), "%s/BOOTSTRAP_PRE_TRACE", cachesPath);
        snprintf(postTracePath, sizeof(postTracePath), "%s/BOOTSTRAP_POST_TRACE", cachesPath);
        
        // Write PRE marker before trace_init
        int fd_pre = open(preTracePath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd_pre >= 0) {
            write(fd_pre, "PRE_TRACE\n", 10);
            fsync(fd_pre);
            close(fd_pre);
        }
        
        // STAGE 0: Minimal trace bootstrap - NOP backend only
        // Heavy backend bring-up moved to Stage 1 in AppDelegate
        trace_config_t trace_config;
        memset(&trace_config, 0, sizeof(trace_config));
        trace_config.backend = TRACE_BACKEND_NOP;  // Minimal, no I/O
        trace_config.level = TRACE_LEVEL_SUMMARY;
        trace_config.ring_size = 256;
        trace_config.category_mask = 0xFF;
        trace_config.event_mask = ~0ULL;
        trace_init(&trace_config);
        
        // Write POST marker after trace_init
        int fd_post = open(postTracePath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd_post >= 0) {
            write(fd_post, "POST_TRACE\n", 11);
            fsync(fd_post);
            close(fd_post);
        }
        
        // NEXT-LAUNCH RECOVERY: Check for previous crash after trace init
        extern int trace_recover_previous_run(const char *marker_path, const char *ring_path);
        int recovered = trace_recover_previous_run(markerPath, ringPath);
        
        // BOOTSTRAP PROOF: Synchronously write proof that main() was reached
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
        
        // RUN STATE MARKER: Mark this run as started
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
