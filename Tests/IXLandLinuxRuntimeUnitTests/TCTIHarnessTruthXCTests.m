#import <XCTest/XCTest.h>

#import "Tests/Support/TCTITestHarness/tcti_harness_truth.h"

@interface TCTIHarnessTruthXCTests : XCTestCase
@end

@implementation TCTIHarnessTruthXCTests

- (void)testAddReg7_13_14CarrierTruth
{
    tcti_harness_snapshot_t snapshot;
    XCTAssertTrue(tcti_harness_case_add_7_13_14(&snapshot));
    XCTAssertEqual(snapshot.out_regs[8], 0xFFFFFEF7FBF8ULL);
    XCTAssertEqual(snapshot.out_regs[14], 0xFFFFFEF7FBF0ULL);
    XCTAssertEqual(snapshot.out_regs[15], 0x8ULL);
    XCTAssertEqual(snapshot.out_regs[0], 0x1111111111111111ULL);
}

- (void)testAddReg0_1_2CarrierTruth
{
    tcti_harness_snapshot_t snapshot;
    XCTAssertTrue(tcti_harness_case_add_0_1_2(&snapshot));
    XCTAssertEqual(snapshot.out_regs[1], 0x42ULL);
    XCTAssertEqual(snapshot.out_regs[2], 0x40ULL);
    XCTAssertEqual(snapshot.out_regs[3], 0x2ULL);
    XCTAssertEqual(snapshot.out_regs[8], 0x3333333333333333ULL);
}

- (void)testMovReg2_7CarrierTruth
{
    tcti_harness_snapshot_t snapshot;
    XCTAssertTrue(tcti_harness_case_mov_2_7(&snapshot));
    XCTAssertEqual(snapshot.out_regs[3], 0x123456789ABCDEF0ULL);
    XCTAssertEqual(snapshot.out_regs[8], 0x123456789ABCDEF0ULL);
    XCTAssertEqual(snapshot.out_regs[0], 0x4444444444444444ULL);
}

@end
