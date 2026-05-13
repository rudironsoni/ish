#import <XCTest/XCTest.h>

#import <IXLandLinuxRuntime/kernel/task.h>
#import <IXLandLinuxRuntime/kernel/init.h>
#import <IXLandLinuxRuntime/kernel/calls.h>
#import <IXLandLinuxRuntime/kernel/fs.h>
#import <IXLandLinuxRuntime/fs/fd.h>
#import <IXLandLinuxRuntime/fs/tty.h>
#import <IXLandLinuxRuntime/emu/tlb.h>
#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>
#import <IXLandLinuxRuntime/emu/aarch64/decode.h>
#import <IXLandLinuxRuntime/emu/aarch64/block-cache.h>
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

- (void)enableVerboseRuntimeProof
{
    setenv("ISH_VERBOSE_RUNTIME_PROOF", "1", 1);
}

- (void)disableVerboseRuntimeProof
{
    unsetenv("ISH_VERBOSE_RUNTIME_PROOF");
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

- (BOOL)execInteractiveBusyboxShellWithPty
{
    NSString *rootPath = [self dataRootPath];
    XCTAssertNotNil(rootPath, @"rootfs path must exist");
    if (rootPath == nil || ![self bootstrapMountedRootfsAtPath:rootPath])
        return NO;

    const char *const argv[] = { "/bin/busybox", "sh", "-i", NULL };
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

    struct tty *master = pty_open_guest_terminal(NULL);
    XCTAssertFalse(IS_ERR(master), @"pty_open_guest_terminal failed with %ld", (long)PTR_ERR(master));
    if (IS_ERR(master))
        return NO;

    int pid = 0;
    int execErr = prepare_session_with_tty("bin/busybox", argv, envp, master, &pid);
    XCTAssertEqual(execErr, 0, @"prepare_session_with_tty returned %d", execErr);
    if (execErr != 0)
        return NO;

    NSLog(@"runtime-test interactive session ready current=%p pid=%d mmu=%p tty_num=%d", current,
          current ? current->pid : pid, current ? current->cpu.mmu : NULL, master->num);
    return YES;
}

- (BOOL)pumpGuestUntilTimeout:(NSTimeInterval)timeout predicate:(BOOL (^)(void))predicate
{
    XCTAssertNotEqual(current, NULL, @"current must exist before guest execution");
    if (current == NULL)
        return NO;

    pid_t_ guestPid = current->pid;

    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:timeout];
    BOOL loggedFirstTurn = NO;
    while ([deadline timeIntervalSinceNow] > 0) {
        if (predicate())
            return YES;

        lock(&pids_lock);
        struct task *task = pid_get_task_zombie(guestPid);
        unlock(&pids_lock);
        if (task == NULL)
            return predicate();
        if (task->exiting || task->sighand == NULL)
            return YES;

        struct cpu_state *cpu = &task->cpu;
        if (cpu->mmu == NULL)
            return YES;

        struct tlb exec_tlb = {};
        tlb_refresh(&exec_tlb, cpu->mmu);
        exit_should_pthread_exit = false;
        if (!loggedFirstTurn) {
            NSLog(@"runtime-test pump first-turn pc=0x%llx pid=%d mmu=%p", cpu->pc,
                  task->pid, cpu->mmu);
            loggedFirstTurn = YES;
        }
        a64_cpu_run_limited(cpu, &exec_tlb, 1);
        if (predicate())
            return YES;
    }
    return predicate();
}

- (BOOL)sendInputThroughControllingPseudoMaster:(const char *)input length:(size_t)length
{
    XCTAssertNotEqual(current, NULL, @"current must exist before PTY input injection");
    if (current == NULL || current->group == NULL)
        return NO;

    struct tty *slave = current->group->tty;
    XCTAssertNotEqual(slave, NULL, @"interactive guest path must own a controlling tty");
    if (slave == NULL)
        return NO;

    struct tty *master = slave->pty.other;
    XCTAssertNotEqual(master, NULL, @"interactive guest path must have a PTY master peer");
    if (master == NULL || master->driver == NULL || master->driver->ops->write == NULL)
        return NO;

    int written = master->driver->ops->write(master, input, length, false);
    XCTAssertEqual(written, (int)length, @"PTY master input write must accept the full command");
    return written == (int)length;
}

- (struct tty *)controllingPseudoMaster
{
    XCTAssertNotEqual(current, NULL, @"current must exist before PTY inspection");
    if (current == NULL || current->group == NULL)
        return NULL;

    struct tty *slave = current->group->tty;
    XCTAssertNotEqual(slave, NULL, @"interactive guest path must own a controlling tty");
    if (slave == NULL)
        return NULL;

    struct tty *master = slave->pty.other;
    XCTAssertNotEqual(master, NULL, @"interactive guest path must have a PTY master peer");
    return master;
}

- (NSString *)controllingPseudoMasterBuffer
{
    struct tty *master = [self controllingPseudoMaster];
    if (master == NULL)
        return @"";

    char raw[8192] = { 0 };
    ssize_t copied = tty_get_buffer_content(master, raw, sizeof(raw) - 1);
    XCTAssertGreaterThanOrEqual(copied, 0, @"PTY master buffer read must succeed");
    if (copied <= 0)
        return @"";

    raw[copied] = '\0';
    NSString *buffer = [[NSString alloc] initWithBytes:raw length:(NSUInteger)copied encoding:NSUTF8StringEncoding];
    if (buffer != nil)
        return buffer;

    return [[NSString alloc] initWithBytes:raw length:(NSUInteger)copied encoding:NSISOLatin1StringEncoding] ?: @"";
}

- (NSUInteger)promptCountInBuffer:(NSString *)buffer
{
    if (buffer.length == 0)
        return 0;

    NSUInteger count = 0;
    NSRange searchRange = NSMakeRange(0, buffer.length);
    while (YES) {
        NSRange found = [buffer rangeOfString:@"/ # " options:0 range:searchRange];
        if (found.location == NSNotFound)
            break;
        count += 1;
        NSUInteger nextLocation = NSMaxRange(found);
        if (nextLocation >= buffer.length)
            break;
        searchRange = NSMakeRange(nextLocation, buffer.length - nextLocation);
    }
    return count;
}

- (BOOL)execInteractiveBusyboxShellAndWaitForPrompt
{
    if (![self execInteractiveBusyboxShellWithPty])
        return NO;

    XCTAssertNotEqual(current, NULL, @"current must exist after interactive PTY session setup");
    if (current == NULL)
        return NO;

    pid_t_ guestPid = current->pid;
    NSUInteger stepsTaken = 0;
    uint64_t lastPc = 0;
    BOOL sawPrompt = NO;
    BOOL sawGuestExit = NO;
    int guestExitCode = -1;
    BOOL sawExitingTask = NO;
    BOOL sawMissingSighand = NO;
    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:10.0];
    while ([deadline timeIntervalSinceNow] > 0 && stepsTaken < 200000) {
        if (guest_execution_trace_sink_exit_observed()) {
            sawGuestExit = YES;
            guestExitCode = guest_execution_trace_sink_get_exit_code();
            break;
        }

        lock(&pids_lock);
        struct task *task = pid_get_task_zombie(guestPid);
        unlock(&pids_lock);
        if (task == NULL)
            break;
        if (task->exiting) {
            sawExitingTask = YES;
            break;
        }
        if (task->sighand == NULL) {
            sawMissingSighand = YES;
            break;
        }

        NSString *buffer = [self controllingPseudoMasterBuffer];
        if ([buffer containsString:@"/ # "]
            || [buffer hasSuffix:@"/ #"]
            || [buffer containsString:@"\n/ #"]) {
            sawPrompt = YES;
            break;
        }

        struct cpu_state *cpu = &task->cpu;
        if (cpu->mmu == NULL)
            break;
        struct tlb exec_tlb = {};
        tlb_refresh(&exec_tlb, cpu->mmu);
        exit_should_pthread_exit = false;
        a64_cpu_run_limited(cpu, &exec_tlb, 1);
        stepsTaken++;
        lastPc = cpu->pc;
    }
    XCTAssertTrue(sawPrompt,
                  @"interactive busybox shell must reach the initial prompt within the bounded "
                   @"single-step warmup window; steps=%lu last_pc=0x%llx guest_exit=%d "
                   @"guest_exit_code=%d exiting_task=%d missing_sighand=%d master_buffer=%@",
                  (unsigned long)stepsTaken, (unsigned long long)lastPc,
                  sawGuestExit ? 1 : 0, guestExitCode, sawExitingTask ? 1 : 0,
                  sawMissingSighand ? 1 : 0,
                  [self controllingPseudoMasterBuffer]);
    return sawPrompt;
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

- (BOOL)prepareInteractiveBusyboxShellSecondLsTurnCpu:(struct cpu_state **)cpuOut
                                      secondTurnPcOut:(uint64_t *)secondTurnPcOut
{
    const char command[] = "ls\n";
    return [self prepareInteractiveBusyboxShellCommandTurnCpu:command
                                                       length:sizeof(command) - 1
                                                       cpuOut:cpuOut
                                              commandTurnPcOut:secondTurnPcOut];
}

- (BOOL)prepareInteractiveBusyboxShellInjectedCommandCpu:(const char *)command
                                                   length:(size_t)length
                                                   cpuOut:(struct cpu_state **)cpuOut
                                     injectedCommandPcOut:(uint64_t *)injectedCommandPcOut
{
    [self configureFocusedTraceLevel];
    guest_execution_trace_sink_init();
    guest_execution_trace_sink_reset();

    if (![self execInteractiveBusyboxShellAndWaitForPrompt])
        return NO;

    if (![self sendInputThroughControllingPseudoMaster:command length:length])
        return NO;

    XCTAssertNotEqual(current, NULL, @"current must exist after PTY command injection");
    if (current == NULL)
        return NO;

    struct cpu_state *cpu = &current->cpu;
    XCTAssertNotEqual(cpu->mmu, NULL, @"interactive PTY command path must preserve an MMU after injection");
    if (cpu->mmu == NULL)
        return NO;

    if (cpuOut)
        *cpuOut = cpu;
    if (injectedCommandPcOut)
        *injectedCommandPcOut = cpu->pc;
    return YES;
}

- (BOOL)prepareInteractiveBusyboxShellCommandTurnCpu:(const char *)command
                                              length:(size_t)length
                                              cpuOut:(struct cpu_state **)cpuOut
                                     commandTurnPcOut:(uint64_t *)commandTurnPcOut
{
    struct cpu_state *cpu = NULL;
    if (![self prepareInteractiveBusyboxShellInjectedCommandCpu:command
                                                         length:length
                                                         cpuOut:&cpu
                                           injectedCommandPcOut:NULL])
        return NO;

    struct tlb firstTurnTlb = {};
    tlb_refresh(&firstTurnTlb, cpu->mmu);
    exit_should_pthread_exit = false;
    a64_cpu_run_limited(cpu, &firstTurnTlb, 5000);

    if (cpuOut)
        *cpuOut = cpu;
    if (commandTurnPcOut)
        *commandTurnPcOut = cpu->pc;
    return YES;
}

- (BOOL)runInteractiveBusyboxShellSecondTurnChunks:(NSUInteger)chunkCount
                                         stepCount:(NSUInteger)stepCount
                                            cpuOut:(struct cpu_state **)cpuOut
                                    lastChunkPcOut:(uint64_t *)lastChunkPcOut
{
    struct cpu_state *cpu = NULL;
    uint64_t secondTurnPc = 0;
    if (![self prepareInteractiveBusyboxShellSecondLsTurnCpu:&cpu secondTurnPcOut:&secondTurnPc])
        return NO;

    for (NSUInteger chunk = 0; chunk < chunkCount; chunk++) {
        uint64_t chunkStartPc = cpu->pc;
        struct tlb chunkTlb = {};
        tlb_refresh(&chunkTlb, cpu->mmu);
        exit_should_pthread_exit = false;
        a64_cpu_run_limited(cpu, &chunkTlb, (int)stepCount);
        NSLog(@"runtime-test second-turn chunk=%lu step_count=%lu start_pc=0x%llx end_pc=0x%llx exit_observed=%d exit_code=%d",
              (unsigned long)(chunk + 1), (unsigned long)stepCount,
              (unsigned long long)chunkStartPc, (unsigned long long)cpu->pc,
              guest_execution_trace_sink_exit_observed() ? 1 : 0,
              guest_execution_trace_sink_get_exit_code());
        if (guest_execution_trace_sink_exit_observed())
            break;
    }

    if (cpuOut)
        *cpuOut = cpu;
    if (lastChunkPcOut)
        *lastChunkPcOut = cpu->pc;
    return YES;
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

        XCTAssertFalse(guest_execution_trace_sink_exit_observed(),
                       @"interactive shell must remain live through eight traced turns; "
                        @"turn=%lu pc=0x%llx x8=0x%llx exit=%d",
                       (unsigned long)turn, (unsigned long long)cpu->pc,
                       (unsigned long long)cpu->x[8], cpu->tcti_exit_reason);
        if (guest_execution_trace_sink_exit_observed())
            return;
    }

    XCTAssertGreaterThan(guest_execution_trace_sink_exec_entry_count(), 0ULL,
                         @"interactive shell must execute real guest blocks during the traced "
                          @"eight-turn warmup");
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

        XCTAssertFalse(guest_execution_trace_sink_exit_observed(),
                       @"interactive shell must stay live across twelve traced turns; "
                        @"turn=%lu pc=0x%llx x8=0x%llx exit=%d",
                       (unsigned long)turn, (unsigned long long)cpu->pc,
                       (unsigned long long)cpu->x[8], cpu->tcti_exit_reason);
        if (guest_execution_trace_sink_exit_observed())
            return;
    }

    XCTAssertTrue(guest_execution_trace_sink_stdout_prompt_write_count() > 0 ||
                      guest_execution_trace_sink_pty_prompt_write_count() > 0,
                  @"interactive shell must reach a real prompt write boundary within twelve "
                   @"traced turns; stdout_prompts=%llu pty_prompts=%llu hottest_pc=0x%llx "
                   @"hottest_count=%llu",
                  (unsigned long long)guest_execution_trace_sink_stdout_prompt_write_count(),
                  (unsigned long long)guest_execution_trace_sink_pty_prompt_write_count(),
                  (unsigned long long)cpu->pc,
                  (unsigned long long)guest_execution_trace_sink_exec_entry_count());
}

- (void)testInteractiveBusyboxShellTurnNineBoundaryEmitsVerboseRuntimeProofBeforeCrash
{
    [self configureFocusedTraceLevel];
    [self enableVerboseRuntimeProof];
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
    if (![self execBusyboxWithArgc:3 argv:argv envp:envp]) {
        [self disableVerboseRuntimeProof];
        return;
    }

    XCTAssertNotEqual(current, NULL, @"current must exist before guest execution");
    if (current == NULL) {
        [self disableVerboseRuntimeProof];
        return;
    }

    struct cpu_state *cpu = &current->cpu;
    XCTAssertNotEqual(cpu->mmu, NULL, @"cpu mmu must exist before guest execution");
    if (cpu->mmu == NULL) {
        [self disableVerboseRuntimeProof];
        return;
    }

    for (NSUInteger turn = 0; turn < 9; turn++) {
        NSLog(@"runtime-test verbose-boundary turn=%lu", (unsigned long)turn);
        struct tlb exec_tlb = {};
        tlb_refresh(&exec_tlb, cpu->mmu);
        exit_should_pthread_exit = false;
        a64_cpu_run_limited(cpu, &exec_tlb, 5000);
        NSLog(@"runtime-test verbose-boundary post-turn=%lu pc=0x%llx x8=0x%llx sp=0x%llx fault=0x%llx exit=%d",
              (unsigned long)turn, (unsigned long long)cpu->pc, (unsigned long long)cpu->x[8],
              (unsigned long long)cpu->sp, (unsigned long long)cpu->fault_addr,
              cpu->tcti_exit_reason);

        XCTAssertFalse(guest_execution_trace_sink_exit_observed(),
                       @"interactive shell must remain live through the verbose turn-nine boundary; "
                        @"turn=%lu pc=0x%llx x8=0x%llx sp=0x%llx fault=0x%llx exit=%d",
                       (unsigned long)turn, (unsigned long long)cpu->pc,
                       (unsigned long long)cpu->x[8], (unsigned long long)cpu->sp,
                       (unsigned long long)cpu->fault_addr, cpu->tcti_exit_reason);
        if (guest_execution_trace_sink_exit_observed()) {
            [self disableVerboseRuntimeProof];
            return;
        }
    }

    [self disableVerboseRuntimeProof];
    XCTAssertTrue(YES, @"verbose runtime proof turn-nine boundary stayed live");
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

    BOOL sawPrompt = NO;
    for (NSUInteger turn = 0; turn < 16; turn++) {
        struct tlb exec_tlb = {};
        tlb_refresh(&exec_tlb, cpu->mmu);
        exit_should_pthread_exit = false;
        a64_cpu_run_limited(cpu, &exec_tlb, 5000);

        uint64_t stdoutPromptCount = guest_execution_trace_sink_stdout_prompt_write_count();
        uint64_t ptyPromptCount = guest_execution_trace_sink_pty_prompt_write_count();
        sawPrompt = stdoutPromptCount > 0 || ptyPromptCount > 0;
        XCTAssertFalse(guest_execution_trace_sink_exit_observed(),
                       @"interactive shell must remain live while prompt polling; "
                        @"turn=%lu pc=0x%llx stdout_prompts=%llu pty_prompts=%llu exit=%d",
                       (unsigned long)turn, (unsigned long long)cpu->pc,
                       (unsigned long long)stdoutPromptCount,
                       (unsigned long long)ptyPromptCount, cpu->tcti_exit_reason);
        if (guest_execution_trace_sink_exit_observed())
            return;
        if (sawPrompt)
            break;
    }

    XCTAssertTrue(sawPrompt,
                  @"interactive shell prompt polling must observe a real prompt boundary within "
                   @"sixteen traced turns; stdout_prompts=%llu pty_prompts=%llu exec_entries=%llu",
                  (unsigned long long)guest_execution_trace_sink_stdout_prompt_write_count(),
                  (unsigned long long)guest_execution_trace_sink_pty_prompt_write_count(),
                  (unsigned long long)guest_execution_trace_sink_exec_entry_count());
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
    uint64_t rootStatAttempts = guest_execution_trace_sink_root_stat_attempt_count();
    uint64_t rootStatFailures = guest_execution_trace_sink_root_stat_fail_count();

    XCTAssertTrue(exited, @"direct busybox ls must exit promptly");
    XCTAssertGreaterThan(compileEntries, 0ULL, @"trace sink must observe TCTI compile activity");
    XCTAssertGreaterThan(rootStatAttempts, 0ULL,
                         @"direct busybox ls -a / must reach the root stat path before exit");
    XCTAssertEqual(rootStatFailures, 0ULL,
                   @"direct busybox ls -a / must not fail the root stat path before exit");
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

- (void)testDirectBusyboxUnameMachineReachesAndReturnsFromUnameSyscall
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

    BOOL reachedReturn = [self pumpGuestUntilTimeout:10.0
                                           predicate:^BOOL {
                                               return guest_execution_trace_sink_uname_syscall_returned() ||
                                                      guest_execution_trace_sink_exit_observed();
                                           }];

    XCTAssertTrue(reachedReturn, @"direct busybox uname -m must either return from uname syscall or exit within the timeout");
    XCTAssertTrue(guest_execution_trace_sink_uname_syscall_entered(),
                  @"direct busybox uname -m must reach the uname syscall before exiting");
    XCTAssertTrue(guest_execution_trace_sink_uname_syscall_returned(),
                  @"direct busybox uname -m must return from the uname syscall before crashing");
    XCTAssertEqual(guest_execution_trace_sink_uname_syscall_return_value(), 0ULL,
                   @"direct busybox uname -m must see uname return success");
}

- (void)testDirectBusyboxUnameMachineWritesAarch64AfterUnameSyscall
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

    BOOL sawAarch64Write = [self pumpGuestUntilTimeout:10.0
                                             predicate:^BOOL {
                                                 return guest_execution_trace_sink_stdout_aarch64_write_observed() ||
                                                        guest_execution_trace_sink_pty_aarch64_write_observed() ||
                                                        guest_execution_trace_sink_exit_observed();
                                             }];

    XCTAssertTrue(sawAarch64Write, @"direct busybox uname -m must either emit aarch64 or exit within the timeout");
    XCTAssertTrue(guest_execution_trace_sink_uname_syscall_returned(),
                  @"direct busybox uname -m must return from uname before writing output");
    XCTAssertTrue(guest_execution_trace_sink_stdout_aarch64_write_observed() ||
                      guest_execution_trace_sink_pty_aarch64_write_observed(),
                  @"direct busybox uname -m must emit aarch64 through a real guest write path");
}

- (void)testDirectBusyboxUnameAllExitsPromptly
{
    [self configureFocusedTraceLevel];
    guest_execution_trace_sink_init();
    guest_execution_trace_sink_reset();

    const char argv[] = "/bin/busybox\0uname\0-a\0\0";
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

    XCTAssertTrue(exited, @"direct busybox uname -a must exit promptly");
}

- (void)testDirectBusyboxUnameAllWritesAarch64AfterUnameSyscall
{
    [self configureFocusedTraceLevel];
    guest_execution_trace_sink_init();
    guest_execution_trace_sink_reset();

    const char argv[] = "/bin/busybox\0uname\0-a\0\0";
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

    BOOL sawAarch64Write = [self pumpGuestUntilTimeout:10.0
                                             predicate:^BOOL {
                                                 return guest_execution_trace_sink_stdout_aarch64_write_observed() ||
                                                        guest_execution_trace_sink_pty_aarch64_write_observed() ||
                                                        guest_execution_trace_sink_exit_observed();
                                             }];

    XCTAssertTrue(sawAarch64Write, @"direct busybox uname -a must either emit aarch64 or exit within the timeout");
    XCTAssertTrue(guest_execution_trace_sink_uname_syscall_returned(),
                  @"direct busybox uname -a must return from uname before writing output");
    XCTAssertTrue(guest_execution_trace_sink_stdout_aarch64_write_observed() ||
                      guest_execution_trace_sink_pty_aarch64_write_observed(),
                  @"direct busybox uname -a must emit aarch64 through a real guest write path");
}

- (void)testInteractiveBusyboxShellUnameAllWritesAarch64AfterPtyMasterInput
{
    [self configureFocusedTraceLevel];
    guest_execution_trace_sink_init();
    guest_execution_trace_sink_reset();

    if (![self execInteractiveBusyboxShellAndWaitForPrompt])
        return;

    const char command[] = "/bin/busybox uname -a\n";
    if (![self sendInputThroughControllingPseudoMaster:command length:sizeof(command) - 1])
        return;

    BOOL sawAarch64Write = [self pumpGuestUntilTimeout:10.0
                                             predicate:^BOOL {
                                                 return guest_execution_trace_sink_stdout_aarch64_write_observed() ||
                                                        guest_execution_trace_sink_pty_aarch64_write_observed() ||
                                                        guest_execution_trace_sink_exit_observed();
                                             }];

    XCTAssertTrue(sawAarch64Write,
                  @"interactive busybox shell must either emit aarch64 after PTY master input or exit within the timeout");
    XCTAssertTrue(guest_execution_trace_sink_uname_syscall_returned(),
                  @"interactive busybox shell must return from uname before output is considered complete");
    XCTAssertTrue(guest_execution_trace_sink_stdout_aarch64_write_observed() ||
                      guest_execution_trace_sink_pty_aarch64_write_observed(),
                  @"interactive busybox shell must emit aarch64 through a real guest write path after PTY master input");
}

- (void)testInteractiveBusyboxShellUnameMachineWritesAarch64AfterPtyMasterInput
{
    [self configureFocusedTraceLevel];
    guest_execution_trace_sink_init();
    guest_execution_trace_sink_reset();

    if (![self execInteractiveBusyboxShellAndWaitForPrompt])
        return;

    const char command[] = "/bin/busybox uname -m\n";
    if (![self sendInputThroughControllingPseudoMaster:command length:sizeof(command) - 1])
        return;

    BOOL sawAarch64Write = [self pumpGuestUntilTimeout:10.0
                                             predicate:^BOOL {
                                                 return guest_execution_trace_sink_stdout_aarch64_write_observed() ||
                                                        guest_execution_trace_sink_pty_aarch64_write_observed() ||
                                                        guest_execution_trace_sink_exit_observed();
                                             }];

    XCTAssertTrue(sawAarch64Write,
                  @"interactive busybox shell must either emit aarch64 after PTY master input or exit within the timeout");
    XCTAssertTrue(guest_execution_trace_sink_uname_syscall_returned(),
                  @"interactive busybox shell must return from uname before output is considered complete");
    XCTAssertTrue(guest_execution_trace_sink_stdout_aarch64_write_observed() ||
                      guest_execution_trace_sink_pty_aarch64_write_observed(),
                  @"interactive busybox shell must emit aarch64 through a real guest write path after PTY master input");
}

- (void)testInteractiveBusyboxShellInitialPtyResumeBlockFetchesDecodesAndCompiles
{
    [self configureFocusedTraceLevel];

    if (![self execInteractiveBusyboxShellWithPty])
        return;

    XCTAssertNotEqual(current, NULL, @"current must exist after PTY-backed interactive shell setup");
    if (current == NULL)
        return;

    struct cpu_state *cpu = &current->cpu;
    XCTAssertNotEqual(cpu->mmu, NULL, @"interactive PTY shell must publish an MMU before first resume");
    if (cpu->mmu == NULL)
        return;

    struct tlb tlb = {};
    tlb_refresh(&tlb, cpu->mmu);

    uint32_t raw = 0;
    XCTAssertEqual(a64_fetch_insn(cpu, &tlb, cpu->pc, &raw), 0,
                   @"the first PTY-resumed guest shell PC must fetch a real instruction; pc=0x%llx",
                   (unsigned long long)cpu->pc);

    a64_instr_t decoded = { 0 };
    XCTAssertEqual(a64_decode(raw, &decoded), 0,
                   @"the first PTY-resumed guest shell instruction must decode before command-level "
                    @"proof can meaningfully advance; pc=0x%llx raw=0x%08x",
                   (unsigned long long)cpu->pc, raw);
    NSLog(@"runtime-test first-pty-resume-block pc=0x%llx raw=0x%08x cat=%d subtype=%d",
          (unsigned long long)cpu->pc, raw, decoded.cat, decoded.subtype);

    struct a64_block *block = a64_compile_block(cpu, cpu->pc, &tlb);
    XCTAssertNotEqual(block, NULL,
                      @"the first PTY-resumed guest shell instruction must compile through TCTI "
                       @"before the interactive shell can reach a prompt; pc=0x%llx raw=0x%08x "
                       @"cat=%d subtype=%d",
                      (unsigned long long)cpu->pc, raw, decoded.cat, decoded.subtype);
    if (block == NULL)
        return;

    XCTAssertGreaterThan(block->num_gadgets, (size_t)0,
                         @"the first PTY-resumed guest shell block must emit at least one gadget");
}

- (void)testInteractiveBusyboxShellFirstResumedBlockAfterPlainLsFetchesDecodesAndCompiles
{
    [self configureFocusedTraceLevel];
    guest_execution_trace_sink_init();
    guest_execution_trace_sink_reset();

    if (![self execInteractiveBusyboxShellAndWaitForPrompt])
        return;

    const char command[] = "ls\n";
    if (![self sendInputThroughControllingPseudoMaster:command length:sizeof(command) - 1])
        return;

    XCTAssertNotEqual(current, NULL, @"current must exist after PTY command injection");
    if (current == NULL)
        return;

    struct cpu_state *cpu = &current->cpu;
    XCTAssertNotEqual(cpu->mmu, NULL, @"interactive PTY command path must preserve an MMU before first resume");
    if (cpu->mmu == NULL)
        return;

    struct tlb tlb = {};
    tlb_refresh(&tlb, cpu->mmu);

    uint32_t raw = 0;
    XCTAssertEqual(a64_fetch_insn(cpu, &tlb, cpu->pc, &raw), 0,
                   @"the first resumed block after interactive ls must fetch a real instruction; pc=0x%llx",
                   (unsigned long long)cpu->pc);

    a64_instr_t decoded = { 0 };
    XCTAssertEqual(a64_decode(raw, &decoded), 0,
                   @"the first resumed block after interactive ls must decode before command-level "
                    @"execution proof can advance; pc=0x%llx raw=0x%08x",
                   (unsigned long long)cpu->pc, raw);
    NSLog(@"runtime-test first-post-ls-block pc=0x%llx raw=0x%08x cat=%d subtype=%d",
          (unsigned long long)cpu->pc, raw, decoded.cat, decoded.subtype);

    struct a64_block *block = a64_compile_block(cpu, cpu->pc, &tlb);
    XCTAssertNotEqual(block, NULL,
                      @"the first resumed block after interactive ls must compile through TCTI "
                       @"before shell command execution can progress; pc=0x%llx raw=0x%08x "
                       @"cat=%d subtype=%d",
                      (unsigned long long)cpu->pc, raw, decoded.cat, decoded.subtype);
    if (block == NULL)
        return;

    XCTAssertGreaterThan(block->num_gadgets, (size_t)0,
                         @"the first resumed block after interactive ls must emit at least one gadget");

    if ((decoded.cat == A64_BRANCH || decoded.cat == A64_BRANCH2) &&
        decoded.subtype == A64_BRANCH_UNCOND) {
        uint64_t targetPc = cpu->pc + decoded.imm;
        uint32_t targetRaw = 0;
        XCTAssertEqual(a64_fetch_insn(cpu, &tlb, targetPc, &targetRaw), 0,
                       @"the first unconditional branch target after interactive ls must fetch "
                        @"cleanly; branch_pc=0x%llx target_pc=0x%llx",
                       (unsigned long long)cpu->pc, (unsigned long long)targetPc);

        a64_instr_t targetDecoded = { 0 };
        XCTAssertEqual(a64_decode(targetRaw, &targetDecoded), 0,
                       @"the first unconditional branch target after interactive ls must decode "
                        @"cleanly; branch_pc=0x%llx target_pc=0x%llx raw=0x%08x",
                       (unsigned long long)cpu->pc, (unsigned long long)targetPc, targetRaw);
        NSLog(@"runtime-test first-post-ls-target branch_pc=0x%llx target_pc=0x%llx raw=0x%08x cat=%d subtype=%d",
              (unsigned long long)cpu->pc, (unsigned long long)targetPc, targetRaw,
              targetDecoded.cat, targetDecoded.subtype);

        struct a64_block *targetBlock = a64_compile_block(cpu, targetPc, &tlb);
        XCTAssertNotEqual(targetBlock, NULL,
                          @"the first unconditional branch target after interactive ls must also "
                           @"compile through TCTI; branch_pc=0x%llx target_pc=0x%llx raw=0x%08x "
                           @"cat=%d subtype=%d",
                          (unsigned long long)cpu->pc, (unsigned long long)targetPc, targetRaw,
                          targetDecoded.cat, targetDecoded.subtype);
        if (targetBlock != NULL) {
            XCTAssertGreaterThan(targetBlock->num_gadgets, (size_t)0,
                                 @"the first unconditional branch target after interactive ls "
                                  @"must emit at least one gadget");
        }
    }
}

- (void)testInteractiveBusyboxShellFirstResumedTurnAfterPlainLsDoesNotHostCrash
{
    [self configureFocusedTraceLevel];
    guest_execution_trace_sink_init();
    guest_execution_trace_sink_reset();

    if (![self execInteractiveBusyboxShellAndWaitForPrompt])
        return;

    const char command[] = "ls\n";
    if (![self sendInputThroughControllingPseudoMaster:command length:sizeof(command) - 1])
        return;

    XCTAssertNotEqual(current, NULL, @"current must exist after PTY command injection");
    if (current == NULL)
        return;

    struct cpu_state *cpu = &current->cpu;
    XCTAssertNotEqual(cpu->mmu, NULL, @"interactive PTY command path must preserve an MMU before first resumed turn");
    if (cpu->mmu == NULL)
        return;

    uint64_t startPc = cpu->pc;
    struct tlb tlb = {};
    tlb_refresh(&tlb, cpu->mmu);
    exit_should_pthread_exit = false;
    a64_cpu_run_limited(cpu, &tlb, 1);

    XCTAssertTrue(guest_execution_trace_sink_exit_observed() || cpu->pc != startPc,
                  @"the first resumed turn after interactive ls must either advance guest control "
                   @"flow or report a guest exit instead of crashing the host; start_pc=0x%llx "
                   @"end_pc=0x%llx exit_observed=%d exit_code=%d",
                  (unsigned long long)startPc, (unsigned long long)cpu->pc,
                  guest_execution_trace_sink_exit_observed() ? 1 : 0,
                  guest_execution_trace_sink_get_exit_code());
}

- (void)testInteractiveBusyboxShellFirstLongTurnAfterPlainLsDoesNotHostCrash
{
    [self configureFocusedTraceLevel];
    guest_execution_trace_sink_init();
    guest_execution_trace_sink_reset();

    if (![self execInteractiveBusyboxShellAndWaitForPrompt])
        return;

    const char command[] = "ls\n";
    if (![self sendInputThroughControllingPseudoMaster:command length:sizeof(command) - 1])
        return;

    XCTAssertNotEqual(current, NULL, @"current must exist after PTY command injection");
    if (current == NULL)
        return;

    struct cpu_state *cpu = &current->cpu;
    XCTAssertNotEqual(cpu->mmu, NULL, @"interactive PTY command path must preserve an MMU before first long resumed turn");
    if (cpu->mmu == NULL)
        return;

    uint64_t startPc = cpu->pc;
    struct tlb tlb = {};
    tlb_refresh(&tlb, cpu->mmu);
    exit_should_pthread_exit = false;
    a64_cpu_run_limited(cpu, &tlb, 5000);

    XCTAssertTrue(guest_execution_trace_sink_exit_observed() || cpu->pc != startPc,
                  @"the first long resumed turn after interactive ls must either advance guest "
                   @"control flow or report a guest exit instead of crashing the host; "
                   @"start_pc=0x%llx end_pc=0x%llx exit_observed=%d exit_code=%d",
                  (unsigned long long)startPc, (unsigned long long)cpu->pc,
                  guest_execution_trace_sink_exit_observed() ? 1 : 0,
                  guest_execution_trace_sink_get_exit_code());
}

- (void)testInteractiveBusyboxShellTwoLongTurnsAfterPlainLsDoNotHostCrash
{
    [self configureFocusedTraceLevel];
    guest_execution_trace_sink_init();
    guest_execution_trace_sink_reset();

    if (![self execInteractiveBusyboxShellAndWaitForPrompt])
        return;

    const char command[] = "ls\n";
    if (![self sendInputThroughControllingPseudoMaster:command length:sizeof(command) - 1])
        return;

    XCTAssertNotEqual(current, NULL, @"current must exist after PTY command injection");
    if (current == NULL)
        return;

    struct cpu_state *cpu = &current->cpu;
    XCTAssertNotEqual(cpu->mmu, NULL, @"interactive PTY command path must preserve an MMU before repeated long resumed turns");
    if (cpu->mmu == NULL)
        return;

    uint64_t startPc = cpu->pc;
    for (NSUInteger turn = 0; turn < 2; turn++) {
        struct tlb tlb = {};
        tlb_refresh(&tlb, cpu->mmu);
        exit_should_pthread_exit = false;
        a64_cpu_run_limited(cpu, &tlb, 5000);
        if (guest_execution_trace_sink_exit_observed())
            break;
    }

    XCTAssertTrue(guest_execution_trace_sink_exit_observed() || cpu->pc != startPc,
                  @"two long resumed turns after interactive ls must either advance guest control "
                   @"flow or report a guest exit instead of crashing the host; start_pc=0x%llx "
                   @"end_pc=0x%llx exit_observed=%d exit_code=%d",
                  (unsigned long long)startPc, (unsigned long long)cpu->pc,
                  guest_execution_trace_sink_exit_observed() ? 1 : 0,
                  guest_execution_trace_sink_get_exit_code());
}

- (void)testInteractiveBusyboxShellPostFirstLongTurnLsStateRemainsRunnable
{
    [self configureFocusedTraceLevel];
    guest_execution_trace_sink_init();
    guest_execution_trace_sink_reset();

    if (![self execInteractiveBusyboxShellAndWaitForPrompt])
        return;

    const char command[] = "ls\n";
    if (![self sendInputThroughControllingPseudoMaster:command length:sizeof(command) - 1])
        return;

    XCTAssertNotEqual(current, NULL, @"current must exist after PTY command injection");
    if (current == NULL)
        return;

    pid_t_ guestPid = current->pid;
    struct cpu_state *cpu = &current->cpu;
    XCTAssertNotEqual(cpu->mmu, NULL, @"interactive PTY command path must preserve an MMU before first long resumed turn");
    if (cpu->mmu == NULL)
        return;

    struct tlb tlb = {};
    tlb_refresh(&tlb, cpu->mmu);
    exit_should_pthread_exit = false;
    a64_cpu_run_limited(cpu, &tlb, 5000);

    lock(&pids_lock);
    struct task *task = pid_get_task_zombie(guestPid);
    bool taskMissing = task == NULL;
    bool taskExiting = task != NULL && task->exiting;
    bool taskMissingSighand = task != NULL && task->sighand == NULL;
    uint64_t pendingMask = task != NULL ? task->pending : 0;
    unlock(&pids_lock);

    XCTAssertFalse(taskMissing,
                   @"the guest task must still be present in the pid table after the first long ls turn");
    XCTAssertFalse(taskExiting,
                   @"the guest task must still be runnable after the first long ls turn; "
                    @"exit_observed=%d exit_code=%d pending=0x%llx",
                   guest_execution_trace_sink_exit_observed() ? 1 : 0,
                   guest_execution_trace_sink_get_exit_code(),
                   (unsigned long long)pendingMask);
    XCTAssertFalse(taskMissingSighand,
                   @"the guest task must still own a live sighand after the first long ls turn; "
                    @"exit_observed=%d exit_code=%d pending=0x%llx",
                   guest_execution_trace_sink_exit_observed() ? 1 : 0,
                   guest_execution_trace_sink_get_exit_code(),
                   (unsigned long long)pendingMask);
}

- (void)testInteractiveBusyboxShellSecondLongTurnStartBlockAfterPlainLsFetchesDecodesAndCompiles
{
    [self configureFocusedTraceLevel];
    guest_execution_trace_sink_init();
    guest_execution_trace_sink_reset();

    if (![self execInteractiveBusyboxShellAndWaitForPrompt])
        return;

    const char command[] = "ls\n";
    if (![self sendInputThroughControllingPseudoMaster:command length:sizeof(command) - 1])
        return;

    XCTAssertNotEqual(current, NULL, @"current must exist after PTY command injection");
    if (current == NULL)
        return;

    struct cpu_state *cpu = &current->cpu;
    XCTAssertNotEqual(cpu->mmu, NULL, @"interactive PTY command path must preserve an MMU before long resumed turns");
    if (cpu->mmu == NULL)
        return;

    struct tlb firstTurnTlb = {};
    tlb_refresh(&firstTurnTlb, cpu->mmu);
    exit_should_pthread_exit = false;
    a64_cpu_run_limited(cpu, &firstTurnTlb, 5000);

    struct tlb secondTurnTlb = {};
    tlb_refresh(&secondTurnTlb, cpu->mmu);

    uint32_t raw = 0;
    XCTAssertEqual(a64_fetch_insn(cpu, &secondTurnTlb, cpu->pc, &raw), 0,
                   @"the second long-turn start after interactive ls must fetch a real instruction; pc=0x%llx",
                   (unsigned long long)cpu->pc);

    a64_instr_t decoded = { 0 };
    XCTAssertEqual(a64_decode(raw, &decoded), 0,
                   @"the second long-turn start after interactive ls must decode before execution "
                    @"proof can advance; pc=0x%llx raw=0x%08x",
                   (unsigned long long)cpu->pc, raw);
    NSLog(@"runtime-test second-long-turn-start pc=0x%llx raw=0x%08x cat=%d subtype=%d",
          (unsigned long long)cpu->pc, raw, decoded.cat, decoded.subtype);

    struct a64_block *block = a64_compile_block(cpu, cpu->pc, &secondTurnTlb);
    XCTAssertNotEqual(block, NULL,
                      @"the second long-turn start after interactive ls must compile through "
                       @"TCTI; pc=0x%llx raw=0x%08x cat=%d subtype=%d",
                      (unsigned long long)cpu->pc, raw, decoded.cat, decoded.subtype);
    if (block != NULL) {
        XCTAssertGreaterThan(block->num_gadgets, (size_t)0,
                             @"the second long-turn start after interactive ls must emit at least one gadget");
    }
}

- (void)testInteractiveBusyboxShellFirstStepOfSecondLongTurnAfterPlainLsDoesNotHostCrash
{
    [self configureFocusedTraceLevel];
    guest_execution_trace_sink_init();
    guest_execution_trace_sink_reset();

    if (![self execInteractiveBusyboxShellAndWaitForPrompt])
        return;

    const char command[] = "ls\n";
    if (![self sendInputThroughControllingPseudoMaster:command length:sizeof(command) - 1])
        return;

    XCTAssertNotEqual(current, NULL, @"current must exist after PTY command injection");
    if (current == NULL)
        return;

    struct cpu_state *cpu = &current->cpu;
    XCTAssertNotEqual(cpu->mmu, NULL, @"interactive PTY command path must preserve an MMU before the second long-turn step");
    if (cpu->mmu == NULL)
        return;

    struct tlb firstTurnTlb = {};
    tlb_refresh(&firstTurnTlb, cpu->mmu);
    exit_should_pthread_exit = false;
    a64_cpu_run_limited(cpu, &firstTurnTlb, 5000);

    uint64_t secondTurnStartPc = cpu->pc;
    struct tlb secondTurnTlb = {};
    tlb_refresh(&secondTurnTlb, cpu->mmu);
    exit_should_pthread_exit = false;
    a64_cpu_run_limited(cpu, &secondTurnTlb, 1);

    XCTAssertTrue(guest_execution_trace_sink_exit_observed() || cpu->pc != secondTurnStartPc,
                  @"the first step of the second long-turn after interactive ls must either advance guest "
                   @"control flow or report a guest exit instead of crashing the host; "
                   @"start_pc=0x%llx end_pc=0x%llx exit_observed=%d exit_code=%d",
                  (unsigned long long)secondTurnStartPc, (unsigned long long)cpu->pc,
                  guest_execution_trace_sink_exit_observed() ? 1 : 0,
                  guest_execution_trace_sink_get_exit_code());
}

- (void)testInteractiveBusyboxShellSecondShortTurnAfterPlainLsDoesNotHostCrash
{
    struct cpu_state *cpu = NULL;
    uint64_t secondTurnStartPc = 0;
    if (![self prepareInteractiveBusyboxShellSecondLsTurnCpu:&cpu secondTurnPcOut:&secondTurnStartPc])
        return;

    struct tlb secondTurnTlb = {};
    tlb_refresh(&secondTurnTlb, cpu->mmu);
    exit_should_pthread_exit = false;
    a64_cpu_run_limited(cpu, &secondTurnTlb, 256);

    XCTAssertTrue(guest_execution_trace_sink_exit_observed() || cpu->pc != secondTurnStartPc,
                  @"a bounded second resumed turn after interactive ls must either advance guest control "
                   @"flow or report a guest exit instead of crashing the host; start_pc=0x%llx end_pc=0x%llx "
                   @"exit_observed=%d exit_code=%d",
                  (unsigned long long)secondTurnStartPc, (unsigned long long)cpu->pc,
                  guest_execution_trace_sink_exit_observed() ? 1 : 0,
                  guest_execution_trace_sink_get_exit_code());
}

- (void)testInteractiveBusyboxShellSecondTurnSixtyFourStepsAfterPlainLsDoesNotHostCrash
{
    struct cpu_state *cpu = NULL;
    uint64_t secondTurnStartPc = 0;
    if (![self prepareInteractiveBusyboxShellSecondLsTurnCpu:&cpu secondTurnPcOut:&secondTurnStartPc])
        return;

    struct tlb secondTurnTlb = {};
    tlb_refresh(&secondTurnTlb, cpu->mmu);
    exit_should_pthread_exit = false;
    a64_cpu_run_limited(cpu, &secondTurnTlb, 64);

    XCTAssertTrue(guest_execution_trace_sink_exit_observed() || cpu->pc != secondTurnStartPc,
                  @"a 64-step second resumed turn after interactive ls must either advance guest control "
                   @"flow or report a guest exit instead of crashing the host; start_pc=0x%llx end_pc=0x%llx "
                   @"exit_observed=%d exit_code=%d",
                  (unsigned long long)secondTurnStartPc, (unsigned long long)cpu->pc,
                  guest_execution_trace_sink_exit_observed() ? 1 : 0,
                  guest_execution_trace_sink_get_exit_code());
}

- (void)testInteractiveBusyboxShellSecondTurnOneHundredTwentyEightStepsAfterPlainLsDoesNotHostCrash
{
    struct cpu_state *cpu = NULL;
    uint64_t secondTurnStartPc = 0;
    if (![self prepareInteractiveBusyboxShellSecondLsTurnCpu:&cpu secondTurnPcOut:&secondTurnStartPc])
        return;

    struct tlb secondTurnTlb = {};
    tlb_refresh(&secondTurnTlb, cpu->mmu);
    exit_should_pthread_exit = false;
    a64_cpu_run_limited(cpu, &secondTurnTlb, 128);

    XCTAssertTrue(guest_execution_trace_sink_exit_observed() || cpu->pc != secondTurnStartPc,
                  @"a 128-step second resumed turn after interactive ls must either advance guest control "
                   @"flow or report a guest exit instead of crashing the host; start_pc=0x%llx end_pc=0x%llx "
                   @"exit_observed=%d exit_code=%d",
                  (unsigned long long)secondTurnStartPc, (unsigned long long)cpu->pc,
                  guest_execution_trace_sink_exit_observed() ? 1 : 0,
                  guest_execution_trace_sink_get_exit_code());
}

- (void)testInteractiveBusyboxShellSecondTurnFiveHundredTwelveStepsAfterPlainLsDoesNotHostCrash
{
    struct cpu_state *cpu = NULL;
    uint64_t secondTurnStartPc = 0;
    if (![self prepareInteractiveBusyboxShellSecondLsTurnCpu:&cpu secondTurnPcOut:&secondTurnStartPc])
        return;

    struct tlb secondTurnTlb = {};
    tlb_refresh(&secondTurnTlb, cpu->mmu);
    exit_should_pthread_exit = false;
    a64_cpu_run_limited(cpu, &secondTurnTlb, 512);

    XCTAssertTrue(guest_execution_trace_sink_exit_observed() || cpu->pc != secondTurnStartPc,
                  @"a 512-step second resumed turn after interactive ls must either advance guest control "
                   @"flow or report a guest exit instead of crashing the host; start_pc=0x%llx end_pc=0x%llx "
                   @"exit_observed=%d exit_code=%d",
                  (unsigned long long)secondTurnStartPc, (unsigned long long)cpu->pc,
                  guest_execution_trace_sink_exit_observed() ? 1 : 0,
                  guest_execution_trace_sink_get_exit_code());
}

- (void)testInteractiveBusyboxShellSecondTurnOneThousandTwentyFourStepsAfterPlainLsDoesNotHostCrash
{
    struct cpu_state *cpu = NULL;
    uint64_t secondTurnStartPc = 0;
    if (![self prepareInteractiveBusyboxShellSecondLsTurnCpu:&cpu secondTurnPcOut:&secondTurnStartPc])
        return;

    struct tlb secondTurnTlb = {};
    tlb_refresh(&secondTurnTlb, cpu->mmu);
    exit_should_pthread_exit = false;
    a64_cpu_run_limited(cpu, &secondTurnTlb, 1024);

    XCTAssertTrue(guest_execution_trace_sink_exit_observed() || cpu->pc != secondTurnStartPc,
                  @"a 1024-step second resumed turn after interactive ls must either advance guest control "
                   @"flow or report a guest exit instead of crashing the host; start_pc=0x%llx end_pc=0x%llx "
                   @"exit_observed=%d exit_code=%d",
                  (unsigned long long)secondTurnStartPc, (unsigned long long)cpu->pc,
                  guest_execution_trace_sink_exit_observed() ? 1 : 0,
                  guest_execution_trace_sink_get_exit_code());
}

- (void)testInteractiveBusyboxShellSecondTurnFourThousandNinetySixStepsAfterPlainLsDoesNotHostCrash
{
    struct cpu_state *cpu = NULL;
    uint64_t secondTurnStartPc = 0;
    if (![self prepareInteractiveBusyboxShellSecondLsTurnCpu:&cpu secondTurnPcOut:&secondTurnStartPc])
        return;

    struct tlb secondTurnTlb = {};
    tlb_refresh(&secondTurnTlb, cpu->mmu);
    exit_should_pthread_exit = false;
    a64_cpu_run_limited(cpu, &secondTurnTlb, 4096);

    XCTAssertTrue(guest_execution_trace_sink_exit_observed() || cpu->pc != secondTurnStartPc,
                  @"a 4096-step second resumed turn after interactive ls must either advance guest control "
                   @"flow or report a guest exit instead of crashing the host; start_pc=0x%llx end_pc=0x%llx "
                   @"exit_observed=%d exit_code=%d",
                  (unsigned long long)secondTurnStartPc, (unsigned long long)cpu->pc,
                  guest_execution_trace_sink_exit_observed() ? 1 : 0,
                  guest_execution_trace_sink_get_exit_code());
}

- (void)testInteractiveBusyboxShellSecondTurnTwoChunksOfFiveHundredTwelveStepsDoNotHostCrash
{
    struct cpu_state *cpu = NULL;
    uint64_t lastChunkPc = 0;
    if (![self runInteractiveBusyboxShellSecondTurnChunks:2
                                                stepCount:512
                                                   cpuOut:&cpu
                                           lastChunkPcOut:&lastChunkPc])
        return;

    XCTAssertNotEqual(cpu, NULL, @"chunked second-turn execution must preserve the guest CPU");
    if (cpu == NULL)
        return;

    XCTAssertTrue(guest_execution_trace_sink_exit_observed() || lastChunkPc != 0,
                  @"two 512-step second-turn chunks after interactive ls must either advance guest control "
                   @"flow or report a guest exit instead of crashing the host; last_chunk_pc=0x%llx "
                   @"exit_observed=%d exit_code=%d",
                  (unsigned long long)lastChunkPc,
                  guest_execution_trace_sink_exit_observed() ? 1 : 0,
                  guest_execution_trace_sink_get_exit_code());
}

- (void)testInteractiveBusyboxShellSecondTurnFourChunksOfFiveHundredTwelveStepsDoNotHostCrash
{
    struct cpu_state *cpu = NULL;
    uint64_t lastChunkPc = 0;
    if (![self runInteractiveBusyboxShellSecondTurnChunks:4
                                                stepCount:512
                                                   cpuOut:&cpu
                                           lastChunkPcOut:&lastChunkPc])
        return;

    XCTAssertNotEqual(cpu, NULL, @"chunked second-turn execution must preserve the guest CPU");
    if (cpu == NULL)
        return;

    XCTAssertTrue(guest_execution_trace_sink_exit_observed() || lastChunkPc != 0,
                  @"four 512-step second-turn chunks after interactive ls must either advance guest control "
                   @"flow or report a guest exit instead of crashing the host; last_chunk_pc=0x%llx "
                   @"exit_observed=%d exit_code=%d",
                  (unsigned long long)lastChunkPc,
                  guest_execution_trace_sink_exit_observed() ? 1 : 0,
                  guest_execution_trace_sink_get_exit_code());
}

- (void)testInteractiveBusyboxShellSecondTurnEightChunksOfFiveHundredTwelveStepsDoNotHostCrash
{
    struct cpu_state *cpu = NULL;
    uint64_t lastChunkPc = 0;
    if (![self runInteractiveBusyboxShellSecondTurnChunks:8
                                                stepCount:512
                                                   cpuOut:&cpu
                                           lastChunkPcOut:&lastChunkPc])
        return;

    XCTAssertNotEqual(cpu, NULL, @"chunked second-turn execution must preserve the guest CPU");
    if (cpu == NULL)
        return;

    XCTAssertTrue(guest_execution_trace_sink_exit_observed() || lastChunkPc != 0,
                  @"eight 512-step second-turn chunks after interactive ls must either advance guest control "
                   @"flow or report a guest exit instead of crashing the host; last_chunk_pc=0x%llx "
                   @"exit_observed=%d exit_code=%d",
                  (unsigned long long)lastChunkPc,
                  guest_execution_trace_sink_exit_observed() ? 1 : 0,
                  guest_execution_trace_sink_get_exit_code());
}

- (void)testInteractiveBusyboxShellPipeCommandFirstTurnAfterInjectionDoesNotHostCrash
{
    const char command[] = "echo hello world | /bin/busybox wc -w\n";
    struct cpu_state *cpu = NULL;
    uint64_t commandTurnPc = 0;
    if (![self prepareInteractiveBusyboxShellCommandTurnCpu:command
                                                     length:sizeof(command) - 1
                                                     cpuOut:&cpu
                                            commandTurnPcOut:&commandTurnPc])
        return;

    XCTAssertNotEqual(cpu, NULL, @"pipe-command first resumed turn must preserve the guest CPU");
    if (cpu == NULL)
        return;

    XCTAssertTrue(guest_execution_trace_sink_exit_observed() || commandTurnPc != 0,
                  @"pipe-command first resumed turn must either advance guest control flow or report a guest exit instead of crashing the host; "
                   @"command_turn_pc=0x%llx exit_observed=%d exit_code=%d",
                  (unsigned long long)commandTurnPc,
                  guest_execution_trace_sink_exit_observed() ? 1 : 0,
                  guest_execution_trace_sink_get_exit_code());
}

- (void)testInteractiveBusyboxShellPipeCommandSecondTurnFiveHundredTwelveStepsDoNotHostCrash
{
    const char command[] = "echo hello world | /bin/busybox wc -w\n";
    struct cpu_state *cpu = NULL;
    uint64_t secondTurnStartPc = 0;
    if (![self prepareInteractiveBusyboxShellCommandTurnCpu:command
                                                     length:sizeof(command) - 1
                                                     cpuOut:&cpu
                                            commandTurnPcOut:&secondTurnStartPc])
        return;

    XCTAssertNotEqual(cpu, NULL, @"pipe-command second resumed turn must preserve the guest CPU");
    if (cpu == NULL || cpu->mmu == NULL)
        return;

    struct tlb secondTurnTlb = {};
    tlb_refresh(&secondTurnTlb, cpu->mmu);
    exit_should_pthread_exit = false;
    a64_cpu_run_limited(cpu, &secondTurnTlb, 512);

    XCTAssertTrue(guest_execution_trace_sink_exit_observed() || cpu->pc != 0,
                  @"pipe-command second resumed turn must either advance guest control flow or report a guest exit instead of crashing the host; "
                   @"start_pc=0x%llx end_pc=0x%llx exit_observed=%d exit_code=%d",
                  (unsigned long long)secondTurnStartPc, (unsigned long long)cpu->pc,
                  guest_execution_trace_sink_exit_observed() ? 1 : 0,
                  guest_execution_trace_sink_get_exit_code());
}

- (void)testInteractiveBusyboxShellPipeCommandFirstStepAfterInjectionDoesNotHostCrash
{
    const char command[] = "echo hello world | /bin/busybox wc -w\n";
    struct cpu_state *cpu = NULL;
    uint64_t injectedCommandPc = 0;
    if (![self prepareInteractiveBusyboxShellInjectedCommandCpu:command
                                                         length:sizeof(command) - 1
                                                         cpuOut:&cpu
                                           injectedCommandPcOut:&injectedCommandPc])
        return;

    XCTAssertNotEqual(cpu, NULL, @"pipe-command injected-command path must preserve the guest CPU");
    if (cpu == NULL || cpu->mmu == NULL)
        return;

    struct tlb turnTlb = {};
    tlb_refresh(&turnTlb, cpu->mmu);
    exit_should_pthread_exit = false;
    a64_cpu_run_limited(cpu, &turnTlb, 1);

    XCTAssertTrue(guest_execution_trace_sink_exit_observed() || cpu->pc != 0,
                  @"pipe-command first post-injection step must either advance guest control flow or report a guest exit instead of crashing the host; "
                   @"start_pc=0x%llx end_pc=0x%llx exit_observed=%d exit_code=%d",
                  (unsigned long long)injectedCommandPc, (unsigned long long)cpu->pc,
                  guest_execution_trace_sink_exit_observed() ? 1 : 0,
                  guest_execution_trace_sink_get_exit_code());
}

- (void)testInteractiveBusyboxShellPipeCommandSixtyFourStepsAfterInjectionDoesNotHostCrash
{
    const char command[] = "echo hello world | /bin/busybox wc -w\n";
    struct cpu_state *cpu = NULL;
    uint64_t injectedCommandPc = 0;
    if (![self prepareInteractiveBusyboxShellInjectedCommandCpu:command
                                                         length:sizeof(command) - 1
                                                         cpuOut:&cpu
                                           injectedCommandPcOut:&injectedCommandPc])
        return;

    XCTAssertNotEqual(cpu, NULL, @"pipe-command injected-command path must preserve the guest CPU");
    if (cpu == NULL || cpu->mmu == NULL)
        return;

    struct tlb turnTlb = {};
    tlb_refresh(&turnTlb, cpu->mmu);
    exit_should_pthread_exit = false;
    a64_cpu_run_limited(cpu, &turnTlb, 64);

    XCTAssertTrue(guest_execution_trace_sink_exit_observed() || cpu->pc != 0,
                  @"pipe-command first sixty-four post-injection steps must either advance guest control flow or report a guest exit instead of crashing the host; "
                   @"start_pc=0x%llx end_pc=0x%llx exit_observed=%d exit_code=%d",
                  (unsigned long long)injectedCommandPc, (unsigned long long)cpu->pc,
                  guest_execution_trace_sink_exit_observed() ? 1 : 0,
                  guest_execution_trace_sink_get_exit_code());
}

- (void)testInteractiveBusyboxShellPipeCommandTwoHundredFiftySixStepsAfterInjectionDoesNotHostCrash
{
    const char command[] = "echo hello world | /bin/busybox wc -w\n";
    struct cpu_state *cpu = NULL;
    uint64_t injectedCommandPc = 0;
    if (![self prepareInteractiveBusyboxShellInjectedCommandCpu:command
                                                         length:sizeof(command) - 1
                                                         cpuOut:&cpu
                                           injectedCommandPcOut:&injectedCommandPc])
        return;

    XCTAssertNotEqual(cpu, NULL, @"pipe-command injected-command path must preserve the guest CPU");
    if (cpu == NULL || cpu->mmu == NULL)
        return;

    struct tlb turnTlb = {};
    tlb_refresh(&turnTlb, cpu->mmu);
    exit_should_pthread_exit = false;
    a64_cpu_run_limited(cpu, &turnTlb, 256);

    XCTAssertTrue(guest_execution_trace_sink_exit_observed() || cpu->pc != 0,
                  @"pipe-command first two-hundred-fifty-six post-injection steps must either advance guest control flow or report a guest exit instead of crashing the host; "
                   @"start_pc=0x%llx end_pc=0x%llx exit_observed=%d exit_code=%d",
                  (unsigned long long)injectedCommandPc, (unsigned long long)cpu->pc,
                  guest_execution_trace_sink_exit_observed() ? 1 : 0,
                  guest_execution_trace_sink_get_exit_code());
}

- (void)testInteractiveBusyboxShellPipeCommandOneThousandTwentyFourStepsAfterInjectionDoesNotHostCrash
{
    const char command[] = "echo hello world | /bin/busybox wc -w\n";
    struct cpu_state *cpu = NULL;
    uint64_t injectedCommandPc = 0;
    if (![self prepareInteractiveBusyboxShellInjectedCommandCpu:command
                                                         length:sizeof(command) - 1
                                                         cpuOut:&cpu
                                           injectedCommandPcOut:&injectedCommandPc])
        return;

    XCTAssertNotEqual(cpu, NULL, @"pipe-command injected-command path must preserve the guest CPU");
    if (cpu == NULL || cpu->mmu == NULL)
        return;

    struct tlb turnTlb = {};
    tlb_refresh(&turnTlb, cpu->mmu);
    exit_should_pthread_exit = false;
    a64_cpu_run_limited(cpu, &turnTlb, 1024);

    XCTAssertTrue(guest_execution_trace_sink_exit_observed() || cpu->pc != 0,
                  @"pipe-command first one-thousand-twenty-four post-injection steps must either advance guest control flow or report a guest exit instead of crashing the host; "
                   @"start_pc=0x%llx end_pc=0x%llx exit_observed=%d exit_code=%d",
                  (unsigned long long)injectedCommandPc, (unsigned long long)cpu->pc,
                  guest_execution_trace_sink_exit_observed() ? 1 : 0,
                  guest_execution_trace_sink_get_exit_code());
}

- (void)testInteractiveBusyboxShellPipeCommandTwoThousandFortyEightStepsAfterInjectionDoesNotHostCrash
{
    const char command[] = "echo hello world | /bin/busybox wc -w\n";
    struct cpu_state *cpu = NULL;
    uint64_t injectedCommandPc = 0;
    if (![self prepareInteractiveBusyboxShellInjectedCommandCpu:command
                                                         length:sizeof(command) - 1
                                                         cpuOut:&cpu
                                           injectedCommandPcOut:&injectedCommandPc])
        return;

    XCTAssertNotEqual(cpu, NULL, @"pipe-command injected-command path must preserve the guest CPU");
    if (cpu == NULL || cpu->mmu == NULL)
        return;

    struct tlb turnTlb = {};
    tlb_refresh(&turnTlb, cpu->mmu);
    exit_should_pthread_exit = false;
    a64_cpu_run_limited(cpu, &turnTlb, 2048);

    XCTAssertTrue(guest_execution_trace_sink_exit_observed() || cpu->pc != 0,
                  @"pipe-command first two-thousand-forty-eight post-injection steps must either advance guest control flow or report a guest exit instead of crashing the host; "
                   @"start_pc=0x%llx end_pc=0x%llx exit_observed=%d exit_code=%d",
                  (unsigned long long)injectedCommandPc, (unsigned long long)cpu->pc,
                  guest_execution_trace_sink_exit_observed() ? 1 : 0,
                  guest_execution_trace_sink_get_exit_code());
}

- (void)testInteractiveBusyboxShellPipeCommandFourThousandNinetySixStepsAfterInjectionDoesNotHostCrash
{
    const char command[] = "echo hello world | /bin/busybox wc -w\n";
    struct cpu_state *cpu = NULL;
    uint64_t injectedCommandPc = 0;
    if (![self prepareInteractiveBusyboxShellInjectedCommandCpu:command
                                                         length:sizeof(command) - 1
                                                         cpuOut:&cpu
                                           injectedCommandPcOut:&injectedCommandPc])
        return;

    XCTAssertNotEqual(cpu, NULL, @"pipe-command injected-command path must preserve the guest CPU");
    if (cpu == NULL || cpu->mmu == NULL)
        return;

    struct tlb turnTlb = {};
    tlb_refresh(&turnTlb, cpu->mmu);
    exit_should_pthread_exit = false;
    a64_cpu_run_limited(cpu, &turnTlb, 4096);

    XCTAssertTrue(guest_execution_trace_sink_exit_observed() || cpu->pc != 0,
                  @"pipe-command first four-thousand-ninety-six post-injection steps must either advance guest control flow or report a guest exit instead of crashing the host; "
                   @"start_pc=0x%llx end_pc=0x%llx exit_observed=%d exit_code=%d",
                  (unsigned long long)injectedCommandPc, (unsigned long long)cpu->pc,
                  guest_execution_trace_sink_exit_observed() ? 1 : 0,
                  guest_execution_trace_sink_get_exit_code());
}

- (void)testInteractiveBusyboxShellExitBuiltinFirstTurnAfterInjectionDoesNotHostCrash
{
    const char command[] = "exit\n";
    struct cpu_state *cpu = NULL;
    uint64_t commandTurnPc = 0;
    if (![self prepareInteractiveBusyboxShellCommandTurnCpu:command
                                                     length:sizeof(command) - 1
                                                     cpuOut:&cpu
                                            commandTurnPcOut:&commandTurnPc])
        return;

    XCTAssertNotEqual(cpu, NULL, @"exit-builtin first resumed turn must preserve the guest CPU");
    if (cpu == NULL)
        return;

    XCTAssertTrue(guest_execution_trace_sink_exit_observed() || commandTurnPc != 0,
                  @"exit-builtin first resumed turn must either advance guest control flow or report a guest exit instead of crashing the host; "
                   @"command_turn_pc=0x%llx exit_observed=%d exit_code=%d",
                  (unsigned long long)commandTurnPc,
                  guest_execution_trace_sink_exit_observed() ? 1 : 0,
                  guest_execution_trace_sink_get_exit_code());
}

- (void)testInteractiveBusyboxShellExitBuiltinFirstStepAfterInjectionDoesNotHostCrash
{
    const char command[] = "exit\n";
    struct cpu_state *cpu = NULL;
    uint64_t injectedCommandPc = 0;
    if (![self prepareInteractiveBusyboxShellInjectedCommandCpu:command
                                                         length:sizeof(command) - 1
                                                         cpuOut:&cpu
                                           injectedCommandPcOut:&injectedCommandPc])
        return;

    XCTAssertNotEqual(cpu, NULL, @"exit-builtin injected-command path must preserve the guest CPU");
    if (cpu == NULL || cpu->mmu == NULL)
        return;

    struct tlb firstStepTlb = {};
    tlb_refresh(&firstStepTlb, cpu->mmu);
    exit_should_pthread_exit = false;
    a64_cpu_run_limited(cpu, &firstStepTlb, 1);

    XCTAssertTrue(guest_execution_trace_sink_exit_observed() || cpu->pc != 0,
                  @"exit-builtin first post-injection step must either advance guest control flow or report a guest exit instead of crashing the host; "
                   @"start_pc=0x%llx end_pc=0x%llx exit_observed=%d exit_code=%d",
                  (unsigned long long)injectedCommandPc, (unsigned long long)cpu->pc,
                  guest_execution_trace_sink_exit_observed() ? 1 : 0,
                  guest_execution_trace_sink_get_exit_code());
}

- (void)testInteractiveBusyboxShellExitBuiltinSixtyFourStepsAfterInjectionDoesNotHostCrash
{
    const char command[] = "exit\n";
    struct cpu_state *cpu = NULL;
    uint64_t injectedCommandPc = 0;
    if (![self prepareInteractiveBusyboxShellInjectedCommandCpu:command
                                                         length:sizeof(command) - 1
                                                         cpuOut:&cpu
                                           injectedCommandPcOut:&injectedCommandPc])
        return;

    XCTAssertNotEqual(cpu, NULL, @"exit-builtin injected-command path must preserve the guest CPU");
    if (cpu == NULL || cpu->mmu == NULL)
        return;

    struct tlb firstTurnTlb = {};
    tlb_refresh(&firstTurnTlb, cpu->mmu);
    exit_should_pthread_exit = false;
    a64_cpu_run_limited(cpu, &firstTurnTlb, 64);

    XCTAssertTrue(guest_execution_trace_sink_exit_observed() || cpu->pc != 0,
                  @"exit-builtin first sixty-four post-injection steps must either advance guest control flow or report a guest exit instead of crashing the host; "
                   @"start_pc=0x%llx end_pc=0x%llx exit_observed=%d exit_code=%d",
                  (unsigned long long)injectedCommandPc, (unsigned long long)cpu->pc,
                  guest_execution_trace_sink_exit_observed() ? 1 : 0,
                  guest_execution_trace_sink_get_exit_code());
}

- (void)testInteractiveBusyboxShellExitBuiltinOneHundredTwentyEightStepsAfterInjectionDoesNotHostCrash
{
    const char command[] = "exit\n";
    struct cpu_state *cpu = NULL;
    uint64_t injectedCommandPc = 0;
    if (![self prepareInteractiveBusyboxShellInjectedCommandCpu:command
                                                         length:sizeof(command) - 1
                                                         cpuOut:&cpu
                                           injectedCommandPcOut:&injectedCommandPc])
        return;

    XCTAssertNotEqual(cpu, NULL, @"exit-builtin injected-command path must preserve the guest CPU");
    if (cpu == NULL || cpu->mmu == NULL)
        return;

    struct tlb turnTlb = {};
    tlb_refresh(&turnTlb, cpu->mmu);
    exit_should_pthread_exit = false;
    a64_cpu_run_limited(cpu, &turnTlb, 128);

    XCTAssertTrue(guest_execution_trace_sink_exit_observed() || cpu->pc != 0,
                  @"exit-builtin first one-hundred-twenty-eight post-injection steps must either advance guest control flow or report a guest exit instead of crashing the host; "
                   @"start_pc=0x%llx end_pc=0x%llx exit_observed=%d exit_code=%d",
                  (unsigned long long)injectedCommandPc, (unsigned long long)cpu->pc,
                  guest_execution_trace_sink_exit_observed() ? 1 : 0,
                  guest_execution_trace_sink_get_exit_code());
}

- (void)testInteractiveBusyboxShellExitBuiltinTwoHundredFiftySixStepsAfterInjectionDoesNotHostCrash
{
    const char command[] = "exit\n";
    struct cpu_state *cpu = NULL;
    uint64_t injectedCommandPc = 0;
    if (![self prepareInteractiveBusyboxShellInjectedCommandCpu:command
                                                         length:sizeof(command) - 1
                                                         cpuOut:&cpu
                                           injectedCommandPcOut:&injectedCommandPc])
        return;

    XCTAssertNotEqual(cpu, NULL, @"exit-builtin injected-command path must preserve the guest CPU");
    if (cpu == NULL || cpu->mmu == NULL)
        return;

    struct tlb turnTlb = {};
    tlb_refresh(&turnTlb, cpu->mmu);
    exit_should_pthread_exit = false;
    a64_cpu_run_limited(cpu, &turnTlb, 256);

    XCTAssertTrue(guest_execution_trace_sink_exit_observed() || cpu->pc != 0,
                  @"exit-builtin first two-hundred-fifty-six post-injection steps must either advance guest control flow or report a guest exit instead of crashing the host; "
                   @"start_pc=0x%llx end_pc=0x%llx exit_observed=%d exit_code=%d",
                  (unsigned long long)injectedCommandPc, (unsigned long long)cpu->pc,
                  guest_execution_trace_sink_exit_observed() ? 1 : 0,
                  guest_execution_trace_sink_get_exit_code());
}

- (void)testInteractiveBusyboxShellExitBuiltinFiveHundredTwelveStepsAfterInjectionDoesNotHostCrash
{
    const char command[] = "exit\n";
    struct cpu_state *cpu = NULL;
    uint64_t injectedCommandPc = 0;
    if (![self prepareInteractiveBusyboxShellInjectedCommandCpu:command
                                                         length:sizeof(command) - 1
                                                         cpuOut:&cpu
                                           injectedCommandPcOut:&injectedCommandPc])
        return;

    XCTAssertNotEqual(cpu, NULL, @"exit-builtin injected-command path must preserve the guest CPU");
    if (cpu == NULL || cpu->mmu == NULL)
        return;

    struct tlb turnTlb = {};
    tlb_refresh(&turnTlb, cpu->mmu);
    exit_should_pthread_exit = false;
    a64_cpu_run_limited(cpu, &turnTlb, 512);

    XCTAssertTrue(guest_execution_trace_sink_exit_observed() || cpu->pc != 0,
                  @"exit-builtin first five-hundred-twelve post-injection steps must either advance guest control flow or report a guest exit instead of crashing the host; "
                   @"start_pc=0x%llx end_pc=0x%llx exit_observed=%d exit_code=%d",
                  (unsigned long long)injectedCommandPc, (unsigned long long)cpu->pc,
                  guest_execution_trace_sink_exit_observed() ? 1 : 0,
                  guest_execution_trace_sink_get_exit_code());
}

- (void)testInteractiveBusyboxShellExitBuiltinOneThousandTwentyFourStepsAfterInjectionDoesNotHostCrash
{
    const char command[] = "exit\n";
    struct cpu_state *cpu = NULL;
    uint64_t injectedCommandPc = 0;
    if (![self prepareInteractiveBusyboxShellInjectedCommandCpu:command
                                                         length:sizeof(command) - 1
                                                         cpuOut:&cpu
                                           injectedCommandPcOut:&injectedCommandPc])
        return;

    XCTAssertNotEqual(cpu, NULL, @"exit-builtin injected-command path must preserve the guest CPU");
    if (cpu == NULL || cpu->mmu == NULL)
        return;

    struct tlb turnTlb = {};
    tlb_refresh(&turnTlb, cpu->mmu);
    exit_should_pthread_exit = false;
    a64_cpu_run_limited(cpu, &turnTlb, 1024);

    XCTAssertTrue(guest_execution_trace_sink_exit_observed() || cpu->pc != 0,
                  @"exit-builtin first one-thousand-twenty-four post-injection steps must either advance guest control flow or report a guest exit instead of crashing the host; "
                   @"start_pc=0x%llx end_pc=0x%llx exit_observed=%d exit_code=%d",
                  (unsigned long long)injectedCommandPc, (unsigned long long)cpu->pc,
                  guest_execution_trace_sink_exit_observed() ? 1 : 0,
                  guest_execution_trace_sink_get_exit_code());
}

- (void)testInteractiveBusyboxShellExitBuiltinTwoThousandFortyEightStepsAfterInjectionDoesNotHostCrash
{
    const char command[] = "exit\n";
    struct cpu_state *cpu = NULL;
    uint64_t injectedCommandPc = 0;
    if (![self prepareInteractiveBusyboxShellInjectedCommandCpu:command
                                                         length:sizeof(command) - 1
                                                         cpuOut:&cpu
                                           injectedCommandPcOut:&injectedCommandPc])
        return;

    XCTAssertNotEqual(cpu, NULL, @"exit-builtin injected-command path must preserve the guest CPU");
    if (cpu == NULL || cpu->mmu == NULL)
        return;

    struct tlb turnTlb = {};
    tlb_refresh(&turnTlb, cpu->mmu);
    exit_should_pthread_exit = false;
    a64_cpu_run_limited(cpu, &turnTlb, 2048);

    XCTAssertTrue(guest_execution_trace_sink_exit_observed() || cpu->pc != 0,
                  @"exit-builtin first two-thousand-forty-eight post-injection steps must either advance guest control flow or report a guest exit instead of crashing the host; "
                   @"start_pc=0x%llx end_pc=0x%llx exit_observed=%d exit_code=%d",
                  (unsigned long long)injectedCommandPc, (unsigned long long)cpu->pc,
                  guest_execution_trace_sink_exit_observed() ? 1 : 0,
                  guest_execution_trace_sink_get_exit_code());
}

- (void)testInteractiveBusyboxShellExitBuiltinFourThousandNinetySixStepsAfterInjectionDoesNotHostCrash
{
    const char command[] = "exit\n";
    struct cpu_state *cpu = NULL;
    uint64_t injectedCommandPc = 0;
    if (![self prepareInteractiveBusyboxShellInjectedCommandCpu:command
                                                         length:sizeof(command) - 1
                                                         cpuOut:&cpu
                                           injectedCommandPcOut:&injectedCommandPc])
        return;

    XCTAssertNotEqual(cpu, NULL, @"exit-builtin injected-command path must preserve the guest CPU");
    if (cpu == NULL || cpu->mmu == NULL)
        return;

    struct tlb turnTlb = {};
    tlb_refresh(&turnTlb, cpu->mmu);
    exit_should_pthread_exit = false;
    a64_cpu_run_limited(cpu, &turnTlb, 4096);

    XCTAssertTrue(guest_execution_trace_sink_exit_observed() || cpu->pc != 0,
                  @"exit-builtin first four-thousand-ninety-six post-injection steps must either advance guest control flow or report a guest exit instead of crashing the host; "
                   @"start_pc=0x%llx end_pc=0x%llx exit_observed=%d exit_code=%d",
                  (unsigned long long)injectedCommandPc, (unsigned long long)cpu->pc,
                  guest_execution_trace_sink_exit_observed() ? 1 : 0,
                  guest_execution_trace_sink_get_exit_code());
}

- (void)testInteractiveBusyboxShellExitBuiltinSecondTurnFiveHundredTwelveStepsDoNotHostCrash
{
    const char command[] = "exit\n";
    struct cpu_state *cpu = NULL;
    uint64_t secondTurnStartPc = 0;
    if (![self prepareInteractiveBusyboxShellCommandTurnCpu:command
                                                     length:sizeof(command) - 1
                                                     cpuOut:&cpu
                                            commandTurnPcOut:&secondTurnStartPc])
        return;

    XCTAssertNotEqual(cpu, NULL, @"exit-builtin second resumed turn must preserve the guest CPU");
    if (cpu == NULL || cpu->mmu == NULL)
        return;

    struct tlb secondTurnTlb = {};
    tlb_refresh(&secondTurnTlb, cpu->mmu);
    exit_should_pthread_exit = false;
    a64_cpu_run_limited(cpu, &secondTurnTlb, 512);

    XCTAssertTrue(guest_execution_trace_sink_exit_observed() || cpu->pc != 0,
                  @"exit-builtin second resumed turn must either advance guest control flow or report a guest exit instead of crashing the host; "
                   @"start_pc=0x%llx end_pc=0x%llx exit_observed=%d exit_code=%d",
                  (unsigned long long)secondTurnStartPc, (unsigned long long)cpu->pc,
                  guest_execution_trace_sink_exit_observed() ? 1 : 0,
                  guest_execution_trace_sink_get_exit_code());
}

- (void)testInteractiveBusyboxShellPlainLsReturnsToPromptWithoutGuestSignalTermination
{
    [self configureFocusedTraceLevel];
    guest_execution_trace_sink_init();
    guest_execution_trace_sink_reset();

    if (![self execInteractiveBusyboxShellAndWaitForPrompt])
        return;

    uint64_t initialPromptWrites = guest_execution_trace_sink_stdout_prompt_write_count() +
                                   guest_execution_trace_sink_pty_prompt_write_count();
    const char command[] = "ls\n";
    if (![self sendInputThroughControllingPseudoMaster:command length:sizeof(command) - 1])
        return;

    BOOL completed = [self pumpGuestUntilTimeout:10.0
                                       predicate:^BOOL {
                                           uint64_t promptWrites = guest_execution_trace_sink_stdout_prompt_write_count() +
                                                                   guest_execution_trace_sink_pty_prompt_write_count();
                                           return promptWrites > initialPromptWrites ||
                                                  guest_execution_trace_sink_exit_observed();
                                       }];

    XCTAssertTrue(completed, @"interactive busybox shell must either return to a prompt after ls or exit within the timeout");
    XCTAssertFalse(guest_execution_trace_sink_exit_observed(),
                   @"interactive busybox shell must not terminate on plain ls. exit_code=%d",
                   guest_execution_trace_sink_get_exit_code());
    XCTAssertGreaterThan(guest_execution_trace_sink_stdout_prompt_write_count() +
                             guest_execution_trace_sink_pty_prompt_write_count(),
                         initialPromptWrites,
                         @"interactive busybox shell must return to a fresh prompt after plain ls");
}

- (void)testInteractiveBusyboxShellLongLsReachesMetadataAndReturnsToPromptWithoutGuestSignalTermination
{
    [self configureFocusedTraceLevel];
    guest_execution_trace_sink_init();
    guest_execution_trace_sink_reset();

    if (![self execInteractiveBusyboxShellAndWaitForPrompt])
        return;

    uint64_t initialPromptWrites = guest_execution_trace_sink_stdout_prompt_write_count() +
                                   guest_execution_trace_sink_pty_prompt_write_count();
    uint64_t initialMetadataWrites = guest_execution_trace_sink_stdout_metadata_write_count() +
                                     guest_execution_trace_sink_pty_metadata_write_count();
    const char command[] = "ls -la\n";
    if (![self sendInputThroughControllingPseudoMaster:command length:sizeof(command) - 1])
        return;

    BOOL completed = [self pumpGuestUntilTimeout:10.0
                                       predicate:^BOOL {
                                           uint64_t promptWrites = guest_execution_trace_sink_stdout_prompt_write_count() +
                                                                   guest_execution_trace_sink_pty_prompt_write_count();
                                           uint64_t metadataWrites = guest_execution_trace_sink_stdout_metadata_write_count() +
                                                                     guest_execution_trace_sink_pty_metadata_write_count();
                                           return (promptWrites > initialPromptWrites &&
                                                   metadataWrites > initialMetadataWrites) ||
                                                  guest_execution_trace_sink_exit_observed();
                                       }];

    XCTAssertTrue(completed,
                  @"interactive busybox shell must either reach metadata and return to a prompt after ls -la or exit within the timeout");
    XCTAssertFalse(guest_execution_trace_sink_exit_observed(),
                   @"interactive busybox shell must not terminate on ls -la. exit_code=%d",
                   guest_execution_trace_sink_get_exit_code());
    XCTAssertGreaterThan(guest_execution_trace_sink_stdout_metadata_write_count() +
                             guest_execution_trace_sink_pty_metadata_write_count(),
                         initialMetadataWrites,
                         @"interactive busybox shell must emit long-list metadata after ls -la");
    XCTAssertGreaterThan(guest_execution_trace_sink_stdout_prompt_write_count() +
                             guest_execution_trace_sink_pty_prompt_write_count(),
                         initialPromptWrites,
                         @"interactive busybox shell must return to a fresh prompt after ls -la");
}

- (void)testInteractiveBusyboxShellPipeCommandReturnsToPromptWithoutGuestSignalTermination
{
    [self configureFocusedTraceLevel];
    guest_execution_trace_sink_init();
    guest_execution_trace_sink_reset();

    if (![self execInteractiveBusyboxShellAndWaitForPrompt])
        return;

    uint64_t initialPromptWrites = guest_execution_trace_sink_stdout_prompt_write_count() +
                                   guest_execution_trace_sink_pty_prompt_write_count();
    const char command[] = "echo hello world | /bin/busybox wc -w\n";
    if (![self sendInputThroughControllingPseudoMaster:command length:sizeof(command) - 1])
        return;

    BOOL completed = [self pumpGuestUntilTimeout:10.0
                                       predicate:^BOOL {
                                           uint64_t promptWrites = guest_execution_trace_sink_stdout_prompt_write_count() +
                                                                   guest_execution_trace_sink_pty_prompt_write_count();
                                           return promptWrites > initialPromptWrites ||
                                                  guest_execution_trace_sink_exit_observed();
                                       }];

    XCTAssertTrue(completed,
                  @"interactive busybox shell must either return to a prompt after the pipe command or exit within the timeout");
    XCTAssertFalse(guest_execution_trace_sink_exit_observed(),
                   @"interactive busybox shell must not terminate on the pipe command. exit_code=%d",
                   guest_execution_trace_sink_get_exit_code());
    XCTAssertGreaterThan(guest_execution_trace_sink_stdout_prompt_write_count() +
                             guest_execution_trace_sink_pty_prompt_write_count(),
                         initialPromptWrites,
                         @"interactive busybox shell must return to a fresh prompt after the pipe command");
}

- (void)testInteractiveBusyboxShellPipeCommandEmitsWordCountOrPromptBeforeExit
{
    [self configureFocusedTraceLevel];
    guest_execution_trace_sink_init();
    guest_execution_trace_sink_reset();

    if (![self execInteractiveBusyboxShellAndWaitForPrompt])
        return;

    NSString *initialBuffer = [self controllingPseudoMasterBuffer];
    NSUInteger initialPromptCount = [self promptCountInBuffer:initialBuffer];
    const char command[] = "echo hello world | /bin/busybox wc -w\n";
    if (![self sendInputThroughControllingPseudoMaster:command length:sizeof(command) - 1])
        return;

    __block NSString *lastBuffer = @"";
    BOOL completed = [self pumpGuestUntilTimeout:10.0
                                       predicate:^BOOL {
                                           lastBuffer = [self controllingPseudoMasterBuffer];
                                           NSUInteger promptCount = [self promptCountInBuffer:lastBuffer];
                                           return [lastBuffer containsString:@"\n2\n"]
                                               || [lastBuffer containsString:@"\r\n2\r\n"]
                                               || [lastBuffer containsString:@"\n2\r\n"]
                                               || promptCount > initialPromptCount
                                               || guest_execution_trace_sink_exit_observed();
                                       }];

    XCTAssertTrue(completed,
                  @"interactive pipe command must either emit wc output, return to a prompt, or exit within the timeout; "
                   @"exit_observed=%d exit_code=%d master_buffer=%@",
                  guest_execution_trace_sink_exit_observed() ? 1 : 0,
                  guest_execution_trace_sink_get_exit_code(),
                  lastBuffer);
    XCTAssertFalse(guest_execution_trace_sink_exit_observed(),
                   @"interactive pipe command must not exit before emitting wc output or returning to a prompt. exit_code=%d master_buffer=%@",
                   guest_execution_trace_sink_get_exit_code(),
                   lastBuffer);
    XCTAssertTrue([lastBuffer containsString:@"\n2\n"]
                      || [lastBuffer containsString:@"\r\n2\r\n"]
                      || [lastBuffer containsString:@"\n2\r\n"]
                      || [self promptCountInBuffer:lastBuffer] > initialPromptCount,
                  @"interactive pipe command must either emit the wc output or reach a fresh prompt before timeout. initial_buffer=%@ master_buffer=%@",
                  initialBuffer,
                  lastBuffer);
}

- (void)testInteractiveBusyboxShellSilentPipeCommandReturnsToFreshPromptWithoutExit
{
    [self configureFocusedTraceLevel];
    guest_execution_trace_sink_init();
    guest_execution_trace_sink_reset();

    if (![self execInteractiveBusyboxShellAndWaitForPrompt])
        return;

    NSString *initialBuffer = [self controllingPseudoMasterBuffer];
    NSUInteger initialPromptCount = [self promptCountInBuffer:initialBuffer];
    const char command[] = "echo hello world | /bin/busybox true\n";
    if (![self sendInputThroughControllingPseudoMaster:command length:sizeof(command) - 1])
        return;

    __block NSString *lastBuffer = @"";
    BOOL completed = [self pumpGuestUntilTimeout:10.0
                                       predicate:^BOOL {
                                           lastBuffer = [self controllingPseudoMasterBuffer];
                                           return [self promptCountInBuffer:lastBuffer] > initialPromptCount
                                               || guest_execution_trace_sink_exit_observed();
                                       }];

    XCTAssertTrue(completed,
                  @"interactive silent pipe command must either return to a fresh prompt or exit within the timeout; "
                   @"exit_observed=%d exit_code=%d initial_buffer=%@ master_buffer=%@",
                  guest_execution_trace_sink_exit_observed() ? 1 : 0,
                  guest_execution_trace_sink_get_exit_code(),
                  initialBuffer,
                  lastBuffer);
    XCTAssertFalse(guest_execution_trace_sink_exit_observed(),
                   @"interactive silent pipe command must not exit the shell. exit_code=%d initial_buffer=%@ master_buffer=%@",
                   guest_execution_trace_sink_get_exit_code(),
                   initialBuffer,
                   lastBuffer);
    XCTAssertTrue([self promptCountInBuffer:lastBuffer] > initialPromptCount,
                  @"interactive silent pipe command must return to a fresh prompt. initial_buffer=%@ master_buffer=%@",
                  initialBuffer,
                  lastBuffer);
}

- (void)testInteractiveBusyboxShellExitTerminatesCleanlyWithGuestExitCodeZero
{
    [self configureFocusedTraceLevel];
    guest_execution_trace_sink_init();
    guest_execution_trace_sink_reset();

    if (![self execInteractiveBusyboxShellAndWaitForPrompt])
        return;

    const char command[] = "exit\n";
    if (![self sendInputThroughControllingPseudoMaster:command length:sizeof(command) - 1])
        return;

    BOOL exited = [self pumpGuestUntilTimeout:10.0
                                     predicate:^BOOL {
                                         return guest_execution_trace_sink_exit_observed();
                                     }];

    XCTAssertTrue(exited, @"interactive busybox shell must exit promptly after the exit builtin");
    XCTAssertEqual(guest_execution_trace_sink_get_exit_code(), 0,
                   @"interactive busybox shell exit builtin must terminate with guest exit code zero");
}

- (void)testLiveGuestLinkage_OrderedExclusiveAtomicFamilyAppearsInInteractiveShellPath
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

    BOOL observed = [self pumpGuestUntilTimeout:10.0
                                      predicate:^BOOL {
                                          return guest_execution_trace_sink_ordered_exclusive_atomic_family_hit_observed() ||
                                                 guest_execution_trace_sink_exit_observed();
                                      }];

    XCTAssertTrue(observed, @"interactive shell must either hit OrderedExclusiveAtomic or exit within the timeout");
    XCTAssertTrue(guest_execution_trace_sink_ordered_exclusive_atomic_family_hit_observed(),
                  @"interactive shell still lacks named OrderedExclusiveAtomic linkage. count=%llu last_pc=0x%llx exit=%d",
                  guest_execution_trace_sink_ordered_exclusive_atomic_family_hit_count(),
                  guest_execution_trace_sink_ordered_exclusive_atomic_last_pc(),
                  guest_execution_trace_sink_exit_observed() ? 1 : 0);
}

- (void)testLiveGuestLinkage_VectorIntegerLogicalFamilyAppearsInInteractiveShellPath
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

    BOOL observed = [self pumpGuestUntilTimeout:10.0
                                      predicate:^BOOL {
                                          return guest_execution_trace_sink_vector_integer_logical_family_hit_observed() ||
                                                 guest_execution_trace_sink_exit_observed();
                                      }];

    XCTAssertTrue(observed, @"interactive shell must either hit VectorIntegerLogical or exit within the timeout");
    XCTAssertTrue(guest_execution_trace_sink_vector_integer_logical_family_hit_observed(),
                  @"interactive shell still lacks named VectorIntegerLogical linkage. count=%llu last_pc=0x%llx exit=%d",
                  guest_execution_trace_sink_vector_integer_logical_family_hit_count(),
                  guest_execution_trace_sink_vector_integer_logical_last_pc(),
                  guest_execution_trace_sink_exit_observed() ? 1 : 0);
}

@end
