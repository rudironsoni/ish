//
//  main.m
//  iSH
//
//  Created by Theodore Dubois on 10/17/17.
//

#import <UIKit/UIKit.h>
#import "AppDelegate.h"
#import "ExceptionExfiltrator.h"
#import <IXLandInstrumentation/IXLandInstrumentation.h>
#import <IXLandInstrumentationBridge.h>

int main(int argc, char * argv[]) {
    ixland_instrumentation_bridge_register();
    ixland_instrumentation_bootstrap();
    @autoreleasepool {
        NSSetUncaughtExceptionHandler(iSHExceptionHandler);
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([AppDelegate class]));
    }
}
