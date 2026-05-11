#import <XCTest/XCTest.h>

#import <IXLandLinuxRuntime/kernel/task.h>
#import <IXLandLinuxRuntime/kernel/init.h>
#import <IXLandLinuxRuntime/kernel/calls.h>
#import <IXLandLinuxRuntime/kernel/fs.h>
#import <IXLandLinuxRuntime/fs/fd.h>
#import <IXLandLinuxRuntime/fs/tty.h>
#import <IXLandLinuxRuntime/emu/tlb.h>
#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>
#import <IXLandLinuxRuntime/emu/aarch64/memory.h>
#import <IXLandInstrumentationTracing/trace.h>
#import "../../Support/GuestExecutionTrace/GuestExecutionTraceSink.h"
#include <sys/stat.h>

extern bool exit_should_pthread_exit;

@interface GuestBusyboxRuntimeExecutionTests : XCTestCase
@end

@implementation GuestBusyboxRuntimeExecutionTests

- (void)configureFocusedTraceLevel
{
    setenv("IXLAND_TRACE_LEVEL", "debug", 1);
    trace_config_set_level_from_string("debug");
}

- (void)disableFocusedTraceLevel
{
    setenv("IXLAND_TRACE_LEVEL", "off", 1);
    trace_config_set_level_from_string("off");
}

- (NSString *)dataRootPath
{
    NSFileManager *fm = [NSFileManager defaultManager];
    NSURL *groupURL =
        [fm containerURLForSecurityApplicationGroupIdentifier:@"group.com.rudironsoni.emuLnx"];
    NSArray<NSString *> *groupPaths = groupURL != nil ? @[ groupURL.path ] : @[];

    for (NSString *groupPath in groupPaths) {
        NSString *rootPath = [groupPath stringByAppendingPathComponent:@"roots/default"];
        if ([fm fileExistsAtPath:rootPath]) {
            return rootPath;
        }
    }
    return nil;
}

- (BOOL)bootstrapMountedRootfsAtPath:(NSString *)rootPath
{
    NSLog(@"runtime-test bootstrap begin root=%@", rootPath);
    int mountErr = mount_root(&rootfs, rootPath.UTF8String);
    XCTAssertTrue(mountErr == 0 || mountErr == -16, @"mount_root returned %d", mountErr);
    if (!(mountErr == 0 || mountErr == -16))
        return NO;
    NSLog(@"runtime-test bootstrap mount_root=%d", mountErr);

    int initErr = become_first_process();
    XCTAssertTrue(initErr == 0 || initErr == -17, @"become_first_process returned %d", initErr);
    if (!(initErr == 0 || initErr == -17))
        return NO;
    NSLog(@"runtime-test bootstrap become_first_process=%d", initErr);

    int childErr = become_new_init_child();
    XCTAssertEqual(childErr, 0, @"become_new_init_child returned %d", childErr);
    if (childErr != 0)
        return NO;
    NSLog(@"runtime-test bootstrap become_new_init_child=%d", childErr);

    if (current != NULL && current->group != NULL && current->group->tty != NULL) {
        tty_set_winsize(current->group->tty,
                        (struct winsize_){ .row = 24, .col = 80, .xpixel = 0, .ypixel = 0 });
    }
    NSLog(@"runtime-test bootstrap ready current=%p mmu=%p", current, current ? current->cpu.mmu : NULL);
    return YES;
}

- (BOOL)execBusyboxWithArgc:(size_t)argc argv:(const char *)argv envp:(const char *)envp
{
    NSString *rootPath = [self dataRootPath];
    XCTAssertNotNil(rootPath, @"rootfs path must exist");
    if (rootPath == nil || ![self bootstrapMountedRootfsAtPath:rootPath])
        return NO;

    NSLog(@"runtime-test exec do_execve begin argc=%zu argv0=%s", argc, argv);
    int execErr = do_execve("bin/busybox", argc, argv, envp);
    XCTAssertEqual(execErr, 0, @"do_execve returned %d", execErr);
    NSLog(@"runtime-test exec do_execve end err=%d current=%p pid=%d mmu=%p", execErr, current,
          current ? current->pid : -1, current ? current->cpu.mmu : NULL);
    return execErr == 0;
}

- (BOOL)pumpGuestUntilTimeout:(NSTimeInterval)timeout predicate:(BOOL (^)(void))predicate
{
    XCTAssertNotEqual(current, NULL, @"current must exist before guest execution");
    if (current == NULL)
        return NO;

    struct cpu_state *cpu = &current->cpu;
    XCTAssertNotEqual(cpu->mmu, NULL, @"cpu mmu must exist before guest execution");
    if (cpu->mmu == NULL)
        return NO;

    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:timeout];
    BOOL loggedFirstTurn = NO;
    while ([deadline timeIntervalSinceNow] > 0) {
        struct tlb exec_tlb = {};
        tlb_refresh(&exec_tlb, cpu->mmu);
        exit_should_pthread_exit = false;
        if (!loggedFirstTurn) {
            NSLog(@"runtime-test pump first-turn pc=0x%llx pid=%d mmu=%p", cpu->pc,
                  current ? current->pid : -1, cpu->mmu);
            loggedFirstTurn = YES;
        }
        a64_cpu_run_limited(cpu, &exec_tlb, 5000);
        if (predicate())
            return YES;
    }
    return predicate();
}

- (BOOL)pumpGuestSingleStepTurns:(NSUInteger)maxSteps
                       predicate:(BOOL (^)(void))predicate
                    stepsTakenOut:(NSUInteger *)stepsTakenOut
                        lastPcOut:(uint64_t *)lastPcOut
{
    XCTAssertNotEqual(current, NULL, @"current must exist before guest execution");
    if (current == NULL)
        return NO;

    struct cpu_state *cpu = &current->cpu;
    XCTAssertNotEqual(cpu->mmu, NULL, @"cpu mmu must exist before guest execution");
    if (cpu->mmu == NULL)
        return NO;

    NSUInteger stepsTaken = 0;
    while (stepsTaken < maxSteps) {
        if (predicate()) {
            if (stepsTakenOut)
                *stepsTakenOut = stepsTaken;
            if (lastPcOut)
                *lastPcOut = cpu->pc;
            return YES;
        }

        struct tlb exec_tlb = {};
        tlb_refresh(&exec_tlb, cpu->mmu);
        exit_should_pthread_exit = false;
        a64_cpu_run_limited(cpu, &exec_tlb, 1);
        stepsTaken++;
    }

    if (stepsTakenOut)
        *stepsTakenOut = stepsTaken;
    if (lastPcOut)
        *lastPcOut = cpu->pc;
    return predicate();
}

- (void)testRootfsRootDirectoryCanBeOpenedAndReadDirectly
{
    [self configureFocusedTraceLevel];

    NSString *rootPath = [self dataRootPath];
    XCTAssertNotNil(rootPath, @"rootfs path must exist");
    if (rootPath == nil || ![self bootstrapMountedRootfsAtPath:rootPath])
        return;

    struct fd *fd = generic_open("/", O_RDONLY_ | O_DIRECTORY_, 0);
    XCTAssertFalse(IS_ERR(fd), @"generic_open(/) failed with %ld", (long)PTR_ERR(fd));
    if (IS_ERR(fd))
        return;

    struct statbuf stat = {};
    XCTAssertEqual(fd->mount->fs->fstat(fd, &stat), 0, @"fstat(/) failed");
    XCTAssertTrue(S_ISDIR(stat.mode), @"fstat(/) must report a directory, mode=0x%x", stat.mode);

    struct dir_entry entry = {};
    int readErr = fd->ops->readdir(fd, &entry);
    XCTAssertGreaterThan(readErr, 0, @"readdir(/) failed with %d", readErr);
    XCTAssertEqual(fd_close(fd), 0, @"fd_close(/) failed");
}

- (void)testRootfsRootDirectoryCanBeOpenedWithLibcStyleDirectoryFlags
{
    [self configureFocusedTraceLevel];

    NSString *rootPath = [self dataRootPath];
    XCTAssertNotNil(rootPath, @"rootfs path must exist");
    if (rootPath == nil || ![self bootstrapMountedRootfsAtPath:rootPath])
        return;

    int flags = O_RDONLY_ | O_NONBLOCK_ | O_DIRECTORY_ | O_CLOEXEC_;
    struct fd *fd = generic_open("/", flags, 0);
    XCTAssertFalse(IS_ERR(fd), @"generic_open(/, libc-style flags) failed with %ld",
                   (long)PTR_ERR(fd));
    if (IS_ERR(fd))
        return;

    XCTAssertEqual(fd_getflags(fd) & (O_NONBLOCK_ | O_CLOEXEC_), O_NONBLOCK_,
                   @"directory fd flags should preserve guest-visible nonblocking state");
    XCTAssertEqual(fd_close(fd), 0, @"fd_close(/) with libc-style flags failed");
}

- (void)testInteractiveBusyboxShellEmitsPromptAtGuestWriteBoundaryPromptly
{
    [self configureFocusedTraceLevel];
    guest_execution_trace_sink_init();
    guest_execution_trace_sink_reset();

    const char argv[] = "/bin/busybox\0sh\0-i\0\0";
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
    if (![self execBusyboxWithArgc:3 argv:argv envp:envp])
        return;

    BOOL sawPrompt = [self pumpGuestUntilTimeout:10.0
                                       predicate:^BOOL {
                                           return guest_execution_trace_sink_stdout_prompt_write_count() > 0 ||
                                                  guest_execution_trace_sink_pty_prompt_write_count() > 0;
                                       }];

    XCTAssertTrue(sawPrompt,
                  @"interactive busybox shell must emit a prompt through guest write boundaries");
}

- (void)testInteractiveBusyboxShellEarlySingleStepTurnsDoNotCrash
{
    [self configureFocusedTraceLevel];

    const char argv[] = "/bin/busybox\0sh\0-i\0\0";
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
    if (![self execBusyboxWithArgc:3 argv:argv envp:envp])
        return;

    XCTAssertNotEqual(current, NULL, @"current must exist before guest execution");
    if (current == NULL)
        return;

    struct cpu_state *cpu = &current->cpu;
    XCTAssertNotEqual(cpu->mmu, NULL, @"cpu mmu must exist before guest execution");
    if (cpu->mmu == NULL)
        return;

    for (NSUInteger step = 0; step < 64; step++) {
        struct tlb exec_tlb = {};
        tlb_refresh(&exec_tlb, cpu->mmu);
        NSLog(@"runtime-test single-step step=%lu pc=0x%llx pid=%d mmu=%p", (unsigned long)step,
              cpu->pc, current ? current->pid : -1, cpu->mmu);
        exit_should_pthread_exit = false;
        a64_cpu_run_limited(cpu, &exec_tlb, 1);
    }

    XCTAssertTrue(YES, @"busybox shell survived the first 64 single-step TCTI turns");
}

- (void)testInteractiveBusyboxShellSingleStepTurnsBalanceTraceIntervals
{
    [self configureFocusedTraceLevel];
    guest_execution_trace_sink_init();
    guest_execution_trace_sink_reset();

    const char argv[] = "/bin/busybox\0sh\0-i\0\0";
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
    if (![self execBusyboxWithArgc:3 argv:argv envp:envp])
        return;

    guest_execution_trace_sink_reset();

    XCTAssertNotEqual(current, NULL, @"current must exist before guest execution");
    if (current == NULL)
        return;

    struct cpu_state *cpu = &current->cpu;
    XCTAssertNotEqual(cpu->mmu, NULL, @"cpu mmu must exist before guest execution");
    if (cpu->mmu == NULL)
        return;

    for (NSUInteger step = 0; step < 8; step++) {
        struct tlb exec_tlb = {};
        tlb_refresh(&exec_tlb, cpu->mmu);
        exit_should_pthread_exit = false;
        a64_cpu_run_limited(cpu, &exec_tlb, 1);
    }

    uint64_t beginCount = guest_execution_trace_sink_begin_interval_calls_count();
    uint64_t endCount = guest_execution_trace_sink_end_interval_calls_count();

    XCTAssertGreaterThan(beginCount, 0ULL,
                         @"single-step runtime turns must emit trace proof events");
    XCTAssertEqual(beginCount, endCount,
                   @"single-step runtime turns must balance trace intervals; begin=%llu end=%llu",
                   (unsigned long long)beginCount, (unsigned long long)endCount);
}

- (void)testInteractiveBusyboxShellPromptAppearsWithinBoundedSingleStepTurns
{
    [self configureFocusedTraceLevel];
    guest_execution_trace_sink_init();
    guest_execution_trace_sink_reset();

    const char argv[] = "/bin/busybox\0sh\0-i\0\0";
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
    if (![self execBusyboxWithArgc:3 argv:argv envp:envp])
        return;

    NSUInteger stepsTaken = 0;
    uint64_t lastPc = 0;
    BOOL sawPrompt = [self pumpGuestSingleStepTurns:1024
                                          predicate:^BOOL {
                                              return guest_execution_trace_sink_stdout_prompt_write_count() > 0 ||
                                                     guest_execution_trace_sink_pty_prompt_write_count() > 0;
                                          }
                                       stepsTakenOut:&stepsTaken
                                           lastPcOut:&lastPc];

    uint64_t compileEntries = guest_execution_trace_sink_compile_entry_count();
    uint64_t maxCompileCount = guest_execution_trace_sink_compile_max_count_per_pc();
    uint64_t maxGenerationCount =
        guest_execution_trace_sink_compile_max_generation_count_per_pc();
    uint64_t execEntries = guest_execution_trace_sink_exec_entry_count();
    uint64_t hottestExecPc = 0;
    uint64_t hottestExecCount = 0;
    guest_execution_trace_sink_get_exec_hottest(&hottestExecPc, &hottestExecCount);

    XCTAssertTrue(sawPrompt,
                  @"interactive busybox shell prompt must appear within 1024 single-step TCTI "
                   @"turns; steps=%lu last_pc=0x%llx compile_entries=%llu "
                   @"max_compile_count=%llu max_generation_count=%llu exec_entries=%llu "
                   @"hottest_exec_pc=0x%llx hottest_exec_count=%llu",
                  (unsigned long)stepsTaken, (unsigned long long)lastPc,
                  (unsigned long long)compileEntries, (unsigned long long)maxCompileCount,
                  (unsigned long long)maxGenerationCount, (unsigned long long)execEntries,
                  (unsigned long long)hottestExecPc, (unsigned long long)hottestExecCount);
}

- (void)testInteractiveBusyboxShellLongTurnDoesNotCrashWithTraceOff
{
    [self disableFocusedTraceLevel];

    const char argv[] = "/bin/busybox\0sh\0-i\0\0";
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
    if (![self execBusyboxWithArgc:3 argv:argv envp:envp])
        return;

    XCTAssertNotEqual(current, NULL, @"current must exist before guest execution");
    if (current == NULL)
        return;

    struct cpu_state *cpu = &current->cpu;
    XCTAssertNotEqual(cpu->mmu, NULL, @"cpu mmu must exist before guest execution");
    if (cpu->mmu == NULL)
        return;

    struct tlb exec_tlb = {};
    tlb_refresh(&exec_tlb, cpu->mmu);
    exit_should_pthread_exit = false;
    a64_cpu_run_limited(cpu, &exec_tlb, 5000);

    XCTAssertTrue(YES, @"5000-block runtime turn completed with trace disabled");
}

- (void)testInteractiveBusyboxShellLongTurnDoesNotCrashWithTraceOnWithoutSink
{
    [self configureFocusedTraceLevel];

    const char argv[] = "/bin/busybox\0sh\0-i\0\0";
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
    if (![self execBusyboxWithArgc:3 argv:argv envp:envp])
        return;

    XCTAssertNotEqual(current, NULL, @"current must exist before guest execution");
    if (current == NULL)
        return;

    struct cpu_state *cpu = &current->cpu;
    XCTAssertNotEqual(cpu->mmu, NULL, @"cpu mmu must exist before guest execution");
    if (cpu->mmu == NULL)
        return;

    struct tlb exec_tlb = {};
    tlb_refresh(&exec_tlb, cpu->mmu);
    exit_should_pthread_exit = false;
    a64_cpu_run_limited(cpu, &exec_tlb, 5000);

    XCTAssertTrue(YES, @"5000-block runtime turn completed with trace enabled and no sink");
}

- (void)testInteractiveBusyboxShellLongTurnDoesNotCrashWithTraceOnWithSink
{
    [self configureFocusedTraceLevel];
    guest_execution_trace_sink_init();
    guest_execution_trace_sink_reset();

    const char argv[] = "/bin/busybox\0sh\0-i\0\0";
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
    if (![self execBusyboxWithArgc:3 argv:argv envp:envp])
        return;

    XCTAssertNotEqual(current, NULL, @"current must exist before guest execution");
    if (current == NULL)
        return;

    struct cpu_state *cpu = &current->cpu;
    XCTAssertNotEqual(cpu->mmu, NULL, @"cpu mmu must exist before guest execution");
    if (cpu->mmu == NULL)
        return;

    struct tlb exec_tlb = {};
    tlb_refresh(&exec_tlb, cpu->mmu);
    exit_should_pthread_exit = false;
    a64_cpu_run_limited(cpu, &exec_tlb, 5000);

    XCTAssertTrue(YES, @"5000-block runtime turn completed with trace enabled and sink registered");
}

- (void)testInteractiveBusyboxShellLongTurnDoesNotCrashWithTraceOnAndNoopActiveSink
{
    [self configureFocusedTraceLevel];
    guest_execution_trace_sink_init_noop_active();
    guest_execution_trace_sink_reset();

    const char argv[] = "/bin/busybox\0sh\0-i\0\0";
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
    if (![self execBusyboxWithArgc:3 argv:argv envp:envp])
        return;

    XCTAssertNotEqual(current, NULL, @"current must exist before guest execution");
    if (current == NULL)
        return;

    struct cpu_state *cpu = &current->cpu;
    XCTAssertNotEqual(cpu->mmu, NULL, @"cpu mmu must exist before guest execution");
    if (cpu->mmu == NULL)
        return;

    struct tlb exec_tlb = {};
    tlb_refresh(&exec_tlb, cpu->mmu);
    exit_should_pthread_exit = false;
    a64_cpu_run_limited(cpu, &exec_tlb, 5000);

    XCTAssertTrue(YES,
                  @"5000-block runtime turn completed with trace enabled and a no-op active sink");
}

- (void)testInteractiveBusyboxShellLongTurnAllowsPromptCounterReadback
{
    [self configureFocusedTraceLevel];
    guest_execution_trace_sink_init();
    guest_execution_trace_sink_reset();

    const char argv[] = "/bin/busybox\0sh\0-i\0\0";
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
    if (![self execBusyboxWithArgc:3 argv:argv envp:envp])
        return;

    XCTAssertNotEqual(current, NULL, @"current must exist before guest execution");
    if (current == NULL)
        return;

    struct cpu_state *cpu = &current->cpu;
    XCTAssertNotEqual(cpu->mmu, NULL, @"cpu mmu must exist before guest execution");
    if (cpu->mmu == NULL)
        return;

    struct tlb exec_tlb = {};
    tlb_refresh(&exec_tlb, cpu->mmu);
    exit_should_pthread_exit = false;
    a64_cpu_run_limited(cpu, &exec_tlb, 5000);

    uint64_t stdoutPromptCount = guest_execution_trace_sink_stdout_prompt_write_count();
    uint64_t ptyPromptCount = guest_execution_trace_sink_pty_prompt_write_count();

    XCTAssertTrue(stdoutPromptCount >= 0 && ptyPromptCount >= 0,
                  @"prompt counter readback must remain safe after a long debug-traced turn");
}

- (void)testInteractiveBusyboxShellRepeatedLongTurnsDoNotCrashWithTraceOnAndSink
{
    [self configureFocusedTraceLevel];
    guest_execution_trace_sink_init();
    guest_execution_trace_sink_reset();

    const char argv[] = "/bin/busybox\0sh\0-i\0\0";
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
    if (![self execBusyboxWithArgc:3 argv:argv envp:envp])
        return;

    XCTAssertNotEqual(current, NULL, @"current must exist before guest execution");
    if (current == NULL)
        return;

    struct cpu_state *cpu = &current->cpu;
    XCTAssertNotEqual(cpu->mmu, NULL, @"cpu mmu must exist before guest execution");
    if (cpu->mmu == NULL)
        return;

    for (NSUInteger turn = 0; turn < 16; turn++) {
        struct tlb exec_tlb = {};
        tlb_refresh(&exec_tlb, cpu->mmu);
        exit_should_pthread_exit = false;
        a64_cpu_run_limited(cpu, &exec_tlb, 5000);
    }

    XCTAssertTrue(YES, @"repeated 5000-block runtime turns completed with trace enabled and sink registered");
}

- (void)testInteractiveBusyboxShellRepeatedLongTurnsDoNotCrashWithTraceOnAndNoopActiveSink
{
    [self configureFocusedTraceLevel];
    guest_execution_trace_sink_init_noop_active();
    guest_execution_trace_sink_reset();

    const char argv[] = "/bin/busybox\0sh\0-i\0\0";
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
    if (![self execBusyboxWithArgc:3 argv:argv envp:envp])
        return;

    XCTAssertNotEqual(current, NULL, @"current must exist before guest execution");
    if (current == NULL)
        return;

    struct cpu_state *cpu = &current->cpu;
    XCTAssertNotEqual(cpu->mmu, NULL, @"cpu mmu must exist before guest execution");
    if (cpu->mmu == NULL)
        return;

    for (NSUInteger turn = 0; turn < 16; turn++) {
        struct tlb exec_tlb = {};
        tlb_refresh(&exec_tlb, cpu->mmu);
        exit_should_pthread_exit = false;
        a64_cpu_run_limited(cpu, &exec_tlb, 5000);
    }

    XCTAssertTrue(YES,
                  @"repeated 5000-block runtime turns completed with trace enabled and a no-op active sink");
}

- (void)testInteractiveBusyboxShellTwoLongTurnsDoNotCrashWithTraceOnAndNoopActiveSink
{
    [self configureFocusedTraceLevel];
    guest_execution_trace_sink_init_noop_active();
    guest_execution_trace_sink_reset();

    const char argv[] = "/bin/busybox\0sh\0-i\0\0";
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
    if (![self execBusyboxWithArgc:3 argv:argv envp:envp])
        return;

    XCTAssertNotEqual(current, NULL, @"current must exist before guest execution");
    if (current == NULL)
        return;

    struct cpu_state *cpu = &current->cpu;
    XCTAssertNotEqual(cpu->mmu, NULL, @"cpu mmu must exist before guest execution");
    if (cpu->mmu == NULL)
        return;

    for (NSUInteger turn = 0; turn < 2; turn++) {
        struct tlb exec_tlb = {};
        tlb_refresh(&exec_tlb, cpu->mmu);
        exit_should_pthread_exit = false;
        a64_cpu_run_limited(cpu, &exec_tlb, 5000);
    }

    XCTAssertTrue(YES,
                  @"two 5000-block runtime turns completed with trace enabled and a no-op active sink");
}

- (void)testInteractiveBusyboxShellEightLongTurnsDoNotCrashWithTraceOnAndNoopActiveSink
{
    [self configureFocusedTraceLevel];
    guest_execution_trace_sink_init_noop_active();
    guest_execution_trace_sink_reset();

    const char argv[] = "/bin/busybox\0sh\0-i\0\0";
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
    if (![self execBusyboxWithArgc:3 argv:argv envp:envp])
        return;

    XCTAssertNotEqual(current, NULL, @"current must exist before guest execution");
    if (current == NULL)
        return;

    struct cpu_state *cpu = &current->cpu;
    XCTAssertNotEqual(cpu->mmu, NULL, @"cpu mmu must exist before guest execution");
    if (cpu->mmu == NULL)
        return;

    for (NSUInteger turn = 0; turn < 8; turn++) {
        struct tlb exec_tlb = {};
        tlb_refresh(&exec_tlb, cpu->mmu);
        exit_should_pthread_exit = false;
        a64_cpu_run_limited(cpu, &exec_tlb, 5000);
    }

    XCTAssertTrue(YES,
                  @"eight 5000-block runtime turns completed with trace enabled and a no-op active sink");
}

- (void)testInteractiveBusyboxShellTwelveLongTurnsDoNotCrashWithTraceOnAndNoopActiveSink
{
    [self configureFocusedTraceLevel];
    guest_execution_trace_sink_init_noop_active();
    guest_execution_trace_sink_reset();

    const char argv[] = "/bin/busybox\0sh\0-i\0\0";
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
    if (![self execBusyboxWithArgc:3 argv:argv envp:envp])
        return;

    XCTAssertNotEqual(current, NULL, @"current must exist before guest execution");
    if (current == NULL)
        return;

    struct cpu_state *cpu = &current->cpu;
    XCTAssertNotEqual(cpu->mmu, NULL, @"cpu mmu must exist before guest execution");
    if (cpu->mmu == NULL)
        return;

    for (NSUInteger turn = 0; turn < 12; turn++) {
        NSLog(@"runtime-test traced-noop turn=%lu", (unsigned long)turn);
        struct tlb exec_tlb = {};
        tlb_refresh(&exec_tlb, cpu->mmu);
        exit_should_pthread_exit = false;
        a64_cpu_run_limited(cpu, &exec_tlb, 5000);
        NSLog(@"runtime-test traced-noop post-turn=%lu pc=0x%llx x8=0x%llx exit=%d",
              (unsigned long)turn, (unsigned long long)cpu->pc,
              (unsigned long long)cpu->x[8], cpu->tcti_exit_reason);
    }

    XCTAssertTrue(YES,
                  @"twelve 5000-block runtime turns completed with trace enabled and a no-op active sink");
}

- (void)testInteractiveBusyboxShellCountedPromptPollingDoesNotCrash
{
    [self configureFocusedTraceLevel];
    guest_execution_trace_sink_init();
    guest_execution_trace_sink_reset();

    const char argv[] = "/bin/busybox\0sh\0-i\0\0";
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
    if (![self execBusyboxWithArgc:3 argv:argv envp:envp])
        return;

    XCTAssertNotEqual(current, NULL, @"current must exist before guest execution");
    if (current == NULL)
        return;

    struct cpu_state *cpu = &current->cpu;
    XCTAssertNotEqual(cpu->mmu, NULL, @"cpu mmu must exist before guest execution");
    if (cpu->mmu == NULL)
        return;

    for (NSUInteger turn = 0; turn < 16; turn++) {
        struct tlb exec_tlb = {};
        tlb_refresh(&exec_tlb, cpu->mmu);
        exit_should_pthread_exit = false;
        a64_cpu_run_limited(cpu, &exec_tlb, 5000);

        uint64_t stdoutPromptCount = guest_execution_trace_sink_stdout_prompt_write_count();
        uint64_t ptyPromptCount = guest_execution_trace_sink_pty_prompt_write_count();
        if (stdoutPromptCount > 0 || ptyPromptCount > 0)
            break;
    }

    XCTAssertTrue(YES, @"counted prompt polling remained stable across repeated traced turns");
}

- (void)testInteractiveBusyboxShellStartupDoesNotFallIntoStackChkFailPath
{
    [self configureFocusedTraceLevel];
    guest_execution_trace_sink_init();
    guest_execution_trace_sink_reset();

    const char argv[] = "/bin/busybox\0sh\0-i\0\0";
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
    if (![self execBusyboxWithArgc:3 argv:argv envp:envp])
        return;

    NSUInteger stepsTaken = 0;
    uint64_t lastPc = 0;
    (void)[self pumpGuestSingleStepTurns:2048
                               predicate:^BOOL {
                                   return guest_execution_trace_sink_stdout_prompt_write_count() > 0 ||
                                          guest_execution_trace_sink_pty_prompt_write_count() > 0;
                               }
                            stepsTakenOut:&stepsTaken
                                lastPcOut:&lastPc];

    uint64_t stackChkBranchCount = 0;
    uint64_t stackChkFailCount = 0;
    uint64_t stackChkTakenTargetCount = 0;
    (void)guest_execution_trace_sink_get_exec_count_for_pc(0x797a4ULL, &stackChkBranchCount);
    (void)guest_execution_trace_sink_get_exec_count_for_pc(0x797a8ULL, &stackChkFailCount);
    (void)guest_execution_trace_sink_get_exec_count_for_pc(0x79860ULL, &stackChkTakenTargetCount);

    XCTAssertEqual(stackChkFailCount, 0ULL,
                   @"interactive busybox shell startup must not fall into __stack_chk_fail while "
                    "warming the prompt path; steps=%lu last_pc=0x%llx branch_hits=%llu "
                    "taken_target_hits=%llu fail_hits=%llu",
                   (unsigned long)stepsTaken, (unsigned long long)lastPc,
                   (unsigned long long)stackChkBranchCount,
                   (unsigned long long)stackChkTakenTargetCount,
                   (unsigned long long)stackChkFailCount);
}

- (void)testInteractiveBusyboxShellCanaryStateMatchesAtCompareOutcomeBoundary
{
    [self configureFocusedTraceLevel];

    const char argv[] = "/bin/busybox\0sh\0-i\0\0";
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
    if (![self execBusyboxWithArgc:3 argv:argv envp:envp])
        return;

    XCTAssertNotEqual(current, NULL, @"current must exist before guest execution");
    if (current == NULL)
        return;

    struct cpu_state *cpu = &current->cpu;
    XCTAssertNotEqual(cpu->mmu, NULL, @"cpu mmu must exist before guest execution");
    if (cpu->mmu == NULL)
        return;

    BOOL reachedCompareBoundary = NO;
    NSUInteger stepsTaken = 0;
    while (stepsTaken < 2048) {
        if (cpu->pc == 0x797a8ULL || cpu->pc == 0x79860ULL) {
            reachedCompareBoundary = YES;
            break;
        }

        struct tlb exec_tlb = {};
        tlb_refresh(&exec_tlb, cpu->mmu);
        exit_should_pthread_exit = false;
        a64_cpu_run_limited(cpu, &exec_tlb, 1);
        stepsTaken++;
    }

    XCTAssertTrue(reachedCompareBoundary,
                  @"busybox shell startup must reach the stack-canary compare outcome boundary; "
                   @"steps=%lu pc=0x%llx",
                  (unsigned long)stepsTaken, (unsigned long long)cpu->pc);
    if (!reachedCompareBoundary)
        return;

    struct tlb probe_tlb = {};
    tlb_refresh(&probe_tlb, cpu->mmu);

    uint64_t savedCanary = 0;
    uint64_t canaryPointer = 0;
    uint64_t liveCanary = 0;
    int savedRet = a64_guest_read64(cpu, &probe_tlb, cpu->sp + 0x8, &savedCanary);
    canaryPointer = cpu->x[20];
    int liveRet = a64_guest_read64(cpu, &probe_tlb, canaryPointer, &liveCanary);

    XCTAssertEqual(savedRet, A64_MEM_OK, @"must read saved canary slot at [sp,#8]");
    XCTAssertEqual(liveRet, A64_MEM_OK, @"must read live canary value from the pointed slot");
    XCTAssertEqual(savedCanary, liveCanary,
                   @"saved stack canary must match live canary at the compare outcome boundary; "
                    "steps=%lu sp=0x%llx x20=0x%llx saved=0x%llx live=0x%llx ptr=0x%llx",
                   (unsigned long)stepsTaken, (unsigned long long)cpu->sp,
                   (unsigned long long)cpu->x[20], (unsigned long long)savedCanary,
                   (unsigned long long)liveCanary, (unsigned long long)canaryPointer);
}

- (void)testDirectBusyboxLsLongSingleEntryReachesRootStatAndMetadataBoundariesPromptly
{
    [self configureFocusedTraceLevel];
    guest_execution_trace_sink_init();
    guest_execution_trace_sink_reset();

    const char argv[] = "/bin/busybox\0ls\0-ld\0/\0\0";
    const char envp[] =
        "TERM=xterm-256color\0"
        "HOME=/root\0"
        "USER=root\0"
        "LOGNAME=root\0"
        "SHELL=/bin/sh\0"
        "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin\0"
        "\0";
    if (![self execBusyboxWithArgc:4 argv:argv envp:envp])
        return;

    BOOL reachedMetadata = [self pumpGuestUntilTimeout:10.0
                                             predicate:^BOOL {
                                                 return guest_execution_trace_sink_root_stat_attempt_count() > 0 &&
                                                        guest_execution_trace_sink_stdout_metadata_write_count() > 0 &&
                                                        guest_execution_trace_sink_pty_metadata_write_count() > 0;
                                             }];

    XCTAssertTrue(reachedMetadata,
                  @"direct busybox ls -ld / must reach root stat and metadata write boundaries");
}

- (void)testDirectBusyboxLsExitsPromptlyWithoutRecompilingAcrossCodeGenerations
{
    [self configureFocusedTraceLevel];
    guest_execution_trace_sink_init();
    guest_execution_trace_sink_reset();

    const char argv[] = "/bin/busybox\0ls\0-a\0/\0\0";
    const char envp[] =
        "TERM=xterm-256color\0"
        "HOME=/root\0"
        "USER=root\0"
        "LOGNAME=root\0"
        "SHELL=/bin/sh\0"
        "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin\0"
        "\0";
    if (![self execBusyboxWithArgc:4 argv:argv envp:envp])
        return;

    BOOL exited = [self pumpGuestUntilTimeout:10.0
                                     predicate:^BOOL {
                                         return guest_execution_trace_sink_exit_observed();
                                     }];

    uint64_t compileEntries = guest_execution_trace_sink_compile_entry_count();
    uint64_t maxCompileCount = guest_execution_trace_sink_compile_max_count_per_pc();
    uint64_t maxGenerationCount =
        guest_execution_trace_sink_compile_max_generation_count_per_pc();

    XCTAssertTrue(exited, @"direct busybox ls must exit promptly");
    XCTAssertGreaterThan(compileEntries, 0ULL, @"trace sink must observe TCTI compile activity");
    XCTAssertEqual(maxCompileCount, maxGenerationCount,
                   @"direct busybox ls must not recompile the same guest pc more than once per "
                    @"code generation");
}

- (void)testDirectBusyboxUnameMachineExitsPromptly
{
    [self configureFocusedTraceLevel];
    guest_execution_trace_sink_init();
    guest_execution_trace_sink_reset();

    const char argv[] = "/bin/busybox\0uname\0-m\0\0";
    const char envp[] =
        "TERM=xterm-256color\0"
        "HOME=/root\0"
        "USER=root\0"
        "LOGNAME=root\0"
        "SHELL=/bin/sh\0"
        "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin\0"
        "\0";
    if (![self execBusyboxWithArgc:3 argv:argv envp:envp])
        return;

    BOOL exited = [self pumpGuestUntilTimeout:10.0
                                     predicate:^BOOL {
                                         return guest_execution_trace_sink_exit_observed();
                                     }];

    XCTAssertTrue(exited, @"direct busybox uname -m must exit promptly");
}

@end
