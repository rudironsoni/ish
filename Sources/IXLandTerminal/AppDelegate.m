//
//  AppDelegate.m
//  iSH
//
//  Created by Theodore Dubois on 10/17/17.
//

#include <resolv.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdlib.h>
#include <sys/stat.h>
#import <SystemConfiguration/SystemConfiguration.h>
#import "AboutViewController.h"
#import "AppDelegate.h"
#import "AppGroup.h"
#import "CurrentRoot.h"
#import "ExceptionExfiltrator.h"
#import "iOSFS.h"
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
#import <IXLandLinuxRuntime/kernel/init.h>
#import <IXLandLinuxRuntime/kernel/calls.h>
#import <IXLandLinuxRuntime/fs/dyndev.h>
#import <IXLandLinuxRuntime/fs/devices.h>
#import <IXLandLinuxRuntime/fs/path.h>
#import <IXLandInstrumentationTracing/trace.h>
#include <fcntl.h>

@interface AppDelegate ()

@property BOOL exiting;
@property SCNetworkReachabilityRef reachability;

@end

static void ios_handle_exit(struct task *task, int code) {
    // we are interested in init and in children of init
    // this is called with pids_lock as an implementation side effect, please do not cite as an example of good API design
    if (task->parent != NULL && task->parent->parent != NULL)
        return;
    // pid should be saved now since task would be freed
    pid_t pid = task->pid;
    dispatch_async(dispatch_get_main_queue(), ^{
        [[NSNotificationCenter defaultCenter] postNotificationName:ProcessExitedNotification
                                                            object:nil
                                                          userInfo:@{@"pid": @(pid),
                                                                     @"code": @(code)}];
    });
}

static void ios_handle_die(const char *msg) {
    NSString *message = [NSString stringWithFormat:@"%s: %s", __func__, msg];
    iSHExceptionHandler([[NSException alloc] initWithName:NSGenericException reason:message userInfo:nil]);
}

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

static const char *IXLandConfiguredTraceLevel(void) {
    const char *level = getenv("IXLAND_TRACE_LEVEL");
    if (level != NULL && level[0] != '\0') {
        return level;
    }
    level = getenv("ISH_TRACE_LEVEL");
    if (level != NULL && level[0] != '\0') {
        return level;
    }

    NSString *defaultsLevel = [NSUserDefaults.standardUserDefaults stringForKey:@"IXLandTraceLevel"];
    if (defaultsLevel.length > 0) {
        return defaultsLevel.UTF8String;
    }

#if DEBUG
    return "debug";
#else
    return "off";
#endif
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

- (void)configureDns {
    struct __res_state res;
    if (EXIT_SUCCESS != res_ninit(&res)) {
        return;
    }
    NSMutableString *resolvConf = [NSMutableString new];
    if (res.dnsrch[0] != NULL) {
        [resolvConf appendString:@"search"];
        for (int i = 0; res.dnsrch[i] != NULL; i++) {
            [resolvConf appendFormat:@" %s", res.dnsrch[i]];
        }
        [resolvConf appendString:@"\n"];
    }
    union res_sockaddr_union servers[NI_MAXSERV];
    int serversFound = res_getservers(&res, servers, NI_MAXSERV);
    char address[NI_MAXHOST];
    for (int i = 0; i < serversFound; i ++) {
        union res_sockaddr_union s = servers[i];
        if (s.sin.sin_len == 0)
            continue;
        getnameinfo((struct sockaddr *) &s.sin, s.sin.sin_len,
                    address, sizeof(address),
                    NULL, 0, NI_NUMERICHOST);
        [resolvConf appendFormat:@"nameserver %s\n", address];
    }
    
    current = pid_get_task(1);
    struct fd *fd = generic_open("/etc/resolv.conf", O_WRONLY_ | O_CREAT_ | O_TRUNC_, 0666);
    if (!IS_ERR(fd)) {
        fd->ops->write(fd, resolvConf.UTF8String, [resolvConf lengthOfBytesUsingEncoding:NSUTF8StringEncoding]);
        fd_close(fd);
    }
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
    return Roots.instance.roots.count > 0;
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
    lastBootstrapRootsAvailable = Roots.instance.roots.count > 0;
    lastBootstrapArchiveURLPresent = Roots.instance.lastArchiveURLPresent;
    lastBootstrapImportAttempted = Roots.instance.lastImportAttempted;
    lastBootstrapImportSucceeded = Roots.instance.lastImportSucceeded;
    lastBootstrapImportErrorDescription = Roots.instance.lastImportErrorDescription;
    lastBootstrapMountRootCalled = NO;
    lastRootMountReturnValue = 0;
    lastMountsNonEmptyAfterRootMount = mounts_is_non_empty();
    lastBootstrapBecomeFirstProcessCalled = NO;
    lastBecomeFirstProcessReturnValue = 0;
    lastBootstrapPID1ExistsAfterBecomeFirstProcess = pid_get_task(1) != NULL;
    lastBootstrapReturnValue = 0;

    NSURL *root = [Roots.instance rootUrl:Roots.instance.defaultRoot];
    NSURL *rootDataURL = root ? [root URLByAppendingPathComponent:@"data"] : nil;
    BOOL rootExists = root ? [[NSFileManager defaultManager] fileExistsAtPath:root.path] : NO;
    BOOL rootDataExists = rootDataURL ? [[NSFileManager defaultManager] fileExistsAtPath:rootDataURL.path] : NO;
    BOOL isTesting = NSProcessInfo.processInfo.environment[@"XCTestConfigurationFilePath"] != nil;
    BOOL usingFallbackRoots = [root.path containsString:@"IXLandTestRoots"];
    lastBootstrapRootsAvailable = Roots.instance.roots.count > 0;
    lastBootstrapArchiveURLPresent = Roots.instance.lastArchiveURLPresent;
    lastBootstrapImportAttempted = Roots.instance.lastImportAttempted;
    lastBootstrapImportSucceeded = Roots.instance.lastImportSucceeded;
    lastBootstrapImportErrorDescription = Roots.instance.lastImportErrorDescription;
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
        int mountErr = mount_root(&fakefs, rootDataURL.fileSystemRepresentation);
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

        generic_mknodat(AT_PWD, "/dev/tty1", S_IFCHR|0666, dev_make(TTY_CONSOLE_MAJOR, 1));
        generic_mknodat(AT_PWD, "/dev/tty2", S_IFCHR|0666, dev_make(TTY_CONSOLE_MAJOR, 2));
        generic_mknodat(AT_PWD, "/dev/tty3", S_IFCHR|0666, dev_make(TTY_CONSOLE_MAJOR, 3));
        generic_mknodat(AT_PWD, "/dev/tty4", S_IFCHR|0666, dev_make(TTY_CONSOLE_MAJOR, 4));
        generic_mknodat(AT_PWD, "/dev/tty5", S_IFCHR|0666, dev_make(TTY_CONSOLE_MAJOR, 5));
        generic_mknodat(AT_PWD, "/dev/tty6", S_IFCHR|0666, dev_make(TTY_CONSOLE_MAJOR, 6));
        generic_mknodat(AT_PWD, "/dev/tty7", S_IFCHR|0666, dev_make(TTY_CONSOLE_MAJOR, 7));
        generic_mknodat(AT_PWD, "/dev/tty", S_IFCHR|0666, dev_make(TTY_ALTERNATE_MAJOR, DEV_TTY_MINOR));
        generic_mknodat(AT_PWD, "/dev/console", S_IFCHR|0666, dev_make(TTY_ALTERNATE_MAJOR, DEV_CONSOLE_MINOR));
        generic_mknodat(AT_PWD, "/dev/ptmx", S_IFCHR|0666, dev_make(TTY_ALTERNATE_MAJOR, DEV_PTMX_MINOR));
        generic_mknodat(AT_PWD, "/dev/null", S_IFCHR|0666, dev_make(MEM_MAJOR, DEV_NULL_MINOR));
        generic_mknodat(AT_PWD, "/dev/zero", S_IFCHR|0666, dev_make(MEM_MAJOR, DEV_ZERO_MINOR));
        generic_mknodat(AT_PWD, "/dev/full", S_IFCHR|0666, dev_make(MEM_MAJOR, DEV_FULL_MINOR));
        generic_mknodat(AT_PWD, "/dev/random", S_IFCHR|0666, dev_make(MEM_MAJOR, DEV_RANDOM_MINOR));
        generic_mknodat(AT_PWD, "/dev/urandom", S_IFCHR|0666, dev_make(MEM_MAJOR, DEV_URANDOM_MINOR));
        generic_mkdirat(AT_PWD, "/dev/pts", 0755);
        generic_setattrat(AT_PWD, "/", (struct attr) {.type = attr_mode, .mode = 0755}, false);

        int err = dyn_dev_register(&clipboard_dev, DEV_CHAR, DYN_DEV_MAJOR, DEV_CLIPBOARD_MINOR);
        if (err != 0) {
            return err;
        }
        generic_mknodat(AT_PWD, "/dev/clipboard", S_IFCHR|0666, dev_make(DYN_DEV_MAJOR, DEV_CLIPBOARD_MINOR));

        err = dyn_dev_register(&location_dev, DEV_CHAR, DYN_DEV_MAJOR, DEV_LOCATION_MINOR);
        if (err != 0) {
            return err;
        }
        generic_mknodat(AT_PWD, "/dev/location", S_IFCHR|0666, dev_make(DYN_DEV_MAJOR, DEV_LOCATION_MINOR));

        do_mount(&procfs, "proc", "/proc", "", 0);
        do_mount(&devptsfs, "devpts", "/dev/pts", "", 0);
        iosfs_init();
        [self configureDns];
        exit_hook = ios_handle_exit;
        die_handler = ios_handle_die;
#if !TARGET_OS_SIMULATOR
        if (sock_tmp_prefix == NULL) {
            NSString *sockTmp = [NSTemporaryDirectory() stringByAppendingString:@"ishsock"];
            sock_tmp_prefix = strdup(sockTmp.UTF8String);
        }
#endif
        runtimePostMountInitialized = YES;
    }

    if (!runtimeConsoleInitialized) {
        tty_drivers[TTY_CONSOLE_MAJOR] = &ios_console_driver;
        set_console_device(TTY_CONSOLE_MAJOR, 1);
        int stdioErr = create_stdio("/dev/console", TTY_CONSOLE_MAJOR, 1);
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
    if ([NSUserDefaults.standardUserDefaults boolForKey:@"recovery"]) {
        return YES;
    }

    trace_config_set_level_from_string(IXLandConfiguredTraceLevel());

    // Activate instrumentation before boot to capture all configured kernel events
    ixland_instrumentation_activate();

    bootError = [self boot];

    return YES;
}

void NetworkReachabilityCallback(SCNetworkReachabilityRef target, SCNetworkReachabilityFlags flags, void *info) {
    AppDelegate *self = (__bridge AppDelegate *) info;
    [self configureDns];
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
        currentTerminalViewController = vc;

        // SceneDelegate owns session startup on iOS 16+. Starting a session here
        // races the real scene lifecycle and can consume runtime ownership before
        // the visible terminal attaches to the PTY-backed guest session.

        // (Test-only accessibility proxy removed)
    }
    // Diagnostic: record a startup event when running under XCTest so we can
    // prove instrumentation is active from app launch in exported logs.
    BOOL isTesting = NSProcessInfo.processInfo.environment[@"XCTestConfigurationFilePath"] != nil;
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
    id app = [UIApplication sharedApplication];
    [app suspend];
}

- (void)applicationDidEnterBackground:(UIApplication *)application {
    if (self.exiting)
        exit(0);
}

@end

NSString *const ProcessExitedNotification = @"ProcessExitedNotification";
