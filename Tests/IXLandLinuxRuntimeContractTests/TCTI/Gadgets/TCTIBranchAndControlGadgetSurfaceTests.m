#import <XCTest/XCTest.h>

#include <dlfcn.h>
#include <stdint.h>

typedef void (*tcti_gadget_t)(void);

extern const tcti_gadget_t gadget_bcond[16];
extern const tcti_gadget_t gadget_cbz_reg[16];
extern const tcti_gadget_t gadget_cbnz_reg[16];
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
extern void gadget_csel_eq_0_1_2(void);
extern void gadget_csel_ne_0_1_2(void);
extern void gadget_csel_cs_0_1_2(void);
extern void gadget_csel_cc_0_1_2(void);

@interface TCTIBranchAndControlGadgetSurfaceTests : XCTestCase
@end

#define TCTI_DECLARE_GADGET_SYMBOL_TEST(_name, _symbol)                                          \
- (void)testGadgetSurface_##_name                                                                 \
{                                                                                                 \
    XCTAssertNotEqual(dlsym(RTLD_DEFAULT, _symbol), NULL,                                         \
                      @"%s must be link-visible because branch/control gadgets are an explicit "  \
                       @"guest-control proof boundary", _symbol);                                 \
}

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

TCTI_DECLARE_GADGET_SYMBOL_TEST(B, "gadget_b")
TCTI_DECLARE_GADGET_SYMBOL_TEST(BCond, "gadget_b_cond")
TCTI_DECLARE_GADGET_SYMBOL_TEST(Bl, "gadget_bl")
TCTI_DECLARE_GADGET_SYMBOL_TEST(Br, "gadget_br")
TCTI_DECLARE_GADGET_SYMBOL_TEST(Blr, "gadget_blr")
TCTI_DECLARE_GADGET_SYMBOL_TEST(Ret, "gadget_ret")
TCTI_DECLARE_GADGET_SYMBOL_TEST(Cbz, "gadget_cbz")
TCTI_DECLARE_GADGET_SYMBOL_TEST(Cbnz, "gadget_cbnz")
TCTI_DECLARE_GADGET_TABLE_TEST(BcondTable, gadget_bcond)
TCTI_DECLARE_GADGET_TABLE_TEST(CbzRegTable, gadget_cbz_reg)
TCTI_DECLARE_GADGET_TABLE_TEST(CbnzRegTable, gadget_cbnz_reg)
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
TCTI_DECLARE_GADGET_POINTER_TEST(CselEqNative, gadget_csel_eq_0_1_2)
TCTI_DECLARE_GADGET_POINTER_TEST(CselNeNative, gadget_csel_ne_0_1_2)
TCTI_DECLARE_GADGET_POINTER_TEST(CselCsNative, gadget_csel_cs_0_1_2)
TCTI_DECLARE_GADGET_POINTER_TEST(CselCcNative, gadget_csel_cc_0_1_2)

@end
