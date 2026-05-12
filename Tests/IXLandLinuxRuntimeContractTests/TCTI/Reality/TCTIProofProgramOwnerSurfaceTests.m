#import <XCTest/XCTest.h>

typedef NS_ENUM(NSUInteger, TCTIOwnerMatrixSection) {
    TCTIOwnerMatrixSectionNone = 0,
    TCTIOwnerMatrixSectionPipeline,
    TCTIOwnerMatrixSectionISAFamilies,
};

@interface TCTIProofProgramOwnerSurfaceTests : XCTestCase
@end

@implementation TCTIProofProgramOwnerSurfaceTests

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
    (TCTIOwnerMatrixSection)targetSection
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
    TCTIOwnerMatrixSection currentSection = TCTIOwnerMatrixSectionNone;

    for (NSString *line in [content componentsSeparatedByString:@"\n"]) {
        if ([line hasPrefix:@"## Pipeline"]) {
            currentSection = TCTIOwnerMatrixSectionPipeline;
            continue;
        }
        if ([line hasPrefix:@"## ISA Families"]) {
            currentSection = TCTIOwnerMatrixSectionISAFamilies;
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

        if (targetSection == TCTIOwnerMatrixSectionPipeline) {
            if (columns.count != 8 || [columns[0] isEqualToString:@"Axis"])
                continue;
            [rows addObject:@{
                @"name" : columns[0],
                @"owners" : columns[7],
            }];
            continue;
        }

        if (columns.count != 9 || [columns[0] isEqualToString:@"Family"])
            continue;
        [rows addObject:@{
            @"name" : columns[0],
            @"owners" : columns[8],
        }];
    }

    return rows;
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

- (void)assertRelativePathExists:(NSString *)relativePath label:(NSString *)label
{
    NSString *repoRoot = [self repoRootPath];
    XCTAssertNotNil(repoRoot, @"repo root path must be derivable from __FILE__");
    if (repoRoot == nil)
        return;

    NSString *fullPath = [repoRoot stringByAppendingPathComponent:relativePath];
    BOOL isDirectory = NO;
    XCTAssertTrue([[NSFileManager defaultManager] fileExistsAtPath:fullPath isDirectory:&isDirectory],
                  @"%@ owner path must exist: %@", label, relativePath);
}

- (void)assertRelativePath:(NSString *)relativePath
            containsTokens:(NSArray<NSString *> *)tokens
                     label:(NSString *)label
{
    NSString *content = [self loadRelativePath:relativePath];
    XCTAssertNotNil(content, @"%@ must be readable", relativePath);
    if (content == nil)
        return;

    for (NSString *token in tokens) {
        XCTAssertTrue([content containsString:token],
                      @"%@ must keep explicit owner anchor `%@` in %@",
                      label, token, relativePath);
    }
}

- (void)assertMatrixOwnersExistForRows:(NSArray<NSDictionary<NSString *, NSString *> *> *)rows
                              rowLabel:(NSString *)rowLabel
{
    for (NSDictionary<NSString *, NSString *> *row in rows) {
        NSArray<NSString *> *owners =
            [row[@"owners"] componentsSeparatedByString:@", "];
        for (NSString *owner in owners) {
            NSString *trimmedOwner =
                [owner stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
            if (trimmedOwner.length == 0)
                continue;
            if ([trimmedOwner containsString:@"**"])
                continue;
            NSString *relativePath =
                [trimmedOwner stringByTrimmingCharactersInSet:
                                  [NSCharacterSet characterSetWithCharactersInString:@"`"]];
            if ([relativePath hasPrefix:@"TCTI/"]) {
                relativePath =
                    [@"Tests/IXLandLinuxRuntimeContractTests/" stringByAppendingString:relativePath];
            } else if ([relativePath hasPrefix:@"IXLandLinuxRuntimeSystemTests/"]) {
                relativePath = [@"Tests/" stringByAppendingString:relativePath];
            } else if ([relativePath hasPrefix:@"IXLandLinuxRuntimePerfTests/"]) {
                relativePath = [@"Tests/" stringByAppendingString:relativePath];
            } else if (![relativePath hasPrefix:@"Tests/"] &&
                       ![relativePath hasPrefix:@"Sources/"] &&
                       ![relativePath hasPrefix:@"docs/"] &&
                       ![relativePath hasPrefix:@"internal/"] &&
                       ![relativePath hasPrefix:@"third_party/"] &&
                       ![relativePath hasPrefix:@"IXLand.xcodeproj/"]) {
                relativePath = [@"Tests/" stringByAppendingString:relativePath];
            }
            [self assertRelativePathExists:relativePath
                                     label:[NSString stringWithFormat:@"%@ %@", rowLabel, row[@"name"]]];
        }
    }
}

- (void)testMatrixOwnerPathsExistForAllPipelineRows
{
    NSArray<NSDictionary<NSString *, NSString *> *> *rows =
        [self loadMatrixRowsForSection:TCTIOwnerMatrixSectionPipeline];
    XCTAssertGreaterThan(rows.count, 0UL, @"pipeline rows must exist in the matrix");
    [self assertMatrixOwnersExistForRows:rows rowLabel:@"Pipeline axis"];
}

- (void)testMatrixOwnerPathsExistForAllISAFamilyRows
{
    NSArray<NSDictionary<NSString *, NSString *> *> *rows =
        [self loadMatrixRowsForSection:TCTIOwnerMatrixSectionISAFamilies];
    XCTAssertGreaterThan(rows.count, 0UL, @"ISA family rows must exist in the matrix");
    [self assertMatrixOwnersExistForRows:rows rowLabel:@"ISA family"];
}

- (void)testOwnerSurface_FetchAxisKeepsRepresentativeAnchors
{
    [self assertRelativePath:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/Pipeline/Fetch/TCTIPipelineFetchTests.m"
              containsTokens:@[
                  @"ValidGuestPC_ReturnsInstructionWord",
                  @"InvalidGuestPC_ReturnsFault",
                  @"PageCrossing_IsDeterministic",
              ]
                       label:@"Fetch axis"];
}

- (void)testOwnerSurface_DecodeAxisKeepsRepresentativeAnchors
{
    [self assertRelativePath:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/Pipeline/Decode/TCTIPipelineDecodeTests.m"
              containsTokens:@[
                  @"ADRPClassifiesAsPCRelativePageAddressing",
                  @"CASPublishesCompareAndSwapRegisterRolesThroughAtomicFamily",
                  @"DUPRepresentativePublishesSIMDFamilyShape",
                  @"PACIARejectsUntilArm64eDecodeOwnershipExists",
              ]
                       label:@"Decode axis"];
}

- (void)testOwnerSurface_LoweringAxisKeepsRepresentativeAnchors
{
    [self assertRelativePath:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/Pipeline/Lowering/TCTIPipelineLoweringTests.m"
              containsTokens:@[
                  @"CBZMemoryBackedRegisterLowers",
                  @"MSRTPIDREL0LowersThroughSysregGadget",
                  @"SIMDLDRQUnsignedImmediateLowers",
                  @"PACIARejectsUntilArm64eLoweringOwnershipExists",
              ]
                       label:@"Lowering axis"];
}

- (void)testOwnerSurface_DispatchPreservationAxisKeepsRepresentativeAnchors
{
    [self assertRelativePath:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/Pipeline/DispatchPreservation/TCTIPipelineDispatchPreservationTests.m"
              containsTokens:@[
                  @"CMPAddCSELBlocksDecodeAndLower",
                  @"FlagsPublishedByOneBlockRemainVisibleToLaterBlocks",
              ]
                       label:@"DispatchPreservation axis"];
}

- (void)testOwnerSurface_ExitWritebackAxisKeepsRepresentativeAnchors
{
    [self assertRelativePath:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/Pipeline/ExitWriteback/TCTIPipelineExitWritebackTests.m"
              containsTokens:@[
                  @"STRPostIndexDecodesLowersAndPublishesUpdatedBaseOnExit",
                  @"LDPPostIndexAliasBaseUsesOriginalAddressThenPublishesWriteback",
              ]
                       label:@"ExitWriteback axis"];
}

- (void)testOwnerSurface_BlockCacheAxisKeepsRepresentativeAnchors
{
    [self assertRelativePath:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/Pipeline/BlockCacheAndGeneration/TCTIPipelineBlockCacheGenerationTests.m"
              containsTokens:@[
                  @"ExecutableGuestWriteInvalidatesCompiledBlockAtSamePC",
                  @"rewriting executable guest bytes at the same PC must invalidate stale",
              ]
                       label:@"BlockCacheAndGeneration axis"];
}

- (void)testOwnerSurface_AtomicAndExclusiveEngineAxisKeepsRepresentativeAnchors
{
    [self assertRelativePath:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/Pipeline/AtomicAndExclusiveEngine/TCTIPipelineAtomicAndExclusiveEngineTests.m"
              containsTokens:@[
                  @"LDAXRAndSTLXRDecodeAndLowerAsAtomicSequence",
                  @"SuccessPathPublishesStatusMemoryAndMonitorState",
                  @"FailedSTLXRLeavesMemoryUntouchedAndReturnsFailure",
              ]
                       label:@"AtomicAndExclusiveEngine axis"];
}

- (void)testOwnerSurface_BaseScalarControlAndFlagsKeepsRepresentativeAnchors
{
    [self assertRelativePath:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/ISAFamilies/BaseScalar/TCTIControlAndFlagsSemanticTests.m"
              containsTokens:@[
                  @"CCMP",
                  @"CSET",
                  @"CBZ",
                  @"BusyboxPromptLoopCCMPBLSExitsTakenPath",
              ]
                       label:@"BaseScalar/ControlAndFlags"];
}

- (void)testOwnerSurface_BaseScalarIntegerALUKeepsRepresentativeAnchors
{
    [self assertRelativePath:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/ISAFamilies/BaseScalar/TCTIIntegerALUSemanticTests.m"
              containsTokens:@[
                  @"ADDToMemoryBackedX23StoresResult",
                  @"UMADDLUsesUnsigned32BitInputs",
                  @"SMADDLUsesSigned32BitInputs",
              ]
                       label:@"BaseScalar/IntegerALU"];
}

- (void)testOwnerSurface_BaseScalarMoveImmediateAndAddressKeepsRepresentativeAnchors
{
    [self assertRelativePath:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/ISAFamilies/BaseScalar/TCTIMoveImmediateAndAddressSemanticTests.m"
              containsTokens:@[
                  @"MuslCallocPLTADRPResolvesLocalGOTPage",
                  @"MuslCallbackSlotADRPADDMaterializesLdsoTargetPage",
                  @"guest x0 after ADRP",
              ]
                       label:@"BaseScalar/MoveImmediateAndAddress"];
}

- (void)testOwnerSurface_BaseScalarLogicalBitfieldShiftKeepsRepresentativeAnchors
{
    [self assertRelativePath:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/ISAFamilies/BaseScalar/TCTILogicalBitfieldShiftSemanticTests.m"
              containsTokens:@[
                  @"LSLV64BitUsesFullShiftAmount",
                  @"LSLVMemoryBackedRegistersRoundtrip",
                  @"LDR/LSLV/ORR/STR",
              ]
                       label:@"BaseScalar/LogicalBitfieldShift"];
}

- (void)testOwnerSurface_MemoryScalarLoadsStoresKeepsRepresentativeAnchors
{
    [self assertRelativePath:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/ISAFamilies/Memory/TCTIScalarLoadStoreSemanticTests.m"
              containsTokens:@[
                  @"LDRFromMemoryBackedBaseSyncsHotDestination",
                  @"SPRelativeLDRSTRRoundtrip",
                  @"LDRX2Immediate0FromMemoryBackedX21",
              ]
                       label:@"Memory/ScalarLoadsStores"];
}

- (void)testOwnerSurface_MemoryPairAndFrameKeepsRepresentativeAnchors
{
    [self assertRelativePath:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/ISAFamilies/Memory/TCTIPairAndFrameSemanticTests.m"
              containsTokens:@[
                  @"LDPX2X0FromMemoryBackedX21",
                  @"GeneratedLDPX2X0BytecodeMatchesManualShape",
                  @"LDPSWPairSignExtendsLiveMuslOffsets",
              ]
                       label:@"Memory/PairAndFrame"];
}

- (void)testOwnerSurface_MemoryOrderedExclusiveAtomicKeepsRepresentativeAnchors
{
    [self assertRelativePath:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/ISAFamilies/Memory/TCTIAtomicAndExclusiveSemanticTests.m"
              containsTokens:@[
                  @"MuslMutexLDAXRSTLXRRoundtripsLockWord",
                  @"CASWWAtomicallySwapsMatchingValue",
                  @"LDAPRWLoadsWordThroughOrderedAcquirePath",
                  @"STUMINWPublishesUnsignedMinWithoutPublishingOldValue",
              ]
                       label:@"Memory/OrderedExclusiveAtomic"];
}

- (void)testOwnerSurface_MemorySpecialAndTaggingKeepsRepresentativeAnchors
{
    [self assertRelativePath:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/ISAFamilies/Memory/TCTISpecialAndTaggingSemanticTests.m"
              containsTokens:@[
                  @"LDGRejectsUntilTaggingSemanticsExist",
                  @"STGRejectsUntilTaggingSemanticsExist",
              ]
                       label:@"Memory/SpecialAndTagging"];
}

- (void)testOwnerSurface_SystemExceptionsBarriersHintsKeepsRepresentativeAnchors
{
    [self assertRelativePath:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/ISAFamilies/System/TCTIExceptionsBarriersHintsSemanticTests.m"
              containsTokens:@[
                  @"DMB/DSB/ISB",
                  @"CLREX",
              ]
                       label:@"System/ExceptionsBarriersHints"];
}

- (void)testOwnerSurface_SystemSysregCacheTLBKeepsRepresentativeAnchors
{
    [self assertRelativePath:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/ISAFamilies/System/TCTISysregCacheTLBSemanticTests.m"
              containsTokens:@[
                  @"TPIDR_EL0",
                  @"MSR/MRS",
              ]
                       label:@"System/SysregCacheTLB"];
}

- (void)testOwnerSurface_SIMDFPScalarFPKeepsRepresentativeAnchors
{
    [self assertRelativePath:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/ISAFamilies/SIMDFP/TCTIScalarFPSemanticTests.m"
              containsTokens:@[
                  @"FADDDoubleAddsIEEE64Operands",
                  @"second scalar FMOV",
              ]
                       label:@"SIMDFP/ScalarFP"];
}

- (void)testOwnerSurface_SIMDFPVectorIntegerLogicalKeepsRepresentativeAnchors
{
    [self assertRelativePath:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/ISAFamilies/SIMDFP/TCTIVectorSemanticTests.m"
              containsTokens:@[
                  @"CNTComputesPerBytePopulationCount",
                  @"TBLLooksUpBytesAndZeroesOutOfRangeIndices",
                  @"MLSSubtractsVectorProductsFromDestination",
                  @"CMGEPublishesSignedGreaterOrEqualMask",
                  @"CMTSTPublishesNonzeroBitIntersectionMask",
              ]
                       label:@"SIMDFP/VectorIntegerLogical"];
}

- (void)testOwnerSurface_SIMDFPVectorMemoryKeepsRepresentativeAnchors
{
    [self assertRelativePath:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/ISAFamilies/SIMDFP/TCTIVectorMemorySemanticTests.m"
              containsTokens:@[
                  @"LDRQLoadsFull128BitVector",
                  @"SIMD LDR must load the full 128-bit vector payload",
              ]
                       label:@"SIMDFP/VectorMemory"];
}

- (void)testOwnerSurface_SIMDFPCryptoAndDotProductKeepsRepresentativeAnchors
{
    [self assertRelativePath:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/ISAFamilies/SIMDFP/TCTICryptoAndDotProductSemanticTests.m"
              containsTokens:@[
                  @"AESERejectsUntilCryptoFamilyIsImplemented",
                  @"PMULLRejectsUntilCryptoFamilyIsImplemented",
              ]
                       label:@"SIMDFP/CryptoAndDotProduct"];
}

- (void)testOwnerSurface_Arm64ePAuthAndAuthenticatedControlKeepsRepresentativeAnchors
{
    [self assertRelativePath:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/ISAFamilies/Arm64e/TCTIPAuthSemanticTests.m"
              containsTokens:@[
                  @"PACIARejectsUntilPAuthIsImplemented",
                  @"AUTIARejectsUntilPAuthIsImplemented",
                  @"BTIRejectsUntilBranchTargetIdentificationIsImplemented",
                  @"LDRAARejectsUntilAuthenticatedLoadsExist",
              ]
                       label:@"Arm64e/PAuthAndAuthenticatedControl"];
}

- (void)testOwnerSurface_RuntimeNorthStarGuestLayerKeepsBusyboxAndUnameAnchors
{
    [self assertRelativePath:@"Tests/IXLandLinuxRuntimeSystemTests/Guest/Execution/GuestBusyboxRuntimeExecutionTests.m"
              containsTokens:@[
                  @"interactive busybox shell prompt must appear within 1024 single-step TCTI",
                  @"direct busybox ls -a / must reach the root stat path before exit",
                  @"direct busybox uname -m must return from the uname syscall before crashing",
                  @"OrderedExclusiveAtomic",
                  @"VectorIntegerLogical",
              ]
                       label:@"Guest runtime north-star layer"];
}

- (void)testOwnerSurface_RuntimeNorthStarPerfLayerKeepsSyntheticPerfDebtVisible
{
    [self assertRelativePath:@"Tests/IXLandLinuxRuntimePerfTests/Perf/TCTIHotPath/TCTIHotPathPerfTests.m"
              containsTokens:@[
                  @"testFetchLatency_RealGuestFetchPath",
                  @"testDecodeLatency_RealInstructionMix",
                  @"testLoweringLatency_RealGenerationPath",
                  @"testBlockCacheAndGenerationLatency_RealCompileAndCachePath",
                  @"testAtomicAndExclusiveEngineLatency_RealAtomicExecutionPath",
              ]
                       label:@"Perf layer"];
}

@end
