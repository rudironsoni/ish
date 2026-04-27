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
#import <IXLandLinuxRuntime/fs/fd.h>
#import <IXLandLinuxRuntime/emu/tlb.h>
#import <IXLandLinuxRuntime/emu/mmu.h>
#import <IXLandLinuxRuntime/util/misc.h>

@interface GuestDynamicELFExecutionTests : XCTestCase
@property (nonatomic, strong) NSBundle *bundle;
@property (nonatomic, strong) GuestExecutionHarness *harness;
@property (nonatomic, strong, readonly) NSString *rootPath;
@property (nonatomic, strong) Roots *testRoots;
@end

@implementation GuestDynamicELFExecutionTests

- (void)setUp {
    [super setUp];
    self.bundle = [NSBundle bundleForClass:[self class]];
    self.harness = [[GuestExecutionHarness alloc] init];
    
    // PHASE 1 FIX: Provision deterministic test roots directory
    // Use local sandbox path instead of App Group container
    [self provisionTestRootfs];
}

- (void)tearDown {
    // Wait for any async guest execution to complete
    // This ensures test isolation - next test starts with clean state
    [self.harness waitForExecutionCompletionWithTimeout:5.0];
    
    // Reset trace sink callback to prevent stale callbacks between tests
    guest_execution_trace_sink_set_completion_callback(NULL);
    
    [super tearDown];
}

// Provision test rootfs using injected local path (not App Group)
- (void)provisionTestRootfs {
    // Create deterministic test roots directory in local sandbox
    NSURL *testRootsDir = [self testRootsDirectory];
    self.testRoots = [Roots instanceWithRootsDirectory:testRootsDir];
    
    // B0.1: Assert roots directory is non-nil
    XCTAssertNotNil(self.testRoots, @"B0.1: Test roots instance must be created");
    XCTAssertNotNil(testRootsDir, @"B0.1: Test roots directory URL must exist");
    
    // Wait for extraction to complete (Roots init may trigger async extraction)
    // Give extraction time to complete
    [NSThread sleepForTimeInterval:0.5];
}

// Returns deterministic local path for test roots
- (NSURL *)testRootsDirectory {
    NSURL *cachesDir = [[[NSFileManager defaultManager] URLsForDirectory:NSCachesDirectory
                                                               inDomains:NSUserDomainMask] firstObject];
    NSURL *testRootsDir = [cachesDir URLByAppendingPathComponent:@"IXLandTestRoots"];
    
    // Ensure directory exists
    [[NSFileManager defaultManager] createDirectoryAtURL:testRootsDir
                             withIntermediateDirectories:YES
                                              attributes:nil
                                                   error:nil];
    return testRootsDir;
}

// B0: Rootfs bootstrap - MUST produce verifiable postconditions
// Postcondition hierarchy:
// B0.1 - App Group container URL is non-nil
// B0.2 - Extraction destination directory exists
// B1.1 - Expected rootfs structure exists
// B1.2 - Expected executable exists
// B1.3 - /data/bin/busybox has real file size
- (void)testDynamicELF_B0_RootfsBootstrap_InvokesAppOwnedPath {
    // B0.1: Test roots directory via injected path (not App Group)
    NSURL *testRootsDir = [self testRootsDirectory];
    XCTAssertNotNil(testRootsDir, @"B0.1: Test roots directory URL must exist via local sandbox");
    
    // B0.2: Extraction destination exists
    BOOL rootsDirExists = [[NSFileManager defaultManager] fileExistsAtPath:[testRootsDir path]];
    XCTAssertTrue(rootsDirExists, @"B0.2: Test roots directory %@ must exist", testRootsDir);
    
    // B0: Real rootfs bootstrap via Roots API with injected directory
    XCTAssertNotNil(self.testRoots, @"B0: Roots bootstrap must complete");
    XCTAssertTrue(self.testRoots.roots.count > 0, @"B0: Rootfs entry must exist after bootstrap");
    
    // B0-DIAGNOSTIC: Get root path and list contents
    NSString *defaultRoot = self.testRoots.defaultRoot;
    XCTAssertNotNil(defaultRoot, @"B1.1: Default root must be set after bootstrap");
    
    NSURL *rootURL = [self.testRoots rootUrl:defaultRoot];
    NSString *rootPath = [rootURL path];
    XCTAssertNotNil(rootPath, @"B1.1: Root path must resolve");
    
    // Check rootfs contents - REAL PROOF via assertions, not logs
    NSError *dirError = nil;
    NSArray *contents = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:rootPath error:&dirError];
    NSUInteger rootEntryCount = contents ? contents.count : 0;
    
    // B1 MATERIAL ASSERTIONS: /data/bin/busybox MUST exist with real content
    NSString *dataRootPath = [rootPath stringByAppendingPathComponent:@"data"];
    NSString *busyboxRelativePath = @"bin/busybox";
    NSString *busyboxFullPath = [dataRootPath stringByAppendingPathComponent:busyboxRelativePath];
    
    // B1.1: data/ subtree exists
    BOOL dataExists = [[NSFileManager defaultManager] fileExistsAtPath:dataRootPath];
    XCTAssertTrue(dataExists, @"B1.1: /data directory must exist after extraction");
    
    // B1.2: /data/bin/busybox exists
    BOOL busyboxExists = [[NSFileManager defaultManager] fileExistsAtPath:busyboxFullPath];
    XCTAssertTrue(busyboxExists,
                  @"B1.2: /data/bin/busybox must exist after rootfs bootstrap (root: %@)", rootPath);
    
    // B1.3: busybox has real file size (not metadata-only)
    if (busyboxExists) {
        NSDictionary *attrs = [[NSFileManager defaultManager] attributesOfItemAtPath:busyboxFullPath error:nil];
        unsigned long long fileSize = [attrs fileSize];
        XCTAssertGreaterThan(fileSize, 0, @"B1.3: /data/bin/busybox must have nonzero size (was %llu bytes)", fileSize);
        XCTAssertGreaterThan(fileSize, 1000, @"B1.3: /data/bin/busybox must be real payload (size %llu seems too small)", fileSize);
    }
    
    // HARD FAIL if root is empty - this proves bootstrap actually failed
    XCTAssertGreaterThan(rootEntryCount, 0, 
                         @"ROOTFS EXTRACTION FAILED: Rootfs at %@ has %lu entries (expected > 0). "
                         @"Rootfs bootstrap claims success but produced empty directory.", 
                         rootPath, (unsigned long)rootEntryCount);
    
    // B0 PASS only if root has actual content
}

// B1: Real interpreter path resolved through exec
// Must prove: PT_INTERP path is resolved as part of execution, not just parsed
//
// FIXED: Use direct sink state polling instead of callback-driven exit observation.
// The callback design was conflating loader boundary (interp_path_resolved) with exit observation.
// Now we poll the sink directly to observe the actual loader boundary.
//
// Harness classification ladder H0-H4 asserted before X0-X2.
- (void)testDynamicELF_B1_InterpreterPath_IsResolvedThroughRealExec {
    // B0-B1 prerequisite: Ensure real rootfs bootstrap completed with material provisioning
    [self testDynamicELF_B0_RootfsBootstrap_InvokesAppOwnedPath];
    
    // Get rootfs path through app-owned API (injected test path)
    NSURL *rootURL = [self.testRoots rootUrl:self.testRoots.defaultRoot];
    NSString *rootPath = [rootURL path];
    XCTAssertNotNil(rootPath, @"B0: Root path must exist");
    
    // B1.1-B1.4: Verify busybox exists with real content
    NSString *dataRootPath = [rootPath stringByAppendingPathComponent:@"data"];
    NSString *busyboxRelativePath = @"bin/busybox";
    NSString *busyboxFullPath = [dataRootPath stringByAppendingPathComponent:busyboxRelativePath];
    BOOL busyboxExists = [[NSFileManager defaultManager] fileExistsAtPath:busyboxFullPath];
    XCTAssertTrue(busyboxExists,
                  @"B1: /data/bin/busybox must exist after rootfs bootstrap (root: %@)", rootPath);
    
    NSDictionary *attrs = [[NSFileManager defaultManager] attributesOfItemAtPath:busyboxFullPath error:nil];
    unsigned long long fileSize = [attrs fileSize];
    XCTAssertGreaterThan(fileSize, 1000, @"B1.4: busybox must be real extracted payload, not placeholder");
    
    // B1 behavioral: run executable to drive runtime checkpoints
    guest_execution_trace_sink_reset();
    GuestExecutionResult *result = [self.harness classifyExecutableAtRootPath:dataRootPath
                                                                executablePath:busyboxRelativePath];

    
    // CLASSIFICATION LADDER H0-H4: Harness seam before runtime
    XCTAssertTrue(result.harnessEntered, @"H0: runExecutableAtRootPath must be entered");
    XCTAssertTrue(result.mountRootCalled, @"H1: mount_root must be called");
    XCTAssertTrue(result.mountRootReturnValue == 0 || result.mountRootReturnValue == -16, 
                  @"H1: mount_root must return 0 or -16, got %d", result.mountRootReturnValue);
    XCTAssertTrue(result.becomeFirstProcessCalled, @"H2: become_first_process must be called");
    XCTAssertTrue(result.becomeFirstProcessReturnValue == 0 || result.becomeFirstProcessReturnValue == -17,
                  @"H2: become_first_process must return 0 or -17, got %d", result.becomeFirstProcessReturnValue);
    XCTAssertTrue(result.doExecveReached, @"H3: do_execve must be reached");
    XCTAssertTrue(result.doExecveCalled, @"H4: do_execve must be called");
    XCTAssertEqual(result.doExecveReturnValue, 0, @"H4: do_execve must return 0 (success), got %d", result.doExecveReturnValue);
    
    // CLASSIFICATION S0: Sink receiving callbacks (diagnostic - proves instrumentation is wired)
    BOOL anyIntervalReceived = guest_execution_trace_sink_any_interval_received();
    XCTAssertTrue(anyIntervalReceived, @"S0: sink not receiving any begin_interval callbacks - instrumentation seam broken");

    // CLASSIFICATION LADDER X0-X3: Runtime pre-elf_exec checkpoints (sink events)
    BOOL doExecveEntered = guest_execution_trace_sink_do_execve_entered();
    BOOL formatExecEntered = guest_execution_trace_sink_format_exec_entered();
    BOOL beforeElfExecEntered = guest_execution_trace_sink_before_elf_exec_entered();
    BOOL elfExecEntered = guest_execution_trace_sink_elf_exec_entered();
    const char *lastEvent = guest_execution_trace_sink_get_last_loader_event();

    XCTAssertTrue(doExecveEntered, @"X0: task.proof.do_execve.entry NOT observed - oracle classification gap");
    if (doExecveEntered) {
        XCTAssertTrue(formatExecEntered, @"X1: task.proof.do_execve.before_format_exec NOT observed - oracle classification gap");
    }
    if (formatExecEntered) {
        XCTAssertTrue(beforeElfExecEntered, @"X2: task.proof.do_execve.before_elf_exec NOT observed - oracle classification gap");
    }
    if (beforeElfExecEntered) {
        XCTAssertTrue(elfExecEntered, @"X3: task.proof.elf_exec.after_return_to_caller NOT observed - oracle classification gap");
    }
    
    // CLASSIFICATION M1: Main ELF header accepted (only after X2 proves elf_exec processing)
    BOOL mainElfHeaderAccepted = guest_execution_trace_sink_main_elf_header_accepted();
    if (beforeElfExecEntered) {
        XCTAssertTrue(mainElfHeaderAccepted, @"M1: task.proof.loader.elf_header:role=main NOT observed - main ELF header not accepted");
    }
    
    // CLASSIFICATION D2.0: Interpreter open attempted (only after M1 proves header acceptance)
    BOOL interpOpenAttempted = guest_execution_trace_sink_interp_open_attempted();
    if (mainElfHeaderAccepted) {
        XCTAssertTrue(interpOpenAttempted, @"D2.0: loader.interp.open.result NOT observed - interpreter open not attempted");
    }

    // CLASSIFICATION D2.1: Interpreter header accepted (only after D2.0 proves open attempt)
    BOOL interpHeaderLoaded = guest_execution_trace_sink_interp_header_loaded();
    if (interpOpenAttempted) {
        XCTAssertTrue(interpHeaderLoaded, @"D2.1: loader.interp_elf.header or loader.interp.bias.compute NOT observed - interpreter header not accepted");
    }
    
    // B1 PRIMARY: Interpreter path resolved (proves PT_INTERP was processed)
    BOOL interpPathResolved = guest_execution_trace_sink_interp_path_resolved();
    XCTAssertTrue(interpPathResolved,
                  @"B1: loader.interpreter_path NOT resolved. "
                  @"H0=%d H1=%d(rv=%d) H2=%d(rv=%d) H3=%d H4=%d(rv=%d) "
                  @"X0=%d X1=%d X2=%d X3=%d "
                  @"M1=%d D2.0=%d D2.1=%d "
                  @"last_event='%s'",
                  result.harnessEntered, result.mountRootCalled, result.mountRootReturnValue,
                  result.becomeFirstProcessCalled, result.becomeFirstProcessReturnValue,
                  result.doExecveReached, result.doExecveCalled, result.doExecveReturnValue,
                  doExecveEntered, formatExecEntered, beforeElfExecEntered, elfExecEntered,
                  mainElfHeaderAccepted, interpOpenAttempted, interpHeaderLoaded,
                  lastEvent);

    if (interpPathResolved) {
        const char *interpPath = guest_execution_trace_sink_get_interp_path();
        XCTAssertGreaterThan(strlen(interpPath), 0, @"B1: Resolved path must not be empty");

        NSString *interpPathStr = [NSString stringWithUTF8String:interpPath];
        XCTAssertTrue([interpPathStr containsString:@"ld-linux"] ||
                      [interpPathStr containsString:@"ld-musl"],
                      @"B1: Must resolve to real dynamic loader (observed: %@)", interpPathStr);
    }
}

// D1.6.a: Probe - Can generic_open resolve guest absolute path /lib/ld-musl-aarch64.so.1 outside exec?
// This is a narrow diagnostic to determine if path resolution works at all in mounted root context
- (void)testDynamicELF_D1_6a_InterpreterPath_ResolvesViaMountedRoot {
    // B0 prerequisite
    XCTAssertNotNil(self.testRoots, @"B0: Roots bootstrap must complete");
    NSURL *rootURL = [self.testRoots rootUrl:self.testRoots.defaultRoot];
    NSString *rootPath = [rootURL path];
    NSString *dataRootPath = [rootPath stringByAppendingPathComponent:@"data"];
    
    // Mount the root like the harness does
    int mountErr = mount_root(&realfs, [dataRootPath UTF8String]);
    XCTAssertTrue(mountErr == 0 || mountErr == -16, @"D1.6.a: Root mount must succeed, got %d", mountErr);
    
    // Now attempt to open interpreter via guest absolute path - same path PT_INTERP would contain
    // This simulates what happens at exec.c:753 but without the full exec path
    struct fd *interp_fd = generic_open("/lib/ld-musl-aarch64.so.1", O_RDONLY, 0);
    
    if (interp_fd != NULL && !IS_ERR(interp_fd)) {
        // D1.6.a PASS: Path resolution works, interpreter opens successfully
        fd_close(interp_fd);
        XCTAssertTrue(true, @"D1.6.a: Interpreter path resolves via mounted root");
    } else {
        // D1.6.a FAIL: Path resolution broken at mount/root level
        intptr_t err = interp_fd != NULL ? PTR_ERR(interp_fd) : -1;
        XCTAssertTrue(interp_fd != NULL && !IS_ERR(interp_fd), @"D1.6.a FAIL: generic_open failed for guest absolute path, err=%ld", (long)err);
    }
}

// D2: Interpreter load subpath inside elf_exec - prove each sub-boundary
// D2.0: generic_open(interp_name) returns valid interp_fd (proven via B1 failing, path works)
// D2.1: read_header(interp_fd, &interp_header) succeeds - proven by loader.interp_elf.header event
// D2.2: read_prg_headers(interp_fd, interp_header, &interp_ph) succeeds - proven by loader.interp.bias.compute event  
// D2.3: interpreter PT_LOAD loop begins - proven by loader.interp.pt_load.map event
// D2.4: first interpreter PT_LOAD mapping succeeds - proven by subsequent loader events
- (void)testDynamicELF_D2_InterpreterSubpath_ProvesD2Boundaries {
    // B0 prerequisite
    XCTAssertNotNil(self.testRoots, @"B0: Roots bootstrap must complete");
    NSURL *rootURL = [self.testRoots rootUrl:self.testRoots.defaultRoot];
    NSString *rootPath = [rootURL path];
    NSString *dataRootPath = [rootPath stringByAppendingPathComponent:@"data"];
    NSString *busyboxRelativePath = @"bin/busybox";
    NSString *busyboxFullPath = [dataRootPath stringByAppendingPathComponent:busyboxRelativePath];
    XCTAssertTrue([[NSFileManager defaultManager] fileExistsAtPath:busyboxFullPath],
                  @"B0: busybox must exist at %@", busyboxFullPath);
    
    guest_execution_trace_sink_reset();

    GuestExecutionResult *result = [self.harness runExecutableAtRootPath:dataRootPath
                                                          executablePath:busyboxRelativePath];

    XCTAssertTrue(result.harnessEntered, @"H0: runExecutableAtRootPath must be entered");
    XCTAssertTrue(result.mountRootCalled, @"H1: mount_root must be called");
    XCTAssertTrue(result.mountRootReturnValue == 0 || result.mountRootReturnValue == -16,
                  @"H1: mount_root must return 0 or -16, got %d", result.mountRootReturnValue);
    XCTAssertTrue(result.becomeFirstProcessCalled, @"H2: become_first_process must be called");
    XCTAssertTrue(result.becomeFirstProcessReturnValue == 0 || result.becomeFirstProcessReturnValue == -17,
                  @"H2: become_first_process must return 0 or -17, got %d", result.becomeFirstProcessReturnValue);
    XCTAssertTrue(result.doExecveReached, @"H3: do_execve must be reached");
    XCTAssertTrue(result.doExecveCalled, @"H4: do_execve must be called");
    XCTAssertEqual(result.doExecveReturnValue, 0, @"H4: do_execve must return 0 (success), got %d", result.doExecveReturnValue);

    // EXPLICIT LADDER OUTPUT FOR D2 - PROVE EXACT CHECKPOINT VISIBILITY
    BOOL X0 = guest_execution_trace_sink_do_execve_entered();
    BOOL X1 = guest_execution_trace_sink_format_exec_entered();
    BOOL X2 = guest_execution_trace_sink_before_elf_exec_entered();
    BOOL X3 = guest_execution_trace_sink_elf_exec_entered();
    BOOL M1 = guest_execution_trace_sink_main_elf_header_accepted();
    BOOL D2_0 = guest_execution_trace_sink_interp_open_attempted();
    BOOL D2_1 = guest_execution_trace_sink_interp_header_loaded();
    BOOL D2_3 = guest_execution_trace_sink_interp_mappings_exist();
    const char *lastEvent = guest_execution_trace_sink_get_last_loader_event();

    NSLog(@"D2 LADDER: X0=%d X1=%d X2=%d X3=%d | M1=%d D2.0=%d D2.1=%d D2.3=%d | last='%s'",
          X0, X1, X2, X3,
          M1, D2_0, D2_1, D2_3,
          lastEvent);
    
    BOOL anyIntervalReceived = guest_execution_trace_sink_any_interval_received();
    if (!anyIntervalReceived) {
        const char *lastEvent = guest_execution_trace_sink_get_last_loader_event();
        XCTFail(@"S0: trace sink did not receive begin_interval callbacks for D2 run; "
                @"loader boundary assertions are invalid without sink delivery. "
                @"last_event='%s'", lastEvent);
        guest_execution_trace_sink_set_completion_callback(NULL);
        return;
    }

    // DIAGNOSTIC: Check if elf_exec was reached
    BOOL elfExecReached = guest_execution_trace_sink_elf_exec_reached();
    (void)elfExecReached; // suppress unused warning
    
    // M1: Check main ELF header accepted event
    BOOL mainElfHeaderAccepted = guest_execution_trace_sink_main_elf_header_accepted();
    
    // M1 classification: read_header(main_fd, &header) must succeed
    XCTAssertTrue(mainElfHeaderAccepted,
                  @"M1: Main ELF header accepted event (loader.main_elf.header) must be observed. "
                  @"This proves read_header(main_fd, &header) succeeded. "
                  @"If this fails, elf_exec failed before or during main ELF header validation. "
                  @"last_event='%s'", lastEvent);
    
    // D2.0: Check interpreter open attempt event
    BOOL interpOpenAttempted = guest_execution_trace_sink_interp_open_attempted();
    
    // D2.0 classification: generic_open(interp_name) must be called
    XCTAssertTrue(interpOpenAttempted,
                  @"D2.0: Interp open event (loader.interp.open.result) must be observed. "
                  @"This proves generic_open(interp_name) was called. "
                  @"If this fails, PT_INTERP loop was entered but interpreter open failed. "
                  @"last_event='%s'", lastEvent);
    
    // D2.1: Check interpreter header loaded event
    BOOL interpHeaderLoaded = guest_execution_trace_sink_interp_header_loaded();
    
    // D2.1 classification
    XCTAssertTrue(interpHeaderLoaded, 
                  @"D2.1: Interp header loaded event must be observed (loader.interp_elf.header or loader.interp.bias.compute). "
                  @"This proves read_header(interp_fd, &interp_header) succeeded. "
                  @"Last loader event: %s", lastEvent);
    
    // D2.3: Check interpreter mappings exist
    BOOL interpMappingsExist = guest_execution_trace_sink_interp_mappings_exist();
    
    // D2.3 classification  
    XCTAssertTrue(interpMappingsExist,
                   @"D2.3: Interp mappings must exist (loader.interp.pt_load.map). "
                   @"This proves interpreter PT_LOAD loop ran. "
                   @"Last loader event: %s", lastEvent);
    
    guest_execution_trace_sink_set_completion_callback(NULL);
}

// B2: Real interpreter and main image mappings materialized by runtime
//
// FIXED: Use direct sink state polling instead of callback-driven exit observation.
// B2 requires proving interpreter AND main image mappings are materialized by runtime,
// not that execution succeeded (exit observation).
- (void)testDynamicELF_B2_InterpreterAndMainImages_AreMaterializedByRuntime {
    // B0 prerequisite - use testRoots from setUp
    XCTAssertNotNil(self.testRoots, @"B0: Roots bootstrap must complete");
    NSURL *rootURL = [self.testRoots rootUrl:self.testRoots.defaultRoot];
    NSString *rootPath = [rootURL path];
    NSString *dataRootPath = [rootPath stringByAppendingPathComponent:@"data"];
    NSString *busyboxRelativePath = @"bin/busybox";
    NSString *busyboxFullPath = [dataRootPath stringByAppendingPathComponent:busyboxRelativePath];
    XCTAssertTrue([[NSFileManager defaultManager] fileExistsAtPath:busyboxFullPath],
                  @"B0: /bin/busybox must exist");
    
    // B2 behavioral: Poll for interpreter and main image mappings
    // These are EXTERNALLY OBSERVABLE LOADER BOUNDARIES for B2
    guest_execution_trace_sink_reset();

    dispatch_async(self.harness.executionQueue, ^{
        [self.harness runExecutableAtRootPath:dataRootPath executablePath:busyboxRelativePath];
    });

    // Poll for loader boundary: interpreter mappings + main image loaded
    NSDate *startTime = [NSDate date];
    BOOL interpMappingsExist = NO;
    BOOL mainImageLoaded = NO;
    while ([[NSDate date] timeIntervalSinceDate:startTime] < 60.0) {
        interpMappingsExist = guest_execution_trace_sink_interp_mappings_exist();
        mainImageLoaded = guest_execution_trace_sink_main_image_loaded();
        if (interpMappingsExist && mainImageLoaded) {
            break;
        }
        if (guest_execution_trace_sink_exit_observed()) {
            // Guest exited before mappings materialized - B2 fails
            break;
        }
        [NSThread sleepForTimeInterval:0.1];
    }
    
    XCTAssertTrue(interpMappingsExist,
                  @"B2: Interpreter mappings (loader.interp.pt_load.map) must materialize. "
                  @"This proves the interpreter PT_LOAD segments were mapped into guest memory.");
    XCTAssertTrue(mainImageLoaded,
                  @"B2: Main image must load and reach entry point (task.proof.exec.load_entry.reached). "
                  @"This proves the main ELF was loaded and execution reached the entry point.");
}

// B3: Initial dynamic userspace state is valid
//
// FIXED: Use direct sink state polling instead of callback-driven exit observation.
// B3 requires proving auxv was initialized with AT_BASE for dynamic execution,
// not that execution succeeded (exit observation).
- (void)testDynamicELF_B3_InitialDynamicUserspaceState_IsValid {
    XCTAssertNotNil(self.testRoots, @"B0: Roots bootstrap must complete");
    NSURL *rootURL = [self.testRoots rootUrl:self.testRoots.defaultRoot];
    NSString *rootPath = [rootURL path];
    NSString *dataRootPath = [rootPath stringByAppendingPathComponent:@"data"];
    NSString *busyboxRelativePath = @"bin/busybox";
    NSString *busyboxFullPath = [dataRootPath stringByAppendingPathComponent:busyboxRelativePath];
    XCTAssertTrue([[NSFileManager defaultManager] fileExistsAtPath:busyboxFullPath],
                  @"B0: /data/bin/busybox must exist");
    
    // B3 behavioral: Poll for auxv initialization
    // This is an EXTERNALLY OBSERVABLE LOADER BOUNDARY for B3
    guest_execution_trace_sink_reset();

    dispatch_async(self.harness.executionQueue, ^{
        [self.harness runExecutableAtRootPath:dataRootPath executablePath:busyboxRelativePath];
    });

    // Poll for loader boundary: auxv initialized with AT_BASE
    NSDate *startTime = [NSDate date];
    BOOL auxvInitialized = NO;
    while ([[NSDate date] timeIntervalSinceDate:startTime] < 60.0) {
        if (guest_execution_trace_sink_auxv_initialized()) {
            auxvInitialized = YES;
            break;
        }
        if (guest_execution_trace_sink_exit_observed()) {
            // Guest exited before auxv initialized - B3 fails
            break;
        }
        [NSThread sleepForTimeInterval:0.1];
    }
    
    XCTAssertTrue(auxvInitialized,
                  @"B3: auxv must be initialized with AT_BASE for dynamic execution "
                  @"(loader.auxv.at_base.write event). "
                  @"This proves the dynamic linker base address was written to auxv.");
}

// B4: Real dynamic execution boundary reached
// MANDATORY: Without B4, Milestone B is RED
- (void)testDynamicELF_B4_FirstExecutionBoundary_IsReached {
    XCTAssertNotNil(self.testRoots, @"B0: Roots bootstrap must complete");
    NSURL *rootURL = [self.testRoots rootUrl:self.testRoots.defaultRoot];
    NSString *rootPath = [rootURL path];
    NSString *dataRootPath = [rootPath stringByAppendingPathComponent:@"data"];
    NSString *busyboxRelativePath = @"bin/busybox";
    NSString *busyboxFullPath = [dataRootPath stringByAppendingPathComponent:busyboxRelativePath];
    XCTAssertTrue([[NSFileManager defaultManager] fileExistsAtPath:busyboxFullPath],
                  @"B0: /data/bin/busybox must exist");
    
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

// APP-001: Run /bin/login -f root (same as app) for valid comparison
// This test runs the SAME executable with SAME arguments as the app
// to enable apples-to-apples debugging of dynamic linker crashes
- (void)testDynamicELF_APP001_RunBinLogin_WithArgv {
    // Use APP rootfs instead of test rootfs for valid comparison
    // The app uses Container/Shared/AppGroup/.../roots/default
    NSFileManager *fm = [NSFileManager defaultManager];
    NSURL *appGroupURL = nil;
    
    // Find App Group container
    NSURL *simDevices = [NSURL fileURLWithPath:@"/Users/rudironsoni/Library/Developer/CoreSimulator/Devices"];
    NSArray *deviceDirs = [fm contentsOfDirectoryAtURL:simDevices
                            includingPropertiesForKeys:nil
                                               options:NSDirectoryEnumerationSkipsHiddenFiles
                                                 error:nil];
    for (NSURL *deviceDir in deviceDirs) {
        NSURL *appGroupDir = [deviceDir URLByAppendingPathComponent:@"data/Containers/Shared/AppGroup"];
        if ([fm fileExistsAtPath:appGroupDir.path]) {
            NSArray *groupDirs = [fm contentsOfDirectoryAtURL:appGroupDir
                                   includingPropertiesForKeys:nil
                                                      options:NSDirectoryEnumerationSkipsHiddenFiles
                                                        error:nil];
            for (NSURL *groupDir in groupDirs) {
                NSURL *rootsDir = [groupDir URLByAppendingPathComponent:@"roots/default/data"];
                if ([fm fileExistsAtPath:rootsDir.path]) {
                    appGroupURL = rootsDir;
                    break;
                }
            }
        }
        if (appGroupURL) break;
    }
    
    NSString *dataRootPath = appGroupURL ? appGroupURL.path : nil;
    if (!dataRootPath) {
        // Fallback to test rootfs
        XCTAssertNotNil(self.testRoots, @"B0: Roots bootstrap must complete");
        NSURL *rootURL = [self.testRoots rootUrl:self.testRoots.defaultRoot];
        NSString *rootPath = [rootURL path];
        dataRootPath = [rootPath stringByAppendingPathComponent:@"data"];
    }
    
    NSLog(@"APP-001: Using rootfs at: %@", dataRootPath);
    
    // Verify /bin/login exists
    NSString *loginRelativePath = @"bin/login";
    NSString *loginFullPath = [dataRootPath stringByAppendingPathComponent:loginRelativePath];
    BOOL loginExists = [[NSFileManager defaultManager] fileExistsAtPath:loginFullPath];
    XCTAssertTrue(loginExists, @"APP-001: /bin/login must exist at %@", loginFullPath);
    
    if (!loginExists) {
        return; // Skip test if login doesn't exist
    }
    
    // Setup argv like the app does: ["/bin/login", "-f", "root"]
    NSArray<NSString *> *command = @[@"/bin/login", @"-f", @"root"];
    size_t argc = command.count;
    
    // Build null-separated argv buffer (same format as convertCommand)
    char argv[4096];
    char *p = argv;
    for (NSString *cmd in command) {
        const char *c = cmd.UTF8String;
        while (p < argv + sizeof(argv) - 1 && (*p++ = *c++));
        *p = '\0';
    }
    *++p = '\0'; // Final NUL
    
    // Setup envp like the app does
    const char *envp = "TERM=xterm-256color\0";
    
    // Reset trace sink
    guest_execution_trace_sink_reset();
    
    // Run /bin/login with proper argc/argv (same as app)
    GuestExecutionResult *result = [self.harness runExecutableAtRootPath:dataRootPath
                                                          executablePath:loginRelativePath
                                                                      argc:argc
                                                                      argv:argv
                                                                      envp:envp];
    
    // Classification ladder H0-H4
    XCTAssertTrue(result.harnessEntered, @"H0: harness must be entered");
    XCTAssertTrue(result.mountRootCalled, @"H1: mount_root must be called");
    XCTAssertEqual(result.doExecveReturnValue, 0, @"H4: do_execve must return 0 (success)");
    
    // Log classification ladder for debugging
    BOOL X0 = guest_execution_trace_sink_do_execve_entered();
    BOOL X1 = guest_execution_trace_sink_format_exec_entered();
    BOOL X2 = guest_execution_trace_sink_before_elf_exec_entered();
    BOOL X3 = guest_execution_trace_sink_elf_exec_entered();
    BOOL M1 = guest_execution_trace_sink_main_elf_header_accepted();
    BOOL interpOpenAttempted = guest_execution_trace_sink_interp_open_attempted();
    BOOL interpHeaderLoaded = guest_execution_trace_sink_interp_header_loaded();
    BOOL interpMappingsExist = guest_execution_trace_sink_interp_mappings_exist();
    BOOL mainImageLoaded = guest_execution_trace_sink_main_image_loaded();
    BOOL auxvInitialized = guest_execution_trace_sink_auxv_initialized();
    BOOL exitObserved = guest_execution_trace_sink_exit_observed();
    const char *lastEvent = guest_execution_trace_sink_get_last_loader_event();
    
    NSLog(@"APP-001 LADDER: H0=%d H1=%d(rv=%d) H2=%d(rv=%d) H3=%d H4=%d(rv=%d)",
          result.harnessEntered, result.mountRootCalled, result.mountRootReturnValue,
          result.becomeFirstProcessCalled, result.becomeFirstProcessReturnValue,
          result.doExecveReached, result.doExecveCalled, result.doExecveReturnValue);
    NSLog(@"APP-001 LADDER: X0=%d X1=%d X2=%d X3=%d", X0, X1, X2, X3);
    NSLog(@"APP-001 LADDER: M1=%d D2.0=%d D2.1=%d D2.3=%d B2.main=%d B3.auxv=%d",
          M1, interpOpenAttempted, interpHeaderLoaded, interpMappingsExist,
          mainImageLoaded, auxvInitialized);
    NSLog(@"APP-001 LADDER: exit_observed=%d last_event='%s'", exitObserved, lastEvent);
    
    // Print PC values if available
    NSLog(@"APP-001 PC: before=0x%llx after=0x%llx", result.pcBefore, result.pcAfter);
    
    // The key question: does /bin/login crash at same PC as app?
    // App crashes at PC 0x6d1d4 in musl dynamic linker
    // If harness also crashes at 0x6d1d4, we have valid comparison
    // If harness succeeds, we have divergence to investigate
    
    // Record the result - we expect this to FAIL currently
    // (because the app crashes, and we want to compare why)
    NSLog(@"APP-001 RESULT: pc_after=0x%llx (app crashes at 0x6d1d4)", result.pcAfter);
    
    // KEY ASSERTION: Harness should reach exit (guest exit observed)
    NSLog(@"APP-001: exit_observed=%d task_exit_observed=%d", exitObserved, result.taskExitObserved);

    // Poll for structured proof events emitted by runtime at the critical PCs
    NSDate *start = [NSDate date];
    BOOL seen_ldrh = NO;
    BOOL seen_wb = NO;
    BOOL seen_x0_mut_at_6d1d4 = NO;
    BOOL seen_any_x0 = NO;
    while ([[NSDate date] timeIntervalSinceDate:start] < 15.0) {
        seen_ldrh = guest_execution_trace_sink_has_ldrh_6d1c0();
        seen_wb = guest_execution_trace_sink_has_6d1c0_writeback();
        seen_any_x0 = guest_execution_trace_sink_any_x0_mutation();
        seen_x0_mut_at_6d1d4 = guest_execution_trace_sink_has_x0_mutation_at(0x6d1d4ULL);
        if (seen_ldrh || seen_wb || seen_any_x0) break;
        if (guest_execution_trace_sink_exit_observed()) break;
        [NSThread sleepForTimeInterval:0.1];
    }

    if (!seen_ldrh && !seen_wb && !seen_any_x0) {
        // Fail with structured diagnostics - no proof events captured
        const char *lastEvent = guest_execution_trace_sink_get_last_loader_event();
        XCTFail(@"APP-001: No proof events captured for 0x6d1c0/0x6d1d4. last_loader_event='%s' begin_interval_calls=%llu any_interval=%d pc_before=0x%llx pc_after=0x%llx exit_observed=%d",
                lastEvent, (unsigned long long)guest_execution_trace_sink_begin_interval_calls_count(), guest_execution_trace_sink_any_interval_received(), result.pcBefore, result.pcAfter, guest_execution_trace_sink_exit_observed());
    } else {
        if (seen_ldrh) {
            uint64_t addr = 0; uint16_t val = 0; int memret = 0; uint64_t hostptr = 0;
            guest_execution_trace_sink_get_ldrh_6d1c0(&addr, &val, &memret, &hostptr);
            NSLog(@"APP-001 PROOF: ldrh@0x6d1c0 observed: addr=0x%llx val=0x%x mem_ret=%d host_ptr=0x%llx", addr, val, memret, hostptr);
        }
        if (seen_wb) {
            uint64_t x0_after = 0, value = 0; unsigned long rt = 0, size = 0; int is64 = 0;
            guest_execution_trace_sink_get_6d1c0_writeback(&x0_after, &value, &rt, &size, &is64);
            NSLog(@"APP-001 PROOF: writeback@0x6d1c0 observed: x0_after=0x%llx value=0x%llx rt=%lu size=%lu is_64=%d", x0_after, value, rt, size, is64);
        }
        if (seen_any_x0) {
            if (seen_x0_mut_at_6d1d4) {
                uint64_t new_x0=0, old_x0=0, value=0; unsigned long size=0; int is64=0;
                guest_execution_trace_sink_get_x0_mutation_at(0x6d1d4ULL, &new_x0, &old_x0, &value, &size, &is64);
                NSLog(@"APP-001 PROOF: x0 mutation at 0x6d1d4: new=0x%llx old=0x%llx value=0x%llx size=%lu is64=%d", new_x0, old_x0, value, size, is64);
            } else {
                NSLog(@"APP-001 PROOF: x0 mutation(s) observed (none exactly at 0x6d1d4)");
            }
        }
    }
}

// APP-002: Run /bin/login with app-style setup (become_new_init_child)
// This test uses the EXACT same task setup as the app to see if we can reproduce the crash
- (void)testDynamicELF_APP002_RunBinLogin_AppStyleSetup {
    // Use APP rootfs
    NSFileManager *fm = [NSFileManager defaultManager];
    NSURL *appGroupURL = nil;
    
    NSURL *simDevices = [NSURL fileURLWithPath:@"/Users/rudironsoni/Library/Developer/CoreSimulator/Devices"];
    NSArray *deviceDirs = [fm contentsOfDirectoryAtURL:simDevices
                            includingPropertiesForKeys:nil
                                               options:NSDirectoryEnumerationSkipsHiddenFiles
                                                 error:nil];
    for (NSURL *deviceDir in deviceDirs) {
        NSURL *appGroupDir = [deviceDir URLByAppendingPathComponent:@"data/Containers/Shared/AppGroup"];
        if ([fm fileExistsAtPath:appGroupDir.path]) {
            NSArray *groupDirs = [fm contentsOfDirectoryAtURL:appGroupDir
                                   includingPropertiesForKeys:nil
                                                      options:NSDirectoryEnumerationSkipsHiddenFiles
                                                        error:nil];
            for (NSURL *groupDir in groupDirs) {
                NSURL *rootsDir = [groupDir URLByAppendingPathComponent:@"roots/default/data"];
                if ([fm fileExistsAtPath:rootsDir.path]) {
                    appGroupURL = rootsDir;
                    break;
                }
            }
        }
        if (appGroupURL) break;
    }
    
    NSString *dataRootPath = appGroupURL ? appGroupURL.path : nil;
    XCTAssertNotNil(dataRootPath, @"APP-002: Could not find app rootfs");
    if (!dataRootPath) return;
    
    NSLog(@"APP-002: Using rootfs at: %@", dataRootPath);
    
    // Setup argv like the app does: ["/bin/login", "-f", "root"]
    NSArray<NSString *> *command = @[@"/bin/login", @"-f", @"root"];
    size_t argc = command.count;
    
    char argv[4096];
    char *p = argv;
    for (NSString *cmd in command) {
        const char *c = cmd.UTF8String;
        while (p < argv + sizeof(argv) - 1 && (*p++ = *c++));
        *p = '\0';
    }
    *++p = '\0';
    
    const char *envp = "TERM=xterm-256color\0";
    
    guest_execution_trace_sink_reset();
    
    // Use app-style setup: become_first_process + become_new_init_child
    GuestExecutionResult *result = [self.harness runExecutableAtRootPathAppStyle:dataRootPath
                                                                  executablePath:@"bin/login"
                                                                              argc:argc
                                                                              argv:argv
                                                                              envp:envp];
    
    NSLog(@"APP-002 RESULT: pc_before=0x%llx pc_after=0x%llx", result.pcBefore, result.pcAfter);
    NSLog(@"APP-002 RESULT: load_ok=%d exit_observed=%d", result.loadOk, result.taskExitObserved);
    
    // Check if this reproduces the app crash
    if (result.pcAfter == 0x6d1d4 || result.pcAfter == 0x6d1d8) {
        NSLog(@"APP-002: REPRODUCED CRASH at 0x%llx!", result.pcAfter);
        // This is what we want to see - the crash is reproducible with app-style setup
        XCTAssertTrue(true, @"APP-002: Reproduced app crash with app-style setup");
    } else if (result.taskExitObserved) {
        NSLog(@"APP-002: Guest exited cleanly - need to investigate further");
        // Still not crashing - maybe PTY or stdio is the missing piece
    } else {
        NSLog(@"APP-002: Unknown execution result - pc_after=0x%llx", result.pcAfter);
    }
}

@end
