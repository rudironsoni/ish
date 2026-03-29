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

int main(int argc, char * argv[]) {
    // APP-FIRST: Bootstrap trace system at earliest app boundary
    // This ensures unified observability across all layers (app, kernel, task, emulator, TCTI)
    trace_config_t trace_config;
    trace_config_from_env(&trace_config);
    trace_init(&trace_config);
    
    NSSetUncaughtExceptionHandler(iSHExceptionHandler);
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([AppDelegate class]));
    }
}
