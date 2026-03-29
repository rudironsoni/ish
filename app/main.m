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
// Returns 0 on success, -1 on failure
static int write_marker_raw(const char *base_path, const char *name, const char *content) {
    char path[1024];
    int n = snprintf(path, sizeof(path), "%s/%s", base_path, name);
    if (n < 0 || n >= (int)sizeof(path)) {
        return -1;
    }
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return -1;
    }
    size_t len = strlen(content);
    ssize_t written = write(fd, content, len);
    int fsync_result = fsync(fd);
    close(fd);
    return (written == (ssize_t)len && fsync_result == 0) ? 0 : -1;
}

// Try multiple candidate paths for markers
static const char* get_marker_base_path(void) {
    // Try HOME/Library/Caches first (normal iOS location)
    const char *home = getenv("HOME");
    if (home) {
        static char path[1024];
        int n = snprintf(path, sizeof(path), "%s/Library/Caches", home);
        if (n > 0 && n < (int)sizeof(path)) {
            return path;
        }
    }
    
    // Fall back to TMPDIR
    const char *tmpdir = getenv("TMPDIR");
    if (tmpdir) {
        return tmpdir;
    }
    
    // Last resort
    return "/tmp";
}

// Write path probe marker to report what paths are available
static void write_path_probe(void) {
    const char *home = getenv("HOME");
    const char *tmpdir = getenv("TMPDIR");
    
    char probe_content[1024];
    int n = snprintf(probe_content, sizeof(probe_content),
                     "HOME=%s\nTMPDIR=%s\n",
                     home ? home : "(null)",
                     tmpdir ? tmpdir : "(null)");
    if (n < 0 || n >= (int)sizeof(probe_content)) {
        return;
    }
    
    // Write to all candidate locations
    if (home) {
        char home_cache[1024];
        snprintf(home_cache, sizeof(home_cache), "%s/Library/Caches", home);
        write_marker_raw(home_cache, "PATH_PROBE_HOME", probe_content);
    }
    if (tmpdir) {
        write_marker_raw(tmpdir, "PATH_PROBE_TMPDIR", probe_content);
    }
    write_marker_raw("/tmp", "PATH_PROBE_FALLBACK", probe_content);
}

// Constructor-stage marker - runs before main()
__attribute__((constructor))
static void constructor_marker(void) {
    const char *base_path = get_marker_base_path();
    
    // Write constructor-stage marker
    write_marker_raw(base_path, "CONSTRUCTOR_PRE_MAIN", "CONSTRUCTOR_REACHED\n");
    
    // Write path probe to validate path mechanism
    write_path_probe();
}

int main(int argc, char * argv[]) {
    const char *base_path = get_marker_base_path();
    
    // ============================================================
    // EARLIEST MAIN MARKER: Before ANY Foundation/Objective-C
    // ============================================================
    write_marker_raw(base_path, "MAIN_PRE_AUTORELEASEPOOL", "MAIN_REACHED\n");
    
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
    write_marker_raw(base_path, "MAIN_POST_MINIMAL_STAGE0", "STAGE0_DONE\n");
    
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
