//
//  AppDelegate.h
//  iSH
//
//  Created by Theodore Dubois on 10/17/17.
//

#import <UIKit/UIKit.h>

@interface AppDelegate : UIResponder <UIApplicationDelegate>

@property (strong, nonatomic) UIWindow *window;
- (void)exitApp;

+ (int)bootError;
+ (BOOL)lastBootstrapRootPresent;
+ (BOOL)lastBootstrapRootExists;
+ (BOOL)lastBootstrapRootDataExists;
+ (BOOL)lastBootstrapRootsAvailable;
+ (BOOL)lastBootstrapArchiveURLPresent;
+ (BOOL)lastBootstrapImportAttempted;
+ (BOOL)lastBootstrapImportSucceeded;
+ (NSString *)lastBootstrapImportErrorDescription;
+ (BOOL)lastBootstrapMountRootCalled;
+ (int)lastRootMountReturnValue;
+ (BOOL)lastMountsNonEmptyAfterRootMount;
+ (BOOL)lastBootstrapBecomeFirstProcessCalled;
+ (int)lastBecomeFirstProcessReturnValue;
+ (BOOL)lastBootstrapPID1ExistsAfterBecomeFirstProcess;
+ (int)lastBootstrapReturnValue;
+ (int)bootstrapRuntimeForSession;

@end

extern NSString *const ProcessExitedNotification;

