#import <XCTest/XCTest.h>

int gen_test_run_all(void);
const char *gen_test_first_failed_test(void);
int gen_test_failed_count(void);
const char *gen_test_failed_test_at(int idx);
int gen_test_first_failed_line(void);
int gen_test_failed_line_at(int idx);

@interface GenXCTests : XCTestCase
@end

@implementation GenXCTests

- (void)testGenHarness
{
    int rc = gen_test_run_all();
    const char *firstFailed = gen_test_first_failed_test();
    int failedCount = gen_test_failed_count();
    int failedLine = gen_test_first_failed_line();
    NSString *firstFailedName = firstFailed ? [NSString stringWithUTF8String:firstFailed] : @"<none>";
    NSMutableString *allFailed = [NSMutableString string];
    for (int i = 0; i < failedCount; i++) {
        const char *name = gen_test_failed_test_at(i);
        int line = gen_test_failed_line_at(i);
        if (name) {
            if (allFailed.length > 0) {
                [allFailed appendString:@","];
            }
            [allFailed appendFormat:@"%s:%d", name, line];
        }
    }

    XCTAssertEqual(rc, 0, @"gen_test harness failed_count=%d first_failed=%@ first_failed_line=%d failed_tests=%@",
                   failedCount,
                   firstFailedName,
                   failedLine,
                   allFailed);
}

@end
