//
//  SceneDelegate.m
//  iSH
//
//  Created by Theodore Dubois on 10/26/19.
//

#import "SceneDelegate.h"
#import "AboutViewController.h"
#import "AppDelegate.h"
#import "TerminalViewController.h"
#import "runtime/presenter_bridge.h"
#import "runtime/bootstrap_bridge.h"
#import <IXLandLinuxRuntime/kernel/init.h>
#import <IXLandLinuxRuntime/kernel/task.h>
#import <ISHInstrumentation.h>

@interface SceneDelegate ()

@property NSString *terminalUUID;
@property BOOL sessionStartupScheduled;

@end

static NSString *const TerminalUUID = @"TerminalUUID";

static BOOL is_non_ui_xctest_session(void) {
    NSDictionary<NSString *, NSString *> *environment = NSProcessInfo.processInfo.environment;
    return environment[@"XCTestConfigurationFilePath"] != nil
        && environment[@"IXLAND_UI_TESTING"] == nil;
}

@implementation SceneDelegate

- (void)scheduleSessionStartupForViewController:(TerminalViewController *)viewController
                                  sceneSession:(UISceneSession *)session {
    if (is_non_ui_xctest_session()) {
        [ISHInstrumentation recordEvent:@"scene.session.start.skipped.non_ui_xctest"];
        return;
    }
    if (self.sessionStartupScheduled) {
        [ISHInstrumentation recordEvent:@"scene.session.start.blocked.already_scheduled"];
        return;
    }
    self.sessionStartupScheduled = YES;

    __weak typeof(self) weakSelf = self;
    __weak TerminalViewController *weakViewController = viewController;
    dispatch_async(dispatch_get_main_queue(), ^{
        typeof(self) strongSelf = weakSelf;
        TerminalViewController *strongViewController = weakViewController;
        if (strongSelf == nil || strongViewController == nil)
            return;

        [ISHInstrumentation recordEvent:@"scene.session.start.bootstrap"];
        int bootstrapErr = runtime_bootstrap_session();
        if (bootstrapErr < 0) {
            strongSelf.sessionStartupScheduled = NO;
            [ISHInstrumentation recordEvent:@"scene.session.start.bootstrap.failed"
                                 attributes:@{ @"return_value": @(bootstrapErr),
                                               @"mounts_non_empty": @(mounts_is_non_empty()),
                                               @"pid1_exists": @(pid_get_task(1) != NULL) }];
            return;
        }

        if (session.stateRestorationActivity == nil) {
            [strongViewController startNewSession];
        } else {
            strongSelf.terminalUUID = session.stateRestorationActivity.userInfo[TerminalUUID];
            [strongViewController reconnectSessionFromTerminalUUID:
             [[NSUUID alloc] initWithUUIDString:strongSelf.terminalUUID]];
        }
    });
}

- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)connectionOptions {
    if ([scene isKindOfClass:[UIWindowScene class]] && self.window == nil) {
        UIWindowScene *windowScene = (UIWindowScene *) scene;
        self.window = [[UIWindow alloc] initWithWindowScene:windowScene];
        UIViewController *rootViewController = [[UIStoryboard storyboardWithName:@"Terminal" bundle:nil] instantiateInitialViewController];
        self.window.rootViewController = rootViewController;
        [self.window makeKeyAndVisible];
    }

    if ([NSUserDefaults.standardUserDefaults boolForKey:@"recovery"]) {
        UINavigationController *vc = [[UIStoryboard storyboardWithName:@"About" bundle:nil] instantiateInitialViewController];
        AboutViewController *avc = (AboutViewController *) vc.topViewController;
        avc.recoveryMode = YES;
        self.window.rootViewController = vc;
        return;
    }

    TerminalViewController *vc = (TerminalViewController *) self.window.rootViewController;
    vc.sceneSession = session;
    [self scheduleSessionStartupForViewController:vc sceneSession:session];
}

- (NSUserActivity *)stateRestorationActivityForScene:(UIScene *)scene {
    NSUserActivity *activity = [[NSUserActivity alloc] initWithActivityType:@"app.ish.scene"];
    TerminalViewController *vc = (TerminalViewController *) self.window.rootViewController;
    if ([vc isKindOfClass:TerminalViewController.class]) {
        self.terminalUUID = vc.sessionTerminalUUID.UUIDString;
        if (self.terminalUUID != nil) {
            [activity addUserInfoEntriesFromDictionary:@{TerminalUUID: self.terminalUUID}];
        }
    }
    return activity;
}

- (void)sceneDidBecomeActive:(UIScene *)scene {
    TerminalViewController *terminalViewController = (TerminalViewController *) self.window.rootViewController;;
    set_active_presenter(terminalViewController);
}

- (void)sceneWillResignActive:(UIScene *)scene {
    TerminalViewController *terminalViewController = (TerminalViewController *) self.window.rootViewController;
    clear_active_presenter(terminalViewController);
}

@end
