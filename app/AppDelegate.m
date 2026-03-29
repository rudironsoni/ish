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
#include "kernel/init.h"
#include "kernel/calls.h"
#include "fs/dyndev.h"
#include "fs/devices.h"
#include "fs/path.h"
#include "trace/trace.h"
#include <fcntl.h>

// Force non-Linux path for testing
#undef ISH_LINUX
#define ISH_LINUX 0

#if ISH_LINUX
#import "LinuxInterop.h"
#endif

@interface AppDelegate ()

@property BOOL exiting;
@property SCNetworkReachabilityRef reachability;

@end

#if !ISH_LINUX
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
#elif ISH_LINUX
void ReportPanic(const char *message) {
    [NSNotificationCenter.defaultCenter postNotificationName:KernelPanicNotification object:nil userInfo:@{@"message":@(message)}];
}
#endif

static int bootError;
static NSString *const kSkipStartupMessage = @"Skip Startup Message";

@implementation AppDelegate

- (int)boot {
    // STAGE 1: Backend attach - upgrade from NOP to full Unified iOS backend
    // Stage 0 in main.m created minimal context, now attach heavy backends
    NSString *cachesDir = [NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES) firstObject];
    const char *cachesPath = cachesDir.UTF8String;
    
    char stage1PrePath[1024];
    char stage1PostPath[1024];
    snprintf(stage1PrePath, sizeof(stage1PrePath), "%s/STAGE1_PRE_BACKEND_ATTACH", cachesPath);
    snprintf(stage1PostPath, sizeof(stage1PostPath), "%s/STAGE1_POST_BACKEND_ATTACH", cachesPath);
    
    // Write Stage 1 PRE marker
    int fd_pre = open(stage1PrePath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_pre >= 0) {
        write(fd_pre, "STAGE1_PRE\n", 11);
        fsync(fd_pre);
        close(fd_pre);
    }
    
    // Reconfigure trace to use Unified iOS backend
    extern trace_ctx_t *g_trace_ctx;
    if (g_trace_ctx) {
        // Shutdown NOP backend from Stage 0
        extern void trace_shutdown(void);
        trace_shutdown();
    }
    // Initialize with full Unified iOS backend (os_log + ring)
    trace_config_t trace_config;
    trace_config_from_env(&trace_config);
    trace_init(&trace_config);
    
    // Write Stage 1 POST marker
    int fd_post = open(stage1PostPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_post >= 0) {
        write(fd_post, "STAGE1_POST\n", 12);
        fsync(fd_post);
        close(fd_post);
    }
    
    trace_emit(TRACE_EVENT_APP_TRACE_BOOTSTRAP_STARTED, 0);
    
    NSString *bootLogPath = [NSTemporaryDirectory() stringByAppendingPathComponent:@"boot.log"];
    NSString *msg = @"[Boot] Starting boot process\n";
    [msg writeToFile:bootLogPath atomically:NO encoding:NSUTF8StringEncoding error:nil];
    trace_emit(TRACE_EVENT_APP_BOOT_STARTED, 0);
    
#if !ISH_LINUX
    NSURL *root = [Roots.instance rootUrl:Roots.instance.defaultRoot];
    msg = [NSString stringWithFormat:@"[Boot] Root URL: %@\n", root];
    [msg writeToFile:bootLogPath atomically:NO encoding:NSUTF8StringEncoding error:nil];

    int err = mount_root(&fakefs, [root URLByAppendingPathComponent:@"data"].fileSystemRepresentation);
    if (err < 0) {
        msg = [NSString stringWithFormat:@"[Boot] ERROR: mount_root failed: %d\n", err];
        [msg writeToFile:bootLogPath atomically:NO encoding:NSUTF8StringEncoding error:nil];
        return err;
    }
    msg = @"[Boot] Root filesystem mounted successfully\n";
    [msg writeToFile:bootLogPath atomically:NO encoding:NSUTF8StringEncoding error:nil];

    fs_register(&iosfs);
    fs_register(&iosfs_unsafe);

    // need to do this first so that we can have a valid current for the generic_mknod calls
    msg = @"[Boot] Calling become_first_process...\n";
    [msg writeToFile:bootLogPath atomically:NO encoding:NSUTF8StringEncoding error:nil];
    err = become_first_process();
    if (err < 0) {
        msg = [NSString stringWithFormat:@"[Boot] ERROR: become_first_process failed: %d\n", err];
        [msg writeToFile:bootLogPath atomically:NO encoding:NSUTF8StringEncoding error:nil];
        return err;
    }
    msg = @"[Boot] First process created successfully\n";
    [msg writeToFile:bootLogPath atomically:NO encoding:NSUTF8StringEncoding error:nil];

    FsInitialize();
    msg = @"[Boot] FsInitialize done\n";
    [msg writeToFile:bootLogPath atomically:NO encoding:NSUTF8StringEncoding error:nil];

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
        msg = [NSString stringWithFormat:@"[Boot] ERROR: dyn_dev_register(clipboard) failed: %d\n", err];
        [msg writeToFile:bootLogPath atomically:NO encoding:NSUTF8StringEncoding error:nil];
        return err;
    }
    msg = @"[Boot] Clipboard device registered\n";
    [msg writeToFile:bootLogPath atomically:NO encoding:NSUTF8StringEncoding error:nil];
    
    generic_mknodat(AT_PWD, "/dev/clipboard", S_IFCHR|0666, dev_make(DYN_DEV_MAJOR, DEV_CLIPBOARD_MINOR));
    
    err = dyn_dev_register(&location_dev, DEV_CHAR, DYN_DEV_MAJOR, DEV_LOCATION_MINOR);
    if (err != 0) {
        msg = [NSString stringWithFormat:@"[Boot] ERROR: dyn_dev_register(location) failed: %d\n", err];
        [msg writeToFile:bootLogPath atomically:NO encoding:NSUTF8StringEncoding error:nil];
        return err;
    }
    msg = @"[Boot] Location device registered\n";
    [msg writeToFile:bootLogPath atomically:NO encoding:NSUTF8StringEncoding error:nil];
    
    generic_mknodat(AT_PWD, "/dev/location", S_IFCHR|0666, dev_make(DYN_DEV_MAJOR, DEV_LOCATION_MINOR));

    do_mount(&procfs, "proc", "/proc", "", 0);
    do_mount(&devptsfs, "devpts", "/dev/pts", "", 0);
    msg = @"[Boot] proc and devpts mounted\n";
    [msg writeToFile:bootLogPath atomically:NO encoding:NSUTF8StringEncoding error:nil];

    iosfs_init(); // let it mount any filesystems from user defaults
    msg = @"[Boot] iosfs_init done\n";
    [msg writeToFile:bootLogPath atomically:NO encoding:NSUTF8StringEncoding error:nil];

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
        msg = [NSString stringWithFormat:@"[Boot] ERROR: create_stdio failed: %d\n", err];
        [msg writeToFile:bootLogPath atomically:NO encoding:NSUTF8StringEncoding error:nil];
        return err;
    }
    msg = @"[Boot] stdio created\n";
    [msg writeToFile:bootLogPath atomically:NO encoding:NSUTF8StringEncoding error:nil];
    
    NSArray<NSString *> *command;
    command = UserPreferences.shared.bootCommand;
    
    // Log the actual command being executed
    NSString *cmdStr = @"[Boot] Boot command: ";
    for (NSString *arg in command) {
        cmdStr = [cmdStr stringByAppendingFormat:@"'%@' ", arg];
    }
    cmdStr = [cmdStr stringByAppendingString:@"\n"];
    [cmdStr writeToFile:bootLogPath atomically:NO encoding:NSUTF8StringEncoding error:nil];
    
    // Also NSLog it - this should definitely show up
    NSLog(@"================================================");
    NSLog(@"iSH BOOT: Starting execution");
    NSLog(@"iSH BOOT: Command path: %@", command[0]);
    NSLog(@"================================================");
    
    char argv[4096];
    [Terminal convertCommand:command toArgs:argv limitSize:sizeof(argv)];
    const char *envp = "TERM=xterm-256color\0";
    msg = @"[Boot] Calling do_execve...\n";
    [msg writeToFile:bootLogPath atomically:NO encoding:NSUTF8StringEncoding error:nil];
    
    NSLog(@"[Boot] About to call do_execve with: %s", command[0].UTF8String);
    NSLog(@"[Boot] Current working directory: %s", getcwd(NULL, 0));
    NSLog(@"[Boot] File system root: %s", root.fileSystemRepresentation);
    
    // CRITICAL DEBUG: Log everything about the do_execve call
    NSString *debugPath = @"/tmp/ish_exec_debug.log";
    NSString *debugInfo = [NSString stringWithFormat:@"path=%s\nargc=%lu\nargv[0]=%s\nenvp=%s\n", 
                           command[0].UTF8String, (unsigned long)command.count, argv, envp];
    [debugInfo writeToFile:debugPath atomically:YES encoding:NSUTF8StringEncoding error:nil];
    
    // Also try to write to console
    write(2, "[iSH] About to call do_execve\n", 31);
    
    // CRITICAL: Verify the file content before calling do_execve
    // Construct the path in the emulated filesystem
    NSURL *exeInRoot = [root URLByAppendingPathComponent:@"data/bin/busybox"];
    
    // Check file attributes
    NSError *attrError = nil;
    NSDictionary *attrs = [NSFileManager.defaultManager attributesOfItemAtPath:exeInRoot.path error:&attrError];
    if (attrs) {
        msg = [NSString stringWithFormat:@"[Boot] File size: %@ bytes\n", attrs[NSFileSize]];
        [msg writeToFile:bootLogPath atomically:NO encoding:NSUTF8StringEncoding error:nil];
        msg = [NSString stringWithFormat:@"[Boot] File permissions: %03lo\n", (unsigned long)[attrs[NSFilePosixPermissions] unsignedIntegerValue]];
        [msg writeToFile:bootLogPath atomically:NO encoding:NSUTF8StringEncoding error:nil];
    } else {
        msg = [NSString stringWithFormat:@"[Boot] ERROR: Cannot get attributes: %@\n", attrError];
        [msg writeToFile:bootLogPath atomically:NO encoding:NSUTF8StringEncoding error:nil];
    }
    
    NSFileHandle *fileHandle = [NSFileHandle fileHandleForReadingAtPath:exeInRoot.path];
    if (fileHandle) {
        // Read ELF header (64 bytes for 64-bit ELF)
        NSData *headerData = [fileHandle readDataOfLength:64];
        [fileHandle closeFile];
        if (headerData.length >= 4) {
            const unsigned char *bytes = headerData.bytes;
            msg = [NSString stringWithFormat:@"[Boot] Read %lu bytes from file\n", (unsigned long)headerData.length];
            [msg writeToFile:bootLogPath atomically:NO encoding:NSUTF8StringEncoding error:nil];
            
            // Log first 16 bytes as hex for comparison with kernel
            msg = @"[Boot] APP LEVEL - First 16 bytes hex: ";
            for (int i = 0; i < 16 && i < headerData.length; i++) {
                msg = [msg stringByAppendingFormat:@"%02x ", bytes[i]];
            }
            msg = [msg stringByAppendingString:@"\n"];
            [msg writeToFile:bootLogPath atomically:NO encoding:NSUTF8StringEncoding error:nil];
            
            msg = [NSString stringWithFormat:@"[Boot] First 4 bytes: %02x %02x %02x %02x\n",
                   bytes[0], bytes[1], bytes[2], bytes[3]];
            [msg writeToFile:bootLogPath atomically:NO encoding:NSUTF8StringEncoding error:nil];
            
            // Parse ELF header
            if (bytes[0] == 0x7f && bytes[1] == 'E' && bytes[2] == 'L' && bytes[3] == 'F') {
                msg = @"[Boot] ELF magic: VALID\n";
                [msg writeToFile:bootLogPath atomically:NO encoding:NSUTF8StringEncoding error:nil];
                
                // Check bitness (byte 4)
                uint8_t bitness = bytes[4];
                msg = [NSString stringWithFormat:@"[Boot] ELF bitness: %d (expected 2 for 64-bit)\n", bitness];
                [msg writeToFile:bootLogPath atomically:NO encoding:NSUTF8StringEncoding error:nil];
                
                // Check endian (byte 5)
                uint8_t endian = bytes[5];
                msg = [NSString stringWithFormat:@"[Boot] ELF endian: %d (expected 1 for little-endian)\n", endian];
                [msg writeToFile:bootLogPath atomically:NO encoding:NSUTF8StringEncoding error:nil];
                
                // Check machine type (bytes 18-19, little endian)
                uint16_t machine = bytes[18] | (bytes[19] << 8);
                msg = [NSString stringWithFormat:@"[Boot] ELF machine: %d (expected 183 for aarch64)\n", machine];
                [msg writeToFile:bootLogPath atomically:NO encoding:NSUTF8StringEncoding error:nil];
                
                // Check type (bytes 16-17, little endian)
                uint16_t type = bytes[16] | (bytes[17] << 8);
                msg = [NSString stringWithFormat:@"[Boot] ELF type: %d (expected 2 for executable)\n", type];
                [msg writeToFile:bootLogPath atomically:NO encoding:NSUTF8StringEncoding error:nil];
                
            } else {
                msg = @"[Boot] WARNING: File does NOT have ELF magic!\n";
                [msg writeToFile:bootLogPath atomically:NO encoding:NSUTF8StringEncoding error:nil];
            }
        } else {
            msg = @"[Boot] ERROR: Could not read file\n";
            [msg writeToFile:bootLogPath atomically:NO encoding:NSUTF8StringEncoding error:nil];
        }
    } else {
        msg = [NSString stringWithFormat:@"[Boot] ERROR: Cannot open %@\n", exeInRoot.path];
        [msg writeToFile:bootLogPath atomically:NO encoding:NSUTF8StringEncoding error:nil];
    }
    
    // Test: Can we write to /tmp from the app?
    NSString *testWritePath = @"/tmp/app_write_test.log";
    [@"App can write to /tmp\n" writeToFile:testWritePath atomically:YES encoding:NSUTF8StringEncoding error:nil];
    
    err = do_execve(command[0].UTF8String, command.count, argv, envp);
    
    write(2, "[iSH] do_execve returned\n", 26);
    NSLog(@"[Boot] do_execve returned with err=%d", err);
    
    if (err < 0) {
        msg = [NSString stringWithFormat:@"[Boot] ERROR: do_execve failed: %d (ENOEXEC = exec format error)\n", err];
        [msg writeToFile:bootLogPath atomically:NO encoding:NSUTF8StringEncoding error:nil];
        msg = @"[Boot] ENOEXEC means: Not a valid ELF, not a script, not a text interpreter file\n";
        [msg writeToFile:bootLogPath atomically:NO encoding:NSUTF8StringEncoding error:nil];
        
        // Check if kernel log file was created in the emulated filesystem
        // The kernel writes to /ish_kernel_debug.log in the emulated filesystem
        // which is at root/data/ish_kernel_debug.log on the host
        NSURL *kernelLogUrl = [root URLByAppendingPathComponent:@"data/ish_kernel_debug.log"];
        BOOL kernelLogExists = [NSFileManager.defaultManager fileExistsAtPath:kernelLogUrl.path];
        msg = [NSString stringWithFormat:@"[Boot] Kernel log exists at %@: %@\n", kernelLogUrl.path, kernelLogExists ? @"YES" : @"NO"];
        [msg writeToFile:bootLogPath atomically:NO encoding:NSUTF8StringEncoding error:nil];
        
        // Try to read kernel log if it exists
        if (kernelLogExists) {
            NSString *kernelLog = [NSString stringWithContentsOfFile:kernelLogUrl.path encoding:NSUTF8StringEncoding error:nil];
            msg = [NSString stringWithFormat:@"[Boot] Kernel log contents:\n%@\n", kernelLog];
            [msg writeToFile:bootLogPath atomically:NO encoding:NSUTF8StringEncoding error:nil];
        }
        
        // Write error to multiple locations
        NSString *errMsg = [NSString stringWithFormat:@"do_execve failed with error: %d\n", err];
        [errMsg writeToFile:@"/tmp/ish_error.log" atomically:YES encoding:NSUTF8StringEncoding error:nil];
        
        return err;
    }
    msg = @"[Boot] do_execve succeeded, starting task...\n";
    [msg writeToFile:bootLogPath atomically:NO encoding:NSUTF8StringEncoding error:nil];
    
    // CRITICAL: Do NOT call task_start here - the init process (pid=1) should
    // NOT execute yet. We only set up the initial process context here.
    // The actual execution will be started by TerminalViewController via startNewSession.
    // task_start(current) would create a detached thread that immediately crashes
    // because iOS will kill the app when the main thread exits.
    NSLog(@"[Boot] Boot setup complete - init process ready but not started");
    msg = @"[Boot] Boot setup complete - init process ready\n";
    [msg writeToFile:bootLogPath atomically:NO encoding:NSUTF8StringEncoding error:nil];
#endif // !ISH_LINUX - End of iOS-specific boot path

    // DISABLED: Linux path - we're using TCTI now
    /*
#else
    // On first launch, this will trigger the import of the default root. Make sure to do this before entering the kernel, because it needs to run something on the main thread, and that would deadlock.
    [Roots instance];
    NSArray<NSString *> *args = @[];
    actuate_kernel([args componentsJoinedByString:@" "].UTF8String);
#endif
    */
    
    return 0;
}

#if ISH_LINUX
const char *DefaultRootPath() {
    return [Roots.instance rootUrl:Roots.instance.defaultRoot].fileSystemRepresentation;
}

void SyncHostname(void) {
    async_do_in_workqueue(^{
        char hostname[256];
        if (gethostname(hostname, sizeof(hostname)) < 0)
            return;
        linux_sethostname(hostname);
    });
}
#endif

- (void)configureDns {
#if !ISH_LINUX
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
#endif
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
    // Debug logging to file
    NSString *logPath = [NSTemporaryDirectory() stringByAppendingPathComponent:@"app_boot.log"];
    NSString *logMsg = @"[AppDelegate] willFinishLaunchingWithOptions called\n";
    [logMsg writeToFile:logPath atomically:NO encoding:NSUTF8StringEncoding error:nil];
    
    NSUserDefaults *defaults = NSUserDefaults.standardUserDefaults;
    if ([defaults boolForKey:@"hail mary"]) {
        [defaults removeObjectForKey:kPreferenceBootCommandKey];
        [defaults removeObjectForKey:kPreferenceLaunchCommandKey];
        [defaults setBool:NO forKey:@"hail mary"];
    }
    if ([NSUserDefaults.standardUserDefaults boolForKey:@"recovery"]) {
        logMsg = @"[AppDelegate] Recovery mode, skipping boot\n";
        [logMsg writeToFile:logPath atomically:NO encoding:NSUTF8StringEncoding error:nil];
        return YES;
    }

    logMsg = @"[AppDelegate] Starting boot sequence...\n";
    [logMsg writeToFile:logPath atomically:NO encoding:NSUTF8StringEncoding error:nil];
    bootError = [self boot];
    logMsg = [NSString stringWithFormat:@"[AppDelegate] Boot returned: %d\n", bootError];
    [logMsg writeToFile:logPath atomically:NO encoding:NSUTF8StringEncoding error:nil];

#if ISH_LINUX
    [NSNotificationCenter.defaultCenter addObserverForName:UIApplicationWillEnterForegroundNotification object:UIApplication.sharedApplication queue:nil usingBlock:^(NSNotification * _Nonnull note) {
        SyncHostname();
    }];
    SyncHostname();
#endif

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

#if !ISH_LINUX
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
#endif
    
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

#if !ISH_LINUX
NSString *const ProcessExitedNotification = @"ProcessExitedNotification";
#else
NSString *const KernelPanicNotification = @"KernelPanicNotification";
#endif
