#import <XCTest/XCTest.h>

#include "../../Support/ISAFamilies/BaseScalar/tcti_scalar_runtime_semantic_scenarios.h"

@interface TCTIMoveImmediateAndAddressSemanticTests : XCTestCase
@end

@implementation TCTIMoveImmediateAndAddressSemanticTests

- (void)testSemanticExecutionContract_MuslCallocPLTADRPResolvesLocalGOTPage
{
    XCTAssertEqual(tcti_semantic_case_musl_calloc_plt_adrp_resolves_local_got_page(), 0ULL,
                   @"TCTI ADRP lowering must derive the guest GOT page from the current PC so "
                    "musl's calloc@plt loads its own GOT slot instead of reusing a stale page");
}

- (void)testSemanticExecutionContract_MuslCallbackSlotADRPADDMaterializesLdsoTargetPage
{
    XCTAssertEqual(
        tcti_semantic_case_musl_callback_slot_adrp_add_materializes_ldso_target_page(), 0ULL,
        @"The live ld-musl callback-slot producer must materialize ADRP from the guest PC page, "
         "then apply the following ADD immediate so the stored slot target stays in the ldso text "
         "image instead of collapsing into a signed page delta.");
}

- (void)testSemanticExecutionContract_MuslLibcNameLiteralBaseMaterializesBeforeCompare
{
    XCTAssertEqual(
        tcti_semantic_case_musl_libc_name_literal_base_materializes_before_compare(), 0ULL,
        @"The live ld-musl libc-name compare setup must materialize the literal base in x0, "
         "copy x25 into x17, and land on the compare loop entry before any byte loads.");
}

- (void)testSemanticExecutionContract_MuslLibcNameSetupPreservesHotX0AcrossMOVX17X25
{
    XCTAssertEqual(
        tcti_semantic_case_musl_libc_name_setup_preserves_hot_x0_across_mov_x17_x25(), 0ULL,
        @"The live ld-musl setup block must preserve hot guest x0 after ADRP while copying x25 "
         "into memory-backed x17 for the later libc-name compare path.");
}

@end
