#import <XCTest/XCTest.h>

@interface TCTIFamilyGapInventoryTests : XCTestCase
@end

@implementation TCTIFamilyGapInventoryTests

- (NSString *)repoRootPath
{
    NSString *sourcePath = [NSString stringWithUTF8String:__FILE__];
    NSRange testsRange =
        [sourcePath rangeOfString:@"/Tests/IXLandLinuxRuntimeContractTests/TCTI/Reality/"];
    XCTAssertNotEqual(testsRange.location, NSNotFound,
                      @"test source path must contain the TCTI contract tree");
    if (testsRange.location == NSNotFound)
        return nil;
    return [sourcePath substringToIndex:testsRange.location];
}

- (NSString *)loadRelativePath:(NSString *)relativePath
{
    NSString *repoRoot = [self repoRootPath];
    XCTAssertNotNil(repoRoot, @"repo root path must be derivable from __FILE__");
    if (repoRoot == nil)
        return nil;

    NSString *fullPath = [repoRoot stringByAppendingPathComponent:relativePath];
    NSError *error = nil;
    NSString *content = [NSString stringWithContentsOfFile:fullPath
                                                  encoding:NSUTF8StringEncoding
                                                     error:&error];
    XCTAssertNotNil(content, @"must read %@: %@", relativePath, error.localizedDescription);
    return content;
}

- (void)assertInventory:(NSArray<NSDictionary<NSString *, NSString *> *> *)inventory
             existsInFile:(NSString *)relativePath
               stageLabel:(NSString *)stageLabel
{
    NSString *content = [self loadRelativePath:relativePath];
    XCTAssertNotNil(content, @"%@ must be readable", relativePath);
    if (content == nil)
        return;

    for (NSDictionary<NSString *, NSString *> *entry in inventory) {
        NSString *pattern = entry[@"pattern"];
        XCTAssertTrue([content containsString:pattern],
                      @"%@: family=%@ bucket=%@ still lacks explicit %@ ownership in %@. "
                       @"Expected pattern `%@`.",
                      stageLabel, entry[@"family"], entry[@"bucket"], stageLabel, relativePath,
                      pattern);
    }
}

- (void)testGapInventory_OrderedExclusiveAtomicMissingMnemonicBucketsStayExplicitAtDecode
{
    NSArray<NSDictionary<NSString *, NSString *> *> *inventory = @[
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"LDAPR*", @"pattern" : @"LDAPR"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"LDXP", @"pattern" : @"LDXP"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"STXP", @"pattern" : @"STXP"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"LDAXP", @"pattern" : @"LDAXP"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"STLXP", @"pattern" : @"STLXP"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"SWP*", @"pattern" : @"SWP"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"LDADD*", @"pattern" : @"LDADD"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"LDCLR*", @"pattern" : @"LDCLR"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"LDEOR*", @"pattern" : @"LDEOR"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"LDSET*", @"pattern" : @"LDSET"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"LDSMAX*", @"pattern" : @"LDSMAX"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"LDSMIN*", @"pattern" : @"LDSMIN"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"LDUMAX*", @"pattern" : @"LDUMAX"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"LDUMIN*", @"pattern" : @"LDUMIN"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"STADD*", @"pattern" : @"STADD"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"STCLR*", @"pattern" : @"STCLR"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"STEOR*", @"pattern" : @"STEOR"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"STSET*", @"pattern" : @"STSET"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"STSMAX*", @"pattern" : @"STSMAX"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"STSMIN*", @"pattern" : @"STSMIN"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"STUMAX*", @"pattern" : @"STUMAX"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"STUMIN*", @"pattern" : @"STUMIN"},
    ];

    [self assertInventory:inventory
              existsInFile:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/Pipeline/Decode/TCTIPipelineDecodeTests.m"
                stageLabel:@"decode"];
}

- (void)testGapInventory_OrderedExclusiveAtomicMissingMnemonicBucketsStayExplicitAtLowering
{
    NSArray<NSDictionary<NSString *, NSString *> *> *inventory = @[
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"LDAPR*", @"pattern" : @"LDAPR"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"LDXP", @"pattern" : @"LDXP"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"STXP", @"pattern" : @"STXP"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"LDAXP", @"pattern" : @"LDAXP"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"STLXP", @"pattern" : @"STLXP"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"SWP*", @"pattern" : @"SWP"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"LDADD*", @"pattern" : @"LDADD"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"LDCLR*", @"pattern" : @"LDCLR"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"LDEOR*", @"pattern" : @"LDEOR"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"LDSET*", @"pattern" : @"LDSET"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"LDSMAX*", @"pattern" : @"LDSMAX"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"LDSMIN*", @"pattern" : @"LDSMIN"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"LDUMAX*", @"pattern" : @"LDUMAX"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"LDUMIN*", @"pattern" : @"LDUMIN"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"STADD*", @"pattern" : @"STADD"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"STCLR*", @"pattern" : @"STCLR"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"STEOR*", @"pattern" : @"STEOR"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"STSET*", @"pattern" : @"STSET"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"STSMAX*", @"pattern" : @"STSMAX"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"STSMIN*", @"pattern" : @"STSMIN"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"STUMAX*", @"pattern" : @"STUMAX"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"STUMIN*", @"pattern" : @"STUMIN"},
    ];

    [self assertInventory:inventory
              existsInFile:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/Pipeline/Lowering/TCTIPipelineLoweringTests.m"
                stageLabel:@"lowering/gadget/register-carrier"];
}

- (void)testGapInventory_OrderedExclusiveAtomicMissingMnemonicBucketsStayExplicitAtSemanticExecution
{
    NSArray<NSDictionary<NSString *, NSString *> *> *inventory = @[
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"LDAPR*", @"pattern" : @"LDAPR"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"LDXP", @"pattern" : @"LDXP"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"STXP", @"pattern" : @"STXP"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"LDAXP", @"pattern" : @"LDAXP"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"STLXP", @"pattern" : @"STLXP"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"SWP*", @"pattern" : @"SWP"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"LDADD*", @"pattern" : @"LDADD"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"LDCLR*", @"pattern" : @"LDCLR"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"LDEOR*", @"pattern" : @"LDEOR"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"LDSET*", @"pattern" : @"LDSET"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"LDSMAX*", @"pattern" : @"LDSMAX"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"LDSMIN*", @"pattern" : @"LDSMIN"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"LDUMAX*", @"pattern" : @"LDUMAX"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"LDUMIN*", @"pattern" : @"LDUMIN"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"STADD*", @"pattern" : @"STADD"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"STCLR*", @"pattern" : @"STCLR"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"STEOR*", @"pattern" : @"STEOR"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"STSET*", @"pattern" : @"STSET"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"STSMAX*", @"pattern" : @"STSMAX"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"STSMIN*", @"pattern" : @"STSMIN"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"STUMAX*", @"pattern" : @"STUMAX"},
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"STUMIN*", @"pattern" : @"STUMIN"},
    ];

    [self assertInventory:inventory
              existsInFile:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/ISAFamilies/Memory/TCTIAtomicAndExclusiveSemanticTests.m"
                stageLabel:@"semantic/runtime"];
}

- (void)testGapInventory_VectorIntegerLogicalMissingMnemonicBucketsStayExplicitAtDecode
{
    NSArray<NSDictionary<NSString *, NSString *> *> *inventory = @[
        @{@"family" : @"SIMDFP/VectorIntegerLogical", @"bucket" : @"CMGE", @"pattern" : @"CMGE"},
        @{@"family" : @"SIMDFP/VectorIntegerLogical", @"bucket" : @"CMHI", @"pattern" : @"CMHI"},
        @{@"family" : @"SIMDFP/VectorIntegerLogical", @"bucket" : @"CMHS", @"pattern" : @"CMHS"},
        @{@"family" : @"SIMDFP/VectorIntegerLogical", @"bucket" : @"CMLE", @"pattern" : @"CMLE"},
        @{@"family" : @"SIMDFP/VectorIntegerLogical", @"bucket" : @"CMLT", @"pattern" : @"CMLT"},
        @{@"family" : @"SIMDFP/VectorIntegerLogical", @"bucket" : @"CMTST", @"pattern" : @"CMTST"},
    ];

    [self assertInventory:inventory
              existsInFile:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/Pipeline/Decode/TCTIPipelineDecodeTests.m"
                stageLabel:@"decode"];
}

- (void)testGapInventory_VectorIntegerLogicalMissingMnemonicBucketsStayExplicitAtLowering
{
    NSArray<NSDictionary<NSString *, NSString *> *> *inventory = @[
        @{@"family" : @"SIMDFP/VectorIntegerLogical", @"bucket" : @"CMGE", @"pattern" : @"CMGE"},
        @{@"family" : @"SIMDFP/VectorIntegerLogical", @"bucket" : @"CMHI", @"pattern" : @"CMHI"},
        @{@"family" : @"SIMDFP/VectorIntegerLogical", @"bucket" : @"CMHS", @"pattern" : @"CMHS"},
        @{@"family" : @"SIMDFP/VectorIntegerLogical", @"bucket" : @"CMLE", @"pattern" : @"CMLE"},
        @{@"family" : @"SIMDFP/VectorIntegerLogical", @"bucket" : @"CMLT", @"pattern" : @"CMLT"},
        @{@"family" : @"SIMDFP/VectorIntegerLogical", @"bucket" : @"CMTST", @"pattern" : @"CMTST"},
    ];

    [self assertInventory:inventory
              existsInFile:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/Pipeline/Lowering/TCTIPipelineLoweringTests.m"
                stageLabel:@"lowering/gadget/register-carrier"];
}

- (void)testGapInventory_VectorIntegerLogicalMissingMnemonicBucketsStayExplicitAtSemanticExecution
{
    NSArray<NSDictionary<NSString *, NSString *> *> *inventory = @[
        @{@"family" : @"SIMDFP/VectorIntegerLogical", @"bucket" : @"CMGE", @"pattern" : @"CMGE"},
        @{@"family" : @"SIMDFP/VectorIntegerLogical", @"bucket" : @"CMHI", @"pattern" : @"CMHI"},
        @{@"family" : @"SIMDFP/VectorIntegerLogical", @"bucket" : @"CMHS", @"pattern" : @"CMHS"},
        @{@"family" : @"SIMDFP/VectorIntegerLogical", @"bucket" : @"CMLE", @"pattern" : @"CMLE"},
        @{@"family" : @"SIMDFP/VectorIntegerLogical", @"bucket" : @"CMLT", @"pattern" : @"CMLT"},
        @{@"family" : @"SIMDFP/VectorIntegerLogical", @"bucket" : @"CMTST", @"pattern" : @"CMTST"},
    ];

    [self assertInventory:inventory
              existsInFile:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/ISAFamilies/SIMDFP/TCTIVectorSemanticTests.m"
                stageLabel:@"semantic/runtime"];
}

- (void)testGapInventory_LiveGuestFailuresNameTheTwoRemainingRedFamiliesExplicitly
{
    NSArray<NSDictionary<NSString *, NSString *> *> *inventory = @[
        @{@"family" : @"Memory/OrderedExclusiveAtomic", @"bucket" : @"guest-family-link", @"pattern" : @"OrderedExclusiveAtomic"},
        @{@"family" : @"SIMDFP/VectorIntegerLogical", @"bucket" : @"guest-family-link", @"pattern" : @"VectorIntegerLogical"},
    ];

    [self assertInventory:inventory
              existsInFile:@"Tests/IXLandLinuxRuntimeSystemTests/Guest/Execution/GuestBusyboxRuntimeExecutionTests.m"
                stageLabel:@"live-guest"];
}

@end
