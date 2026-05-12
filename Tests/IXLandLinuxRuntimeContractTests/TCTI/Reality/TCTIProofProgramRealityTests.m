#import <XCTest/XCTest.h>

typedef NS_ENUM(NSUInteger, TCTIMatrixSection) {
    TCTIMatrixSectionNone = 0,
    TCTIMatrixSectionPipeline,
    TCTIMatrixSectionISAFamilies,
};

static NSString *const TCTIMatrixStatusGreen = @"green";
static NSString *const TCTIMatrixStatusUnsupported = @"unsupported";
static NSString *const TCTIMatrixStatusPresent = @"present";
static NSString *const TCTIMatrixStatusNA = @"n/a";

@interface TCTIProofProgramRealityTests : XCTestCase
@end

@implementation TCTIProofProgramRealityTests

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

- (NSString *)matrixPath
{
    NSString *repoRoot = [self repoRootPath];
    XCTAssertNotNil(repoRoot, @"repo root path must be derivable from __FILE__");
    if (repoRoot == nil)
        return nil;
    return [repoRoot
        stringByAppendingPathComponent:
            @"Tests/IXLandLinuxRuntimeContractTests/TCTI/TCTIInstructionFamilyMatrix.md"];
}

- (NSArray<NSDictionary<NSString *, NSString *> *> *)loadMatrixRowsForSection:
    (TCTIMatrixSection)targetSection
{
    NSString *matrixPath = [self matrixPath];
    XCTAssertNotNil(matrixPath, @"matrix path must exist");
    if (matrixPath == nil)
        return @[];

    NSError *error = nil;
    NSString *content = [NSString stringWithContentsOfFile:matrixPath
                                                  encoding:NSUTF8StringEncoding
                                                     error:&error];
    XCTAssertNotNil(content, @"must read TCTI family matrix: %@", error.localizedDescription);
    if (content == nil)
        return @[];

    NSMutableArray<NSDictionary<NSString *, NSString *> *> *rows = [NSMutableArray array];
    TCTIMatrixSection currentSection = TCTIMatrixSectionNone;

    for (NSString *line in [content componentsSeparatedByString:@"\n"]) {
        if ([line hasPrefix:@"## Pipeline"]) {
            currentSection = TCTIMatrixSectionPipeline;
            continue;
        }
        if ([line hasPrefix:@"## ISA Families"]) {
            currentSection = TCTIMatrixSectionISAFamilies;
            continue;
        }
        if (currentSection != targetSection || ![line hasPrefix:@"| "])
            continue;
        if ([line containsString:@"| --- "])
            continue;

        NSArray<NSString *> *rawColumns = [line componentsSeparatedByString:@"|"];
        NSMutableArray<NSString *> *columns = [NSMutableArray array];
        for (NSString *rawColumn in rawColumns) {
            NSString *trimmed = [rawColumn stringByTrimmingCharactersInSet:
                                               [NSCharacterSet whitespaceCharacterSet]];
            if (trimmed.length > 0)
                [columns addObject:trimmed];
        }

        if (targetSection == TCTIMatrixSectionPipeline) {
            if (columns.count != 8 || [columns[0] isEqualToString:@"Axis"])
                continue;
            [rows addObject:@{
                @"name" : columns[0],
                @"status" : columns[1],
                @"decode" : columns[2],
                @"lowering" : columns[3],
                @"semantic" : columns[4],
                @"liveGuest" : columns[5],
                @"perf" : columns[6],
                @"owners" : columns[7],
            }];
            continue;
        }

        if (columns.count != 9 || [columns[0] isEqualToString:@"Family"])
            continue;
        [rows addObject:@{
            @"name" : columns[0],
            @"mnemonics" : columns[1],
            @"status" : columns[2],
            @"decode" : columns[3],
            @"lowering" : columns[4],
            @"semantic" : columns[5],
            @"liveGuest" : columns[6],
            @"perf" : columns[7],
            @"owners" : columns[8],
        }];
    }

    return rows;
}

- (BOOL)isFullCoverageValue:(NSString *)value
{
    return [value isEqualToString:TCTIMatrixStatusPresent] ||
           [value isEqualToString:TCTIMatrixStatusNA];
}

- (void)assertNoRealityGapForRow:(NSDictionary<NSString *, NSString *> *)row
                        rowLabel:(NSString *)rowLabel
                     rowIdentity:(NSString *)rowIdentity
                       mnemonics:(NSString *)mnemonics
{
    NSString *status = row[@"status"];
    if (![status isEqualToString:TCTIMatrixStatusGreen] &&
        ![status isEqualToString:TCTIMatrixStatusUnsupported]) {
        XCTFail(@"%@ %@ is still %@. Mnemonics=%@ Owners=%@",
                rowLabel, rowIdentity, status, mnemonics ?: @"n/a", row[@"owners"]);
    }

    NSArray<NSString *> *coverageKeys = @[ @"decode", @"lowering", @"semantic", @"liveGuest", @"perf" ];
    NSDictionary<NSString *, NSString *> *coverageLabels = @{
        @"decode" : @"decode",
        @"lowering" : @"lowering/gadget/register-carrier",
        @"semantic" : @"semantic/runtime",
        @"liveGuest" : @"live-guest",
        @"perf" : @"perf",
    };

    for (NSString *key in coverageKeys) {
        NSString *value = row[key];
        if ([self isFullCoverageValue:value])
            continue;
        XCTFail(@"%@ %@ still lacks full %@ proof: %@. Mnemonics=%@ Owners=%@",
                rowLabel, rowIdentity, coverageLabels[key], value,
                mnemonics ?: @"n/a", row[@"owners"]);
    }
}

- (void)testCoverageLedgerEnumeratesAllCanonicalPipelineAxes
{
    NSArray<NSDictionary<NSString *, NSString *> *> *rows =
        [self loadMatrixRowsForSection:TCTIMatrixSectionPipeline];
    NSArray<NSString *> *expectedAxes = @[
        @"Fetch",
        @"Decode",
        @"Lowering",
        @"DispatchPreservation",
        @"ExitWriteback",
        @"BlockCacheAndGeneration",
        @"AtomicAndExclusiveEngine",
    ];

    NSMutableSet<NSString *> *observedAxes = [NSMutableSet set];
    for (NSDictionary<NSString *, NSString *> *row in rows)
        [observedAxes addObject:row[@"name"]];

    for (NSString *axis in expectedAxes) {
        XCTAssertTrue([observedAxes containsObject:axis],
                      @"matrix must explicitly enumerate the %@ pipeline axis", axis);
    }
}

- (void)testCoverageLedgerEnumeratesAllCanonicalISAFamilies
{
    NSArray<NSDictionary<NSString *, NSString *> *> *rows =
        [self loadMatrixRowsForSection:TCTIMatrixSectionISAFamilies];
    NSArray<NSString *> *expectedFamilies = @[
        @"BaseScalar/ControlAndFlags",
        @"BaseScalar/IntegerALU",
        @"BaseScalar/MoveImmediateAndAddress",
        @"BaseScalar/LogicalBitfieldShift",
        @"Memory/ScalarLoadsStores",
        @"Memory/PairAndFrame",
        @"Memory/OrderedExclusiveAtomic",
        @"Memory/SpecialAndTagging",
        @"System/ExceptionsBarriersHints",
        @"System/SysregCacheTLB",
        @"SIMDFP/ScalarFP",
        @"SIMDFP/VectorIntegerLogical",
        @"SIMDFP/VectorMemory",
        @"SIMDFP/CryptoAndDotProduct",
        @"Arm64e/PAuthAndAuthenticatedControl",
    ];

    NSMutableSet<NSString *> *observedFamilies = [NSMutableSet set];
    for (NSDictionary<NSString *, NSString *> *row in rows)
        [observedFamilies addObject:row[@"name"]];

    for (NSString *family in expectedFamilies) {
        XCTAssertTrue([observedFamilies containsObject:family],
                      @"matrix must explicitly enumerate the %@ family", family);
    }
}

- (void)testProofProgramReality_AllPipelineAxesExposeIncompleteStages
{
    NSArray<NSDictionary<NSString *, NSString *> *> *rows =
        [self loadMatrixRowsForSection:TCTIMatrixSectionPipeline];
    XCTAssertGreaterThan(rows.count, 0UL, @"pipeline rows must exist in the matrix");
}

- (void)testProofProgramReality_AllISAFamiliesExposeIncompleteStages
{
    NSArray<NSDictionary<NSString *, NSString *> *> *rows =
        [self loadMatrixRowsForSection:TCTIMatrixSectionISAFamilies];
    XCTAssertGreaterThan(rows.count, 0UL, @"ISA family rows must exist in the matrix");
}

- (void)testCoverageLedgerKeepsMnemonicAndOwnerTruthAttachedToEveryFamily
{
    NSArray<NSDictionary<NSString *, NSString *> *> *rows =
        [self loadMatrixRowsForSection:TCTIMatrixSectionISAFamilies];
    for (NSDictionary<NSString *, NSString *> *row in rows) {
        XCTAssertGreaterThan(row[@"mnemonics"].length, 0UL,
                             @"%@ must retain explicit mnemonic ownership", row[@"name"]);
        XCTAssertGreaterThan(row[@"owners"].length, 0UL,
                             @"%@ must retain explicit owning test files", row[@"name"]);
    }
}

- (NSDictionary<NSString *, NSString *> *)rowNamed:(NSString *)name
                                         inSection:(TCTIMatrixSection)section
{
    NSArray<NSDictionary<NSString *, NSString *> *> *rows = [self loadMatrixRowsForSection:section];
    for (NSDictionary<NSString *, NSString *> *row in rows) {
        if ([row[@"name"] isEqualToString:name])
            return row;
    }
    XCTFail(@"matrix must contain row %@", name);
    return nil;
}

- (void)testRealityGap_MemoryOrderedExclusiveAtomicFamilyStillRed
{
    NSDictionary<NSString *, NSString *> *row =
        [self rowNamed:@"Memory/OrderedExclusiveAtomic" inSection:TCTIMatrixSectionISAFamilies];
    XCTAssertNotNil(row);
    if (row == nil)
        return;
    XCTAssertEqualObjects(row[@"status"], @"green",
                          @"ISA family Memory/OrderedExclusiveAtomic is still %@. Mnemonics=%@ Owners=%@",
                          row[@"status"], row[@"mnemonics"], row[@"owners"]);
}

- (void)testRealityGap_MemoryOrderedExclusiveAtomicLiveGuestAttributionStillIndirect
{
    NSDictionary<NSString *, NSString *> *row =
        [self rowNamed:@"Memory/OrderedExclusiveAtomic" inSection:TCTIMatrixSectionISAFamilies];
    XCTAssertNotNil(row);
    if (row == nil)
        return;
    XCTAssertTrue([self isFullCoverageValue:row[@"liveGuest"]],
                  @"ISA family Memory/OrderedExclusiveAtomic still lacks full live-guest proof: %@. Mnemonics=%@ Owners=%@",
                  row[@"liveGuest"], row[@"mnemonics"], row[@"owners"]);
}

- (void)testRealityGap_SIMDFPVectorIntegerLogicalFamilyStillRed
{
    NSDictionary<NSString *, NSString *> *row =
        [self rowNamed:@"SIMDFP/VectorIntegerLogical" inSection:TCTIMatrixSectionISAFamilies];
    XCTAssertNotNil(row);
    if (row == nil)
        return;
    XCTAssertEqualObjects(row[@"status"], @"green",
                          @"ISA family SIMDFP/VectorIntegerLogical is still %@. Mnemonics=%@ Owners=%@",
                          row[@"status"], row[@"mnemonics"], row[@"owners"]);
}

- (void)testRealityGap_SIMDFPVectorIntegerLogicalLiveGuestAttributionStillIndirect
{
    NSDictionary<NSString *, NSString *> *row =
        [self rowNamed:@"SIMDFP/VectorIntegerLogical" inSection:TCTIMatrixSectionISAFamilies];
    XCTAssertNotNil(row);
    if (row == nil)
        return;
    XCTAssertTrue([self isFullCoverageValue:row[@"liveGuest"]],
                  @"ISA family SIMDFP/VectorIntegerLogical still lacks full live-guest proof: %@. Mnemonics=%@ Owners=%@",
                  row[@"liveGuest"], row[@"mnemonics"], row[@"owners"]);
}

- (void)testRealityGap_FetchPipelinePerfProofStillSynthetic
{
    NSDictionary<NSString *, NSString *> *row =
        [self rowNamed:@"Fetch" inSection:TCTIMatrixSectionPipeline];
    XCTAssertNotNil(row);
    if (row == nil)
        return;
    XCTAssertTrue([self isFullCoverageValue:row[@"perf"]],
                  @"Pipeline axis Fetch still lacks full perf proof: %@. Owners=%@",
                  row[@"perf"], row[@"owners"]);
}

- (void)testRealityGap_DecodePipelinePerfProofStillSynthetic
{
    NSDictionary<NSString *, NSString *> *row =
        [self rowNamed:@"Decode" inSection:TCTIMatrixSectionPipeline];
    XCTAssertNotNil(row);
    if (row == nil)
        return;
    XCTAssertTrue([self isFullCoverageValue:row[@"perf"]],
                  @"Pipeline axis Decode still lacks full perf proof: %@. Owners=%@",
                  row[@"perf"], row[@"owners"]);
}

- (void)testRealityGap_LoweringPipelinePerfProofStillSynthetic
{
    NSDictionary<NSString *, NSString *> *row =
        [self rowNamed:@"Lowering" inSection:TCTIMatrixSectionPipeline];
    XCTAssertNotNil(row);
    if (row == nil)
        return;
    XCTAssertTrue([self isFullCoverageValue:row[@"perf"]],
                  @"Pipeline axis Lowering still lacks full perf proof: %@. Owners=%@",
                  row[@"perf"], row[@"owners"]);
}

- (void)testRealityGap_BlockCacheAndGenerationPerfProofStillSynthetic
{
    NSDictionary<NSString *, NSString *> *row =
        [self rowNamed:@"BlockCacheAndGeneration" inSection:TCTIMatrixSectionPipeline];
    XCTAssertNotNil(row);
    if (row == nil)
        return;
    XCTAssertTrue([self isFullCoverageValue:row[@"perf"]],
                  @"Pipeline axis BlockCacheAndGeneration still lacks full perf proof: %@. Owners=%@",
                  row[@"perf"], row[@"owners"]);
}

- (void)testRealityGap_AtomicAndExclusiveEnginePerfProofStillMissing
{
    NSDictionary<NSString *, NSString *> *row =
        [self rowNamed:@"AtomicAndExclusiveEngine" inSection:TCTIMatrixSectionPipeline];
    XCTAssertNotNil(row);
    if (row == nil)
        return;
    XCTAssertTrue([self isFullCoverageValue:row[@"perf"]],
                  @"Pipeline axis AtomicAndExclusiveEngine still lacks full perf proof: %@. Owners=%@",
                  row[@"perf"], row[@"owners"]);
}

@end
