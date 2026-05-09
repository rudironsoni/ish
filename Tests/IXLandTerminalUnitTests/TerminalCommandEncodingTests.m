#import <XCTest/XCTest.h>

#import "Terminal.h"

@interface TerminalCommandEncodingTests : XCTestCase
@end

@implementation TerminalCommandEncodingTests

- (void)testConvertCommandProducesImmediateDoubleNullTerminator {
    char argv[128];
    memset(argv, 0x7f, sizeof(argv));

    [Terminal convertCommand:@[@"/bin/busybox", @"sh", @"-i"] toArgs:argv limitSize:sizeof(argv)];

    static const char expected[] = "/bin/busybox\0sh\0-i\0\0";
    XCTAssertEqual(memcmp(argv, expected, sizeof(expected)), 0,
                   @"convertCommand must produce a double-NUL-terminated argv buffer");
}

@end
