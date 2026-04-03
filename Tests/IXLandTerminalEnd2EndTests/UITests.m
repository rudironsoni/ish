//
//  UITests.m
//  UITests
//
//  Created by Theodore Dubois on 11/13/20.
//

#import <XCTest/XCTest.h>

@interface UITests : XCTestCase
@end

@implementation UITests

- (void)setUp {
    self.continueAfterFailure = NO;
}

// Helper to access terminal text via accessibility value
- (NSString *)terminalText {
    XCUIApplication *app = [[XCUIApplication alloc] init];
    XCUIElement *terminalVC = app.otherElements[@"TerminalViewController"];
    return terminalVC.value;
}

@end
