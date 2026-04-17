// GuestExecutionHarness.h
// Test-owned execution harness for deterministic guest execution boundaries
// Owner: Tests/Support/GuestExecutionHarness

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, GuestBoundaryReason) {
    GuestBoundaryReasonNone = 0,
    GuestBoundaryReasonSyscall,
    GuestBoundaryReasonFault,
    GuestBoundaryReasonBlockComplete,
    GuestBoundaryReasonIterationLimit,
    GuestBoundaryReasonError
};

// Forward declarations for C types
struct cpu_state;
struct tlb;

// Structured execution result - boundary outcomes are explicit
// No vibes, only structured proof
@interface GuestExecutionResult : NSObject
@property (nonatomic, strong) NSString *fixtureName;
@property (nonatomic, assign) BOOL loadOk;
@property (nonatomic, assign) uint64_t pcBefore;
@property (nonatomic, assign) uint64_t pcAfter;
@property (nonatomic, assign) uint64_t spBefore;
@property (nonatomic, assign) uint64_t spAfter;
@property (nonatomic, assign) uint64_t x0After;
@property (nonatomic, assign) uint64_t x8After;
@property (nonatomic, assign) BOOL blockCompiled;
@property (nonatomic, assign) BOOL blockExecuted;
@property (nonatomic, assign) GuestBoundaryReason boundaryReason;
@property (nonatomic, assign) BOOL reachedSyscallBoundary;
@property (nonatomic, assign) BOOL reachedFaultBoundary;
@property (nonatomic, assign) BOOL taskExitObserved;
@property (nonatomic, assign) int exitCode;
@property (nonatomic, assign) BOOL returnedToHarness;
@property (nonatomic, assign) BOOL completed;
@property (nonatomic, strong) NSString *errorMessage;
// Harness classification ladder H0-H4
@property (nonatomic, assign) BOOL harnessEntered;
@property (nonatomic, assign) BOOL mountRootCalled;
@property (nonatomic, assign) int mountRootReturnValue;
@property (nonatomic, assign) BOOL becomeFirstProcessCalled;
@property (nonatomic, assign) int becomeFirstProcessReturnValue;
@property (nonatomic, assign) BOOL doExecveReached;
@property (nonatomic, assign) BOOL doExecveCalled;
@property (nonatomic, assign) int doExecveReturnValue;
@end

@interface GuestExecutionHarness : NSObject

@property (nonatomic, strong, readonly) dispatch_queue_t executionQueue;

// Lane A1: Execute until first externally useful boundary
// Returns structured result - does NOT rely on guest exit returning
- (GuestExecutionResult *)runFixtureUntilFirstBoundary:(NSString *)fixtureName
                                             extension:(NSString *)ext
                                               bundle:(NSBundle *)bundle;

// Lane A2: Run to deterministic guest exit, observed from outside
- (GuestExecutionResult *)runFixtureToGuestExitSync:(NSString *)fixtureName
                                       extension:(NSString *)ext
                                           bundle:(NSBundle *)bundle;

// Run executable at rootfs path
// rootPath: The fakefs root directory (e.g., .../roots/default/data/)
// executablePath: Relative path within rootfs (e.g., "bin/busybox")
- (GuestExecutionResult *)runExecutableAtRootPath:(NSString *)rootPath
                                   executablePath:(NSString *)executablePath;

// Run executable through do_execve only, without entering guest CPU execution
- (GuestExecutionResult *)classifyExecutableAtRootPath:(NSString *)rootPath
                                        executablePath:(NSString *)executablePath;

@end

// Trace sink observation API
bool guest_execution_trace_sink_exit_observed(void);

NS_ASSUME_NONNULL_END
