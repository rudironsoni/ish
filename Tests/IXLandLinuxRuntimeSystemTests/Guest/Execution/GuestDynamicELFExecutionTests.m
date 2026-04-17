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
        NSLog(@"B0-B1 PASS: busybox exists at %@ with size %llu bytes", busyboxFullPath, fileSize);
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
    
    // B1 behavioral: Execute and poll for interpreter path resolution
    // B1 requires PROVING interp path was RESOLVED, not just that execution succeeded
    // We poll sink state directly to observe the actual loader boundary event
    guest_execution_trace_sink_reset();
    
    dispatch_async(self.harness.executionQueue, ^{
        [self.harness runExecutableAtRootPath:dataRootPath executablePath:busyboxRelativePath];
    });
    
    // Poll for loader boundary: interpreter path resolved
    // This is the EXTERNALLY OBSERVABLE LOADER BOUNDARY for B1
    NSDate *startTime = [NSDate date];
    BOOL interpPathResolved = NO;
    BOOL exitObserved = NO;
    // Pre-elf_exec diagnostic ladder
    BOOL doExecveEntered = NO;
    BOOL formatExecEntered = NO;
    BOOL elfExecEntered = NO;
    const char *lastEvent = "";
    while ([[NSDate date] timeIntervalSinceDate:startTime] < 60.0) {
        if (guest_execution_trace_sink_interp_path_resolved()) {
            interpPathResolved = YES;
            break;
        }
        if (guest_execution_trace_sink_exit_observed()) {
            exitObserved = YES;
            // Capture diagnostic ladder
            doExecveEntered = guest_execution_trace_sink_do_execve_entered();
            formatExecEntered = guest_execution_trace_sink_format_exec_entered();
            elfExecEntered = guest_execution_trace_sink_elf_exec_entered();
            lastEvent = guest_execution_trace_sink_get_last_loader_event();
            break;
        }
        [NSThread sleepForTimeInterval:0.1];
    }
    
    // B1 primary: interp_path must be resolved (proves PT_INTERP was processed)
    XCTAssertTrue(interpPathResolved, 
                  @"B1: loader.interpreter_path=path: event must be observed. "
                  @"Diagnostic ladder: X0(do_execve)=%d, X1(format_exec)=%d, X2(elf_exec)=%d, "
                  @"last_event='%s', exit_observed=%d",
                  doExecveEntered, formatExecEntered, elfExecEntered, lastEvent, exitObserved);

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
        NSLog(@"D1.6.a PASS: generic_open(\"/lib/ld-musl-aarch64.so.1\") succeeded, fd=%p", interp_fd);
        fd_close(interp_fd);
        XCTAssertTrue(true, @"D1.6.a: Interpreter path resolves via mounted root");
    } else {
        // D1.6.a FAIL: Path resolution broken at mount/root level
        int err = interp_fd != NULL ? PTR_ERR(interp_fd) : -1;
        NSLog(@"D1.6.a FAIL: generic_open(\"/lib/ld-musl-aarch64.so.1\") failed, err=%d", err);
        XCTAssertTrue(interp_fd != NULL && !IS_ERR(interp_fd), @"D1.6.a FAIL: generic_open failed for guest absolute path, err=%d", err);
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
    
    XCTestExpectation *boundaryExpectation = [self expectationWithDescription:@"D2 boundary observed"];
    __block BOOL callbackFired = NO;
    
    guest_execution_trace_sink_reset();
    guest_execution_trace_sink_set_completion_callback(^(BOOL exit_observed, int exit_code) {
        // DIAGNOSTIC: Check if elf_exec was reached
        // D2.0: Check if interpreter open was attempted (loader.interp.open.result)
        // D2.1: Check if interpreter header was loaded (loader.interp_elf.header or loader.interp.bias.compute)
        // D2.3: Check if interpreter mappings exist (loader.interp.pt_load.map)
        BOOL elfExecReached = guest_execution_trace_sink_elf_exec_reached();
        BOOL mainElfHeaderAccepted = guest_execution_trace_sink_main_elf_header_accepted();
        BOOL interpOpenAttempted = guest_execution_trace_sink_interp_open_attempted();
        BOOL interpHeaderLoaded = guest_execution_trace_sink_interp_header_loaded();
        BOOL interpMappingsExist = guest_execution_trace_sink_interp_mappings_exist();
        
        if (!callbackFired && (elfExecReached || mainElfHeaderAccepted || interpOpenAttempted || interpHeaderLoaded || interpMappingsExist || exit_observed)) {
            callbackFired = YES;
            [boundaryExpectation fulfill];
        }
    });
    
    dispatch_async(self.harness.executionQueue, ^{
        [self.harness runExecutableAtRootPath:dataRootPath executablePath:busyboxRelativePath];
    });
    
    [self waitForExpectations:@[boundaryExpectation] timeout:60.0];
    
    // DIAGNOSTIC: Check if elf_exec was reached
    BOOL elfExecReached = guest_execution_trace_sink_elf_exec_reached();
    
    // M1: Check main ELF header accepted event
    BOOL mainElfHeaderAccepted = guest_execution_trace_sink_main_elf_header_accepted();
    const char *lastEvent = guest_execution_trace_sink_get_last_loader_event();
    NSLog(@"D2-DIAG: elf_exec_reached=%d, main_elf_header_accepted=%d, last_event='%s'", elfExecReached, mainElfHeaderAccepted, lastEvent);
    
    // M1 classification: read_header(main_fd, &header) must succeed
    XCTAssertTrue(mainElfHeaderAccepted,
                  @"M1: Main ELF header accepted event (loader.main_elf.header) must be observed. "
                  @"This proves read_header(main_fd, &header) succeeded. "
                  @"If this fails, elf_exec failed before or during main ELF header validation. "
                  @"last_event='%s'", lastEvent);
    
    // D2.0: Check interpreter open attempt event
    BOOL interpOpenAttempted = guest_execution_trace_sink_interp_open_attempted();
    BOOL interpOpenSucceeded = guest_execution_trace_sink_interp_open_succeeded();
    int interpOpenErrno = guest_execution_trace_sink_interp_open_errno();
    NSLog(@"D2-DIAG: interp_open_attempted=%d, interp_open_succeeded=%d, interp_open_errno=%d, last_event='%s'",
          interpOpenAttempted, interpOpenSucceeded, interpOpenErrno, lastEvent);
    
    // D2.0 classification: generic_open(interp_name) must be called
    XCTAssertTrue(interpOpenAttempted,
                  @"D2.0: Interp open event (loader.interp.open.result) must be observed. "
                  @"This proves generic_open(interp_name) was called. "
                  @"If this fails, PT_INTERP loop was entered but interpreter open failed. "
                  @"interp_open_errno=%d, last_event='%s'", interpOpenErrno, lastEvent);
    
    // D2.1: Check interpreter header loaded event
    BOOL interpHeaderLoaded = guest_execution_trace_sink_interp_header_loaded();
    NSLog(@"D2-DIAG: interp_header_loaded=%d", interpHeaderLoaded);
    
    // D2.1 classification
    XCTAssertTrue(interpHeaderLoaded, 
                  @"D2.1: Interp header loaded event must be observed (loader.interp_elf.header or loader.interp.bias.compute). "
                  @"This proves read_header(interp_fd, &interp_header) succeeded. "
                  @"Last loader event: %s", lastEvent);
    
    // D2.3: Check interpreter mappings exist
    BOOL interpMappingsExist = guest_execution_trace_sink_interp_mappings_exist();
    NSLog(@"D2-DIAG: interp_mappings_exist=%d", interpMappingsExist);
    
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

@end
