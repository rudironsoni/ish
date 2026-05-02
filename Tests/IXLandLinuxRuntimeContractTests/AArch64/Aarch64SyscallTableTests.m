#import <XCTest/XCTest.h>

#import <IXLandLinuxRuntime/kernel/aarch64/calls.h>

extern a64_syscall_t syscall_table_a64[A64_SYS_MAX];

@interface Aarch64SyscallTableTests : XCTestCase
@end

@implementation Aarch64SyscallTableTests

- (void)testMemorySyscallNumbersMatchLinuxArm64ABI
{
    XCTAssertEqual(A64_SYS_brk, 214);
    XCTAssertEqual(A64_SYS_munmap, 215);
    XCTAssertEqual(A64_SYS_mremap, 216);
    XCTAssertEqual(A64_SYS_mmap, 222);
    XCTAssertEqual(A64_SYS_mprotect, 226);
    XCTAssertEqual(A64_SYS_madvise, 233);
}

- (void)testBrkDispatchesAtArm64SyscallNumber
{
    XCTAssertNotEqual(A64_SYS_brk, A64_SYS_llistxattr);
    XCTAssertNotEqual(syscall_table_a64[A64_SYS_brk], NULL);
    XCTAssertEqual(syscall_table_a64[A64_SYS_llistxattr], NULL);
}

- (void)testGuestBootstrapSyscallsDispatchAtArm64Numbers
{
    XCTAssertNotEqual(syscall_table_a64[A64_SYS_newfstatat], NULL);
    XCTAssertNotEqual(syscall_table_a64[A64_SYS_futex], NULL);
    XCTAssertNotEqual(syscall_table_a64[A64_SYS_set_robust_list], NULL);
    XCTAssertNotEqual(syscall_table_a64[A64_SYS_get_robust_list], NULL);
    XCTAssertNotEqual(syscall_table_a64[A64_SYS_clock_gettime], NULL);
    XCTAssertNotEqual(syscall_table_a64[A64_SYS_clock_getres], NULL);
    XCTAssertNotEqual(syscall_table_a64[A64_SYS_prlimit64], NULL);
    XCTAssertNotEqual(syscall_table_a64[A64_SYS_statx], NULL);
}

@end
