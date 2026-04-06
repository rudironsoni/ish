//
//  AppDelegate.m
//  iSH
//
//  Created by Theodore Dubois on 10/17/17.
//

#include <resolv.h>
#include <arpa/inet.h>
#include <netdb.h>
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
static NSString *const kSkipStartupMessage = @"Skip Startup Message";

@implementation AppDelegate

- (int)boot {
    [ISHInstrumentation recordEvent:@"app.trace.bootstrap_started"];
    [ISHInstrumentation recordEvent:@"app.boot.started"];
    
    NSURL *root = [Roots.instance rootUrl:Roots.instance.defaultRoot];

    int err = mount_root(&fakefs, [root URLByAppendingPathComponent:@"data"].fileSystemRepresentation);
    if (err < 0) {
        return err;
    }

    fs_register(&iosfs);
    fs_register(&iosfs_unsafe);

    // CONTRACT: PID 1 must always be created before any session can start
    // This is required for both shell-only mode (UI testing) and full-guest mode
    err = become_first_process();
    if (err < 0) {
        return err;
    }

    // Shell-only mode: Establish base runtime state but skip guest execution
#if ISH_RUNTIME_MODE_VALUE == 0
    {
        FsInitialize();
        [ISHInstrumentation recordEvent:@"session.bootstrap.deferred"];
        return 0;  // Success - guest execution bypassed, but PID 1 exists
    }
#endif

    FsInitialize();

    // create some device nodes
    // this will do nothing if they already exist
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
    
    // Permissions on / have been broken for a while, let's fix them
    generic_setattrat(AT_PWD, "/", (struct attr) {.type = attr_mode, .mode = 0755}, false);
    
    // Register clipboard device driver and create device node for it
    err = dyn_dev_register(&clipboard_dev, DEV_CHAR, DYN_DEV_MAJOR, DEV_CLIPBOARD_MINOR);
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

    iosfs_init(); // let it mount any filesystems from user defaults

    [self configureDns];
    
    exit_hook = ios_handle_exit;
    die_handler = ios_handle_die;
#if !TARGET_OS_SIMULATOR
    NSString *sockTmp = [NSTemporaryDirectory() stringByAppendingString:@"ishsock"];
    sock_tmp_prefix = strdup(sockTmp.UTF8String);
#endif
    
    tty_drivers[TTY_CONSOLE_MAJOR] = &ios_console_driver;
    set_console_device(TTY_CONSOLE_MAJOR, 1);
    err = create_stdio("/dev/console", TTY_CONSOLE_MAJOR, 1);
    if (err < 0) {
        return err;
    }
    
    NSArray<NSString *> *command;
    command = UserPreferences.shared.bootCommand;
    
    char argv[4096];
    [Terminal convertCommand:command toArgs:argv limitSize:sizeof(argv)];
    const char *envp = "TERM=xterm-256color\0";
    
    err = do_execve(command[0].UTF8String, command.count, argv, envp);
    
    if (err < 0) {
        return err;
    }
    
    return 0;
}

- (void)configureDns {
    struct __res_state res;
    if (EXIT_SUCCESS != res_ninit(&res)) {
        exit(2);
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

+ (void)maybePresentStartupMessageOnViewController:(UIViewController *)vc {
    if ([NSUserDefaults.standardUserDefaults integerForKey:kSkipStartupMessage] >= 1)
        return;
    if (!FsIsManaged()) {
        UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"Install iSH’s built-in APK?"
                                                                       message:@"iSH now includes the APK package manager, but it must be manually activated."
                                                                preferredStyle:UIAlertControllerStyleAlert];
        [alert addAction:[UIAlertAction actionWithTitle:@"Show me how"
                                                  style:UIAlertActionStyleDefault
                                                handler:^(UIAlertAction * _Nonnull action) {
            [UIApplication openURL:@"https://go.ish.app/get-apk"];
        }]];
        [alert addAction:[UIAlertAction actionWithTitle:@"Don't show again"
                                                  style:UIAlertActionStyleDefault
                                                handler:nil]];
        [vc presentViewController:alert animated:YES completion:nil];
    }
    [NSUserDefaults.standardUserDefaults setInteger:1 forKey:kSkipStartupMessage];
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

    // Activate instrumentation before boot to capture all kernel events
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
        // For iOS <13, where the app delegate owns the window instead of the scene
        if ([NSUserDefaults.standardUserDefaults boolForKey:@"recovery"]) {
            UINavigationController *vc = [[UIStoryboard storyboardWithName:@"About" bundle:nil] instantiateInitialViewController];
            AboutViewController *avc = (AboutViewController *) vc.topViewController;
            avc.recoveryMode = YES;
            self.window.rootViewController = vc;
            return YES;
        }
        TerminalViewController *vc = (TerminalViewController *) self.window.rootViewController;
        currentTerminalViewController = vc;
        [vc startNewSession];
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
