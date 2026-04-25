// GuestExecutionHarness.m
// Test-owned execution harness for deterministic guest execution boundaries
// Owner: Tests/Support/GuestExecutionHarness

#import "GuestExecutionHarness.h"
#import "GuestExecutionProbe.h"
#import "GuestExecutionTraceSink.h"
#import <XCTest/XCTest.h>
#import <IXLandInstrumentation/IXLandInstrumentation.h>

#import <IXLandLinuxRuntime/kernel/task.h>
#import <IXLandLinuxRuntime/kernel/init.h>
#import <IXLandLinuxRuntime/kernel/calls.h>
#import <IXLandLinuxRuntime/fs/real.h>
#import <IXLandLinuxRuntime/emu/tlb.h>
#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>

// GCD compatibility: disable pthread_exit in do_exit to avoid libdispatch crashes
extern bool exit_should_pthread_exit;

@interface GuestExecutionHarness ()
@property (nonatomic, strong) XCTestExpectation *exitExpectation;
@property (nonatomic, strong) dispatch_queue_t executionQueue;
- (NSString *)materializeFixture:(NSString *)name extension:(NSString *)ext bundle:(NSBundle *)bundle;
- (BOOL)setupRuntimeWithPath:(NSString *)tempPath;
- (GuestExecutionResult *)prepareExecutableAtRootPath:(NSString *)rootPath
                      executablePath:(NSString *)executablePath;
@end

@implementation GuestExecutionResult

+ (instancetype)resultFromProbe:(guest_execution_probe_result_t *)probe {
    GuestExecutionResult *result = [[GuestExecutionResult alloc] init];
    result.fixtureName = [NSString stringWithUTF8String:probe->fixture_name ?: ""];
    result.loadOk = probe->load_ok;
    result.pcBefore = probe->pc_before;
    result.pcAfter = probe->pc_after;
    result.spBefore = probe->sp_before;
    result.spAfter = probe->sp_after;
    result.x0After = probe->x0_after;
    result.x8After = probe->x8_after;
    result.blockCompiled = probe->block_compiled;
    result.blockExecuted = probe->block_executed;
    result.boundaryReason = (GuestBoundaryReason)probe->boundary_reason;
    result.reachedSyscallBoundary = probe->reached_syscall_boundary;
    result.reachedFaultBoundary = probe->reached_fault_boundary;
    result.taskExitObserved = probe->task_exit_observed;
    result.exitCode = probe->exit_code;
    result.returnedToHarness = probe->returned_to_harness;
    result.completed = probe->completed;
    result.errorMessage = probe->error_message ? [NSString stringWithUTF8String:probe->error_message] : nil;
    // Harness classification ladder H0-H4
    result.harnessEntered = probe->harness_entered;
    result.mountRootCalled = probe->mount_root_called;
    result.mountRootReturnValue = probe->mount_root_return_value;
    result.becomeFirstProcessCalled = probe->become_first_process_called;
    result.becomeFirstProcessReturnValue = probe->become_first_process_return_value;
    result.doExecveReached = probe->do_execve_reached;
    result.doExecveCalled = probe->do_execve_called;
    result.doExecveReturnValue = probe->do_execve_return_value;
    return result;
}

@end

@implementation GuestExecutionHarness

- (instancetype)init {
    self = [super init];
    if (self) {
        guest_execution_trace_sink_init();
        ixland_instrumentation_activate();
        _executionQueue = dispatch_queue_create("com.ixland.guestexecution", DISPATCH_QUEUE_SERIAL);
    }
    return self;
}

// Lane A1: First block execution - synchronous, returns after 1 iteration
- (GuestExecutionResult *)runFixtureUntilFirstBoundary:(NSString *)fixtureName
                                            extension:(NSString *)ext
                                              bundle:(NSBundle *)bundle {
    probe_reset();
    guest_execution_trace_sink_reset();
    probe_begin_fixture([[NSString stringWithFormat:@"%@.%@", fixtureName, ext] UTF8String]);
    
    NSString *tempPath = [self materializeFixture:fixtureName extension:ext bundle:bundle];
    if (!tempPath) {
        probe_set_error("Failed to materialize fixture");
        probe_set_completed(true);
        return [GuestExecutionResult resultFromProbe:probe_get_result()];
    }
    probe_get_result()->load_ok = true;
    
    if (![self setupRuntimeWithPath:tempPath]) {
        probe_set_error("Failed to setup runtime");
        probe_set_completed(true);
        return [GuestExecutionResult resultFromProbe:probe_get_result()];
    }
    
    struct cpu_state *cpu = &current->cpu;
    uint64_t pc_before = cpu->pc;
    uint64_t sp_before = cpu->sp;
    probe_capture_pc_before(pc_before);
    probe_get_result()->sp_before = sp_before;
    
    struct tlb exec_tlb = {};
    tlb_refresh(&exec_tlb, cpu->mmu);
    
    // Disable pthread_exit for GCD compatibility - guest returns normally
    exit_should_pthread_exit = false;
    // Capture values before guest run - after guest exit, cpu state may be invalid
    a64_cpu_run_limited(cpu, (struct tlb *)&exec_tlb, 1);
    
    uint64_t pc_after = 0;
    uint64_t sp_after = 0;
    uint64_t x0_after = 0;
    uint64_t x8_after = 0;
    
    // Only capture post-exit state if current is still valid
    // After mm_release during exit, task-owned state may be invalidated
    if (current != NULL) {
        pc_after = cpu->pc;
        sp_after = cpu->sp;
        x0_after = cpu->x[0];
        x8_after = cpu->x[8];
    }
    
    probe_capture_pc_after(pc_after);
    probe_get_result()->sp_after = sp_after;
    probe_get_result()->x0_after = x0_after;
    probe_get_result()->x8_after = x8_after;
    probe_get_result()->block_executed = (pc_after != pc_before);
    
    probe_set_completed(true);
    probe_get_result()->returned_to_harness = true;
    
    return [GuestExecutionResult resultFromProbe:probe_get_result()];
}

// Lane A2: Guest exit observed externally via XCTestExpectation
// Guest runs on background queue, exit captured by trace sink, expectation fulfilled
- (GuestExecutionResult *)runFixtureToGuestExit:(NSString *)fixtureName
                                     extension:(NSString *)ext
                                         bundle:(NSBundle *)bundle
                                     expectation:(XCTestExpectation *)expectation {
    probe_reset();
    guest_execution_trace_sink_reset();
    probe_begin_fixture([[NSString stringWithFormat:@"%@.%@", fixtureName, ext] UTF8String]);
    
    NSString *tempPath = [self materializeFixture:fixtureName extension:ext bundle:bundle];
    if (!tempPath) {
        probe_set_error("Failed to materialize fixture");
        probe_set_completed(true);
        [expectation fulfill];
        return [GuestExecutionResult resultFromProbe:probe_get_result()];
    }
    probe_get_result()->load_ok = true;
    
    if (![self setupRuntimeWithPath:tempPath]) {
        probe_set_error("Failed to setup runtime");
        probe_set_completed(true);
        [expectation fulfill];
        return [GuestExecutionResult resultFromProbe:probe_get_result()];
    }
    
    struct cpu_state *cpu = &current->cpu;
    probe_capture_pc_before(cpu->pc);
    
    struct tlb exec_tlb = {};
    tlb_refresh(&exec_tlb, cpu->mmu);
    
    // Execute on background queue - trace sink captures exit event
    // When trace sink sees guest.do_exit_group.entry, it fulfills expectation
    dispatch_async(self.executionQueue, ^{
        // Disable pthread_exit for GCD compatibility - guest returns normally
        exit_should_pthread_exit = false;
        a64_cpu_run_limited(cpu, (struct tlb *)&exec_tlb, 100);
    });
    
    return [GuestExecutionResult resultFromProbe:probe_get_result()];
}

// Synchronous version for cases where execution returns
- (GuestExecutionResult *)runFixtureToGuestExitSync:(NSString *)fixtureName
                                       extension:(NSString *)ext
                                           bundle:(NSBundle *)bundle {
    probe_reset();
    // DO NOT reset trace sink here - test owns callback lifecycle
    // guest_execution_trace_sink_reset();  // <-- REMOVED: test sets callback before calling this
    probe_begin_fixture([[NSString stringWithFormat:@"%@.%@", fixtureName, ext] UTF8String]);
    
    NSString *tempPath = [self materializeFixture:fixtureName extension:ext bundle:bundle];
    if (!tempPath) {
        probe_set_error("Failed to materialize fixture");
        probe_set_completed(true);
        return [GuestExecutionResult resultFromProbe:probe_get_result()];
    }
    probe_get_result()->load_ok = true;
    
    if (![self setupRuntimeWithPath:tempPath]) {
        probe_set_error("Failed to setup runtime");
        probe_set_completed(true);
        return [GuestExecutionResult resultFromProbe:probe_get_result()];
    }
    
    struct cpu_state *cpu = &current->cpu;
    uint64_t pc_before = cpu->pc;
    probe_capture_pc_before(pc_before);
    
    struct tlb exec_tlb = {};
    tlb_refresh(&exec_tlb, cpu->mmu);
    
    // Capture value before guest run - after exit, cpu state may be invalid
    uint64_t pc_after = 0;
    
    // Disable pthread_exit for GCD compatibility - guest returns normally
    exit_should_pthread_exit = false;
    // Run with high iteration limit to reach exit syscall
    // For A2: we need to run until guest.do_exit_group.entry fires
    a64_cpu_run_limited(cpu, (struct tlb *)&exec_tlb, 10000);
    
    // Only capture post-exit state if current is still valid
    if (current != NULL) {
        pc_after = cpu->pc;
    }
    
    probe_capture_pc_after(pc_after);
    probe_set_completed(true);
    
    return [GuestExecutionResult resultFromProbe:probe_get_result()];
}

- (NSString *)materializeFixture:(NSString *)name extension:(NSString *)ext bundle:(NSBundle *)bundle {
    NSString *path = [bundle pathForResource:name ofType:ext];
    if (!path) return nil;
    
    NSData *data = [NSData dataWithContentsOfFile:path];
    if (!data || data.length == 0) return nil;
    
    NSString *tempDir = NSTemporaryDirectory();
    NSString *tempPath = [tempDir stringByAppendingPathComponent:
                          [NSString stringWithFormat:@"ixland_test_%@.%@", name, ext]];
    
    NSError *error = nil;
    if (![data writeToFile:tempPath options:NSDataWritingAtomic error:&error]) return nil;
    
    return tempPath;
}

- (BOOL)setupRuntimeWithPath:(NSString *)tempPath {
    NSString *tempDir = [tempPath stringByDeletingLastPathComponent];
    int mountErr = mount_root(&realfs, [tempDir UTF8String]);
    if (mountErr != 0 && mountErr != -16) return NO;
    
    int initErr = become_first_process();
    if (initErr != 0 && initErr != -17) return NO;
    
    NSString *fileName = [tempPath lastPathComponent];
    NSString *execPath = [@"/" stringByAppendingString:fileName];
    int execErr = do_execve([execPath UTF8String], 0, "\0", "\0");
    
    return (execErr == 0);
}

// Run executable at arbitrary path (e.g., from extracted rootfs)
// NOTE: rootPath should be the fakefs root (e.g., .../roots/default/data/)
// executablePath is the relative path within the rootfs (e.g., "bin/busybox")
//
// FIXED: Removed post-exit ownership hazard. The harness no longer captures
// cpu = &current->cpu before guest run and dereferences cpu->pc after exit.
// Instead, sink events track externally observable boundaries. Tests poll
// sink state directly to observe loader boundaries.
//
// Harness classification ladder H0-H4 captured in probe state.
- (GuestExecutionResult *)prepareExecutableAtRootPath:(NSString *)rootPath
executablePath:(NSString *)executablePath {
    probe_reset();
    probe_begin_fixture([executablePath UTF8String]);
    
    probe_get_result()->harness_entered = true;
    
    probe_get_result()->mount_root_called = true;
    int mountErr = mount_root(&realfs, [rootPath UTF8String]);
    probe_get_result()->mount_root_return_value = mountErr;
    if (mountErr != 0 && mountErr != -16) {
        probe_set_error("Failed to mount rootfs");
        probe_set_completed(true);
        return [GuestExecutionResult resultFromProbe:probe_get_result()];
    }
    
    probe_get_result()->become_first_process_called = true;
    int initErr = become_first_process();
    probe_get_result()->become_first_process_return_value = initErr;
    if (initErr != 0 && initErr != -17) {
        probe_set_error("Failed to become first process");
        probe_set_completed(true);
        return [GuestExecutionResult resultFromProbe:probe_get_result()];
    }
    
    probe_get_result()->do_execve_reached = true;
    int execErr = do_execve([executablePath UTF8String], 0, "\0", "\0");
    probe_get_result()->do_execve_called = true;
    probe_get_result()->do_execve_return_value = execErr;
    if (execErr != 0) {
        probe_set_error("Failed to execve");
        probe_set_completed(true);
        return [GuestExecutionResult resultFromProbe:probe_get_result()];
    }
    
    probe_get_result()->load_ok = true;
    return nil;
}

- (GuestExecutionResult *)classifyExecutableAtRootPath:(NSString *)rootPath
                                        executablePath:(NSString *)executablePath {
    GuestExecutionResult *earlyResult = [self prepareExecutableAtRootPath:rootPath
                                                           executablePath:executablePath];
    if (earlyResult != nil) {
        return earlyResult;
    }
    probe_set_completed(true);
    return [GuestExecutionResult resultFromProbe:probe_get_result()];
}

- (GuestExecutionResult *)runExecutableAtRootPath:(NSString *)rootPath
                                   executablePath:(NSString *)executablePath {
    GuestExecutionResult *earlyResult = [self prepareExecutableAtRootPath:rootPath
                                                           executablePath:executablePath];
    if (earlyResult != nil) {
        return earlyResult;
    }
    
    if (current == NULL) {
        probe_set_error("current is NULL before CPU run");
        probe_set_completed(true);
        return [GuestExecutionResult resultFromProbe:probe_get_result()];
    }

    struct cpu_state *cpu = &current->cpu;
    if (cpu->mmu == NULL) {
        probe_set_error("cpu->mmu is NULL before CPU run");
        probe_set_completed(true);
        return [GuestExecutionResult resultFromProbe:probe_get_result()];
    }

    uint64_t pc_before = cpu->pc;
    probe_capture_pc_before(pc_before);
    
    struct tlb exec_tlb = {};
    tlb_refresh(&exec_tlb, cpu->mmu);
    
    // Disable pthread_exit for GCD compatibility - guest returns normally
    exit_should_pthread_exit = false;
    a64_cpu_run_limited(cpu, &exec_tlb, 10000);
    
    probe_get_result()->task_exit_observed = guest_execution_trace_sink_exit_observed();
    probe_get_result()->exit_code = guest_execution_trace_sink_get_exit_code();
    probe_set_completed(true);
    
    return [GuestExecutionResult resultFromProbe:probe_get_result()];
}

@end
