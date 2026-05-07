#import <XCTest/XCTest.h>

#import <IXLandLinuxRuntime/kernel/task.h>
#import <IXLandLinuxRuntime/kernel/init.h>
#import <IXLandLinuxRuntime/kernel/calls.h>
#import <IXLandLinuxRuntime/fs/tty.h>
#import <IXLandLinuxRuntime/emu/tlb.h>
#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>
#import <IXLandTerminal/AppDelegate.h>
#import <IXLandTerminal/LinuxInterop.h>
#import <IXLandTerminal/Terminal.h>

extern bool exit_should_pthread_exit;

@interface GuestBusyboxLsReproTests : XCTestCase
@end

@implementation GuestBusyboxLsReproTests

- (NSString *)dataRootPath {
    NSFileManager *fm = [NSFileManager defaultManager];
    NSArray<NSString *> *groupPaths = [fm containerURLsForSecurityApplicationGroupIdentifier:@"group.com.rudironsoni.emuLnx"].count > 0
        ? @[[fm containerURLForSecurityApplicationGroupIdentifier:@"group.com.rudironsoni.emuLnx"].path]
        : @[];

    for (NSString *groupPath in groupPaths) {
        NSString *rootPath = [groupPath stringByAppendingPathComponent:@"roots/default"];
        if ([fm fileExistsAtPath:rootPath]) {
            return rootPath;
        }
    }
    return nil;
}

- (void)testBusyboxLsDotRepro {
    NSString *rootPath = [self dataRootPath];
    XCTAssertNotNil(rootPath, @"rootfs path must exist");
    if (rootPath == nil) {
        return;
    }

    int mountErr = mount_root(&rootfs, rootPath.UTF8String);
    XCTAssertTrue(mountErr == 0 || mountErr == -16, @"mount_root returned %d", mountErr);

    int initErr = become_first_process();
    XCTAssertTrue(initErr == 0 || initErr == -17, @"become_first_process returned %d", initErr);

    int childErr = become_new_init_child();
    XCTAssertEqual(childErr, 0, @"become_new_init_child returned %d", childErr);

    if (current != NULL && current->tty != NULL) {
        tty_set_winsize(current->tty, (struct winsize_){ .row = 1, .col = 1, .xpixel = 0, .ypixel = 0 });
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
    int execErr = do_execve("bin/busybox", 3, argv, envp);
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

- (void)testBusyboxLsDotReproAppStyle {
    NSString *rootPath = [self dataRootPath];
    XCTAssertNotNil(rootPath, @"rootfs path must exist");
    if (rootPath == nil) {
        return;
    }

    int mountErr = mount_root(&rootfs, rootPath.UTF8String);
    XCTAssertTrue(mountErr == 0 || mountErr == -16, @"mount_root returned %d", mountErr);

    int initErr = become_first_process();
    XCTAssertTrue(initErr == 0 || initErr == -17, @"become_first_process returned %d", initErr);

    __block Terminal *terminal = nil;
    __block int startErr = -1;
    __block int startPid = 0;
    dispatch_semaphore_t startSemaphore = dispatch_semaphore_create(0);

    const char *argv[] = { "/bin/busybox", "sh", NULL };
    const char envp[] =
        "TERM=xterm-256color\0"
        "HOME=/root\0"
        "USER=root\0"
        "LOGNAME=root\0"
        "SHELL=/bin/sh\0"
        "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin\0"
        "\0";
    linux_start_session("/bin/busybox", argv, envp, ^(int retval, int pid, nsobj_t terminalObject) {
        startErr = retval;
        startPid = pid;
        if (terminalObject != NULL)
            terminal = (__bridge Terminal *)terminalObject;
        dispatch_semaphore_signal(startSemaphore);
    });

    long waitResult = dispatch_semaphore_wait(startSemaphore, dispatch_time(DISPATCH_TIME_NOW, (int64_t)(10 * NSEC_PER_SEC)));
    XCTAssertEqual(waitResult, 0L, @"linux_start_session did not complete");
    XCTAssertEqual(startErr, 0, @"linux_start_session returned %d", startErr);
    XCTAssertNotEqual(startPid, 0, @"session pid must be assigned");
    XCTAssertNotNil(terminal, @"terminal object must be returned");

    dispatch_async(dispatch_get_main_queue(), ^{
        [terminal sendInput:[@"ls -a /\n" dataUsingEncoding:NSUTF8StringEncoding]];
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

- (void)testBusyboxLsDotReproInteractiveShell {
    NSString *rootPath = [self dataRootPath];
    XCTAssertNotNil(rootPath, @"rootfs path must exist");
    if (rootPath == nil) {
        return;
    }

    int mountErr = mount_root(&rootfs, rootPath.UTF8String);
    XCTAssertTrue(mountErr == 0 || mountErr == -16, @"mount_root returned %d", mountErr);

    int initErr = become_first_process();
    XCTAssertTrue(initErr == 0 || initErr == -17, @"become_first_process returned %d", initErr);

    __block Terminal *terminal = nil;
    __block int startErr = -1;
    __block int startPid = 0;
    dispatch_semaphore_t startSemaphore = dispatch_semaphore_create(0);

    const char *argv[] = { "/bin/busybox", "sh", "-i", NULL };
    const char envp[] =
        "TERM=xterm-256color\0"
        "HOME=/root\0"
        "USER=root\0"
        "LOGNAME=root\0"
        "SHELL=/bin/sh\0"
        "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin\0"
        "\0";
    linux_start_session("/bin/busybox", argv, envp, ^(int retval, int pid, nsobj_t terminalObject) {
        startErr = retval;
        startPid = pid;
        if (terminalObject != NULL)
            terminal = (__bridge Terminal *)terminalObject;
        dispatch_semaphore_signal(startSemaphore);
    });

    long waitResult = dispatch_semaphore_wait(startSemaphore, dispatch_time(DISPATCH_TIME_NOW, (int64_t)(10 * NSEC_PER_SEC)));
    XCTAssertEqual(waitResult, 0L, @"linux_start_session did not complete");
    XCTAssertEqual(startErr, 0, @"linux_start_session returned %d", startErr);
    XCTAssertNotEqual(startPid, 0, @"session pid must be assigned");
    XCTAssertNotNil(terminal, @"terminal object must be returned");

    dispatch_async(dispatch_get_main_queue(), ^{
        [terminal sendInput:[@"ls -a /\n" dataUsingEncoding:NSUTF8StringEncoding]];
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

- (void)testBusyboxLsDotReproLoadedTerminalView {
    NSString *rootPath = [self dataRootPath];
    XCTAssertNotNil(rootPath, @"rootfs path must exist");
    if (rootPath == nil) {
        return;
    }

    int mountErr = mount_root(&rootfs, rootPath.UTF8String);
    XCTAssertTrue(mountErr == 0 || mountErr == -16, @"mount_root returned %d", mountErr);

    int initErr = become_first_process();
    XCTAssertTrue(initErr == 0 || initErr == -17, @"become_first_process returned %d", initErr);

    __block Terminal *terminal = nil;
    __block int startErr = -1;
    __block int startPid = 0;
    dispatch_semaphore_t startSemaphore = dispatch_semaphore_create(0);

    const char *argv[] = { "/bin/busybox", "sh", "-i", NULL };
    const char envp[] =
        "TERM=xterm-256color\0"
        "HOME=/root\0"
        "USER=root\0"
        "LOGNAME=root\0"
        "SHELL=/bin/sh\0"
        "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin\0"
        "\0";
    linux_start_session("/bin/busybox", argv, envp, ^(int retval, int pid, nsobj_t terminalObject) {
        startErr = retval;
        startPid = pid;
        if (terminalObject != NULL)
            terminal = (__bridge Terminal *)terminalObject;
        dispatch_semaphore_signal(startSemaphore);
    });

    long waitResult = dispatch_semaphore_wait(startSemaphore, dispatch_time(DISPATCH_TIME_NOW, (int64_t)(10 * NSEC_PER_SEC)));
    XCTAssertEqual(waitResult, 0L, @"linux_start_session did not complete");
    XCTAssertEqual(startErr, 0, @"linux_start_session returned %d", startErr);
    XCTAssertNotEqual(startPid, 0, @"session pid must be assigned");
    XCTAssertNotNil(terminal, @"terminal object must be returned");

    dispatch_sync(dispatch_get_main_queue(), ^{
        UIView *view = terminal.webView;
        view.frame = CGRectMake(0, 0, 320, 480);
        [view layoutIfNeeded];
    });

    dispatch_async(dispatch_get_main_queue(), ^{
        [terminal sendInput:[@"ls -a /\n" dataUsingEncoding:NSUTF8StringEncoding]];
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
    NSString *rootPath = [self dataRootPath];
    XCTAssertNotNil(rootPath, @"rootfs path must exist");
    if (rootPath == nil) {
        return;
    }

    int mountErr = mount_root(&rootfs, rootPath.UTF8String);
    XCTAssertTrue(mountErr == 0 || mountErr == -16, @"mount_root returned %d", mountErr);

    int initErr = become_first_process();
    XCTAssertTrue(initErr == 0 || initErr == -17, @"become_first_process returned %d", initErr);

    __block Terminal *terminal = nil;
    __block int startErr = -1;
    __block int startPid = 0;
    dispatch_semaphore_t startSemaphore = dispatch_semaphore_create(0);

    const char *argv[] = { "/bin/busybox", "sh", "-i", NULL };
    const char envp[] =
        "TERM=xterm-256color\0"
        "HOME=/root\0"
        "USER=root\0"
        "LOGNAME=root\0"
        "SHELL=/bin/sh\0"
        "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin\0"
        "\0";
    linux_start_session("/bin/busybox", argv, envp, ^(int retval, int pid, nsobj_t terminalObject) {
        startErr = retval;
        startPid = pid;
        if (terminalObject != NULL)
            terminal = (__bridge Terminal *)terminalObject;
        dispatch_semaphore_signal(startSemaphore);
    });

    long waitResult = dispatch_semaphore_wait(startSemaphore, dispatch_time(DISPATCH_TIME_NOW, (int64_t)(10 * NSEC_PER_SEC)));
    XCTAssertEqual(waitResult, 0L, @"linux_start_session did not complete");
    XCTAssertEqual(startErr, 0, @"linux_start_session returned %d", startErr);
    XCTAssertNotEqual(startPid, 0, @"session pid must be assigned");
    XCTAssertNotNil(terminal, @"terminal object must be returned");

    dispatch_sync(dispatch_get_main_queue(), ^{
        UIView *view = terminal.webView;
        view.frame = CGRectMake(0, 0, 320, 480);
        [view layoutIfNeeded];
        [(id)view insertText:@"ls -a /\n"];
    });

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
    NSString *rootPath = [self dataRootPath];
    XCTAssertNotNil(rootPath, @"rootfs path must exist");
    if (rootPath == nil) {
        return;
    }

    int mountErr = mount_root(&rootfs, rootPath.UTF8String);
    XCTAssertTrue(mountErr == 0 || mountErr == -16, @"mount_root returned %d", mountErr);

    int initErr = become_first_process();
    XCTAssertTrue(initErr == 0 || initErr == -17, @"become_first_process returned %d", initErr);

    __block Terminal *terminal = nil;
    __block int startErr = -1;
    __block int startPid = 0;
    dispatch_semaphore_t startSemaphore = dispatch_semaphore_create(0);

    const char *argv[] = { "/bin/busybox", "sh", "-i", NULL };
    const char envp[] =
        "TERM=xterm-256color\0"
        "HOME=/root\0"
        "USER=root\0"
        "LOGNAME=root\0"
        "SHELL=/bin/sh\0"
        "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin\0"
        "\0";
    linux_start_session("/bin/busybox", argv, envp, ^(int retval, int pid, nsobj_t terminalObject) {
        startErr = retval;
        startPid = pid;
        if (terminalObject != NULL)
            terminal = (__bridge Terminal *)terminalObject;
        dispatch_semaphore_signal(startSemaphore);
    });

    long waitResult = dispatch_semaphore_wait(startSemaphore, dispatch_time(DISPATCH_TIME_NOW, (int64_t)(10 * NSEC_PER_SEC)));
    XCTAssertEqual(waitResult, 0L, @"linux_start_session did not complete");
    XCTAssertEqual(startErr, 0, @"linux_start_session returned %d", startErr);
    XCTAssertNotEqual(startPid, 0, @"session pid must be assigned");
    XCTAssertNotNil(terminal, @"terminal object must be returned");

    dispatch_sync(dispatch_get_main_queue(), ^{
        UIView *view = terminal.webView;
        view.frame = CGRectMake(0, 0, 320, 480);
        [view layoutIfNeeded];
    });

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
        [(id)terminal.webView insertText:@"ls -a /\n"];
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
    NSString *rootPath = [self dataRootPath];
    XCTAssertNotNil(rootPath, @"rootfs path must exist");
    if (rootPath == nil) {
        return;
    }

    int mountErr = mount_root(&rootfs, rootPath.UTF8String);
    XCTAssertTrue(mountErr == 0 || mountErr == -16, @"mount_root returned %d", mountErr);

    int initErr = become_first_process();
    XCTAssertTrue(initErr == 0 || initErr == -17, @"become_first_process returned %d", initErr);

    __block Terminal *terminal = nil;
    __block int startErr = -1;
    __block int startPid = 0;
    dispatch_semaphore_t startSemaphore = dispatch_semaphore_create(0);

    const char *argv[] = { "/bin/busybox", "sh", "-i", NULL };
    const char envp[] =
        "TERM=xterm-256color\0"
        "HOME=/root\0"
        "USER=root\0"
        "LOGNAME=root\0"
        "SHELL=/bin/sh\0"
        "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin\0"
        "\0";
    linux_start_session("/bin/busybox", argv, envp, ^(int retval, int pid, nsobj_t terminalObject) {
        startErr = retval;
        startPid = pid;
        if (terminalObject != NULL)
            terminal = (__bridge Terminal *)terminalObject;
        dispatch_semaphore_signal(startSemaphore);
    });

    long waitResult = dispatch_semaphore_wait(startSemaphore, dispatch_time(DISPATCH_TIME_NOW, (int64_t)(10 * NSEC_PER_SEC)));
    XCTAssertEqual(waitResult, 0L, @"linux_start_session did not complete");
    XCTAssertEqual(startErr, 0, @"linux_start_session returned %d", startErr);
    XCTAssertNotEqual(startPid, 0, @"session pid must be assigned");
    XCTAssertNotNil(terminal, @"terminal object must be returned");

    dispatch_sync(dispatch_get_main_queue(), ^{
        UIView *view = terminal.webView;
        view.frame = CGRectMake(0, 0, 320, 480);
        [view layoutIfNeeded];
    });

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
            [(id)terminal.webView insertText:piece];
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
    NSString *rootPath = [self dataRootPath];
    XCTAssertNotNil(rootPath, @"rootfs path must exist");
    if (rootPath == nil) {
        return;
    }

    int mountErr = mount_root(&rootfs, rootPath.UTF8String);
    XCTAssertTrue(mountErr == 0 || mountErr == -16, @"mount_root returned %d", mountErr);

    int initErr = become_first_process();
    XCTAssertTrue(initErr == 0 || initErr == -17, @"become_first_process returned %d", initErr);

    __block Terminal *terminal = nil;
    __block int startErr = -1;
    __block int startPid = 0;
    dispatch_semaphore_t startSemaphore = dispatch_semaphore_create(0);

    const char *argv[] = { "/bin/busybox", "sh", "-i", NULL };
    const char envp[] =
        "TERM=xterm-256color\0"
        "HOME=/root\0"
        "USER=root\0"
        "LOGNAME=root\0"
        "SHELL=/bin/sh\0"
        "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin\0"
        "\0";
    linux_start_session("/bin/busybox", argv, envp, ^(int retval, int pid, nsobj_t terminalObject) {
        startErr = retval;
        startPid = pid;
        if (terminalObject != NULL)
            terminal = (__bridge Terminal *)terminalObject;
        dispatch_semaphore_signal(startSemaphore);
    });

    long waitResult = dispatch_semaphore_wait(startSemaphore, dispatch_time(DISPATCH_TIME_NOW, (int64_t)(10 * NSEC_PER_SEC)));
    XCTAssertEqual(waitResult, 0L, @"linux_start_session did not complete");
    XCTAssertEqual(startErr, 0, @"linux_start_session returned %d", startErr);
    XCTAssertNotEqual(startPid, 0, @"session pid must be assigned");
    XCTAssertNotNil(terminal, @"terminal object must be returned");

    dispatch_sync(dispatch_get_main_queue(), ^{
        UIView *view = terminal.webView;
        view.frame = CGRectMake(0, 0, 320, 480);
        [view layoutIfNeeded];
        [(id)view focusForTesting];
    });

    NSDate *promptDeadline = [NSDate dateWithTimeIntervalSinceNow:60.0];
    while ([promptDeadline timeIntervalSinceNow] > 0) {
        NSString *text = [terminal screenTextForTesting] ?: @"";
        if ([text containsString:@"/ # "] || [text hasSuffix:@"/ #"]) {
            break;
        }
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
    }

    dispatch_async(dispatch_get_main_queue(), ^{
        [(id)terminal.webView insertText:@"ls -a /\n"];
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
    NSString *rootPath = [self dataRootPath];
    XCTAssertNotNil(rootPath, @"rootfs path must exist");
    if (rootPath == nil) {
        return;
    }

    int mountErr = mount_root(&rootfs, rootPath.UTF8String);
    XCTAssertTrue(mountErr == 0 || mountErr == -16, @"mount_root returned %d", mountErr);

    int initErr = become_first_process();
    XCTAssertTrue(initErr == 0 || initErr == -17, @"become_first_process returned %d", initErr);

    __block Terminal *terminal = nil;
    __block int startErr = -1;
    __block int startPid = 0;
    dispatch_semaphore_t startSemaphore = dispatch_semaphore_create(0);

    const char *argv[] = { "/bin/busybox", "sh", "-i", NULL };
    const char envp[] =
        "TERM=xterm-256color\0"
        "HOME=/root\0"
        "USER=root\0"
        "LOGNAME=root\0"
        "SHELL=/bin/sh\0"
        "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin\0"
        "\0";
    linux_start_session("/bin/busybox", argv, envp, ^(int retval, int pid, nsobj_t terminalObject) {
        startErr = retval;
        startPid = pid;
        if (terminalObject != NULL)
            terminal = (__bridge Terminal *)terminalObject;
        dispatch_semaphore_signal(startSemaphore);
    });

    long waitResult = dispatch_semaphore_wait(startSemaphore, dispatch_time(DISPATCH_TIME_NOW, (int64_t)(10 * NSEC_PER_SEC)));
    XCTAssertEqual(waitResult, 0L, @"linux_start_session did not complete");
    XCTAssertEqual(startErr, 0, @"linux_start_session returned %d", startErr);
    XCTAssertNotEqual(startPid, 0, @"session pid must be assigned");
    XCTAssertNotNil(terminal, @"terminal object must be returned");

    dispatch_sync(dispatch_get_main_queue(), ^{
        UIView *view = terminal.webView;
        view.frame = CGRectMake(0, 0, 320, 480);
        [view layoutIfNeeded];
        UITextField *inputField = [view valueForKey:@"uiTestInputField"];
        XCTAssertNotNil(inputField, @"uiTestInputField must exist in test mode");
        [inputField becomeFirstResponder];
        NSString *command = @"ls -a /\n";
        for (NSUInteger i = 0; i < command.length; i++) {
            unichar ch = [command characterAtIndex:i];
            NSString *piece = [NSString stringWithCharacters:&ch length:1];
            [inputField insertText:piece];
        }
    });

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

- (void)testBusyboxLsDotReproAfterAppBootstrap {
    NSString *rootPath = [self dataRootPath];
    XCTAssertNotNil(rootPath, @"rootfs path must exist");
    if (rootPath == nil) {
        return;
    }

    int bootstrapErr = [AppDelegate bootstrapRuntimeForSession];
    XCTAssertEqual(bootstrapErr, 0, @"bootstrapRuntimeForSession returned %d", bootstrapErr);

    __block Terminal *terminal = nil;
    __block int startErr = -1;
    __block int startPid = 0;
    dispatch_semaphore_t startSemaphore = dispatch_semaphore_create(0);

    const char *argv[] = { "/bin/busybox", "sh", "-i", NULL };
    const char envp[] =
        "TERM=xterm-256color\0"
        "HOME=/root\0"
        "USER=root\0"
        "LOGNAME=root\0"
        "SHELL=/bin/sh\0"
        "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin\0"
        "\0";
    linux_start_session("/bin/busybox", argv, envp, ^(int retval, int pid, nsobj_t terminalObject) {
        startErr = retval;
        startPid = pid;
        if (terminalObject != NULL)
            terminal = (__bridge Terminal *)terminalObject;
        dispatch_semaphore_signal(startSemaphore);
    });

    long waitResult = dispatch_semaphore_wait(startSemaphore, dispatch_time(DISPATCH_TIME_NOW, (int64_t)(10 * NSEC_PER_SEC)));
    XCTAssertEqual(waitResult, 0L, @"linux_start_session did not complete");
    XCTAssertEqual(startErr, 0, @"linux_start_session returned %d", startErr);
    XCTAssertNotEqual(startPid, 0, @"session pid must be assigned");
    XCTAssertNotNil(terminal, @"terminal object must be returned");

    dispatch_async(dispatch_get_main_queue(), ^{
        [terminal sendInput:[@"ls -a /\n" dataUsingEncoding:NSUTF8StringEncoding]];
    });

    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:30.0];
    NSString *lastObserved = @"";
    while ([deadline timeIntervalSinceNow] > 0) {
        lastObserved = [terminal screenTextForTesting] ?: @"";
        if ([lastObserved containsString:@"bin"])
            return;
        if ([lastObserved containsString:@"Out of memory"]) {
            XCTFail(@"App bootstrap repro reports OOM. Output: %@", lastObserved);
            return;
        }
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
    }

    XCTFail(@"Timed out waiting for terminal text containing 'bin'. Last observed: %@", lastObserved);
}

- (void)testBusyboxLsDotReproAfterAppBootstrapWithInitChild {
    NSString *rootPath = [self dataRootPath];
    XCTAssertNotNil(rootPath, @"rootfs path must exist");
    if (rootPath == nil) {
        return;
    }

    int bootstrapErr = [AppDelegate bootstrapRuntimeForSession];
    XCTAssertEqual(bootstrapErr, 0, @"bootstrapRuntimeForSession returned %d", bootstrapErr);

    int childErr = become_new_init_child();
    XCTAssertEqual(childErr, 0, @"become_new_init_child returned %d", childErr);

    __block Terminal *terminal = nil;
    __block int startErr = -1;
    __block int startPid = 0;
    dispatch_semaphore_t startSemaphore = dispatch_semaphore_create(0);

    const char *argv[] = { "/bin/busybox", "sh", "-i", NULL };
    const char envp[] =
        "TERM=xterm-256color\0"
        "HOME=/root\0"
        "USER=root\0"
        "LOGNAME=root\0"
        "SHELL=/bin/sh\0"
        "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin\0"
        "\0";
    linux_start_session("/bin/busybox", argv, envp, ^(int retval, int pid, nsobj_t terminalObject) {
        startErr = retval;
        startPid = pid;
        if (terminalObject != NULL)
            terminal = (__bridge Terminal *) terminalObject;
        dispatch_semaphore_signal(startSemaphore);
    });

    long waitResult = dispatch_semaphore_wait(startSemaphore, dispatch_time(DISPATCH_TIME_NOW, (int64_t)(10 * NSEC_PER_SEC)));
    XCTAssertEqual(waitResult, 0L, @"linux_start_session did not complete");
    XCTAssertEqual(startErr, 0, @"linux_start_session returned %d", startErr);
    XCTAssertNotEqual(startPid, 0, @"session pid must be assigned");
    XCTAssertNotNil(terminal, @"terminal object must be returned");

    dispatch_async(dispatch_get_main_queue(), ^{
        [terminal sendInput:[@"ls -a /\n" dataUsingEncoding:NSUTF8StringEncoding]];
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
    NSString *rootPath = [self dataRootPath];
    XCTAssertNotNil(rootPath, @"rootfs path must exist");
    if (rootPath == nil) {
        return;
    }

    int bootstrapErr = [AppDelegate bootstrapRuntimeForSession];
    XCTAssertEqual(bootstrapErr, 0, @"bootstrapRuntimeForSession returned %d", bootstrapErr);

    int childErr = become_new_init_child();
    XCTAssertEqual(childErr, 0, @"become_new_init_child returned %d", childErr);

    __block Terminal *terminal = nil;
    __block int startErr = -1;
    __block int startPid = 0;
    dispatch_semaphore_t startSemaphore = dispatch_semaphore_create(0);

    const char *argv[] = { "/bin/busybox", "sh", "-i", NULL };
    const char envp[] =
        "TERM=xterm-256color\0"
        "HOME=/root\0"
        "USER=root\0"
        "LOGNAME=root\0"
        "SHELL=/bin/sh\0"
        "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin\0"
        "\0";
    linux_start_session("/bin/busybox", argv, envp, ^(int retval, int pid, nsobj_t terminalObject) {
        startErr = retval;
        startPid = pid;
        if (terminalObject != NULL)
            terminal = (__bridge Terminal *) terminalObject;
        dispatch_semaphore_signal(startSemaphore);
    });

    long waitResult = dispatch_semaphore_wait(startSemaphore, dispatch_time(DISPATCH_TIME_NOW, (int64_t)(10 * NSEC_PER_SEC)));
    XCTAssertEqual(waitResult, 0L, @"linux_start_session did not complete");
    XCTAssertEqual(startErr, 0, @"linux_start_session returned %d", startErr);
    XCTAssertNotEqual(startPid, 0, @"session pid must be assigned");
    XCTAssertNotNil(terminal, @"terminal object must be returned");

    dispatch_sync(dispatch_get_main_queue(), ^{
        UIView *view = terminal.webView;
        view.frame = CGRectMake(0, 0, 320, 480);
        [view layoutIfNeeded];
        [(id)view focusForTesting];
        [(id)view insertText:@"ls -a /\n"];
    });

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
