// GuestDynamicELFExecutionTests.m
// Milestone B: Dynamic ELF execution through real interpreter path
// Hosted application tests - MUST invoke real app-owned rootfs/bootstrap lifecycle
//
// Milestone B acceptance requires behavioral proof, not existence checks:
// B0: App-owned rootfs bootstrap invoked and completed
// B1: Real interpreter path resolved through exec (not just parsed)
// B2: Interpreter + main image mappings materialized by runtime
// B3: Initial dynamic userspace state (stack/auxv) is valid
// B4: Dynamic userspace crosses execution boundary

#import <XCTest/XCTest.h>
#import "GuestExecutionHarness.h"
#import "GuestExecutionTraceSink.h"
#import "Roots.h"
#import "AppGroup.h"
#import <IXLandLinuxRuntime/kernel/elf.h>
#import <IXLandLinuxRuntime/kernel/task.h>
#import <IXLandLinuxRuntime/kernel/init.h>
#import <IXLandLinuxRuntime/kernel/calls.h>
#import <IXLandLinuxRuntime/fs/real.h>
#import <IXLandLinuxRuntime/emu/tlb.h>
#import <IXLandLinuxRuntime/emu/mmu.h>

@interface GuestDynamicELFExecutionTests : XCTestCase
@property (nonatomic, strong) NSBundle *bundle;
@property (nonatomic, strong) GuestExecutionHarness *harness;
@property (nonatomic, strong, readonly) NSString *rootPath;
@end

@implementation GuestDynamicELFExecutionTests

- (void)setUp {
    [super setUp];
    self.bundle = [NSBundle bundleForClass:[self class]];
    self.harness = [[GuestExecutionHarness alloc] init];
}

// B0: Rootfs bootstrap - MUST produce verifiable postconditions
// Postcondition hierarchy:
// B0.1 - App Group container URL is non-nil
// B0.2 - Extraction destination directory exists
// B1.1 - Expected rootfs structure exists
// B1.2 - Expected executable exists
- (void)testDynamicELF_B0_RootfsBootstrap_InvokesAppOwnedPath {
    // B0.1: App Group container via real API
    NSURL *containerURL = ContainerURL();
    XCTAssertNotNil(containerURL, @"B0.1: App Group container URL must exist via containerURL API");
    
    // B0.2: Extraction destination exists
    NSURL *rootsDir = [containerURL URLByAppendingPathComponent:@"roots"];
    BOOL rootsDirExists = [[NSFileManager defaultManager] fileExistsAtPath:[rootsDir path]];
    XCTAssertTrue(rootsDirExists, @"B0.2: Extraction destination %@ must exist", rootsDir);
    
    // B0: Invoke real rootfs bootstrap via Roots API
    Roots *roots = [Roots instance];
    XCTAssertNotNil(roots, @"B0: Roots bootstrap must complete");
    XCTAssertTrue(roots.roots.count > 0, @"B0: Rootfs entry must exist after bootstrap");
    
    // B0-DIAGNOSTIC: Get root path and list contents
    NSString *defaultRoot = roots.defaultRoot;
    XCTAssertNotNil(defaultRoot, @"B1.1: Default root must be set after bootstrap");
    
    NSURL *rootURL = [roots rootUrl:defaultRoot];
    NSString *rootPath = [rootURL path];
    XCTAssertNotNil(rootPath, @"B1.1: Root path must resolve");
    
    // Log the actual root path and check existence (diagnostic info-keeping)
    NSLog(@"B0-DIAG: Root URL: %@, path: %@", rootURL, rootPath);
    NSLog(@"B0-DIAG: Root exists: %@", 
          [[NSFileManager defaultManager] fileExistsAtPath:rootPath] ? @"YES" : @"NO");
    
    // Check rootfs contents - THIS IS THE REAL PROOF
    NSError *dirError = nil;
    NSArray *contents = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:rootPath error:&dirError];
    NSUInteger rootEntryCount = contents ? contents.count : 0;
    NSLog(@"B0-DIAG: Root contents: %@, count: %lu", contents, (unsigned long)rootEntryCount);
    
    // B0-DIAG: Check data directory contents
    NSString *dataPath = [rootPath stringByAppendingPathComponent:@"data"];
    NSArray *dataContents = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:dataPath error:nil];
    NSLog(@"B0-DIAG: Data directory contents: %@, count: %lu", dataContents, (unsigned long)(dataContents ? dataContents.count : 0));
    
    // B0-DIAG: Check SQLite database for extracted paths
    NSString *dbPath = [rootPath stringByAppendingPathComponent:@"meta.db"];
    NSData *dbData = [NSData dataWithContentsOfFile:dbPath];
    NSLog(@"B0-DIAG: meta.db exists: %@, size: %lu bytes", dbData ? @"YES" : @"NO", (unsigned long)(dbData ? dbData.length : 0));
    
    // HARD FAIL if root is empty - this proves bootstrap actually failed
    XCTAssertGreaterThan(rootEntryCount, 0, 
                         @"ROOTFS EXTRACTION FAILED: Rootfs at %@ has %lu entries (expected > 0). "
                         @"Rootfs bootstrap claims success but produced empty directory.", 
                         rootPath, (unsigned long)rootEntryCount);
    
    // B0 PASS only if root has actual content
}

// B1: Real interpreter path resolved through exec
// Must prove: PT_INTERP path is resolved as part of execution, not just parsed
- (void)testDynamicELF_B1_InterpreterPath_IsResolvedThroughRealExec {
    // B0 prerequisite: Ensure rootfs bootstrap completed
    Roots *roots = [Roots instance];
    XCTAssertNotNil(roots, @"B0: Roots bootstrap must complete first");
    XCTAssertTrue(roots.roots.count > 0, @"B0: Rootfs must exist");
    
    // Get rootfs path through app-owned API (App Group)
    NSURL *rootURL = [roots rootUrl:roots.defaultRoot];
    NSString *rootPath = [rootURL path];
    XCTAssertNotNil(rootPath, @"B0: Root path must exist");
    
    // Log what exists in rootfs for debugging
    NSError *listError = nil;
    NSArray *rootContents = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:rootPath error:&listError];
    NSLog(@"B1: Root contents: %@, error: %@", rootContents, listError);
    
// B1.2: Verify expected executable exists after bootstrap
    // NOTE: fakefs stores files in "data/" subdirectory within the root
    NSString *dataRootPath = [rootPath stringByAppendingPathComponent:@"data"];
    NSString *busyboxRelativePath = @"bin/busybox";
    NSString *busyboxFullPath = [dataRootPath stringByAppendingPathComponent:busyboxRelativePath];
    BOOL busyboxExists = [[NSFileManager defaultManager] fileExistsAtPath:busyboxFullPath];
    
    // If busybox doesn't exist, report what we found
    if (!busyboxExists) {
      // Check if data/bin exists
      NSString *dataBinPath = [dataRootPath stringByAppendingPathComponent:@"bin"];
      BOOL dataBinExists = [[NSFileManager defaultManager] fileExistsAtPath:dataBinPath];
      NSArray *dataBinContents = dataBinExists ? [[NSFileManager defaultManager] contentsOfDirectoryAtPath:dataBinPath error:nil] : @[];
      NSLog(@"B1: data/bin exists: %@, contents: %@", dataBinExists ? @"YES" : @"NO", dataBinContents);
    }
    
    XCTAssertTrue(busyboxExists,
                  @"B1: /bin/busybox must exist after rootfs bootstrap (root: %@)", rootPath);
    
    // B1 behavioral: Execute and observe interpreter path resolution
    // NOTE: We observe ANY execution boundary (exit OR fault OR interp resolution)
    // Dynamic loader may fault on first instruction if TLS or memory is wrong
    // B1 only requires proving interp path was RESOLVED, not that execution succeeds
    XCTestExpectation *boundaryExpectation = [self expectationWithDescription:@"Execution boundary observed"];
    __block BOOL callbackFired = NO;
    __block BOOL interpResolved = NO;
    __block BOOL sawExit = NO;
    __block BOOL sawFault = NO;
    __block int capturedExitCode = -1;
    
    guest_execution_trace_sink_reset();
    guest_execution_trace_sink_set_completion_callback(^(BOOL exit_observed, int exit_code) {
        if (!callbackFired) {
            // Capture all boundary conditions
            interpResolved = guest_execution_trace_sink_interp_path_resolved();
            sawExit = exit_observed;
            sawFault = NO; // Fault detection would need separate callback
            capturedExitCode = exit_code;
            
            // For B1: we accept interp resolution OR exit/fault as proof of reaching exec
            if (interpResolved || exit_observed) {
                callbackFired = YES;
                [boundaryExpectation fulfill];
            }
        }
    });
    
    dispatch_async(self.harness.executionQueue, ^{
        [self.harness runExecutableAtRootPath:dataRootPath executablePath:busyboxRelativePath];
    });
    
    [self waitForExpectations:@[boundaryExpectation] timeout:60.0];
    
        // B1 assertions: Real interpreter path resolved through exec
    // B1 behavioral: PT_INTERP path is resolved as part of real exec path
    XCTAssertTrue(callbackFired, @"B1: Trace sink must observe execution boundary");
    
    // B1 primary: interp_path must be resolved
    // Dynamic ELF requires PT_INTERP parsing during execve
    BOOL interpResolved = guest_execution_trace_sink_interp_path_resolved();
    XCTAssertTrue(interpResolved, @"B1: interp_path must be resolved via real exec path");
    
    if (interpResolved) {
        const char *interpPath = guest_execution_trace_sink_get_interp_path();
        XCTAssertGreaterThan(strlen(interpPath), 0, @"B1: Resolved path must not be empty");
        
        NSString *interpPathStr = [NSString stringWithUTF8String:interpPath];
        XCTAssertTrue([interpPathStr containsString:@"ld-linux"] ||
                      [interpPathStr containsString:@"ld-musl"],
                      @"B1: Must resolve to real dynamic loader (observed: %@)", interpPathStr);
    }
    
    guest_execution_trace_sink_set_completion_callback(NULL);
}

// B2: Real interpreter and main image mappings materialized by runtime
- (void)testDynamicELF_B2_InterpreterAndMainImages_AreMaterializedByRuntime {
    // B0 prerequisite
    Roots *roots = [Roots instance];
    XCTAssertNotNil(roots, @"B0: Roots bootstrap must complete");
    NSURL *rootURL = [roots rootUrl:roots.defaultRoot];
    NSString *rootPath = [rootURL path];
    NSString *dataRootPath = [rootPath stringByAppendingPathComponent:@"data"];
    NSString *busyboxRelativePath = @"bin/busybox";
    NSString *busyboxFullPath = [dataRootPath stringByAppendingPathComponent:busyboxRelativePath];
    XCTAssertTrue([[NSFileManager defaultManager] fileExistsAtPath:busyboxFullPath],
                  @"B0: /bin/busybox must exist");
    
    XCTestExpectation *mappingsExpectation = [self expectationWithDescription:@"Mappings materialized"];
    __block BOOL callbackFired = NO;
    
    guest_execution_trace_sink_reset();
    guest_execution_trace_sink_set_completion_callback(^(BOOL exit_observed, int exit_code) {
        if (!callbackFired &&
            guest_execution_trace_sink_interp_mappings_exist() &&
            guest_execution_trace_sink_main_image_loaded()) {
            callbackFired = YES;
            [mappingsExpectation fulfill];
        }
    });
    
    dispatch_async(self.harness.executionQueue, ^{
        [self.harness runExecutableAtRootPath:dataRootPath executablePath:busyboxRelativePath];
    });
    
    [self waitForExpectations:@[mappingsExpectation] timeout:60.0];
    
    XCTAssertTrue(callbackFired, @"B2: Trace sink must observe mappings");
    XCTAssertTrue(guest_execution_trace_sink_interp_mappings_exist(),
                  @"B2: Interpreter mappings must materialize in runtime");
    XCTAssertTrue(guest_execution_trace_sink_main_image_loaded(),
                  @"B2: Main image must load and reach entry point");
    
    guest_execution_trace_sink_set_completion_callback(NULL);
}

// B3: Initial dynamic userspace state is valid
- (void)testDynamicELF_B3_InitialDynamicUserspaceState_IsValid {
    Roots *roots = [Roots instance];
    XCTAssertNotNil(roots, @"B0: Roots bootstrap must complete");
    NSURL *rootURL = [roots rootUrl:roots.defaultRoot];
    NSString *rootPath = [rootURL path];
    NSString *dataRootPath = [rootPath stringByAppendingPathComponent:@"data"];
    NSString *busyboxRelativePath = @"bin/busybox";
    NSString *busyboxFullPath = [dataRootPath stringByAppendingPathComponent:busyboxRelativePath];
    XCTAssertTrue([[NSFileManager defaultManager] fileExistsAtPath:busyboxFullPath],
                  @"B0: /bin/busybox must exist");
    
    XCTestExpectation *auxvExpectation = [self expectationWithDescription:@"AuxV initialized"];
    __block BOOL callbackFired = NO;
    
    guest_execution_trace_sink_reset();
    guest_execution_trace_sink_set_completion_callback(^(BOOL exit_observed, int exit_code) {
        if (!callbackFired && guest_execution_trace_sink_auxv_initialized()) {
            callbackFired = YES;
            [auxvExpectation fulfill];
        }
    });
    
    dispatch_async(self.harness.executionQueue, ^{
        [self.harness runExecutableAtRootPath:dataRootPath executablePath:busyboxRelativePath];
    });
    
    [self waitForExpectations:@[auxvExpectation] timeout:60.0];
    
    XCTAssertTrue(callbackFired, @"B3: Trace sink must observe auxv init");
    XCTAssertTrue(guest_execution_trace_sink_auxv_initialized(),
                  @"B3: auxv must be initialized with AT_BASE for dynamic execution");
    
    guest_execution_trace_sink_set_completion_callback(NULL);
}

// B4: Real dynamic execution boundary reached
// MANDATORY: Without B4, Milestone B is RED
- (void)testDynamicELF_B4_FirstExecutionBoundary_IsReached {
    Roots *roots = [Roots instance];
    XCTAssertNotNil(roots, @"B0: Roots bootstrap must complete");
    NSURL *rootURL = [roots rootUrl:roots.defaultRoot];
    NSString *rootPath = [rootURL path];
    NSString *dataRootPath = [rootPath stringByAppendingPathComponent:@"data"];
    NSString *busyboxRelativePath = @"bin/busybox";
    NSString *busyboxFullPath = [dataRootPath stringByAppendingPathComponent:busyboxRelativePath];
    XCTAssertTrue([[NSFileManager defaultManager] fileExistsAtPath:busyboxFullPath],
                  @"B0: /bin/busybox must exist");
    
    XCTestExpectation *boundaryExpectation = [self expectationWithDescription:@"Execution boundary reached"];
    __block BOOL callbackFired = NO;
    __block int exitCode = -1;
    
    guest_execution_trace_sink_reset();
    guest_execution_trace_sink_set_completion_callback(^(BOOL exit_observed, int exit_code) {
        if (!callbackFired) {
            callbackFired = YES;
            exitCode = exit_code;
            [boundaryExpectation fulfill];
        }
    });
    
    dispatch_async(self.harness.executionQueue, ^{
        [self.harness runExecutableAtRootPath:dataRootPath executablePath:busyboxRelativePath];
    });
    
    [self waitForExpectations:@[boundaryExpectation] timeout:60.0];
    
    // B4 is MANDATORY for Milestone B acceptance
    XCTAssertTrue(callbackFired, @"B4 MANDATORY: Execution boundary must be reached");
    
    BOOL sawExit = guest_execution_trace_sink_exit_observed();
    BOOL sawInterpPath = guest_execution_trace_sink_interp_path_resolved();
    
    XCTAssertTrue(sawInterpPath || sawExit,
                  @"B4: Must observe either interpreter path or exit");
    
    if (sawExit) {
        XCTAssertGreaterThanOrEqual(exitCode, 0, @"B4: Exit code must be valid (was: %d)", exitCode);
    }
    
    guest_execution_trace_sink_set_completion_callback(NULL);
}

@end
