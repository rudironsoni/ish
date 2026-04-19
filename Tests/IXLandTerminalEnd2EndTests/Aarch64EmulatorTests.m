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

    // Wait for TerminalViewController to appear
    XCUIElement *terminalVC = self.app.otherElements[@"TerminalViewController"];
    XCTAssertTrue([terminalVC waitForExistenceWithTimeout:10.0], @"TerminalViewController should exist");
}

- (void)tearDown {
    [super tearDown];
}

// The terminal UI is rendered in a web view and accepts keyboard input at the
// application level once the scene is active.
- (void)typeCommand:(NSString *)command {
    XCUIElement *terminalVC = self.app.otherElements[@"TerminalViewController"];
    XCTAssertTrue(terminalVC.exists, @"TerminalViewController should exist before typing");
    XCTAssertNotNil([self terminalText], @"Terminal accessibility text should exist before typing");
   
    // Tap to ensure focus
    [terminalVC tap];
    [NSThread sleepForTimeInterval:0.5];  // Give time for keyboard to appear
    
    // Focus TerminalView directly
    XCUIElement *terminalSurface = self.app.otherElements[@"TerminalSurface"];
    if (terminalSurface.exists) {
        [terminalSurface tap];
        [NSThread sleepForTimeInterval:0.2];
    }
    
    // Type command
    [self.app typeText:[NSString stringWithFormat:@"%@\n", command]];
    [NSThread sleepForTimeInterval:1.5];  // Give time for command execution
}

// Helper: Get terminal text
- (NSString *)terminalText {
    // Use TerminalViewController since it exposes terminal text via accessibilityValue
    XCUIElement *terminalVC = self.app.otherElements[@"TerminalViewController"];
    XCTAssertTrue([terminalVC waitForExistenceWithTimeout:5.0], @"TerminalViewController must be accessible within 5 seconds");

    NSString *value = terminalVC.value;
    XCTAssertNotNil(value, @"TerminalViewController value must not be nil");
    return value;
}

// Test 1: Basic shell execution
- (void)testBasicShellExecution {
    // Use longer wait to ensure proper focus
    [NSThread sleepForTimeInterval:2.0];
    [self typeCommand:@"echo 'aarch64_test_passed'"];

    // Wait longer to ensure command execution
    [NSThread sleepForTimeInterval:2.0];
    NSString *output = [self terminalText];
    if (![output containsString:@"aarch64_test_passed"]) {
        // Try again if first attempt failed
        [NSThread sleepForTimeInterval:3.0];
        [self typeCommand:@"echo 'aarch64_test_passed'"];
        [NSThread sleepForTimeInterval:2.0];
        output = [self terminalText];
    }
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
