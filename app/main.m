//
//  main.m
//  iSH
//
//  Created by Theodore Dubois on 10/17/17.
//

#import <UIKit/UIKit.h>
#import "AppDelegate.h"
#import "ExceptionExfiltrator.h"

// Minimal Stage 0 trace bootstrap only
#include "trace/trace.h"
#include <string.h>

int main(int argc, char * argv[]) {
    // Stage 0: Minimal trace context bootstrap
    // Heavy backend attach moved to AppDelegate
    trace_config_t trace_config;
    memset(&trace_config, 0, sizeof(trace_config));
    trace_config.backend = TRACE_BACKEND_NOP;
    trace_config.level = TRACE_LEVEL_OFF;
    trace_config.ring_size = 256;
    trace_config.category_mask = 0;
    trace_config.event_mask = 0;
    trace_init(&trace_config);
    
    @autoreleasepool {
        NSSetUncaughtExceptionHandler(iSHExceptionHandler);
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([AppDelegate class]));
    }
}
