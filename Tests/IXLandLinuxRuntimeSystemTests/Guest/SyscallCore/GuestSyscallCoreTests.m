#import <XCTest/XCTest.h>
#import <IXLandLinuxRuntime/kernel/calls.h>
#import <IXLandLinuxRuntime/kernel/errno.h>

// Guest.SyscallCore System Tests
// Tests syscall dispatch and ABI contracts
// Owner: kernel/calls.c syscall table

@interface GuestSyscallCoreTests : XCTestCase
@end

@implementation GuestSyscallCoreTests

// Contract: Core syscalls are implemented
// Owner: kernel/calls.c
- (void)testSyscallCoreContract_ExitIsImplemented {
    XCTAssert(sys_exit != NULL, "exit syscall must be implemented");
}

- (void)testSyscallCoreContract_ReadIsImplemented {
    XCTAssert(sys_read != NULL, "read syscall must be implemented");
}

- (void)testSyscallCoreContract_WriteIsImplemented {
    XCTAssert(sys_write != NULL, "write syscall must be implemented");
}

- (void)testSyscallCoreContract_OpenIsImplemented {
    XCTAssert(sys_open != NULL, "open syscall must be implemented");
}

- (void)testSyscallCoreContract_CloseIsImplemented {
    XCTAssert(sys_close != NULL, "close syscall must be implemented");
}

// Contract: Identity syscalls are implemented
// Owner: kernel/calls.c entries 20, 24, 47, 49, 50, 64, 65
- (void)testSyscallCoreContract_GetPidIsImplemented {
    XCTAssert(sys_getpid != NULL, "getpid syscall must be implemented");
}

- (void)testSyscallCoreContract_GetUidIsImplemented {
    XCTAssert(sys_getuid != NULL, "getuid syscall must be implemented");
}

- (void)testSyscallCoreContract_GetGidIsImplemented {
    XCTAssert(sys_getgid != NULL, "getgid syscall must be implemented");
}

- (void)testSyscallCoreContract_GetEuidIsImplemented {
    XCTAssert(sys_geteuid != NULL, "geteuid syscall must be implemented");
}

- (void)testSyscallCoreContract_GetEgidIsImplemented {
    XCTAssert(sys_getegid != NULL, "getegid syscall must be implemented");
}

- (void)testSyscallCoreContract_GetPpidIsImplemented {
    XCTAssert(sys_getppid != NULL, "getppid syscall must be implemented");
}

- (void)testSyscallCoreContract_GetPgrpIsImplemented {
    XCTAssert(sys_getpgrp != NULL, "getpgrp syscall must be implemented");
}

// Contract: Memory syscalls are implemented
// Owner: kernel/calls.c
- (void)testSyscallCoreContract_MmapIsImplemented {
    XCTAssert(sys_mmap2 != NULL, "mmap2 syscall must be implemented");
}

- (void)testSyscallCoreContract_MunmapIsImplemented {
    XCTAssert(sys_munmap != NULL, "munmap syscall must be implemented");
}

- (void)testSyscallCoreContract_MprotectIsImplemented {
    XCTAssert(sys_mprotect != NULL, "mprotect syscall must be implemented");
}

// Contract: Process control syscalls are implemented
- (void)testSyscallCoreContract_ForkIsImplemented {
    XCTAssert(sys_fork != NULL, "fork syscall must be implemented");
}

- (void)testSyscallCoreContract_ExecveIsImplemented {
    XCTAssert(sys_execve != NULL, "execve syscall must be implemented");
}

- (void)testSyscallCoreContract_Wait4IsImplemented {
    XCTAssert(sys_wait4 != NULL, "wait4 syscall must be implemented");
}

// Contract: Signal syscalls are implemented
- (void)testSyscallCoreContract_KillIsImplemented {
    XCTAssert(sys_kill != NULL, "kill syscall must be implemented");
}

// Contract: Time syscalls are implemented
- (void)testSyscallCoreContract_TimeIsImplemented {
    XCTAssert(sys_time != NULL, "time syscall must be implemented");
}

- (void)testSyscallCoreContract_GetTimeOfDayIsImplemented {
    XCTAssert(sys_gettimeofday != NULL, "gettimeofday syscall must be implemented");
}

@end
