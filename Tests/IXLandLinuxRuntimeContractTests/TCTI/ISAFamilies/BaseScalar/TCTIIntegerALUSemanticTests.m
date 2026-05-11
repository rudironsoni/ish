#import <XCTest/XCTest.h>

#include "../../Support/ISAFamilies/BaseScalar/tcti_scalar_runtime_semantic_scenarios.h"
#include "../../Support/ISAFamilies/Memory/tcti_memory_atomic_runtime_semantic_scenarios.h"
#include "../../Support/ISAFamilies/Memory/tcti_memory_pair_semantic_scenarios.h"

@interface TCTIIntegerALUSemanticTests : XCTestCase
@end

@implementation TCTIIntegerALUSemanticTests

- (void)testSemanticExecutionContract_MOVZSMULLPrefixPreservesExpectedX0
{
    XCTAssertEqual(tcti_semantic_case_movz_smull_prefix_preserves_expected_x0(), 0ULL,
                   @"The live movz; smull prefix must leave guest x0 at the exact 64-bit product "
                    "of W0 and W1 before any later helper/store path runs");
}

- (void)testSemanticExecutionContract_UMADDLUsesUnsigned32BitInputs
{
    XCTAssertEqual(tcti_semantic_case_umaddl_uses_unsigned_32bit_inputs(), 35ULL,
                   @"TCTI UMADDL lowering must use unsigned 32-bit operands from Wn/Wm rather "
                    "than stale 64-bit register contents");
}

- (void)testSemanticExecutionContract_ADDToMemoryBackedX23StoresResult
{
    XCTAssertEqual(tcti_semantic_case_add_hot_pair_to_memory_backed_x23(), 0x5655ac10ULL,
                   @"TCTI ADD with a memory-backed high destination must store the computed "
                    "result into architectural x23");
}

- (void)testSemanticExecutionContract_BusyboxAllocatorMSUBCallbackRoundtrip
{
    XCTAssertEqual(tcti_semantic_case_busybox_allocator_msub_callback_roundtrip(), 0ULL,
                   @"TCTI must preserve the current busybox allocator bookkeeping block, "
                    "including SDIV/MSUB, stack spill/reload, BLR/RET, and the final "
                    "accumulator update used before the ls OOM path.");
}

- (void)testSemanticExecutionContract_UDIVPreservesFlagsForFollowingCSEL
{
    XCTAssertEqual(tcti_semantic_case_udiv_preserves_flags_for_csel_eq(), 0x1111111111111111ULL,
                   @"TCTI UDIV fallback must preserve NZCV so a following CSEL EQ observes the "
                    "pre-divide flags instead of helper-call host flags");
}

- (void)testSemanticExecutionContract_MuslOpenedLibcValidationUsesMulAlias
{
    XCTAssertEqual(tcti_semantic_case_musl_opened_libc_validation_uses_mul_alias(), 0ULL,
                   @"TCTI must decode and execute the live MUL alias on musl's opened-libc "
                    "validation path so ld-musl computes the expected ELF size product instead "
                    "of taking the false failure path.");
}

- (void)testSemanticExecutionContract_ADDShiftedHotUsesScratchCarrier
{
    XCTAssertEqual(tcti_semantic_case_add_shifted_hot_uses_scratch_carrier(), 0ULL,
                   @"TCTI ADD shifted-register must use scratch carriers without losing the "
                    "shifted operand or corrupting hot destination registers");
}

- (void)testSemanticExecutionContract_ADDExtendedUXTWUses32BitOperand
{
    XCTAssertEqual(tcti_semantic_case_add_extended_uxtw_uses_32bit_operand(), 0ULL,
                   @"TCTI ADD extended-register must zero-extend Wm and add it to Xn before "
                    "GNU hash chain address calculation");
}

- (void)testSemanticExecutionContract_MuslCallocOverflowGuardUsesRealUMULH
{
    XCTAssertEqual(
        tcti_semantic_case_musl_calloc_overflow_guard_umulh_stays_zero_for_small_product(), 0ULL,
        @"TCTI must decode and execute musl calloc's UMULH overflow guard as a real multiply-high "
         "operation, not as a 3-source multiply-add with a stale Ra addend.");
}

- (void)testSemanticExecutionContract_PLTRELRELAStrideSelector
{
    XCTAssertEqual(tcti_semantic_case_pltrel_rela_stride_selector(), 3ULL,
                   @"TCTI must compute DT_RELA PLT relocation stride as 3 entries after "
                    "CMP/CSET/ADD");
}

- (void)testSemanticExecutionContract_SMADDLUsesSigned32BitInputs
{
    XCTAssertEqual(tcti_semantic_case_smaddl_uses_signed_32bit_inputs(), 12ULL,
                   @"TCTI SMADDL lowering must use signed 32-bit operands from Wn/Wm rather than "
                    "leaking stale high bits from Xn/Xm");
}

- (void)testSemanticExecutionContract_MuslSecsToTmSMULHAsrSubBlockKeepsDayCount
{
    XCTAssertEqual(tcti_semantic_case_musl_secs_to_tm_smulh_asr_sub_block(), 0x255dULL,
                   @"TCTI must preserve musl __secs_to_tm's live SMULH/ASR/SUB block so "
                    "localtime_r keeps the day count for normal 2026 timestamps instead of "
                    "taking the EINVAL/NULL return path");
}

@end
