#import <XCTest/XCTest.h>

#include <stdint.h>

typedef void (*tcti_gadget_t)(void);

extern tcti_gadget_t gadget_b;
extern const tcti_gadget_t gadget_bcond[16];
extern const tcti_gadget_t gadget_cbz_wreg[16];
extern const tcti_gadget_t gadget_cbnz_wreg[16];
extern const tcti_gadget_t gadget_cbz_xreg[16];
extern const tcti_gadget_t gadget_cbnz_xreg[16];
extern const tcti_gadget_t gadget_tbz_reg[16];
extern const tcti_gadget_t gadget_tbnz_reg[16];
extern const tcti_gadget_t gadget_tbz_wreg[16];
extern const tcti_gadget_t gadget_tbnz_wreg[16];
extern const tcti_gadget_t gadget_tbz_xreg[16];
extern const tcti_gadget_t gadget_tbnz_xreg[16];
extern void gadget_br_impl(void);
extern void gadget_bcond_fallback_impl(void);

@interface TCTIBranchAndControlGadgetSurfaceTests : XCTestCase
@end

#define TCTI_DECLARE_GADGET_POINTER_TEST(_name, _symbol)                                           \
- (void)testGadgetSurface_##_name                                                                   \
{                                                                                                   \
    XCTAssertNotEqual((uintptr_t)(_symbol), (uintptr_t)0,                                           \
                      @"%s must stay populated because control-flow lowering emits it", #_symbol);  \
}

#define TCTI_DECLARE_GADGET_TABLE_TEST(_name, _table)                                               \
- (void)testGadgetSurface_##_name                                                                   \
{                                                                                                   \
    XCTAssertNotEqual((uintptr_t)(_table[0]), (uintptr_t)0,                                         \
                      @"%s must keep the low table entry populated", #_table);                      \
    XCTAssertNotEqual((uintptr_t)(_table[15]), (uintptr_t)0,                                        \
                      @"%s must keep the high table entry populated", #_table);                     \
}

@implementation TCTIBranchAndControlGadgetSurfaceTests

TCTI_DECLARE_GADGET_POINTER_TEST(B, gadget_b)
TCTI_DECLARE_GADGET_TABLE_TEST(BcondTable, gadget_bcond)
TCTI_DECLARE_GADGET_TABLE_TEST(CbzWRegTable, gadget_cbz_wreg)
TCTI_DECLARE_GADGET_TABLE_TEST(CbnzWRegTable, gadget_cbnz_wreg)
TCTI_DECLARE_GADGET_TABLE_TEST(CbzXRegTable, gadget_cbz_xreg)
TCTI_DECLARE_GADGET_TABLE_TEST(CbnzXRegTable, gadget_cbnz_xreg)
TCTI_DECLARE_GADGET_TABLE_TEST(TbzRegTable, gadget_tbz_reg)
TCTI_DECLARE_GADGET_TABLE_TEST(TbnzRegTable, gadget_tbnz_reg)
TCTI_DECLARE_GADGET_TABLE_TEST(TbzWRegTable, gadget_tbz_wreg)
TCTI_DECLARE_GADGET_TABLE_TEST(TbnzWRegTable, gadget_tbnz_wreg)
TCTI_DECLARE_GADGET_TABLE_TEST(TbzXRegTable, gadget_tbz_xreg)
TCTI_DECLARE_GADGET_TABLE_TEST(TbnzXRegTable, gadget_tbnz_xreg)
TCTI_DECLARE_GADGET_POINTER_TEST(BrImpl, gadget_br_impl)
TCTI_DECLARE_GADGET_POINTER_TEST(BcondFallbackImpl, gadget_bcond_fallback_impl)

@end
