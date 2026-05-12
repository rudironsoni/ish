//
//  AppDelegate.m
//  iSH
//
//  Created by Theodore Dubois on 10/17/17.
//

#include <stdlib.h>
#include <sys/stat.h>
#include <netinet/in.h>
#import <SystemConfiguration/SystemConfiguration.h>
#import "AboutViewController.h"
#import "AppDelegate.h"
#import "AppGroup.h"
#import "CurrentRoot.h"
#import "ExceptionExfiltrator.h"
#import "iOSFS.h"
#import "runtime/presenter_bridge.h"
#import "root_registry.h"
#import "SceneDelegate.h"
#import "PasteboardDevice.h"
#import "LocationDevice.h"
#import "NSObject+SaneKVO.h"
#import "Roots.h"
#import "TerminalViewController.h"
#import "UserPreferences.h"
#import "UIApplication+OpenURL.h"
#import "Instrumentation/ISHRuntimeFlags.h"
#import <IXLandInstrumentation/IXLandInstrumentation.h>
#import <IXLandInstrumentationBridge.h>
#import <ISHInstrumentation.h>
#import <IXLandLinuxRuntime/kernel/bootstrap.h>
#import <IXLandLinuxRuntime/kernel/init.h>
#import <IXLandLinuxRuntime/kernel/calls.h>
#import <IXLandLinuxRuntime/fs/devices.h>
#import <IXLandLinuxRuntime/fs/path.h>
#import "runtime/bootstrap_bridge.h"
#import "runtime/dns_bridge.h"
#import "root_bootstrap.h"
#include <fcntl.h>

@interface AppDelegate ()

@property BOOL exiting;
@property SCNetworkReachabilityRef reachability;

@end

static int bootError;
static BOOL lastBootstrapRootPresent;
static BOOL lastBootstrapRootExists;
static BOOL lastBootstrapRootDataExists;
static BOOL lastBootstrapRootsAvailable;
static BOOL lastBootstrapArchiveURLPresent;
static BOOL lastBootstrapImportAttempted;
static BOOL lastBootstrapImportSucceeded;
static NSString *lastBootstrapImportErrorDescription;
static BOOL lastBootstrapMountRootCalled;
static int lastRootMountReturnValue;
static BOOL lastMountsNonEmptyAfterRootMount;
static BOOL lastBootstrapBecomeFirstProcessCalled;
static int lastBecomeFirstProcessReturnValue;
static BOOL lastBootstrapPID1ExistsAfterBecomeFirstProcess;
static int lastBootstrapReturnValue;
static BOOL runtimePostMountInitialized;
static BOOL runtimeConsoleInitialized;
static __weak AppDelegate *appDelegate;

static BOOL is_ui_testing_session(void) {
    NSDictionary<NSString *, NSString *> *environment = NSProcessInfo.processInfo.environment;
    return environment[@"XCTestConfigurationFilePath"] != nil || environment[@"IXLAND_UI_TESTING"] != nil;
}

@implementation AppDelegate

+ (AppDelegate *)sharedInstance {
    return (AppDelegate *)[UIApplication sharedApplication].delegate;
}

- (int)boot {
    [ISHInstrumentation recordEvent:@"app.trace.bootstrap_started"];
    [ISHInstrumentation recordEvent:@"app.boot.started"];
    return [self _bootstrapRuntimeForSession];
}

+ (int)bootError {
    return bootError;
}

+ (BOOL)lastBootstrapRootPresent {
    return lastBootstrapRootPresent;
}

+ (BOOL)lastBootstrapRootExists {
    return lastBootstrapRootExists;
}

+ (BOOL)lastBootstrapRootDataExists {
    return lastBootstrapRootDataExists;
}

+ (BOOL)lastBootstrapRootsAvailable {
    return ios_root_has_available_roots();
}

+ (BOOL)lastBootstrapArchiveURLPresent {
    return lastBootstrapArchiveURLPresent;
}

+ (BOOL)lastBootstrapImportAttempted {
    return lastBootstrapImportAttempted;
}

+ (BOOL)lastBootstrapImportSucceeded {
    return lastBootstrapImportSucceeded;
}

+ (NSString *)lastBootstrapImportErrorDescription {
    return lastBootstrapImportErrorDescription ?: @"";
}

+ (BOOL)lastBootstrapMountRootCalled {
    return lastBootstrapMountRootCalled;
}

+ (int)lastRootMountReturnValue {
    return lastRootMountReturnValue;
}

+ (BOOL)lastMountsNonEmptyAfterRootMount {
    return lastMountsNonEmptyAfterRootMount;
}

+ (BOOL)lastBootstrapBecomeFirstProcessCalled {
    return lastBootstrapBecomeFirstProcessCalled;
}

+ (int)lastBecomeFirstProcessReturnValue {
    return lastBecomeFirstProcessReturnValue;
}

+ (BOOL)lastBootstrapPID1ExistsAfterBecomeFirstProcess {
    return lastBootstrapPID1ExistsAfterBecomeFirstProcess;
}

+ (int)lastBootstrapReturnValue {
    return lastBootstrapReturnValue;
}

+ (int)bootstrapRuntimeForSession {
    @synchronized(AppDelegate.class) {
        BOOL runtimeReady = mounts_is_non_empty() && pid_get_task(1) != NULL && runtimePostMountInitialized && runtimeConsoleInitialized;
        if (runtimeReady) {
            return 0;
        }
        int bootstrapResult = [[AppDelegate sharedInstance] _bootstrapRuntimeForSession];
        if (bootstrapResult < 0) {
            bootError = bootstrapResult;
        }
        return bootstrapResult;
    }
}

- (int)_bootstrapRuntimeForSession {
    [ISHInstrumentation recordEvent:@"app.runtime.bootstrap.start"];
    BOOL mountsWereEmptyAtEntry = !mounts_is_non_empty();
    if (mountsWereEmptyAtEntry) {
        runtimePostMountInitialized = NO;
        runtimeConsoleInitialized = NO;
    }
    lastBootstrapRootPresent = NO;
    lastBootstrapRootExists = NO;
    lastBootstrapRootDataExists = NO;
    lastBootstrapRootsAvailable = ios_root_has_available_roots();
    lastBootstrapArchiveURLPresent = ios_root_last_archive_url_present();
    lastBootstrapImportAttempted = ios_root_last_import_attempted();
    lastBootstrapImportSucceeded = ios_root_last_import_succeeded();
    lastBootstrapImportErrorDescription = ios_root_last_import_error_description();
    lastBootstrapMountRootCalled = NO;
    lastRootMountReturnValue = 0;
    lastMountsNonEmptyAfterRootMount = mounts_is_non_empty();
    lastBootstrapBecomeFirstProcessCalled = NO;
    lastBecomeFirstProcessReturnValue = 0;
    lastBootstrapPID1ExistsAfterBecomeFirstProcess = pid_get_task(1) != NULL;
    lastBootstrapReturnValue = 0;

    NSURL *root = ios_root_default_url();
    NSURL *rootDataURL = ios_root_default_data_url();
    BOOL rootExists = root ? [[NSFileManager defaultManager] fileExistsAtPath:root.path] : NO;
    BOOL rootDataExists = rootDataURL ? [[NSFileManager defaultManager] fileExistsAtPath:rootDataURL.path] : NO;
    BOOL isTesting = NSProcessInfo.processInfo.environment[@"XCTestConfigurationFilePath"] != nil;
    BOOL usingFallbackRoots = [root.path containsString:@"IXLandTestRoots"];
    lastBootstrapRootsAvailable = ios_root_has_available_roots();
    lastBootstrapArchiveURLPresent = ios_root_last_archive_url_present();
    lastBootstrapImportAttempted = ios_root_last_import_attempted();
    lastBootstrapImportSucceeded = ios_root_last_import_succeeded();
    lastBootstrapImportErrorDescription = ios_root_last_import_error_description();
    lastBootstrapRootPresent = root != nil;
    lastBootstrapRootExists = rootExists;
    lastBootstrapRootDataExists = rootDataExists;

    [ISHInstrumentation recordEvent:@"app.runtime.bootstrap.root.present"
                         attributes:@{ @"root_present": @(root != nil),
                                       @"root_path": root.path ?: @"",
                                       @"root_data_path": rootDataURL.path ?: @"",
                                       @"root_exists": @(rootExists),
                                       @"root_data_exists": @(rootDataExists),
                                       @"roots_available": @(lastBootstrapRootsAvailable),
                                       @"archive_url_present": @(lastBootstrapArchiveURLPresent),
                                       @"import_attempted": @(lastBootstrapImportAttempted),
                                       @"import_succeeded": @(lastBootstrapImportSucceeded),
                                       @"import_error": lastBootstrapImportErrorDescription ?: @"",
                                       @"is_testing": @(isTesting),
                                       @"using_fallback_roots": @(usingFallbackRoots) }];

    if (root == nil) {
        [ISHInstrumentation recordEvent:@"app.runtime.bootstrap.root.missing"];
        lastBootstrapReturnValue = _ENODEV;
        return lastBootstrapReturnValue;
    }

    if (!rootExists || !rootDataExists) {
        [ISHInstrumentation recordEvent:@"app.runtime.bootstrap.root.missing_data"
                             attributes:@{ @"root_exists": @(rootExists),
                                           @"root_data_exists": @(rootDataExists) }];
        lastBootstrapReturnValue = _ENODEV;
        return lastBootstrapReturnValue;
    }

    if (!mounts_is_non_empty()) {
        lastBootstrapMountRootCalled = YES;
        [ISHInstrumentation recordEvent:@"app.runtime.bootstrap.mount_root.enter"
                             attributes:@{ @"root_data_path": rootDataURL.path ?: @"",
                                           @"root_data_exists": @(rootDataExists) }];
        int mountErr = ios_rootfs_mount(rootDataURL.fileSystemRepresentation);
        lastRootMountReturnValue = mountErr;
        lastMountsNonEmptyAfterRootMount = mounts_is_non_empty();
        [ISHInstrumentation recordEvent:@"app.runtime.bootstrap.mount_root.exit"
                             attributes:@{ @"return_value": @(mountErr),
                                           @"root_data_path": rootDataURL.path ?: @"",
                                           @"root_data_exists": @(rootDataExists),
                                           @"mounts_non_empty": @(lastMountsNonEmptyAfterRootMount) }];
        if (mountErr < 0) {
            lastBootstrapReturnValue = mountErr;
            return lastBootstrapReturnValue;
        }
        if (!lastMountsNonEmptyAfterRootMount) {
            [ISHInstrumentation recordEvent:@"app.runtime.bootstrap.mount_root.empty_postmount"];
            lastBootstrapReturnValue = _ENODEV;
            return lastBootstrapReturnValue;
        }
    }

    struct task *init = pid_get_task(1);
    if (mountsWereEmptyAtEntry && init != NULL) {
        lastBootstrapPID1ExistsAfterBecomeFirstProcess = YES;
    }
    if (init == NULL) {
        lastBootstrapBecomeFirstProcessCalled = YES;
        [ISHInstrumentation recordEvent:@"app.runtime.bootstrap.become_first_process.enter"];
        int processErr = become_first_process();
        lastBecomeFirstProcessReturnValue = processErr;
        [ISHInstrumentation recordEvent:@"app.runtime.bootstrap.become_first_process.exit"
                             attributes:@{ @"return_value": @(processErr),
                                           @"mounts_non_empty": @(mounts_is_non_empty()) }];
        if (processErr < 0) {
            lastBootstrapPID1ExistsAfterBecomeFirstProcess = NO;
            lastBootstrapReturnValue = processErr;
            return lastBootstrapReturnValue;
        }
        init = pid_get_task(1);
        lastBootstrapPID1ExistsAfterBecomeFirstProcess = init != NULL;
        if (init == NULL) {
            [ISHInstrumentation recordEvent:@"app.runtime.bootstrap.pid1_still_missing"];
            lastBootstrapReturnValue = _ENODEV;
            return lastBootstrapReturnValue;
        }
    }

#if ISH_RUNTIME_MODE_VALUE == 0
    if (!runtimePostMountInitialized) {
        FsInitialize();
        runtimePostMountInitialized = YES;
        [ISHInstrumentation recordEvent:@"session.bootstrap.deferred"];
    }
    [ISHInstrumentation recordEvent:@"app.runtime.bootstrap.success"
                         attributes:@{ @"mounts_non_empty": @(mounts_is_non_empty()),
                                       @"pid1_exists": @(pid_get_task(1) != NULL) }];
    lastBootstrapReturnValue = 0;
    return lastBootstrapReturnValue;
#endif

    if (!runtimePostMountInitialized) {
        FsInitialize();
        int err = runtime_finish_post_mount_setup();
        if (err < 0)
            return err;
        runtime_configure_dns();
        runtime_install_process_hooks();
        runtime_configure_socket_prefix();
        runtimePostMountInitialized = YES;
    }

    if (!runtimeConsoleInitialized) {
        int stdioErr = runtime_prepare_console(&terminal_console_driver, 1);
        if (stdioErr < 0) {
            lastBootstrapReturnValue = stdioErr;
            return lastBootstrapReturnValue;
        }
        runtimeConsoleInitialized = YES;
    }

    [ISHInstrumentation recordEvent:@"app.boot.runtime_owner.ready"
                         attributes:@{ @"mounts_non_empty": @(mounts_is_non_empty()),
                                       @"pid1_ready": @(current != NULL && current->pid == 1) }];
    [ISHInstrumentation recordEvent:@"app.runtime.bootstrap.success"
                         attributes:@{ @"mounts_non_empty": @(mounts_is_non_empty()),
                                       @"pid1_exists": @(pid_get_task(1) != NULL) }];
    lastBootstrapReturnValue = (mounts_is_non_empty() && pid_get_task(1) != NULL) ? 0 : _ENODEV;
    return lastBootstrapReturnValue;
}

- (BOOL)application:(UIApplication *)application willFinishLaunchingWithOptions:(NSDictionary<UIApplicationLaunchOptionsKey,id> *)launchOptions {
    NSUserDefaults *defaults = NSUserDefaults.standardUserDefaults;
    if ([defaults boolForKey:@"hail mary"]) {
        [defaults removeObjectForKey:kPreferenceBootCommandKey];
        [defaults removeObjectForKey:kPreferenceLaunchCommandKey];
        [defaults setBool:NO forKey:@"hail mary"];
    }
    if (is_ui_testing_session()) {
        // UI tests must start from the canonical interactive shell instead of
        // inheriting persisted simulator defaults from prior manual sessions.
        [defaults removeObjectForKey:kPreferenceBootCommandKey];
        [defaults removeObjectForKey:kPreferenceLaunchCommandKey];
    }
    if ([NSUserDefaults.standardUserDefaults boolForKey:@"recovery"]) {
        return YES;
    }

    // Activate instrumentation before boot to capture all configured kernel events
    ixland_instrumentation_activate();

    bootError = 0;
    [ISHInstrumentation recordEvent:@"app.boot.deferred_to_scene"];

    return YES;
}

void NetworkReachabilityCallback(SCNetworkReachabilityRef target, SCNetworkReachabilityFlags flags, void *info) {
    (void) target;
    (void) flags;
    (void) info;
    runtime_configure_dns();
}

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
    // get the network permissions popup to appear on chinese devices
    [[NSURLSession.sharedSession dataTaskWithURL:[NSURL URLWithString:@"http://captive.apple.com"]] resume];

    if ([NSUserDefaults.standardUserDefaults boolForKey:@"FASTLANE_SNAPSHOT"])
        [UIView setAnimationsEnabled:NO];

    NSString *ishVersion = [NSString stringWithFormat:@"iSH %@ (%@)",
                         [NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"],
                         [NSBundle.mainBundle objectForInfoDictionaryKey:(NSString *) kCFBundleVersionKey]];
    extern const char *proc_ish_version;
    proc_ish_version = strdup(ishVersion.UTF8String);
    // this defaults key is set when taking app store screenshots
    extern const char *uname_hostname_override;
    NSString *hostnameOverride = UserPreferences.shared._hostnameOverride;
    if (@available(iOS 16.0, *)) { // Hostname obfuscation is in effect
        hostnameOverride = hostnameOverride ? hostnameOverride : UserPreferences.shared.hostnameOverride;
    }
    if (hostnameOverride) {
        uname_hostname_override = strdup(hostnameOverride.UTF8String);
    }
    
    [UserPreferences.shared observe:@[@"shouldDisableDimming"] options:NSKeyValueObservingOptionInitial
                              owner:self usingBlock:^(typeof(self) self) {
        dispatch_async(dispatch_get_main_queue(), ^{
            UIApplication.sharedApplication.idleTimerDisabled = UserPreferences.shared.shouldDisableDimming;
        });
    }];
    
    // This code is IPv4 and IPv6 aware: see https://developer.apple.com/library/archive/samplecode/Reachability/Listings/ReadMe_md.html
    struct sockaddr_in address = {
        .sin_len = sizeof(address),
        .sin_family = AF_INET,
    };
    self.reachability = SCNetworkReachabilityCreateWithAddress(kCFAllocatorDefault, (struct sockaddr *) &address);
    SCNetworkReachabilityContext context = {
        .info = (__bridge void *) self,
    };
    SCNetworkReachabilitySetCallback(self.reachability, NetworkReachabilityCallback, &context);
    SCNetworkReachabilityScheduleWithRunLoop(self.reachability, CFRunLoopGetMain(), kCFRunLoopCommonModes);

    if (self.window != nil) {
        if ([NSUserDefaults.standardUserDefaults boolForKey:@"recovery"]) {
            UINavigationController *vc = [[UIStoryboard storyboardWithName:@"About" bundle:nil] instantiateInitialViewController];
            AboutViewController *avc = (AboutViewController *) vc.topViewController;
            avc.recoveryMode = YES;
            self.window.rootViewController = vc;
            return YES;
        }
        TerminalViewController *vc = (TerminalViewController *) self.window.rootViewController;
        set_active_presenter(vc);

        // SceneDelegate owns session startup on iOS 16+. Starting a session here
        // races the real scene lifecycle and can consume runtime ownership before
        // the visible terminal attaches to the PTY-backed guest session.

        // (Test-only accessibility proxy removed)
    }
    // Diagnostic: record a startup event when running under XCTest so we can
    // prove instrumentation is active from app launch in exported logs.
    BOOL isTesting = is_ui_testing_session();
    if (isTesting) {
        [ISHInstrumentation recordEvent:@"app.didFinishLaunching.testing" attributes:@{ @"has_window": @(self.window != nil) }];
    }
    return YES;
}

- (void)application:(UIApplication *)application didDiscardSceneSessions:(NSSet<UISceneSession *> *)sceneSessions API_AVAILABLE(ios(13.0)) {
    for (UISceneSession *sceneSession in sceneSessions) {
        NSString *terminalUUID = sceneSession.stateRestorationActivity.userInfo[@"TerminalUUID"];
        [[Terminal terminalWithUUID:[[NSUUID alloc] initWithUUIDString:terminalUUID]] destroy];
    }
}

- (void)dealloc {
    if (self.reachability != NULL) {
        SCNetworkReachabilityUnscheduleFromRunLoop(self.reachability, CFRunLoopGetMain(), kCFRunLoopCommonModes);
        CFRelease(self.reachability);
    }
}

- (void)exitApp {
    self.exiting = YES;
    for (UIScene *scene in UIApplication.sharedApplication.connectedScenes) {
        if (![scene isKindOfClass:[UIWindowScene class]])
            continue;
        UIWindowScene *windowScene = (UIWindowScene *) scene;
        for (UIWindow *window in windowScene.windows) {
            UIViewController *rootViewController = window.rootViewController;
            if ([rootViewController isKindOfClass:TerminalViewController.class]) {
                [(TerminalViewController *) rootViewController prepareForSceneDeactivation];
            } else {
                [window endEditing:NO];
            }
        }
    }
    id app = [UIApplication sharedApplication];
    [app suspend];
}

- (void)applicationDidEnterBackground:(UIApplication *)application {
    if (self.exiting)
        exit(0);
}

@end

NSString *const ProcessExitedNotification = @"ProcessExitedNotification";
