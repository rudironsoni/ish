//
//  Aarch64EmulatorTests.m
//  UITests
//
//  E2E tests for aarch64 emulation in iSH
//

#import <XCTest/XCTest.h>

@interface Aarch64EmulatorTests : XCTestCase
@property XCUIApplication *app;
@end

@implementation Aarch64EmulatorTests

- (void)setUp {
    [super setUp];
    self.continueAfterFailure = NO;

    self.app = [[XCUIApplication alloc] init];
    self.app.launchEnvironment = @{ @"IXLAND_UI_TESTING": @"1" };
    [self.app launch];

    // TerminalViewController is no longer used; wait for TerminalSurface
    XCUIElement *terminalSurface = self.app.otherElements[@"TerminalSurface"];
    XCTAssertTrue([terminalSurface waitForExistenceWithTimeout:10.0], @"TerminalSurface must exist");
    [self failIfStartupAlertExistsWithTimeout:1.0];
}

- (void)tearDown {
    [super tearDown];
}

- (NSString *)startupAlertPayloadForAlert:(XCUIElement *)alert {
    NSMutableArray<NSString *> *parts = [NSMutableArray array];
    NSString *title = alert.label ?: @"";
    [parts addObject:[NSString stringWithFormat:@"alert_title=%@", title]];

    NSMutableArray<NSString *> *staticTexts = [NSMutableArray array];
    XCUIElementQuery *query = alert.staticTexts;
    NSUInteger count = query.count;
    for (NSUInteger i = 0; i < count; i++) {
        XCUIElement *element = [query elementBoundByIndex:i];
        NSString *label = element.label ?: @"";
        [staticTexts addObject:[NSString stringWithFormat:@"staticText[%lu]=%@", (unsigned long)i, label]];
    }
    if (staticTexts.count == 0)
        [staticTexts addObject:@"staticText[none]"];
    [parts addObjectsFromArray:staticTexts];

    NSString *debugDescription = alert.debugDescription ?: @"";
    [parts addObject:[NSString stringWithFormat:@"alert_debugDescription=%@", debugDescription]];
    return [parts componentsJoinedByString:@"\n"];
}

- (void)failIfStartupAlertExistsWithTimeout:(NSTimeInterval)timeout {
    XCUIElement *alert = [self.app.alerts elementBoundByIndex:0];
    if ([alert waitForExistenceWithTimeout:timeout]) {
        NSString *payload = [self startupAlertPayloadForAlert:alert];
        if ([payload containsString:@"could not start session"] || [alert.label isEqualToString:@"could not start session"]) {
            XCTFail(@"Startup alert payload:\n%@", payload);
        }
    }
}

// The terminal UI is rendered in a web view and accepts keyboard input at the
// application level once the scene is active.
- (void)typeCommand:(NSString *)command {
    XCUIElement *terminalSurface = self.app.otherElements[@"TerminalSurface"];
    XCTAssertTrue([terminalSurface waitForExistenceWithTimeout:5.0], @"TerminalSurface must be accessible within 5 seconds");
    [self waitForTerminalReadyWithTimeout:180.0];
    [terminalSurface tap];
    [NSThread sleepForTimeInterval:0.5];
    NSString *payload = [NSString stringWithFormat:@"%@\n", command];
    for (NSUInteger index = 0; index < payload.length; index++) {
        NSString *piece = [payload substringWithRange:NSMakeRange(index, 1)];
        [self.app typeText:piece];
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
    }
}

- (NSString *)terminalText {
    XCUIElement *terminalSurface = self.app.otherElements[@"TerminalSurface"];
    XCTAssertTrue([terminalSurface waitForExistenceWithTimeout:5.0], @"TerminalSurface must be accessible");
    NSString *value = terminalSurface.value;
    XCTAssertNotNil(value, @"TerminalSurface value must not be nil");
    return value;
}

- (NSString *)waitForTerminalReadyWithTimeout:(NSTimeInterval)timeout {
    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:timeout];
    NSString *lastObserved = [self terminalText];
    while ([deadline timeIntervalSinceNow] > 0) {
        [self failIfStartupAlertExistsWithTimeout:0.0];
        lastObserved = [self terminalText];
        if ([self terminalTextContainsShellPrompt:lastObserved])
            return lastObserved;
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
    }
    XCTFail(@"Timed out waiting for shell prompt. Last observed TerminalSurface.value: %@", lastObserved);
    return lastObserved;
}

- (BOOL)terminalTextContainsShellPrompt:(NSString *)text {
    if (text.length == 0 || [text isEqualToString:@"No terminal output"])
        return NO;
    return [text containsString:@"/ # "]
        || [text hasSuffix:@"/ #"]
        || [text containsString:@"\n/ #"];
}

- (NSString *)waitForTerminalTextContaining:(NSString *)expected timeout:(NSTimeInterval)timeout {
    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:timeout];
    NSString *lastObserved = [self terminalText];
    while ([deadline timeIntervalSinceNow] > 0) {
        [self failIfStartupAlertExistsWithTimeout:0.0];
        lastObserved = [self terminalText];
        if ([lastObserved containsString:expected])
            return lastObserved;
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
    }
    XCTFail(@"Timed out waiting for terminal text containing '%@'. Last observed TerminalSurface.value: %@", expected, lastObserved);
    return lastObserved;
}

- (NSUInteger)promptCountInTerminalText:(NSString *)text {
    if (text.length == 0)
        return 0;
    NSUInteger count = 0;
    NSRange searchRange = NSMakeRange(0, text.length);
    while (YES) {
        NSRange found = [text rangeOfString:@"/ # " options:0 range:searchRange];
        if (found.location == NSNotFound)
            break;
        count += 1;
        NSUInteger nextLocation = NSMaxRange(found);
        if (nextLocation >= text.length)
            break;
        searchRange = NSMakeRange(nextLocation, text.length - nextLocation);
    }
    return count;
}

- (XCUIElement *)waitForSessionExitAlertWithTimeout:(NSTimeInterval)timeout {
    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:timeout];
    while ([deadline timeIntervalSinceNow] > 0) {
        XCUIElement *alert = [self.app.alerts elementBoundByIndex:0];
        if (alert.exists) {
            NSString *payload = [self startupAlertPayloadForAlert:alert];
            if ([payload containsString:@"session ended"]
                || [payload containsString:@"session exited"]
                || [payload containsString:@"session crashed"]) {
                return alert;
            }
        }
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
    }
    XCTFail(@"Timed out waiting for session exit alert");
    return [self.app.alerts elementBoundByIndex:0];
}

// Test 1: Basic shell execution
- (void)testBasicShellExecution {
    [self typeCommand:@"echo aarch64_test_passed"];
    NSString *output = [self waitForTerminalTextContaining:@"aarch64_test_passed" timeout:30.0];
    XCTAssertTrue([output containsString:@"aarch64_test_passed"],
                  @"Should see computed shell output in terminal. Actual output: %@", output);
}

- (void)testPromptAppearsWithoutExtraInput {
    NSString *output = [self waitForTerminalReadyWithTimeout:180.0];
    XCTAssertTrue([self terminalTextContainsShellPrompt:output],
                  @"Shell prompt must appear without extra keystrokes. Actual output: %@", output);
}

- (void)testSingleKeystrokeEchoAppearsImmediately {
    XCUIElement *terminalSurface = self.app.otherElements[@"TerminalSurface"];
    XCTAssertTrue([terminalSurface waitForExistenceWithTimeout:5.0], @"TerminalSurface must be accessible");
    [self waitForTerminalReadyWithTimeout:180.0];
    [terminalSurface tap];
    [self.app typeText:@"e"];

    NSString *output = [self waitForTerminalTextContaining:@"/ # e" timeout:5.0];
    XCTAssertTrue([output containsString:@"/ # e"],
                  @"A single typed character must echo after one keystroke. Actual output: %@", output);
}

- (void)testRootDirectoryListingDoesNotReportOutOfMemory {
    [self typeCommand:@"/bin/busybox ls -a /"];
    NSString *output = [self waitForTerminalTextContaining:@"bin" timeout:30.0];
    XCTAssertFalse([output containsString:@"Out of memory"],
                   @"Root directory listing must not fail in guest opendir/calloc. Actual output: %@",
                   output);
    XCTAssertFalse([output containsString:@"Segmentation fault"],
                   @"Root directory listing must not segfault in the interactive shell. Actual output: %@",
                   output);
    XCTAssertTrue([output containsString:@"bin"],
                  @"Root directory listing should include /bin. Actual output: %@", output);
}

- (void)testPlainLsDoesNotSegfault
{
    [self typeCommand:@"ls"];
    NSString *output = [self waitForTerminalTextContaining:@"bin" timeout:30.0];
    XCTAssertFalse([output containsString:@"Segmentation fault"],
                   @"Plain ls in the interactive shell must not segfault. Actual output: %@",
                   output);
    XCTAssertTrue([output containsString:@"bin"],
                  @"Plain ls should include /bin in the root directory listing. Actual output: %@",
                  output);
}

- (void)testLongLsDoesNotSegfault
{
    [self typeCommand:@"ls -la"];
    NSString *output = [self waitForTerminalTextContaining:@"drwx" timeout:30.0];
    XCTAssertFalse([output containsString:@"Segmentation fault"],
                   @"Long-format ls in the interactive shell must not segfault. Actual output: %@",
                   output);
    XCTAssertTrue([output containsString:@"drwx"],
                  @"Long-format ls should include permission metadata. Actual output: %@",
                  output);
}

- (void)testLongLsReachesHeaderPromptly
{
    [self typeCommand:@"ls -la"];
    NSString *output = [self waitForTerminalTextContaining:@"total 0" timeout:10.0];
    XCTAssertTrue([output containsString:@"total 0"],
                  @"Long-format ls should reach the header promptly. Actual output: %@",
                  output);
}

- (void)testLongNumericLsReachesHeaderPromptly
{
    [self typeCommand:@"ls -ln"];
    NSString *output = [self waitForTerminalTextContaining:@"total 0" timeout:10.0];
    XCTAssertTrue([output containsString:@"total 0"],
                  @"Numeric long-format ls should reach the header promptly. Actual output: %@",
                  output);
}

- (void)testLongNumericLsDoesNotSegfault
{
    [self typeCommand:@"ls -ln"];
    NSString *output = [self waitForTerminalTextContaining:@"drwx" timeout:30.0];
    XCTAssertFalse([output containsString:@"Segmentation fault"],
                   @"Numeric long-format ls in the interactive shell must not segfault. Actual output: %@",
                   output);
    XCTAssertTrue([output containsString:@"drwx"],
                  @"Numeric long-format ls should include permission metadata. Actual output: %@",
                  output);
}

// Test 2: Verify aarch64 architecture
- (void)testArchitectureDetection {
    NSString *expected = @"aarch64";
    [self typeCommand:@"/bin/busybox uname -m"];
    NSString *output = [self waitForTerminalTextContaining:expected timeout:10.0];
    XCTAssertTrue([output containsString:expected],
                  @"Should report aarch64 architecture. Actual output: %@", output);
}

// Test 3: Basic arithmetic via expr
- (void)testArithmetic {
    NSString *expected = @"8";
    [self typeCommand:@"/bin/busybox expr 5 + 3"];
    NSString *output = [self waitForTerminalTextContaining:expected timeout:10.0];
    XCTAssertTrue([output containsString:expected],
                  @"Should calculate 5+3=8. Actual output: %@", output);
}

// Test 4: Exit codes
- (void)testExitCode {
    NSString *expected = @"EXIT:1:END";
    [self typeCommand:@"false ; echo EXIT:$?:END"];
    NSString *output = [self waitForTerminalTextContaining:expected timeout:10.0];
    XCTAssertTrue([output containsString:expected],
                  @"Should report exit code 1. Actual output: %@", output);
}

// Test 5: File operations
- (void)testFileOperations {
    [self typeCommand:@"echo test_content > /tmp/test_file"];
    [self typeCommand:@"/bin/busybox cat /tmp/test_file"];
    NSString *output = [self waitForTerminalTextContaining:@"test_content" timeout:10.0];
    XCTAssertTrue([output containsString:@"test_content"],
                  @"Should read written file. Actual output: %@", output);
}

// Test 6: Process creation (fork)
- (void)testProcessCreation {
    NSString *expectedPrefix = @"/bin/";
    [self typeCommand:@"echo $SHELL"];
    NSString *output = [self waitForTerminalTextContaining:expectedPrefix timeout:10.0];
    XCTAssertTrue([output containsString:expectedPrefix],
                  @"Should report shell path. Actual output: %@", output);
}

// Test 7: Pipes
- (void)testPipes {
    NSString *expected = @"2";
    [self typeCommand:@"echo hello world | /bin/busybox wc -w"];
    NSString *output = [self waitForTerminalTextContaining:expected timeout:10.0];
    XCTAssertTrue([output containsString:expected],
                  @"Should count 2 words. Actual output: %@", output);
}

// Test 8: Environment variables
- (void)testEnvironmentVariables {
    [self typeCommand:@"export TEST_VAR=aarch64_value ; echo $TEST_VAR"];
    NSString *output = [self waitForTerminalTextContaining:@"aarch64_value" timeout:10.0];
    XCTAssertTrue([output containsString:@"aarch64_value"],
                  @"Should read environment variable. Actual output: %@", output);
}

// Test 9: Signal handling
- (void)testSignalHandling {
    [self typeCommand:@"/bin/busybox sleep 0.1 ; echo completed"];
    NSString *output = [self waitForTerminalTextContaining:@"completed" timeout:10.0];
    XCTAssertTrue([output containsString:@"completed"],
                  @"Should complete after sleep. Actual output: %@", output);
}

// Test 10: Complex command sequence
- (void)testCommandSequence {
    [self typeCommand:@"echo SEQ:1:END ; echo SEQ:2:END ; echo SEQ:3:END"];
    NSString *output = [self waitForTerminalTextContaining:@"SEQ:3:END" timeout:10.0];
    XCTAssertTrue([output containsString:@"SEQ:1:END"],
                  @"Should show SEQ:1:END. Actual output: %@", output);
    XCTAssertTrue([output containsString:@"SEQ:2:END"],
                  @"Should show SEQ:2:END. Actual output: %@", output);
    XCTAssertTrue([output containsString:@"SEQ:3:END"],
                  @"Should show SEQ:3:END. Actual output: %@", output);
}

- (void)testInteractiveExitDoesNotSilentlyRestartSession {
    NSString *beforeExit = [self waitForTerminalReadyWithTimeout:180.0];
    NSUInteger promptCountBeforeExit = [self promptCountInTerminalText:beforeExit];

    XCUIElement *terminalSurface = self.app.otherElements[@"TerminalSurface"];
    XCTAssertTrue([terminalSurface waitForExistenceWithTimeout:5.0], @"TerminalSurface must be accessible");
    [terminalSurface tap];
    [self.app typeText:@"exit\n"];

    XCUIElement *alert = [self waitForSessionExitAlertWithTimeout:10.0];
    NSString *payload = [self startupAlertPayloadForAlert:alert];
    XCTAssertTrue([payload containsString:@"restart_suppressed=true"],
                  @"Exit alert must surface that auto-restart was suppressed. Payload: %@", payload);
    XCTAssertTrue([payload containsString:@"exit_code=0"],
                  @"Exit alert must surface the exit code. Payload: %@", payload);

    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:1.0]];
    NSString *afterExit = [self terminalText];
    NSUInteger promptCountAfterExit = [self promptCountInTerminalText:afterExit];
    XCTAssertEqual(promptCountAfterExit, promptCountBeforeExit,
                   @"Interactive exit must not silently relaunch a fresh prompt. Before: %@ After: %@",
                   beforeExit, afterExit);
}

@end
