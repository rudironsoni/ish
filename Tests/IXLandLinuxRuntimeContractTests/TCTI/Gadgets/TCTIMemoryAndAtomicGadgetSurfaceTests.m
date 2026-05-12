#import <XCTest/XCTest.h>

#include <stdint.h>

typedef void (*tcti_gadget_t)(void);

extern void gadget_load_sp(void);
extern void gadget_store_sp(void);
extern const tcti_gadget_t gadget_load_xreg_16_to_30[18];
extern const tcti_gadget_t gadget_store_xreg_16_to_30[18];
extern tcti_gadget_t gadget_ldr_x;
extern tcti_gadget_t gadget_str_x;
extern tcti_gadget_t gadget_atomic_ldst;
extern tcti_gadget_t gadget_simd_ldst;
extern tcti_gadget_t gadget_extend_x14;
extern tcti_gadget_t gadget_exit;
extern tcti_gadget_t gadget_pc_advance;

@interface TCTIMemoryAndAtomicGadgetSurfaceTests : XCTestCase
@end

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
    XCTAssertNotEqual((uintptr_t)(_table[17]), (uintptr_t)0,                                       \
                      @"%s must keep the last helper populated", #_table);                         \
}

@implementation TCTIMemoryAndAtomicGadgetSurfaceTests

TCTI_DECLARE_GADGET_POINTER_TEST(LoadSP, gadget_load_sp)
TCTI_DECLARE_GADGET_POINTER_TEST(StoreSP, gadget_store_sp)
TCTI_DECLARE_GADGET_TABLE_TEST(LoadXReg16To30, gadget_load_xreg_16_to_30)
TCTI_DECLARE_GADGET_TABLE_TEST(StoreXReg16To30, gadget_store_xreg_16_to_30)
TCTI_DECLARE_GADGET_POINTER_TEST(LdrX, gadget_ldr_x)
TCTI_DECLARE_GADGET_POINTER_TEST(StrX, gadget_str_x)
TCTI_DECLARE_GADGET_POINTER_TEST(AtomicLdst, gadget_atomic_ldst)
TCTI_DECLARE_GADGET_POINTER_TEST(SimdLdst, gadget_simd_ldst)
TCTI_DECLARE_GADGET_POINTER_TEST(ExtendX14, gadget_extend_x14)
TCTI_DECLARE_GADGET_POINTER_TEST(Exit, gadget_exit)
TCTI_DECLARE_GADGET_POINTER_TEST(PCAdvance, gadget_pc_advance)

@end
