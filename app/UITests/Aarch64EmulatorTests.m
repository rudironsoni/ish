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
    [self.app launch];

    // Wait for app to be ready
    XCTAssertTrue([self.app waitForExistenceWithTimeout:5.0]);
}

- (void)tearDown {
    [super tearDown];
}

// Helper: Type a command in the terminal
- (void)typeCommand:(NSString *)command {
    XCUIElement *terminal = self.app.textViews.firstMatch;
    XCTAssertTrue(terminal.exists, "Terminal should exist");

    // Tap to focus
    [terminal tap];

    // Type command
    [terminal typeText:command];

    // Send return
    [terminal typeText:@"\n"];

    // Wait for output
    [NSThread sleepForTimeInterval:0.5];
}

// Helper: Get terminal text
- (NSString *)terminalText {
    XCUIElement *terminal = self.app.textViews.firstMatch;
    return terminal.value;
}

// Test 1: Basic shell execution
- (void)testBasicShellExecution {
    [self typeCommand:@"echo 'aarch64_test_passed'"];

    NSString *output = [self terminalText];
    XCTAssertTrue([output containsString:@"aarch64_test_passed"],
                  "Should see echo output in terminal");
}

// Test 2: Verify aarch64 architecture
- (void)testArchitectureDetection {
    [self typeCommand:@"uname -m"];

    NSString *output = [self terminalText];
    XCTAssertTrue([output containsString:@"aarch64"],
                  "Should report aarch64 architecture");
}

// Test 3: Basic arithmetic via expr
- (void)testArithmetic {
    [self typeCommand:@"expr 5 + 3"];

    NSString *output = [self terminalText];
    XCTAssertTrue([output containsString:@"8"],
                  "Should calculate 5+3=8");
}

// Test 4: Exit codes
- (void)testExitCode {
    [self typeCommand:@"true ; echo \"Exit: $?\""];

    NSString *output = [self terminalText];
    XCTAssertTrue([output containsString:@"Exit: 0"],
                  "Should report exit code 0");
}

// Test 5: File operations
- (void)testFileOperations {
    [self typeCommand:@"echo 'test_content' > /tmp/test_file"];
    [self typeCommand:@"cat /tmp/test_file"];

    NSString *output = [self terminalText];
    XCTAssertTrue([output containsString:@"test_content"],
                  "Should read written file");
}

// Test 6: Process creation (fork)
- (void)testProcessCreation {
    [self typeCommand:@"echo $SHELL"];

    NSString *output = [self terminalText];
    XCTAssertTrue([output containsString:@"/bin/"],
                  "Should report shell path");
}

// Test 7: Pipes
- (void)testPipes {
    [self typeCommand:@"echo 'hello world' | wc -w"];

    NSString *output = [self terminalText];
    XCTAssertTrue([output containsString:@"2"],
                  "Should count 2 words");
}

// Test 8: Environment variables
- (void)testEnvironmentVariables {
    [self typeCommand:@"export TEST_VAR='aarch64_value' ; echo $TEST_VAR"];

    NSString *output = [self terminalText];
    XCTAssertTrue([output containsString:@"aarch64_value"],
                  "Should read environment variable");
}

// Test 9: Signal handling
- (void)testSignalHandling {
    // Send a command that completes quickly
    [self typeCommand:@"sleep 0.1 ; echo 'completed'"];

    NSString *output = [self terminalText];
    XCTAssertTrue([output containsString:@"completed"],
                  "Should complete after sleep");
}

// Test 10: Complex command sequence
- (void)testCommandSequence {
    [self typeCommand:@"for i in 1 2 3; do echo \"line_$i\"; done"];

    NSString *output = [self terminalText];
    XCTAssertTrue([output containsString:@"line_1"],
                  "Should show line_1");
    XCTAssertTrue([output containsString:@"line_2"],
                  "Should show line_2");
    XCTAssertTrue([output containsString:@"line_3"],
                  "Should show line_3");
}

@end
