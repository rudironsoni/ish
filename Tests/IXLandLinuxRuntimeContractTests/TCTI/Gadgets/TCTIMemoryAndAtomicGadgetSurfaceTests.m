#import <XCTest/XCTest.h>

#include <dlfcn.h>
#include <stdint.h>

typedef void (*tcti_gadget_t)(void);

extern void gadget_load_sp(void);
extern void gadget_store_sp(void);
extern const tcti_gadget_t gadget_load_xreg_16_to_30[15];
extern const tcti_gadget_t gadget_store_xreg_16_to_30[15];
extern tcti_gadget_t gadget_extend_x14;
extern tcti_gadget_t gadget_exit;
extern tcti_gadget_t gadget_pc_advance;

@interface TCTIMemoryAndAtomicGadgetSurfaceTests : XCTestCase
@end

#define TCTI_DECLARE_GADGET_SYMBOL_TEST(_name, _symbol)                                          \
- (void)testGadgetSurface_##_name                                                                 \
{                                                                                                 \
    XCTAssertNotEqual(dlsym(RTLD_DEFAULT, _symbol), NULL,                                         \
                      @"%s must be link-visible because memory/atomic gadgets are an explicit "   \
                       @"TCTI runtime boundary", _symbol);                                        \
}

#define TCTI_DECLARE_GADGET_POINTER_TEST(_name, _symbol)                                          \
- (void)testGadgetSurface_##_name                                                                  \
{                                                                                                  \
    XCTAssertNotEqual((uintptr_t)(_symbol), (uintptr_t)0,                                          \
                      @"%s must stay populated because memory lowering emits it", #_symbol);       \
}

#define TCTI_DECLARE_GADGET_TABLE_TEST(_name, _table)                                              \
- (void)testGadgetSurface_##_name                                                                  \
{                                                                                                  \
    XCTAssertNotEqual((uintptr_t)(_table[0]), (uintptr_t)0,                                        \
                      @"%s must keep the first helper populated", #_table);                        \
    XCTAssertNotEqual((uintptr_t)(_table[14]), (uintptr_t)0,                                       \
                      @"%s must keep the last helper populated", #_table);                         \
}

@implementation TCTIMemoryAndAtomicGadgetSurfaceTests

TCTI_DECLARE_GADGET_SYMBOL_TEST(LdrImm, "gadget_ldr_imm")
TCTI_DECLARE_GADGET_SYMBOL_TEST(LdrReg, "gadget_ldr_reg")
TCTI_DECLARE_GADGET_SYMBOL_TEST(LdrbImm, "gadget_ldrb_imm")
TCTI_DECLARE_GADGET_SYMBOL_TEST(LdrhImm, "gadget_ldrh_imm")
TCTI_DECLARE_GADGET_SYMBOL_TEST(LdrswImm, "gadget_ldrsw_imm")
TCTI_DECLARE_GADGET_SYMBOL_TEST(StrImm, "gadget_str_imm")
TCTI_DECLARE_GADGET_SYMBOL_TEST(StrReg, "gadget_str_reg")
TCTI_DECLARE_GADGET_SYMBOL_TEST(StrbImm, "gadget_strb_imm")
TCTI_DECLARE_GADGET_SYMBOL_TEST(StrhImm, "gadget_strh_imm")
TCTI_DECLARE_GADGET_SYMBOL_TEST(Ldp, "gadget_ldp")
TCTI_DECLARE_GADGET_SYMBOL_TEST(Stp, "gadget_stp")
TCTI_DECLARE_GADGET_SYMBOL_TEST(LdrX, "gadget_ldr_x")
TCTI_DECLARE_GADGET_SYMBOL_TEST(StrX, "gadget_str_x")
TCTI_DECLARE_GADGET_SYMBOL_TEST(AtomicLdst, "gadget_atomic_ldst")
TCTI_DECLARE_GADGET_SYMBOL_TEST(SimdLdst, "gadget_simd_ldst")
TCTI_DECLARE_GADGET_POINTER_TEST(LoadSP, gadget_load_sp)
TCTI_DECLARE_GADGET_POINTER_TEST(StoreSP, gadget_store_sp)
TCTI_DECLARE_GADGET_TABLE_TEST(LoadXReg16To30, gadget_load_xreg_16_to_30)
TCTI_DECLARE_GADGET_TABLE_TEST(StoreXReg16To30, gadget_store_xreg_16_to_30)
TCTI_DECLARE_GADGET_POINTER_TEST(ExtendX14, gadget_extend_x14)
TCTI_DECLARE_GADGET_POINTER_TEST(Exit, gadget_exit)
TCTI_DECLARE_GADGET_POINTER_TEST(PCAdvance, gadget_pc_advance)

@end
