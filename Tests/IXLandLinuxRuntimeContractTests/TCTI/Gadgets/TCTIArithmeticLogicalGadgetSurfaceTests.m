#import <XCTest/XCTest.h>

#include <stdint.h>

typedef void (*tcti_gadget_t)(void);

extern const tcti_gadget_t gadget_mov_reg[16][16];
extern const tcti_gadget_t gadget_add_imm[16][16][16];
extern const tcti_gadget_t gadget_sub_imm[16][16][16];
extern const tcti_gadget_t gadget_mov_imm[16];
extern const tcti_gadget_t gadget_add_reg[16][16][16];
extern const tcti_gadget_t gadget_sub_reg[16][16][16];
extern const tcti_gadget_t gadget_and_reg[16][16][16];
extern const tcti_gadget_t gadget_orr_reg[16][16][16];
extern const tcti_gadget_t gadget_eor_reg[16][16][16];
extern const tcti_gadget_t gadget_cmp_reg[16][16];
extern const tcti_gadget_t gadget_adds_reg[16][16][16];
extern const tcti_gadget_t gadget_subs_reg[16][16][16];
extern const tcti_gadget_t gadget_rbit_wreg[16][16];
extern const tcti_gadget_t gadget_rbit_xreg[16][16];
extern const tcti_gadget_t gadget_clz_wreg[16][16];
extern const tcti_gadget_t gadget_clz_xreg[16][16];
extern const tcti_gadget_t gadget_rev_wreg[16][16];
extern const tcti_gadget_t gadget_rev_xreg[16][16];
extern const tcti_gadget_t gadget_rev16_wreg[16][16];
extern const tcti_gadget_t gadget_rev16_xreg[16][16];
extern const tcti_gadget_t gadget_rev32_xreg[16][16];
extern tcti_gadget_t gadget_sbfm;
extern tcti_gadget_t gadget_bfm;
extern tcti_gadget_t gadget_ubfm;
extern tcti_gadget_t gadget_movk;
extern tcti_gadget_t gadget_write_reg_imm;
extern tcti_gadget_t gadget_addsub_imm_fallback;
extern tcti_gadget_t gadget_addsub_reg_fallback;
extern tcti_gadget_t gadget_addsub_ext_fallback;
extern tcti_gadget_t gadget_logical_imm_fallback;
extern tcti_gadget_t gadget_logical_reg_fallback;
extern tcti_gadget_t gadget_multiply_add_fallback;
extern tcti_gadget_t gadget_shift_reg_fallback;
extern tcti_gadget_t gadget_extract_fallback;
extern tcti_gadget_t gadget_div_fallback;
extern tcti_gadget_t gadget_csel_fallback;
extern tcti_gadget_t gadget_ccmp_fallback;

@interface TCTIArithmeticLogicalGadgetSurfaceTests : XCTestCase
@end

#define TCTI_DECLARE_GADGET_POINTER_TEST(_name, _symbol)                                          \
- (void)testGadgetSurface_##_name                                                                  \
{                                                                                                  \
    XCTAssertNotEqual((uintptr_t)(_symbol), (uintptr_t)0,                                          \
                      @"%s must stay populated because arithmetic/logical lowering consumes it",   \
                      #_symbol);                                                                    \
}

#define TCTI_DECLARE_GADGET_TABLE_TEST(_name, _table)                                              \
- (void)testGadgetSurface_##_name                                                                  \
{                                                                                                  \
    XCTAssertNotEqual((uintptr_t)(_table[0]), (uintptr_t)0,                                        \
                      @"%s must keep real entries because generator table dispatch depends on it", \
                      #_table);                                                                     \
    XCTAssertNotEqual((uintptr_t)(_table[14]), (uintptr_t)0,                                       \
                      @"%s must cover the hot architectural edge of its table", #_table);          \
}

#define TCTI_DECLARE_GADGET_MATRIX_TEST(_name, _table)                                             \
- (void)testGadgetSurface_##_name                                                                  \
{                                                                                                  \
    XCTAssertNotEqual((uintptr_t)(_table[0][0][0]), (uintptr_t)0,                                  \
                      @"%s[0][0][0] must exist for generator lookup", #_table);                    \
    XCTAssertNotEqual((uintptr_t)(_table[14][14][14]), (uintptr_t)0,                               \
                      @"%s[14][14][14] must exist for generator lookup", #_table);                 \
}

#define TCTI_DECLARE_GADGET_TABLE2_TEST(_name, _table)                                             \
- (void)testGadgetSurface_##_name                                                                  \
{                                                                                                  \
    XCTAssertNotEqual((uintptr_t)(_table[0][0]), (uintptr_t)0,                                     \
                      @"%s[0][0] must exist for generator lookup", #_table);                       \
    XCTAssertNotEqual((uintptr_t)(_table[14][14]), (uintptr_t)0,                                   \
                      @"%s[14][14] must exist for generator lookup", #_table);                     \
}

#define TCTI_DECLARE_GADGET_VECTOR_TEST(_name, _table)                                             \
- (void)testGadgetSurface_##_name                                                                  \
{                                                                                                  \
    XCTAssertNotEqual((uintptr_t)(_table[0]), (uintptr_t)0,                                        \
                      @"%s[0] must exist for direct generator lookup", #_table);                   \
    XCTAssertNotEqual((uintptr_t)(_table[14]), (uintptr_t)0,                                       \
                      @"%s[14] must exist for direct generator lookup", #_table);                  \
}

@implementation TCTIArithmeticLogicalGadgetSurfaceTests

TCTI_DECLARE_GADGET_TABLE2_TEST(MovRegTable, gadget_mov_reg)
TCTI_DECLARE_GADGET_MATRIX_TEST(AddImmTable, gadget_add_imm)
TCTI_DECLARE_GADGET_MATRIX_TEST(SubImmTable, gadget_sub_imm)
TCTI_DECLARE_GADGET_VECTOR_TEST(MovImmTable, gadget_mov_imm)
TCTI_DECLARE_GADGET_MATRIX_TEST(AddRegTable, gadget_add_reg)
TCTI_DECLARE_GADGET_MATRIX_TEST(SubRegTable, gadget_sub_reg)
TCTI_DECLARE_GADGET_MATRIX_TEST(AndRegTable, gadget_and_reg)
TCTI_DECLARE_GADGET_MATRIX_TEST(OrrRegTable, gadget_orr_reg)
TCTI_DECLARE_GADGET_MATRIX_TEST(EorRegTable, gadget_eor_reg)
TCTI_DECLARE_GADGET_TABLE2_TEST(CmpRegTable, gadget_cmp_reg)
TCTI_DECLARE_GADGET_MATRIX_TEST(AddsRegTable, gadget_adds_reg)
TCTI_DECLARE_GADGET_MATRIX_TEST(SubsRegTable, gadget_subs_reg)
TCTI_DECLARE_GADGET_TABLE2_TEST(RbitWRegTable, gadget_rbit_wreg)
TCTI_DECLARE_GADGET_TABLE2_TEST(RbitXRegTable, gadget_rbit_xreg)
TCTI_DECLARE_GADGET_TABLE2_TEST(ClzWRegTable, gadget_clz_wreg)
TCTI_DECLARE_GADGET_TABLE2_TEST(ClzXRegTable, gadget_clz_xreg)
TCTI_DECLARE_GADGET_TABLE2_TEST(RevWRegTable, gadget_rev_wreg)
TCTI_DECLARE_GADGET_TABLE2_TEST(RevXRegTable, gadget_rev_xreg)
TCTI_DECLARE_GADGET_TABLE2_TEST(Rev16WRegTable, gadget_rev16_wreg)
TCTI_DECLARE_GADGET_TABLE2_TEST(Rev16XRegTable, gadget_rev16_xreg)
TCTI_DECLARE_GADGET_TABLE2_TEST(Rev32XRegTable, gadget_rev32_xreg)
TCTI_DECLARE_GADGET_POINTER_TEST(Sbfm, gadget_sbfm)
TCTI_DECLARE_GADGET_POINTER_TEST(Bfm, gadget_bfm)
TCTI_DECLARE_GADGET_POINTER_TEST(Ubfm, gadget_ubfm)
TCTI_DECLARE_GADGET_POINTER_TEST(Movk, gadget_movk)
TCTI_DECLARE_GADGET_POINTER_TEST(WriteRegImm, gadget_write_reg_imm)
TCTI_DECLARE_GADGET_POINTER_TEST(AddSubImmFallback, gadget_addsub_imm_fallback)
TCTI_DECLARE_GADGET_POINTER_TEST(AddSubRegFallback, gadget_addsub_reg_fallback)
TCTI_DECLARE_GADGET_POINTER_TEST(AddSubExtFallback, gadget_addsub_ext_fallback)
TCTI_DECLARE_GADGET_POINTER_TEST(LogicalImmFallback, gadget_logical_imm_fallback)
TCTI_DECLARE_GADGET_POINTER_TEST(LogicalRegFallback, gadget_logical_reg_fallback)
TCTI_DECLARE_GADGET_POINTER_TEST(MultiplyAddFallback, gadget_multiply_add_fallback)
TCTI_DECLARE_GADGET_POINTER_TEST(ShiftRegFallback, gadget_shift_reg_fallback)
TCTI_DECLARE_GADGET_POINTER_TEST(ExtractFallback, gadget_extract_fallback)
TCTI_DECLARE_GADGET_POINTER_TEST(DivFallback, gadget_div_fallback)
TCTI_DECLARE_GADGET_POINTER_TEST(CselFallback, gadget_csel_fallback)
TCTI_DECLARE_GADGET_POINTER_TEST(CcmpFallback, gadget_ccmp_fallback)

@end
