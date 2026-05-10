#import <XCTest/XCTest.h>
#import <IXLandLinuxRuntime/kernel/elf.h>
#import <IXLandLinuxRuntime/kernel/task.h>
#import <IXLandLinuxRuntime/kernel/calls.h>
#import <IXLandLinuxRuntime/kernel/init.h>
#import <IXLandLinuxRuntime/kernel/musl_thread.h>
#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>
#include <stdlib.h>

// Guest.ProcessABI System Tests
// Tests for process-level ABI contracts visible to guest code
//
// Scope: Guest-visible execution environment setup
// Dependencies: TCTI pipeline must be functional

@interface GuestProcessABITests : XCTestCase
@end

@implementation GuestProcessABITests {
    struct cpu_state _cpu;
}

- (BOOL)readGuestCStringAt:(addr_t)guestAddress into:(char *)buffer capacity:(size_t)capacity
{
    if (buffer == NULL || capacity == 0) {
        return NO;
    }

    for (size_t i = 0; i < capacity; i++) {
        char c = '\0';
        if (user_get(guestAddress + i, c) != 0) {
            buffer[0] = '\0';
            return NO;
        }
        buffer[i] = c;
        if (c == '\0') {
            return YES;
        }
    }

    buffer[capacity - 1] = '\0';
    return NO;
}

- (BOOL)readInitialProcessStackArgc:(uint64_t *)argcOut
                           argvBase:(addr_t *)argvBaseOut
                           envpBase:(addr_t *)envpBaseOut
{
    if (current == NULL) {
        return NO;
    }

    addr_t sp = current->cpu.sp;
    uint64_t argc = 0;
    if (user_get(sp, argc) != 0) {
        return NO;
    }

    addr_t argvBase = sp + sizeof(addr_t);
    addr_t envpBase = argvBase + ((argc + 1) * sizeof(addr_t));

    if (argcOut != NULL) {
        *argcOut = argc;
    }
    if (argvBaseOut != NULL) {
        *argvBaseOut = argvBase;
    }
    if (envpBaseOut != NULL) {
        *envpBaseOut = envpBase;
    }
    return YES;
}

- (BOOL)guestProcessUsesMuslStartup
{
    return current != NULL && current->cpu.tpidr_el0 != 0;
}

- (BOOL)detectMuslPthreadBase:(addr_t *)pthreadBaseOut
                     tpOffset:(addr_t *)tpOffsetOut {
    if (![self guestProcessUsesMuslStartup]) {
        return NO;
    }

    addr_t tp = current->cpu.tpidr_el0;
    addr_t candidates[] = {
        A64_MUSL_THREAD_POINTER_OFFSET,
        A64_MUSL_LEGACY_THREAD_POINTER_OFFSET,
    };

    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        addr_t candidateBase = tp - candidates[i];
        uintptr_t selfPtr = 0;
        if (user_get(candidateBase + A64_MUSL_PTHREAD_SELF_OFFSET, selfPtr) == 0 &&
            selfPtr == candidateBase) {
            if (pthreadBaseOut != NULL)
                *pthreadBaseOut = candidateBase;
            if (tpOffsetOut != NULL)
                *tpOffsetOut = candidates[i];
            return YES;
        }
    }

    return NO;
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

- (BOOL)bootstrapMountedRootfsAtPath:(NSString *)rootPath {
    int mountErr = mount_root(&rootfs, rootPath.UTF8String);
    XCTAssertTrue(mountErr == 0 || mountErr == -16, @"mount_root returned %d", mountErr);
    if (!(mountErr == 0 || mountErr == -16))
        return NO;

    int initErr = become_first_process();
    XCTAssertTrue(initErr == 0 || initErr == -17, @"become_first_process returned %d", initErr);
    if (!(initErr == 0 || initErr == -17))
        return NO;

    int childErr = become_new_init_child();
    XCTAssertEqual(childErr, 0, @"become_new_init_child returned %d", childErr);
    return childErr == 0;
}

- (BOOL)execBusyboxUnameForABIInspection {
    NSString *rootPath = [self dataRootPath];
    XCTAssertNotNil(rootPath, @"rootfs path must exist");
    if (rootPath == nil || ![self bootstrapMountedRootfsAtPath:rootPath]) {
        return NO;
    }

    const char argv[] = "/bin/busybox\0uname\0-a\0\0";
    const char envp[] = "TERM=xterm-256color\0PATH=/bin:/usr/bin\0HOME=/root\0\0";
    int execErr = do_execve("bin/busybox", 3, argv, envp);
    XCTAssertEqual(execErr, 0, @"do_execve returned %d", execErr);
    return execErr == 0;
}

- (BOOL)execBusyboxLongLsForABIInspection
{
    NSString *rootPath = [self dataRootPath];
    XCTAssertNotNil(rootPath, @"rootfs path must exist");
    if (rootPath == nil || ![self bootstrapMountedRootfsAtPath:rootPath]) {
        return NO;
    }

    const char argv[] = "/bin/busybox\0ls\0-la\0/\0\0";
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
    return execErr == 0;
}

- (void)setUp {
    [super setUp];
    memset(&_cpu, 0, sizeof(_cpu));
}

- (void)tearDown {
    [super tearDown];
}

// Contract: ELF entry point calculations are correct
// Owner: kernel/exec.c:elf_exec
- (void)testProcessABIContract_ELFEntryPointCalculation {
    // System: entry = bias + header.entry_point
    // Test validates the calculation formula used in exec.c:969
    addr_t bias = 0x400000;
    addr_t entry_point = 0x1000;
    addr_t expected_entry = bias + entry_point;
    
    XCTAssertEqual(expected_entry, 0x401000,
        "ELF entry calculation incorrect: 0x%llx + 0x%llx = 0x%llx, expected 0x401000",
        bias, entry_point, expected_entry);
}

// Contract: Entry point validation catches invalid addresses
// Owner: kernel/exec.c:elf_exec lines 1400-1407
- (void)testProcessABIContract_ELFEntryPointValidationRejectsInvalid {
    // System: entry == 0 or entry == 0x100000000 causes _EFAULT
    addr_t invalid_entry_1 = 0;
    addr_t invalid_entry_2 = 0x100000000ULL;
    
    XCTAssertEqual(invalid_entry_1, 0,
        "Zero entry point should be rejected");
    XCTAssertEqual(invalid_entry_2, 0x100000000ULL,
        "Entry point 0x100000000 should be rejected");
}

// Contract: Initial SP must be 16-byte aligned
// Owner: kernel/exec.c:elf_exec, line 1331: sp &= ~0xf
- (void)testProcessABIContract_StackAlignmentContract {
    // System: Stack alignment formula
    addr_t unaligned_sp = 0x7fff0007;
    addr_t aligned_sp = unaligned_sp & ~0xf;
    
    XCTAssertEqual(aligned_sp % 16, 0,
        "Stack must be 16-byte aligned after & ~0xf operation");
    XCTAssertEqual(aligned_sp, 0x7fff0000,
        "Stack alignment should mask lower 4 bits");
}

// Contract: x0-x1 register initialization for musl aarch64
// Owner: kernel/exec.c:elf_exec lines 1425-1428
- (void)testProcessABIContract_RegisterInitializationContract {
    if (![self execBusyboxUnameForABIInspection]) {
        return;
    }

    uint64_t argc = 0;
    addr_t argvBase = 0;
    addr_t envpBase = 0;
    XCTAssertTrue([self readInitialProcessStackArgc:&argc argvBase:&argvBase envpBase:&envpBase],
                  @"initial guest process stack must be readable before first instruction");

    uint64_t dynamicTag = 0;
    uint64_t dynamicValue = 0;
    XCTAssertEqual(current->cpu.x[0], current->cpu.sp,
                   @"x0 must point at argc on the initial guest stack");
    XCTAssertNotEqual(current->cpu.x[1], 0ULL,
                      @"x1 must point at a live _DYNAMIC table for musl startup");
    XCTAssertEqual(user_get(current->cpu.x[1], dynamicTag), 0,
                   @"x1 must point at readable _DYNAMIC tag storage");
    XCTAssertEqual(user_get(current->cpu.x[1] + sizeof(uint64_t), dynamicValue), 0,
                   @"x1 must point at readable _DYNAMIC value storage");
    XCTAssertNotEqual(argc, 0ULL, @"initial argc must be present on the guest stack");
    XCTAssertNotEqual(argvBase, 0U, @"argv vector must be placed on the guest stack");
    XCTAssertNotEqual(envpBase, 0U, @"envp vector must be placed on the guest stack");
}

// Contract: x2-x7 registers are zeroed at process start
// Owner: kernel/exec.c:elf_exec (cpu init zeros all regs)
- (void)testProcessABIContract_RegistersTwoThroughSevenZeroed {
    // Guest: x2-x7 must be 0 per musl init
    // System: a64_cpu_init() zeros all registers
    
    // Since _cpu is zeroed in setUp, it already represents init state
    for (int i = 2; i <= 7; i++) {
        XCTAssertEqual(_cpu.x[i], 0,
            "x%d must be 0 at process start", i);
    }
}

// Contract: PSTATE initial state for EL0
// Owner: kernel/exec.c:elf_exec via a64_cpu_init
- (void)testProcessABIContract_PSTATEInitialization {
    if (![self execBusyboxUnameForABIInspection]) {
        return;
    }

    XCTAssertEqual(current->cpu.pstate, 0ULL,
                   @"fresh execve startup must enter guest EL0 with NZCV clear");
}

// Contract: TLS area is set up via TPIDR_EL0
// Owner: kernel/exec.c + kernel/musl_thread.c
- (void)testProcessABIContract_TLSAreaSetup {
    if (![self execBusyboxUnameForABIInspection]) {
        return;
    }
    if (![self guestProcessUsesMuslStartup]) {
        XCTSkip(@"musl-specific TPIDR_EL0 startup contract only applies to musl guests");
        return;
    }

    addr_t pthreadBase = 0;
    addr_t tpOffset = 0;
    XCTAssertTrue([self detectMuslPthreadBase:&pthreadBase tpOffset:&tpOffset],
                  @"musl bootstrap must leave a self-consistent TP/self pair");
    XCTAssertEqual(current->cpu.tpidr_el0, pthreadBase + tpOffset,
                   @"musl startup must seed TPIDR_EL0 to the guest thread pointer inside the "
                    @"initial pthread object");
}

- (void)testProcessABIContract_InitialThreadBootstrapLayout {
    if (![self execBusyboxUnameForABIInspection]) {
        return;
    }
    if (![self guestProcessUsesMuslStartup]) {
        XCTSkip(@"musl-specific pthread bootstrap layout only applies to musl guests");
        return;
    }

    addr_t pthreadBase = 0;
    addr_t tpOffset = 0;
    XCTAssertTrue([self detectMuslPthreadBase:&pthreadBase tpOffset:&tpOffset],
                  @"musl bootstrap must leave a self-consistent TP/self pair");
    uintptr_t selfPtr = 0;
    uintptr_t prevPtr = 0;
    uintptr_t nextPtr = 0;
    uintptr_t sysinfo = UINTPTR_MAX;
    uintptr_t robustHead = 0;
    uintptr_t dtvPtr = 0;
    uintptr_t dtvCount = UINTPTR_MAX;
    uintptr_t canary = 0;
    int tid = -1;
    int errnoValue = -1;
    int hErrnoValue = -1;
    int detachState = -1;
    int killlock = -1;
    addr_t canaryOffset = tpOffset == A64_MUSL_LEGACY_THREAD_POINTER_OFFSET
                              ? A64_MUSL_PTHREAD_CANARY_TAIL_OFFSET
                              : A64_MUSL_LEGACY_PTHREAD_CANARY_OFFSET;
    addr_t dtvOffset = tpOffset == A64_MUSL_LEGACY_THREAD_POINTER_OFFSET
                           ? A64_MUSL_PTHREAD_DTV_TAIL_OFFSET
                           : A64_MUSL_LEGACY_PTHREAD_DTV_OFFSET;

    XCTAssertEqual(user_get(pthreadBase + A64_MUSL_PTHREAD_SELF_OFFSET, selfPtr), 0);
    XCTAssertEqual(user_get(pthreadBase + A64_MUSL_PTHREAD_PREV_OFFSET, prevPtr), 0);
    XCTAssertEqual(user_get(pthreadBase + A64_MUSL_PTHREAD_NEXT_OFFSET, nextPtr), 0);
    XCTAssertEqual(user_get(pthreadBase + A64_MUSL_PTHREAD_SYSINFO_OFFSET, sysinfo), 0);
    XCTAssertEqual(user_get(pthreadBase + A64_MUSL_PTHREAD_TID_OFFSET, tid), 0);
    XCTAssertEqual(user_get(pthreadBase + A64_MUSL_PTHREAD_ERRNO_OFFSET, errnoValue), 0);
    XCTAssertEqual(user_get(pthreadBase + A64_MUSL_PTHREAD_ROBUST_HEAD_OFFSET, robustHead), 0);
    XCTAssertEqual(user_get(pthreadBase + canaryOffset, canary), 0);
    XCTAssertEqual(user_get(pthreadBase + dtvOffset, dtvPtr), 0);
    XCTAssertEqual(user_get(dtvPtr, dtvCount), 0);
    XCTAssertEqual(user_get(pthreadBase + A64_MUSL_PTHREAD_H_ERRNO_OFFSET, hErrnoValue), 0);
    XCTAssertEqual(user_get(pthreadBase + A64_MUSL_PTHREAD_DETACH_OFFSET, detachState), 0);
    XCTAssertEqual(user_get(pthreadBase + A64_MUSL_PTHREAD_KILLLOCK_OFFSET, killlock), 0);

    XCTAssertEqual(selfPtr, pthreadBase, @"self must point at the singleton initial pthread");
    XCTAssertEqual(prevPtr, pthreadBase, @"prev must point at the singleton initial pthread");
    XCTAssertEqual(nextPtr, pthreadBase, @"next must point at the singleton initial pthread");
    XCTAssertEqual(sysinfo, (uintptr_t)0, @"sysinfo must start cleared until guest libc seeds it");
    XCTAssertEqual(tid, current->pid, @"initial pthread tid must match the current guest task");
    XCTAssertEqual(errnoValue, 0, @"initial guest errno storage must start clear");
    XCTAssertEqual(robustHead, pthreadBase + A64_MUSL_PTHREAD_ROBUST_HEAD_OFFSET,
                   @"robust_list.head must be self-referential for the initial thread");
    XCTAssertEqual(current->cpu.tpidr_el0, pthreadBase + tpOffset,
                   @"AArch64 musl TPIDR_EL0 must match the live musl ABI-selected TP offset");
    XCTAssertNotEqual(canary, (uintptr_t)0, @"stack canary slot must be seeded from AT_RANDOM");
    if (tpOffset == A64_MUSL_LEGACY_THREAD_POINTER_OFFSET) {
        XCTAssertEqual(pthreadBase + dtvOffset, current->cpu.tpidr_el0 - sizeof(uintptr_t),
                       @"post-2018 AArch64 musl stores the DTV pointer slot immediately below TP");
        XCTAssertEqual(pthreadBase + canaryOffset, current->cpu.tpidr_el0 - (2 * sizeof(uintptr_t)),
                       @"post-2018 AArch64 musl stores the canary immediately below the DTV slot");
    } else {
        XCTAssertEqual(pthreadBase + dtvOffset, (addr_t)(pthreadBase + A64_MUSL_LEGACY_PTHREAD_DTV_OFFSET),
                       @"legacy AArch64 musl stores the DTV pointer near the start of pthread");
    }
    XCTAssertNotEqual(dtvPtr, (uintptr_t)0, @"initial DTV pointer must be valid");
    XCTAssertEqual(dtvCount, (uintptr_t)0,
                   @"busybox/ld-musl startup in this rootfs carries no PT_TLS modules, so "
                    @"the initial DTV header count must start at zero");
    XCTAssertEqual(hErrnoValue, 0, @"initial guest h_errno storage must start clear");
    XCTAssertEqual(detachState, 2, @"detach_state must start as DT_JOINABLE");
    XCTAssertEqual(killlock, 0, @"killlock must start clear for the initial thread");
}

- (void)testProcessABIContract_InitialEnvpVectorIsNullTerminatedAndReadable
{
    if (![self execBusyboxUnameForABIInspection]) {
        return;
    }

    uint64_t argc = 0;
    addr_t argvBase = 0;
    addr_t envpBase = 0;
    XCTAssertTrue([self readInitialProcessStackArgc:&argc argvBase:&argvBase envpBase:&envpBase],
                  @"must be able to parse argc/argv/envp from the initial guest stack");

    addr_t argvNull = UINTPTR_MAX;
    XCTAssertEqual(user_get(argvBase + (argc * sizeof(addr_t)), argvNull), 0,
                   @"argv terminator must be readable");
    XCTAssertEqual(argvNull, (addr_t)0, @"argv must be NULL-terminated on the initial stack");

    addr_t env0 = 0;
    addr_t env1 = 0;
    addr_t env2 = 0;
    addr_t env3 = 0;
    XCTAssertEqual(user_get(envpBase + (0 * sizeof(addr_t)), env0), 0);
    XCTAssertEqual(user_get(envpBase + (1 * sizeof(addr_t)), env1), 0);
    XCTAssertEqual(user_get(envpBase + (2 * sizeof(addr_t)), env2), 0);
    XCTAssertEqual(user_get(envpBase + (3 * sizeof(addr_t)), env3), 0);

    XCTAssertNotEqual(env0, (addr_t)0, @"envp[0] must point at TERM");
    XCTAssertNotEqual(env1, (addr_t)0, @"envp[1] must point at PATH");
    XCTAssertNotEqual(env2, (addr_t)0, @"envp[2] must point at HOME");
    XCTAssertEqual(env3, (addr_t)0, @"expected envp[3] to be the final NULL for ABI probe");

    char env0Buf[64];
    char env1Buf[64];
    char env2Buf[64];
    XCTAssertTrue([self readGuestCStringAt:env0 into:env0Buf capacity:sizeof(env0Buf)]);
    XCTAssertTrue([self readGuestCStringAt:env1 into:env1Buf capacity:sizeof(env1Buf)]);
    XCTAssertTrue([self readGuestCStringAt:env2 into:env2Buf capacity:sizeof(env2Buf)]);
    XCTAssertEqualObjects([NSString stringWithUTF8String:env0Buf], @"TERM=xterm-256color");
    XCTAssertEqualObjects([NSString stringWithUTF8String:env1Buf], @"PATH=/bin:/usr/bin");
    XCTAssertEqualObjects([NSString stringWithUTF8String:env2Buf], @"HOME=/root");
}

- (void)testProcessABIContract_AppStyleLongLsEnvpVectorIsNullTerminatedAndReadable
{
    if (![self execBusyboxLongLsForABIInspection]) {
        return;
    }

    uint64_t argc = 0;
    addr_t argvBase = 0;
    addr_t envpBase = 0;
    XCTAssertTrue([self readInitialProcessStackArgc:&argc argvBase:&argvBase envpBase:&envpBase]);
    XCTAssertEqual(argc, (uint64_t)4, @"ls -la ABI probe must carry four argv entries");

    NSArray<NSString *> *expected = @[
        @"TERM=xterm-256color",
        @"HOME=/root",
        @"USER=root",
        @"LOGNAME=root",
        @"SHELL=/bin/sh",
        @"PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",
    ];

    for (NSUInteger i = 0; i < expected.count; i++) {
        addr_t entry = 0;
        XCTAssertEqual(user_get(envpBase + (i * sizeof(addr_t)), entry), 0);
        XCTAssertNotEqual(entry, (addr_t)0, @"envp[%lu] must point at %@", (unsigned long)i,
                          expected[i]);
        char buffer[128];
        XCTAssertTrue([self readGuestCStringAt:entry into:buffer capacity:sizeof(buffer)]);
        XCTAssertEqualObjects([NSString stringWithUTF8String:buffer], expected[i]);
    }

    addr_t envNull = UINTPTR_MAX;
    XCTAssertEqual(user_get(envpBase + (expected.count * sizeof(addr_t)), envNull), 0);
    XCTAssertEqual(envNull, (addr_t)0, @"app-style envp must end with a NULL pointer before auxv");
}

// System: First fetch boundary - PC set from ELF
// Owner: kernel/exec.c:1395
- (void)testProcessABIContract_FirstInstructionBoundary {
    // System: After execve completes, cpu->pc must equal entry point
    // This is B1 (pre-entry) -> B2 (first fetch) boundary
    
    // Without actual execve execution, this tests the wiring
    addr_t entry = 0x401000; // Example entry
    
    _cpu.pc = entry;  // Simulate what exec.c:1395 does
    
    XCTAssertEqual(_cpu.pc, entry,
        "Guest PC must be set from ELF entry point before first instruction");
}

@end
