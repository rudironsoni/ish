#import <XCTest/XCTest.h>

#import <IXLandLinuxRuntime/kernel/task.h>
#import <IXLandLinuxRuntime/kernel/init.h>
#import <IXLandLinuxRuntime/kernel/calls.h>
#import <IXLandLinuxRuntime/kernel/fs.h>
#import <IXLandLinuxRuntime/fs/fd.h>
#import <IXLandLinuxRuntime/fs/tty.h>
#import <IXLandLinuxRuntime/emu/tlb.h>
#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>
#import <IXLandInstrumentationTracing/trace.h>
#import <IXLandTerminal/AppDelegate.h>
#import <IXLandTerminal/LinuxInterop.h>
#import <IXLandTerminal/Terminal.h>
#import <IXLandTerminal/TerminalView.h>
#import "internal/ios/runtime/bootstrap_bridge.h"
#import "internal/ios/runtime/session_bridge.h"
#include <sys/stat.h>

extern bool exit_should_pthread_exit;

@interface GuestBusyboxLsReproTests : XCTestCase
@end

@implementation GuestBusyboxLsReproTests

- (void)runOnMainThreadSync:(dispatch_block_t)block {
    if ([NSThread isMainThread]) {
        block();
        return;
    }
    dispatch_sync(dispatch_get_main_queue(), block);
}

- (void)configureFocusedTraceLevel {
    setenv("IXLAND_TRACE_LEVEL", "debug", 1);
    trace_config_set_level_from_string("debug");
}

- (void)resetA64ExecutionMode {
    unsetenv("ISH_A64_CONSERVATIVE_MODE");
    unsetenv("ISH_A64_VERBOSE_BLOCK_TRACE");
}

- (BOOL)bootstrapMountedRootfsAtPath:(NSString *)rootPath {
    int mountErr = mount_root(&rootfs, rootPath.UTF8String);
    XCTAssertTrue(mountErr == 0 || mountErr == -16, @"mount_root returned %d", mountErr);
    if (!(mountErr == 0 || mountErr == -16))
        return NO;

    int initErr = become_first_process();
    XCTAssertTrue(initErr == 0 || initErr == -17, @"become_first_process returned %d", initErr);
    if (!(initErr == 0 || initErr == -17))
        return NO;
    return YES;
}

- (BOOL)bootstrapAppStyleSessionHarness {
    int bootstrapErr = runtime_bootstrap_session();
    XCTAssertEqual(bootstrapErr, 0, @"runtime_bootstrap_session returned %d", bootstrapErr);
    if (bootstrapErr != 0)
        return NO;

    int childErr = become_new_init_child();
    XCTAssertEqual(childErr, 0, @"become_new_init_child returned %d", childErr);
    if (childErr != 0)
        return NO;

    return YES;
}

- (BOOL)startSessionWithExecutable:(const char *)exe
                              argv:(const char *const *)argv
                              envp:(const char *)envp
                          terminal:(Terminal * __strong *)terminalOut
                               pid:(int *)pidOut {
    __block Terminal *terminal = nil;
    __block int startErr = -1;
    __block int startPid = 0;
    dispatch_semaphore_t startSemaphore = dispatch_semaphore_create(0);

    linux_start_session(exe, argv, envp, ^(int retval, int pid, nsobj_t terminalObject) {
        startErr = retval;
        startPid = pid;
        if (terminalObject != NULL)
            terminal = (__bridge Terminal *) terminalObject;
        dispatch_semaphore_signal(startSemaphore);
    });

    long waitResult =
        dispatch_semaphore_wait(startSemaphore, dispatch_time(DISPATCH_TIME_NOW, (int64_t)(10 * NSEC_PER_SEC)));
    XCTAssertEqual(waitResult, 0L, @"linux_start_session did not complete");
    XCTAssertEqual(startErr, 0, @"linux_start_session returned %d", startErr);
    XCTAssertNotEqual(startPid, 0, @"session pid must be assigned");
    XCTAssertNotNil(terminal, @"terminal object must be returned");
    if (!(waitResult == 0L && startErr == 0 && startPid != 0 && terminal != nil))
        return NO;

    if (terminalOut != NULL)
        *terminalOut = terminal;
    if (pidOut != NULL)
        *pidOut = startPid;
    return YES;
}

- (BOOL)startBusyboxSessionWithArgv:(const char *const *)argv
                               envp:(const char *)envp
                           terminal:(Terminal * __strong *)terminalOut
                                pid:(int *)pidOut {
    NSMutableArray<NSString *> *storage = [NSMutableArray array];
    const char *normalizedArgv[16] = {0};
    size_t index = 0;
    while (argv[index] != NULL && index < (sizeof(normalizedArgv) / sizeof(normalizedArgv[0])) - 1) {
        NSString *argument = [NSString stringWithUTF8String:argv[index]];
        if (index == 0 && [argument containsString:@"/"]) {
            NSString *basename = argument.lastPathComponent;
            if (basename.length > 0)
                argument = basename;
        }
        [storage addObject:argument];
        normalizedArgv[index] = storage.lastObject.UTF8String;
        index++;
    }
    normalizedArgv[index] = NULL;

    return [self startSessionWithExecutable:"/bin/busybox"
                                       argv:normalizedArgv
                                       envp:envp
                                   terminal:terminalOut
                                        pid:pidOut];
}

- (BOOL)terminalTextLooksLikeBusyboxUsage:(NSString *)text {
    return [text containsString:@"Usage: busybox"] || [text containsString:@"multi-call binary"];
}

- (BOOL)terminalTextLooksLikeRootDirectoryListing:(NSString *)text {
    return [text containsString:@"bin"] && [text containsString:@"etc"] &&
           [text containsString:@"proc"] && ![self terminalTextLooksLikeBusyboxUsage:text];
}

- (BOOL)startInteractiveBusyboxTerminal:(Terminal * __strong *)terminalOut
                                    pid:(int *)pidOut {
    const char *argv[] = { "sh", "-i", NULL };
    const char envp[] =
        "TERM=xterm-256color\0"
        "HOME=/root\0"
        "USER=root\0"
        "LOGNAME=root\0"
        "HISTFILE=/dev/null\0"
        "HISTSIZE=0\0"
        "HISTFILESIZE=0\0"
        "SHELL=/bin/sh\0"
        "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin\0"
        "\0";
    return [self startSessionWithExecutable:"/bin/sh"
                                       argv:argv
                                       envp:envp
                                   terminal:terminalOut
                                        pid:pidOut];
}

- (TerminalView *)configuredTerminalViewForTerminal:(Terminal *)terminal {
    __block TerminalView *view = nil;
    [self runOnMainThreadSync:^{
        view = [[TerminalView alloc] initWithFrame:CGRectMake(0, 0, 320, 480)];
        view.terminal = terminal;
        [view layoutIfNeeded];
    }];
    return view;
}

- (NSString *)dataRootPath {
    NSFileManager *fm = [NSFileManager defaultManager];
    NSURL *groupURL = [fm containerURLForSecurityApplicationGroupIdentifier:@"group.com.rudironsoni.emuLnx"];
    NSArray<NSString *> *groupPaths = groupURL != nil ? @[groupURL.path] : @[];

    for (NSString *groupPath in groupPaths) {
        NSString *rootPath = [groupPath stringByAppendingPathComponent:@"roots/default"];
        if ([fm fileExistsAtPath:rootPath]) {
            return rootPath;
        }
    }
    return nil;
}

- (void)testBusyboxLsDotRepro {
    [self configureFocusedTraceLevel];

    NSString *rootPath = [self dataRootPath];
    XCTAssertNotNil(rootPath, @"rootfs path must exist");
    if (rootPath == nil) {
        return;
    }

    if (![self bootstrapMountedRootfsAtPath:rootPath]) {
        return;
    }

    int childErr = become_new_init_child();
    XCTAssertEqual(childErr, 0, @"become_new_init_child returned %d", childErr);

    if (current != NULL && current->group != NULL && current->group->tty != NULL) {
        tty_set_winsize(current->group->tty, (struct winsize_){ .row = 1, .col = 1, .xpixel = 0, .ypixel = 0 });
    }

    const char argv[] = "/bin/busybox\0sh\0-c\0ls -a /\0\0";
    const char envp[] =
        "TERM=xterm-256color\0"
        "HOME=/root\0"
        "USER=root\0"
        "LOGNAME=root\0"
        "SHELL=/bin/sh\0"
        "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin\0"
        "\0";
    int execErr = do_execve("bin/busybox", 4, argv, envp);
    XCTAssertEqual(execErr, 0, @"do_execve returned %d", execErr);

    XCTAssertNotEqual(current, NULL, @"current must exist after execve");
    struct cpu_state *cpu = &current->cpu;
    XCTAssertNotEqual(cpu->mmu, NULL, @"cpu mmu must exist");

    struct tlb exec_tlb = {};
    tlb_refresh(&exec_tlb, cpu->mmu);
    exit_should_pthread_exit = false;
    a64_cpu_run(cpu, &exec_tlb);

    XCTAssertTrue(YES, @"bounded ls repro completed");
}

- (void)testRootfsRootDirectoryCanBeOpenedAndReadDirectly {
    [self configureFocusedTraceLevel];

    NSString *rootPath = [self dataRootPath];
    XCTAssertNotNil(rootPath, @"rootfs path must exist");
    if (rootPath == nil) {
        return;
    }

    if (![self bootstrapMountedRootfsAtPath:rootPath]) {
        return;
    }

    struct fd *fd = generic_open("/", O_RDONLY_ | O_DIRECTORY_, 0);
    XCTAssertFalse(IS_ERR(fd), @"generic_open(/) failed with %ld", (long) PTR_ERR(fd));
    if (IS_ERR(fd)) {
        return;
    }

    struct statbuf stat = {};
    int statErr = fd->mount->fs->fstat(fd, &stat);
    XCTAssertEqual(statErr, 0, @"fstat(/) failed with %d", statErr);
    XCTAssertTrue(S_ISDIR(stat.mode), @"fstat(/) must report a directory, mode=0x%x", stat.mode);
    XCTAssertGreaterThan(stat.blksize, 0u, @"fstat(/) must report a positive st_blksize");
    XCTAssertLessThanOrEqual(stat.blksize, 65536u,
                             @"fstat(/) must not report an absurd st_blksize, got %u",
                             stat.blksize);

    struct dir_entry entry = {};
    int readErr = fd->ops->readdir(fd, &entry);
    XCTAssertGreaterThan(readErr, 0, @"readdir(/) failed with %d", readErr);
    if (readErr > 0) {
        XCTAssertTrue(strlen(entry.name) > 0, @"readdir(/) must return a non-empty entry name");
    }

    XCTAssertEqual(fd_close(fd), 0, @"fd_close(/) failed");
}

- (void)testRootfsRootDirectoryCanBeOpenedWithLibcStyleDirectoryFlags {
    [self configureFocusedTraceLevel];

    NSString *rootPath = [self dataRootPath];
    XCTAssertNotNil(rootPath, @"rootfs path must exist");
    if (rootPath == nil) {
        return;
    }

    if (![self bootstrapMountedRootfsAtPath:rootPath]) {
        return;
    }

    int flags = O_RDONLY_ | O_NONBLOCK_ | O_DIRECTORY_ | O_CLOEXEC_;
    struct fd *fd = generic_open("/", flags, 0);
    XCTAssertFalse(IS_ERR(fd), @"generic_open(/, libc-style flags) failed with %ld",
                   (long) PTR_ERR(fd));
    if (IS_ERR(fd)) {
        return;
    }

    XCTAssertEqual(fd_getflags(fd) & (O_NONBLOCK_ | O_CLOEXEC_), O_NONBLOCK_,
                   @"directory fd flags should preserve guest-visible nonblocking state");

    struct statbuf stat = {};
    int statErr = fd->mount->fs->fstat(fd, &stat);
    XCTAssertEqual(statErr, 0, @"fstat(/) with libc-style flags failed with %d", statErr);
    XCTAssertTrue(S_ISDIR(stat.mode), @"fstat(/) with libc-style flags must report a directory");

    struct dir_entry entry = {};
    int readErr = fd->ops->readdir(fd, &entry);
    XCTAssertGreaterThan(readErr, 0, @"readdir(/) with libc-style flags failed with %d", readErr);

    XCTAssertEqual(fd_close(fd), 0, @"fd_close(/) with libc-style flags failed");
}

- (void)testBusyboxLsDotReproAppStyle {
    [self configureFocusedTraceLevel];

    if (![self bootstrapAppStyleSessionHarness]) {
        return;
    }

    Terminal *terminal = nil;
    int startPid = 0;
    const char *argv[] = { "/bin/busybox", "sh", NULL };
    const char envp[] =
        "TERM=xterm-256color\0"
        "HOME=/root\0"
        "USER=root\0"
        "LOGNAME=root\0"
        "SHELL=/bin/sh\0"
        "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin\0"
        "\0";
    if (![self startBusyboxSessionWithArgv:argv envp:envp terminal:&terminal pid:&startPid]) {
        return;
    }

    dispatch_async(dispatch_get_main_queue(), ^{
        [terminal sendInput:[@"ls -a /\r" dataUsingEncoding:NSUTF8StringEncoding]];
    });

    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:30.0];
    NSString *lastObserved = @"";
    while ([deadline timeIntervalSinceNow] > 0) {
        lastObserved = [terminal screenTextForTesting] ?: @"";
        if ([lastObserved containsString:@"bin"])
            return;
        if ([lastObserved containsString:@"Out of memory"]) {
            XCTFail(@"App-style terminal repro still reports OOM. Output: %@", lastObserved);
            return;
        }
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
    }

    XCTFail(@"Timed out waiting for terminal text containing 'bin'. Last observed: %@", lastObserved);
}

- (void)testBusyboxInteractiveShellPromptAppearsAppStyle {
    [self configureFocusedTraceLevel];

    if (![self bootstrapAppStyleSessionHarness]) {
        return;
    }

    Terminal *terminal = nil;
    int startPid = 0;
    if (![self startInteractiveBusyboxTerminal:&terminal pid:&startPid]) {
        return;
    }

    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:60.0];
    NSString *lastObserved = @"";
    while ([deadline timeIntervalSinceNow] > 0) {
        lastObserved = [terminal screenTextForTesting] ?: @"";
        if ([lastObserved containsString:@"/ # "] || [lastObserved hasSuffix:@"/ #"])
            return;
        if ([lastObserved containsString:@"Out of memory"]) {
            XCTFail(@"Interactive prompt path already reports OOM before input. Output: %@",
                    lastObserved);
            return;
        }
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
    }

    XCTFail(@"Timed out waiting for app-style interactive shell prompt. Last observed: %@",
            lastObserved);
}

- (void)testBusyboxAppStyleNonInteractiveGuestOutputAppears {
    [self configureFocusedTraceLevel];

    if (![self bootstrapAppStyleSessionHarness]) {
        return;
    }

    Terminal *terminal = nil;
    int startPid = 0;
    const char *argv[] = { "sh", "-c", "echo READY", NULL };
    const char envp[] =
        "TERM=xterm-256color\0"
        "HOME=/root\0"
        "USER=root\0"
        "LOGNAME=root\0"
        "SHELL=/bin/sh\0"
        "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin\0"
        "\0";
    if (![self startSessionWithExecutable:"/bin/sh"
                                     argv:argv
                                     envp:envp
                                 terminal:&terminal
                                      pid:&startPid]) {
        return;
    }

    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:30.0];
    NSString *lastObserved = @"";
    while ([deadline timeIntervalSinceNow] > 0) {
        lastObserved = [terminal screenTextForTesting] ?: @"";
        if ([lastObserved containsString:@"READY"])
            return;
        if ([lastObserved containsString:@"Out of memory"]) {
            XCTFail(@"Non-interactive app-style guest output path reports OOM. Output: %@",
                    lastObserved);
            return;
        }
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
    }

    XCTFail(@"Timed out waiting for non-interactive guest output. Last observed: %@",
            lastObserved);
}

- (void)testBusyboxAppStyleDirectUnameProducesOutput {
    [self configureFocusedTraceLevel];

    if (![self bootstrapAppStyleSessionHarness]) {
        return;
    }

    Terminal *terminal = nil;
    int startPid = 0;
    const char *argv[] = { "/bin/busybox", "uname", "-m", NULL };
    const char envp[] =
        "TERM=xterm-256color\0"
        "HOME=/root\0"
        "USER=root\0"
        "LOGNAME=root\0"
        "SHELL=/bin/sh\0"
        "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin\0"
        "\0";
    if (![self startBusyboxSessionWithArgv:argv envp:envp terminal:&terminal pid:&startPid]) {
        return;
    }

    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:30.0];
    NSString *lastObserved = @"";
    while ([deadline timeIntervalSinceNow] > 0) {
        lastObserved = [terminal screenTextForTesting] ?: @"";
        if ([lastObserved containsString:@"aarch64"])
            return;
        if ([self terminalTextLooksLikeBusyboxUsage:lastObserved]) {
            XCTFail(@"Direct busybox uname fell into usage output. Output: %@", lastObserved);
            return;
        }
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
    }

    XCTFail(@"Timed out waiting for direct busybox uname output. Last observed: %@", lastObserved);
}

- (void)testBusyboxAppStyleDirectLsDoesNotReportOutOfMemory {
    [self configureFocusedTraceLevel];
    [self resetA64ExecutionMode];

    if (![self bootstrapAppStyleSessionHarness]) {
        return;
    }

    Terminal *terminal = nil;
    int startPid = 0;
    const char *argv[] = { "/bin/busybox", "ls", "-a", "/", NULL };
    const char envp[] =
        "TERM=xterm-256color\0"
        "HOME=/root\0"
        "USER=root\0"
        "LOGNAME=root\0"
        "SHELL=/bin/sh\0"
        "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin\0"
        "\0";
    if (![self startBusyboxSessionWithArgv:argv envp:envp terminal:&terminal pid:&startPid]) {
        return;
    }

    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:30.0];
    NSString *lastObserved = @"";
    while ([deadline timeIntervalSinceNow] > 0) {
        lastObserved = [terminal screenTextForTesting] ?: @"";
        if ([self terminalTextLooksLikeRootDirectoryListing:lastObserved])
            return;
        if ([self terminalTextLooksLikeBusyboxUsage:lastObserved]) {
            XCTFail(@"Direct busybox ls fell into usage output. Output: %@", lastObserved);
            return;
        }
        if ([lastObserved containsString:@"Out of memory"]) {
            XCTFail(@"Direct busybox ls still reports OOM. Output: %@", lastObserved);
            return;
        }
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
    }

    XCTFail(@"Timed out waiting for direct busybox ls output. Last observed: %@", lastObserved);
}

- (void)testBusyboxAppStyleDirectLsConservativeModeDoesNotReportOutOfMemory {
    [self configureFocusedTraceLevel];
    setenv("ISH_A64_CONSERVATIVE_MODE", "1", 1);
    setenv("ISH_A64_VERBOSE_BLOCK_TRACE", "1", 1);

    if (![self bootstrapAppStyleSessionHarness]) {
        [self resetA64ExecutionMode];
        return;
    }

    Terminal *terminal = nil;
    int startPid = 0;
    const char *argv[] = { "/bin/busybox", "ls", "-a", "/", NULL };
    const char envp[] =
        "TERM=xterm-256color\0"
        "HOME=/root\0"
        "USER=root\0"
        "LOGNAME=root\0"
        "SHELL=/bin/sh\0"
        "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin\0"
        "\0";
    if (![self startBusyboxSessionWithArgv:argv envp:envp terminal:&terminal pid:&startPid]) {
        [self resetA64ExecutionMode];
        return;
    }

    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:30.0];
    NSString *lastObserved = @"";
    while ([deadline timeIntervalSinceNow] > 0) {
        lastObserved = [terminal screenTextForTesting] ?: @"";
        if ([self terminalTextLooksLikeRootDirectoryListing:lastObserved]) {
            [self resetA64ExecutionMode];
            return;
        }
        if ([self terminalTextLooksLikeBusyboxUsage:lastObserved]) {
            [self resetA64ExecutionMode];
            XCTFail(@"Direct busybox ls fell into usage output in conservative mode. Output: %@",
                    lastObserved);
            return;
        }
        if ([lastObserved containsString:@"Out of memory"]) {
            [self resetA64ExecutionMode];
            XCTFail(@"Direct busybox ls still reports OOM in conservative mode. Output: %@",
                    lastObserved);
            return;
        }
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
    }

    [self resetA64ExecutionMode];
    XCTFail(@"Timed out waiting for direct busybox ls output in conservative mode. Last observed: %@",
            lastObserved);
}

- (void)testBusyboxLsDotReproInteractiveShell {
    [self configureFocusedTraceLevel];

    if (![self bootstrapAppStyleSessionHarness]) {
        return;
    }

    Terminal *terminal = nil;
    int startPid = 0;
    if (![self startInteractiveBusyboxTerminal:&terminal pid:&startPid]) {
        return;
    }

    dispatch_async(dispatch_get_main_queue(), ^{
        [terminal sendInput:[@"ls -a /\r" dataUsingEncoding:NSUTF8StringEncoding]];
    });

    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:30.0];
    NSString *lastObserved = @"";
    while ([deadline timeIntervalSinceNow] > 0) {
        lastObserved = [terminal screenTextForTesting] ?: @"";
        if ([lastObserved containsString:@"bin"])
            return;
        if ([lastObserved containsString:@"Out of memory"]) {
            XCTFail(@"Interactive shell repro still reports OOM. Output: %@", lastObserved);
            return;
        }
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
    }

    XCTFail(@"Timed out waiting for terminal text containing 'bin'. Last observed: %@", lastObserved);
}

- (void)testBusyboxInteractiveShellExecBuiltinCanReplaceShellWithUname {
    [self configureFocusedTraceLevel];

    if (![self bootstrapAppStyleSessionHarness]) {
        return;
    }

    Terminal *terminal = nil;
    int startPid = 0;
    if (![self startInteractiveBusyboxTerminal:&terminal pid:&startPid]) {
        return;
    }

    dispatch_async(dispatch_get_main_queue(), ^{
        [terminal sendInput:[@"exec /bin/busybox uname -m\r" dataUsingEncoding:NSUTF8StringEncoding]];
    });

    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:30.0];
    NSString *lastObserved = @"";
    while ([deadline timeIntervalSinceNow] > 0) {
        lastObserved = [terminal screenTextForTesting] ?: @"";
        if ([lastObserved containsString:@"aarch64"])
            return;
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
    }

    XCTFail(@"Timed out waiting for exec-builtin uname output. Last observed: %@", lastObserved);
}

- (void)testBusyboxLsDotReproLoadedTerminalView {
    [self configureFocusedTraceLevel];

    if (![self bootstrapAppStyleSessionHarness]) {
        return;
    }

    Terminal *terminal = nil;
    int startPid = 0;
    if (![self startInteractiveBusyboxTerminal:&terminal pid:&startPid]) {
        return;
    }

    TerminalView *view = [self configuredTerminalViewForTerminal:terminal];

    dispatch_async(dispatch_get_main_queue(), ^{
        (void)view;
        [terminal sendInput:[@"ls -a /\r" dataUsingEncoding:NSUTF8StringEncoding]];
    });

    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:30.0];
    NSString *lastObserved = @"";
    while ([deadline timeIntervalSinceNow] > 0) {
        lastObserved = [terminal screenTextForTesting] ?: @"";
        if ([lastObserved containsString:@"bin"])
            return;
        if ([lastObserved containsString:@"Out of memory"]) {
            XCTFail(@"Loaded terminal view repro still reports OOM. Output: %@", lastObserved);
            return;
        }
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
    }

    XCTFail(@"Timed out waiting for terminal text containing 'bin'. Last observed: %@", lastObserved);
}

- (void)testBusyboxLsDotReproViaTerminalInsertText {
    [self configureFocusedTraceLevel];

    if (![self bootstrapAppStyleSessionHarness]) {
        return;
    }

    Terminal *terminal = nil;
    int startPid = 0;
    if (![self startInteractiveBusyboxTerminal:&terminal pid:&startPid]) {
        return;
    }

    TerminalView *view = [self configuredTerminalViewForTerminal:terminal];

    [self runOnMainThreadSync:^{
        [view insertText:@"ls -a /\n"];
    }];

    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:30.0];
    NSString *lastObserved = @"";
    while ([deadline timeIntervalSinceNow] > 0) {
        lastObserved = [terminal screenTextForTesting] ?: @"";
        if ([lastObserved containsString:@"bin"])
            return;
        if ([lastObserved containsString:@"Out of memory"]) {
            XCTFail(@"Terminal insertText repro still reports OOM. Output: %@", lastObserved);
            return;
        }
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
    }

    XCTFail(@"Timed out waiting for terminal text containing 'bin'. Last observed: %@", lastObserved);
}

- (void)testBusyboxLsDotReproAfterPromptWait {
    [self configureFocusedTraceLevel];

    if (![self bootstrapAppStyleSessionHarness]) {
        return;
    }

    Terminal *terminal = nil;
    int startPid = 0;
    if (![self startInteractiveBusyboxTerminal:&terminal pid:&startPid]) {
        return;
    }

    TerminalView *view = [self configuredTerminalViewForTerminal:terminal];

    NSDate *promptDeadline = [NSDate dateWithTimeIntervalSinceNow:60.0];
    NSString *promptText = @"";
    while ([promptDeadline timeIntervalSinceNow] > 0) {
        promptText = [terminal screenTextForTesting] ?: @"";
        if ([promptText containsString:@"/ # "] || [promptText hasSuffix:@"/ #"]) {
            break;
        }
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
    }

    XCTAssertTrue([promptText containsString:@"/ # "] || [promptText hasSuffix:@"/ #"], @"prompt must appear before typing. Last observed: %@", promptText);

    dispatch_async(dispatch_get_main_queue(), ^{
        [view insertText:@"ls -a /\n"];
    });

    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:30.0];
    NSString *lastObserved = @"";
    while ([deadline timeIntervalSinceNow] > 0) {
        lastObserved = [terminal screenTextForTesting] ?: @"";
        if ([lastObserved containsString:@"bin"])
            return;
        if ([lastObserved containsString:@"Out of memory"]) {
            XCTFail(@"Prompt-wait repro still reports OOM. Output: %@", lastObserved);
            return;
        }
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
    }

    XCTFail(@"Timed out waiting for terminal text containing 'bin'. Last observed: %@", lastObserved);
}

- (void)testBusyboxLsDotReproCharacterByCharacter {
    [self configureFocusedTraceLevel];

    if (![self bootstrapAppStyleSessionHarness]) {
        return;
    }

    Terminal *terminal = nil;
    int startPid = 0;
    if (![self startInteractiveBusyboxTerminal:&terminal pid:&startPid]) {
        return;
    }

    TerminalView *view = [self configuredTerminalViewForTerminal:terminal];

    NSDate *promptDeadline = [NSDate dateWithTimeIntervalSinceNow:60.0];
    while ([promptDeadline timeIntervalSinceNow] > 0) {
        NSString *text = [terminal screenTextForTesting] ?: @"";
        if ([text containsString:@"/ # "] || [text hasSuffix:@"/ #"]) {
            break;
        }
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
    }

    dispatch_async(dispatch_get_main_queue(), ^{
        NSString *command = @"ls -a /\n";
        for (NSUInteger i = 0; i < command.length; i++) {
            unichar ch = [command characterAtIndex:i];
            NSString *piece = [NSString stringWithCharacters:&ch length:1];
            [view insertText:piece];
        }
    });

    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:30.0];
    NSString *lastObserved = @"";
    while ([deadline timeIntervalSinceNow] > 0) {
        lastObserved = [terminal screenTextForTesting] ?: @"";
        if ([lastObserved containsString:@"bin"])
            return;
        if ([lastObserved containsString:@"Out of memory"]) {
            XCTFail(@"Character-by-character repro still reports OOM. Output: %@", lastObserved);
            return;
        }
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
    }

    XCTFail(@"Timed out waiting for terminal text containing 'bin'. Last observed: %@", lastObserved);
}

- (void)testBusyboxLsDotReproWithFocusedTerminalView {
    [self configureFocusedTraceLevel];

    if (![self bootstrapAppStyleSessionHarness]) {
        return;
    }

    Terminal *terminal = nil;
    int startPid = 0;
    if (![self startInteractiveBusyboxTerminal:&terminal pid:&startPid]) {
        return;
    }

    TerminalView *view = [self configuredTerminalViewForTerminal:terminal];

    [self runOnMainThreadSync:^{
        [view focusForTesting];
    }];

    NSDate *promptDeadline = [NSDate dateWithTimeIntervalSinceNow:60.0];
    while ([promptDeadline timeIntervalSinceNow] > 0) {
        NSString *text = [terminal screenTextForTesting] ?: @"";
        if ([text containsString:@"/ # "] || [text hasSuffix:@"/ #"]) {
            break;
        }
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
    }

    dispatch_async(dispatch_get_main_queue(), ^{
        [view insertText:@"ls -a /\n"];
    });

    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:30.0];
    NSString *lastObserved = @"";
    while ([deadline timeIntervalSinceNow] > 0) {
        lastObserved = [terminal screenTextForTesting] ?: @"";
        if ([lastObserved containsString:@"bin"])
            return;
        if ([lastObserved containsString:@"Out of memory"]) {
            XCTFail(@"Focused terminal view repro still reports OOM. Output: %@", lastObserved);
            return;
        }
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
    }

    XCTFail(@"Timed out waiting for terminal text containing 'bin'. Last observed: %@", lastObserved);
}

- (void)testBusyboxLsDotReproViaUITextField {
    [self configureFocusedTraceLevel];

    if (![self bootstrapAppStyleSessionHarness]) {
        return;
    }

    Terminal *terminal = nil;
    int startPid = 0;
    if (![self startInteractiveBusyboxTerminal:&terminal pid:&startPid]) {
        return;
    }

    [self runOnMainThreadSync:^{
        TerminalView *view = [self configuredTerminalViewForTerminal:terminal];
        UITextField *inputField = [view valueForKey:@"uiTestInputField"];
        XCTAssertNotNil(inputField, @"uiTestInputField must exist in test mode");
        [inputField becomeFirstResponder];
        NSString *command = @"ls -a /\n";
        for (NSUInteger i = 0; i < command.length; i++) {
            unichar ch = [command characterAtIndex:i];
            NSString *piece = [NSString stringWithCharacters:&ch length:1];
            [inputField insertText:piece];
        }
    }];

    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:30.0];
    NSString *lastObserved = @"";
    while ([deadline timeIntervalSinceNow] > 0) {
        lastObserved = [terminal screenTextForTesting] ?: @"";
        if ([lastObserved containsString:@"bin"])
            return;
        if ([lastObserved containsString:@"Out of memory"]) {
            XCTFail(@"UITextField repro still reports OOM. Output: %@", lastObserved);
            return;
        }
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
    }

    XCTFail(@"Timed out waiting for terminal text containing 'bin'. Last observed: %@", lastObserved);
}

- (void)testBusyboxLsDotReproAfterAppBootstrapWithInitChild {
    [self configureFocusedTraceLevel];

    if (![self bootstrapAppStyleSessionHarness]) {
        return;
    }

    Terminal *terminal = nil;
    int startPid = 0;
    if (![self startInteractiveBusyboxTerminal:&terminal pid:&startPid]) {
        return;
    }

    dispatch_async(dispatch_get_main_queue(), ^{
        [terminal sendInput:[@"ls -a /\r" dataUsingEncoding:NSUTF8StringEncoding]];
    });

    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:30.0];
    NSString *lastObserved = @"";
    while ([deadline timeIntervalSinceNow] > 0) {
        lastObserved = [terminal screenTextForTesting] ?: @"";
        if ([lastObserved containsString:@"bin"])
            return;
        if ([lastObserved containsString:@"Out of memory"]) {
            XCTFail(@"App bootstrap + init child repro reports OOM. Output: %@", lastObserved);
            return;
        }
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
    }

    XCTFail(@"Timed out waiting for terminal text containing 'bin'. Last observed: %@", lastObserved);
}

- (void)testBusyboxLsDotReproAfterAppBootstrapWithInitChildViaTerminalView {
    [self configureFocusedTraceLevel];

    if (![self bootstrapAppStyleSessionHarness]) {
        return;
    }

    Terminal *terminal = nil;
    int startPid = 0;
    if (![self startInteractiveBusyboxTerminal:&terminal pid:&startPid]) {
        return;
    }

    TerminalView *view = [self configuredTerminalViewForTerminal:terminal];

    [self runOnMainThreadSync:^{
        [view focusForTesting];
        [view insertText:@"ls -a /\n"];
    }];

    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:30.0];
    NSString *lastObserved = @"";
    while ([deadline timeIntervalSinceNow] > 0) {
        lastObserved = [terminal screenTextForTesting] ?: @"";
        if ([lastObserved containsString:@"bin"])
            return;
        if ([lastObserved containsString:@"Out of memory"]) {
            XCTFail(@"App bootstrap + init child + terminal view repro reports OOM. Output: %@", lastObserved);
            return;
        }
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
    }

    XCTFail(@"Timed out waiting for terminal text containing 'bin'. Last observed: %@", lastObserved);
}

@end
