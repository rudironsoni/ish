//
//  SceneDelegate.m
//  iSH
//
//  Created by Theodore Dubois on 10/26/19.
//

#import "SceneDelegate.h"
#import "AboutViewController.h"
#import "AppDelegate.h"
#import <IXLandLinuxRuntime/kernel/init.h>
#import <IXLandLinuxRuntime/kernel/task.h>
#import <ISHInstrumentation.h>

TerminalViewController *currentTerminalViewController = NULL;

@interface SceneDelegate ()

@property NSString *terminalUUID;
@property BOOL sessionStartupScheduled;

@end

static NSString *const TerminalUUID = @"TerminalUUID";

@implementation SceneDelegate

- (void)scheduleSessionStartupForViewController:(TerminalViewController *)viewController
                                  sceneSession:(UISceneSession *)session {
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
        int bootstrapErr = [AppDelegate bootstrapRuntimeForSession];
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
    currentTerminalViewController = terminalViewController;
}

- (void)sceneWillResignActive:(UIScene *)scene {
    TerminalViewController *terminalViewController = (TerminalViewController *) self.window.rootViewController;

    if (currentTerminalViewController == terminalViewController) {
        currentTerminalViewController = NULL;
    }
}

@end
