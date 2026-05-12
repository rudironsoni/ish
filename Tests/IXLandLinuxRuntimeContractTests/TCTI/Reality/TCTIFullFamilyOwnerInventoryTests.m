#import <XCTest/XCTest.h>

@interface TCTIFullFamilyOwnerInventoryTests : XCTestCase
@end

@implementation TCTIFullFamilyOwnerInventoryTests

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
                      @"%@ must keep explicit anchor `%@` in %@",
                      label, token, relativePath);
    }
}

- (void)testPipelineInventory_Fetch
{
    [self assertRelativePath:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/Pipeline/Fetch/TCTIPipelineFetchTests.m"
              containsTokens:@[
                  @"ValidGuestPC_ReturnsInstructionWord",
                  @"InvalidGuestPC_ReturnsFault",
                  @"PageCrossing_IsDeterministic",
              ]
                       label:@"Pipeline/Fetch"];
}

- (void)testPipelineInventory_Decode
{
    [self assertRelativePath:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/Pipeline/Decode/TCTIPipelineDecodeTests.m"
              containsTokens:@[
                  @"ADRPClassifiesAsPCRelativePageAddressing",
                  @"CASPublishesCompareAndSwapRegisterRolesThroughAtomicFamily",
                  @"DUPRepresentativePublishesSIMDFamilyShape",
                  @"PACIARejectsUntilArm64eDecodeOwnershipExists",
              ]
                       label:@"Pipeline/Decode"];
}

- (void)testPipelineInventory_Lowering
{
    [self assertRelativePath:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/Pipeline/Lowering/TCTIPipelineLoweringTests.m"
              containsTokens:@[
                  @"MSRTPIDREL0LowersThroughSysregGadget",
                  @"LDAXRDecodesAndLowersAsAtomicTCTI",
                  @"CMEQLowersThroughVectorCompareMaskGadget",
                  @"PACIARejectsUntilArm64eLoweringOwnershipExists",
              ]
                       label:@"Pipeline/Lowering"];
}

- (void)testPipelineInventory_DispatchPreservation
{
    [self assertRelativePath:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/Pipeline/DispatchPreservation/TCTIPipelineDispatchPreservationTests.m"
              containsTokens:@[
                  @"CMPAddCSELBlocksDecodeAndLower",
                  @"FlagsPublishedByOneBlockRemainVisibleToLaterBlocks",
              ]
                       label:@"Pipeline/DispatchPreservation"];
}

- (void)testPipelineInventory_ExitWriteback
{
    [self assertRelativePath:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/Pipeline/ExitWriteback/TCTIPipelineExitWritebackTests.m"
              containsTokens:@[
                  @"STRPostIndexDecodesLowersAndPublishesUpdatedBaseOnExit",
                  @"LDPPostIndexAliasBaseUsesOriginalAddressThenPublishesWriteback",
              ]
                       label:@"Pipeline/ExitWriteback"];
}

- (void)testPipelineInventory_BlockCacheAndGeneration
{
    [self assertRelativePath:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/Pipeline/BlockCacheAndGeneration/TCTIPipelineBlockCacheGenerationTests.m"
              containsTokens:@[
                  @"ExecutableGuestWriteInvalidatesCompiledBlockAtSamePC",
                  @"invalidate stale ",
                   @"compiled blocks before the second execution",
              ]
                       label:@"Pipeline/BlockCacheAndGeneration"];
}

- (void)testPipelineInventory_AtomicAndExclusiveEngine
{
    [self assertRelativePath:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/Pipeline/AtomicAndExclusiveEngine/TCTIPipelineAtomicAndExclusiveEngineTests.m"
              containsTokens:@[
                  @"LDAXRAndSTLXRDecodeAndLowerAsAtomicSequence",
                  @"SuccessPathPublishesStatusMemoryAndMonitorState",
                  @"FailedSTLXRLeavesMemoryUntouchedAndReturnsFailure",
              ]
                       label:@"Pipeline/AtomicAndExclusiveEngine"];
}

- (void)testFamilyInventory_BaseScalarControlAndFlags
{
    [self assertRelativePath:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/ISAFamilies/BaseScalar/TCTIControlAndFlagsSemanticTests.m"
              containsTokens:@[
                  @"CCMP",
                  @"CSET",
                  @"CBZ",
                  @"BusyboxPromptLoopCCMPBLSExitsTakenPath",
              ]
                       label:@"Family/BaseScalar/ControlAndFlags"];
}

- (void)testFamilyInventory_BaseScalarIntegerALU
{
    [self assertRelativePath:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/ISAFamilies/BaseScalar/TCTIIntegerALUSemanticTests.m"
              containsTokens:@[
                  @"ADDToMemoryBackedX23StoresResult",
                  @"UDIV",
                  @"UMADDLUsesUnsigned32BitInputs",
                  @"SMADDLUsesSigned32BitInputs",
              ]
                       label:@"Family/BaseScalar/IntegerALU"];
}

- (void)testFamilyInventory_BaseScalarMoveImmediateAndAddress
{
    [self assertRelativePath:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/ISAFamilies/BaseScalar/TCTIMoveImmediateAndAddressSemanticTests.m"
              containsTokens:@[
                  @"ADRP",
                  @"MuslCallocPLTADRPResolvesLocalGOTPage",
                  @"MuslCallbackSlotADRPADDMaterializesLdsoTargetPage",
              ]
                       label:@"Family/BaseScalar/MoveImmediateAndAddress"];
}

- (void)testFamilyInventory_BaseScalarLogicalBitfieldShift
{
    [self assertRelativePath:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/ISAFamilies/BaseScalar/TCTILogicalBitfieldShiftSemanticTests.m"
              containsTokens:@[
                  @"LSLV64BitUsesFullShiftAmount",
                  @"LSLVMemoryBackedRegistersRoundtrip",
                  @"LDR/LSLV/ORR/STR",
              ]
                       label:@"Family/BaseScalar/LogicalBitfieldShift"];
}

- (void)testFamilyInventory_MemoryScalarLoadsStores
{
    [self assertRelativePath:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/ISAFamilies/Memory/TCTIScalarLoadStoreSemanticTests.m"
              containsTokens:@[
                  @"LDRFromMemoryBackedBaseSyncsHotDestination",
                  @"SPRelativeLDRSTRRoundtrip",
                  @"RegOffsetLDRX0AliasBaseReadsExpectedQword",
              ]
                       label:@"Family/Memory/ScalarLoadsStores"];
}

- (void)testFamilyInventory_MemoryPairAndFrame
{
    [self assertRelativePath:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/ISAFamilies/Memory/TCTIPairAndFrameSemanticTests.m"
              containsTokens:@[
                  @"LDPX2X0FromMemoryBackedX21",
                  @"LDPPostIndexFirstDestinationPreservesPairBase",
                  @"LDPSWPairSignExtendsLiveMuslOffsets",
              ]
                       label:@"Family/Memory/PairAndFrame"];
}

- (void)testFamilyInventory_MemoryOrderedExclusiveAtomic
{
    [self assertRelativePath:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/ISAFamilies/Memory/TCTIAtomicAndExclusiveSemanticTests.m"
              containsTokens:@[
                  @"MuslMutexLDAXRSTLXRRoundtripsLockWord",
                  @"LDARWLoadsWordWithoutRequiringPreexistingExclusiveMonitor",
                  @"CASWWAtomicallySwapsMatchingValue",
                  @"TCTI_DECLARE_ATOMIC_SEMANTIC_GAP_TEST(OrderedExclusiveAtomic_LDAPR",
              ]
                       label:@"Family/Memory/OrderedExclusiveAtomic"];
}

- (void)testFamilyInventory_MemorySpecialAndTagging
{
    [self assertRelativePath:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/ISAFamilies/Memory/TCTISpecialAndTaggingSemanticTests.m"
              containsTokens:@[
                  @"LDGRejectsUntilTaggingSemanticsExist",
                  @"STGRejectsUntilTaggingSemanticsExist",
              ]
                       label:@"Family/Memory/SpecialAndTagging"];
}

- (void)testFamilyInventory_SystemExceptionsBarriersHints
{
    [self assertRelativePath:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/ISAFamilies/System/TCTIExceptionsBarriersHintsSemanticTests.m"
              containsTokens:@[
                  @"DMB/DSB/ISB",
                  @"CLREX",
              ]
                       label:@"Family/System/ExceptionsBarriersHints"];
}

- (void)testFamilyInventory_SystemSysregCacheTLB
{
    [self assertRelativePath:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/ISAFamilies/System/TCTISysregCacheTLBSemanticTests.m"
              containsTokens:@[
                  @"TPIDR_EL0",
                  @"MSR/MRS",
              ]
                       label:@"Family/System/SysregCacheTLB"];
}

- (void)testFamilyInventory_SIMDFPScalarFP
{
    [self assertRelativePath:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/ISAFamilies/SIMDFP/TCTIScalarFPSemanticTests.m"
              containsTokens:@[
                  @"FADDDoubleAddsIEEE64Operands",
                  @"second scalar FMOV",
              ]
                       label:@"Family/SIMDFP/ScalarFP"];
}

- (void)testFamilyInventory_SIMDFPVectorIntegerLogical
{
    [self assertRelativePath:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/ISAFamilies/SIMDFP/TCTIVectorSemanticTests.m"
              containsTokens:@[
                  @"CNTComputesPerBytePopulationCount",
                  @"TBLLooksUpBytesAndZeroesOutOfRangeIndices",
                  @"MLSSubtractsVectorProductsFromDestination",
                  @"TCTI_DECLARE_VECTOR_SEMANTIC_GAP_TEST(VectorIntegerLogical_CMGE",
              ]
                       label:@"Family/SIMDFP/VectorIntegerLogical"];
}

- (void)testFamilyInventory_SIMDFPVectorMemory
{
    [self assertRelativePath:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/ISAFamilies/SIMDFP/TCTIVectorMemorySemanticTests.m"
              containsTokens:@[
                  @"LDRQLoadsFull128BitVector",
                  @"128-bit vector payload",
              ]
                       label:@"Family/SIMDFP/VectorMemory"];
}

- (void)testFamilyInventory_SIMDFPCryptoAndDotProduct
{
    [self assertRelativePath:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/ISAFamilies/SIMDFP/TCTICryptoAndDotProductSemanticTests.m"
              containsTokens:@[
                  @"AESERejectsUntilCryptoFamilyIsImplemented",
                  @"PMULLRejectsUntilCryptoFamilyIsImplemented",
              ]
                       label:@"Family/SIMDFP/CryptoAndDotProduct"];
}

- (void)testFamilyInventory_Arm64ePAuthAndAuthenticatedControl
{
    [self assertRelativePath:@"Tests/IXLandLinuxRuntimeContractTests/TCTI/ISAFamilies/Arm64e/TCTIPAuthSemanticTests.m"
              containsTokens:@[
                  @"PACIARejectsUntilPAuthIsImplemented",
                  @"AUTIARejectsUntilPAuthIsImplemented",
                  @"BTIRejectsUntilBranchTargetIdentificationIsImplemented",
                  @"LDRAARejectsUntilAuthenticatedLoadsExist",
              ]
                       label:@"Family/Arm64e/PAuthAndAuthenticatedControl"];
}

- (void)testRuntimeInventory_GuestLayerAnchors
{
    [self assertRelativePath:@"Tests/IXLandLinuxRuntimeSystemTests/Guest/Execution/GuestBusyboxRuntimeExecutionTests.m"
              containsTokens:@[
                  @"interactive busybox shell prompt must appear within 1024 single-step TCTI",
                  @"direct busybox ls -a / must reach the root stat path before exit",
                  @"direct busybox uname -m must return from the uname syscall before crashing",
                  @"OrderedExclusiveAtomic",
                  @"VectorIntegerLogical",
              ]
                       label:@"Runtime/GuestLayer"];
}

- (void)testRuntimeInventory_PerfLayerAnchors
{
    [self assertRelativePath:@"Tests/IXLandLinuxRuntimePerfTests/Perf/TCTIHotPath/TCTIHotPathPerfTests.m"
              containsTokens:@[
                  @"testFetchLatency_HotInstructions",
                  @"testDecodeLatency_CommonInstructions",
                  @"testLoweringLatency_ALUOperations",
                  @"testGadgetChains_HotSequences",
                  @"testPerfGapInventory_FetchProofStillSynthetic",
                  @"testPerfGapInventory_AtomicAndExclusiveEngineProofStillMissing",
              ]
                       label:@"Runtime/PerfLayer"];
}

@end
