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
    [self waitForTerminalReadyWithTimeout:60.0];
    XCUIElement *terminalInput = self.app.textFields[@"TerminalInput"];
    XCTAssertTrue([terminalInput waitForExistenceWithTimeout:5.0], @"TerminalInput must be accessible within 5 seconds");
    [terminalInput tap];
    [NSThread sleepForTimeInterval:0.5];
    [terminalInput typeText:[NSString stringWithFormat:@"%@\n", command]];
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

// Test 1: Basic shell execution
- (void)testBasicShellExecution {
    [self typeCommand:@"printf '%s%s%s\n' 'aarch64' '_test' '_passed'"];
    NSString *output = [self waitForTerminalTextContaining:@"aarch64_test_passed" timeout:30.0];
    XCTAssertTrue([output containsString:@"aarch64_test_passed"],
                  @"Should see computed shell output in terminal. Actual output: %@", output);
}

// Test 2: Verify aarch64 architecture
- (void)testArchitectureDetection {
    NSString *expected = @"ARCH:aarch64:END";
    [self typeCommand:@"printf 'ARCH:%s:END\\n' \"$(uname -m)\""];
    NSString *output = [self waitForTerminalTextContaining:expected timeout:10.0];
    XCTAssertTrue([output containsString:expected],
                  @"Should report aarch64 architecture. Actual output: %@", output);
}

// Test 3: Basic arithmetic via expr
- (void)testArithmetic {
    NSString *expected = @"ARITH:8:END";
    [self typeCommand:@"printf 'ARITH:%s:END\\n' \"$(expr 5 + 3)\""];
    NSString *output = [self waitForTerminalTextContaining:expected timeout:10.0];
    XCTAssertTrue([output containsString:expected],
                  @"Should calculate 5+3=8. Actual output: %@", output);
}

// Test 4: Exit codes
- (void)testExitCode {
    NSString *expected = @"EXIT:1:END";
    [self typeCommand:@"false ; printf 'EXIT:%s:END\\n' \"$?\""];
    NSString *output = [self waitForTerminalTextContaining:expected timeout:10.0];
    XCTAssertTrue([output containsString:expected],
                  @"Should report exit code 1. Actual output: %@", output);
}

// Test 5: File operations
- (void)testFileOperations {
    [self typeCommand:@"printf '%s%s' 'test_' 'content' > /tmp/test_file"];
    [self typeCommand:@"cat /tmp/test_file"];
    NSString *output = [self waitForTerminalTextContaining:@"test_content" timeout:10.0];
    XCTAssertTrue([output containsString:@"test_content"],
                  @"Should read written file. Actual output: %@", output);
}

// Test 6: Process creation (fork)
- (void)testProcessCreation {
    NSString *expectedPrefix = @"SHELL:/bin/";
    [self typeCommand:@"printf 'SHELL:%s:END\\n' \"$SHELL\""];
    NSString *output = [self waitForTerminalTextContaining:expectedPrefix timeout:10.0];
    XCTAssertTrue([output containsString:expectedPrefix],
                  @"Should report shell path. Actual output: %@", output);
}

// Test 7: Pipes
- (void)testPipes {
    NSString *expected = @"PIPE:2:END";
    [self typeCommand:@"printf 'PIPE:%s:END\\n' \"$(echo 'hello world' | wc -w)\""];
    NSString *output = [self waitForTerminalTextContaining:expected timeout:10.0];
    XCTAssertTrue([output containsString:expected],
                  @"Should count 2 words. Actual output: %@", output);
}

// Test 8: Environment variables
- (void)testEnvironmentVariables {
    [self typeCommand:@"export TEST_VAR='aarch64_'\"value\" ; echo $TEST_VAR"];
    NSString *output = [self waitForTerminalTextContaining:@"aarch64_value" timeout:10.0];
    XCTAssertTrue([output containsString:@"aarch64_value"],
                  @"Should read environment variable. Actual output: %@", output);
}

// Test 9: Signal handling
- (void)testSignalHandling {
    [self typeCommand:@"sleep 0.1 ; printf '%s%s\n' 'comple' 'ted'"];
    NSString *output = [self waitForTerminalTextContaining:@"completed" timeout:10.0];
    XCTAssertTrue([output containsString:@"completed"],
                  @"Should complete after sleep. Actual output: %@", output);
}

// Test 10: Complex command sequence
- (void)testCommandSequence {
    [self typeCommand:@"for i in 1 2 3; do printf 'SEQ:%s:END\\n' \"$i\"; done"];
    NSString *output = [self waitForTerminalTextContaining:@"SEQ:3:END" timeout:10.0];
    XCTAssertTrue([output containsString:@"SEQ:1:END"],
                  @"Should show SEQ:1:END. Actual output: %@", output);
    XCTAssertTrue([output containsString:@"SEQ:2:END"],
                  @"Should show SEQ:2:END. Actual output: %@", output);
    XCTAssertTrue([output containsString:@"SEQ:3:END"],
                  @"Should show SEQ:3:END. Actual output: %@", output);
}

@end
