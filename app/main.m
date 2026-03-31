//
//  main.m
//  iSH
//
//  Created by Theodore Dubois on 10/17/17.
//

#import <UIKit/UIKit.h>
#import "AppDelegate.h"
#import "ExceptionExfiltrator.h"
#import "Instrumentation/ISHInstrumentationBridge.h"

int main(int argc, char * argv[]) {
    ish_instrumentation_bootstrap();
    @autoreleasepool {
        NSSetUncaughtExceptionHandler(iSHExceptionHandler);
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([AppDelegate class]));
    }
}
