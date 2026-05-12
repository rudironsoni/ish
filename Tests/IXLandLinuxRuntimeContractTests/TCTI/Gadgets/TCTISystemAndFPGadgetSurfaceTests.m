#import <XCTest/XCTest.h>

#include <stdint.h>

typedef void (*tcti_gadget_t)(void);

extern tcti_gadget_t gadget_svc;
extern tcti_gadget_t gadget_mrs;
extern tcti_gadget_t gadget_msr;
extern tcti_gadget_t gadget_isb;
extern tcti_gadget_t gadget_dsb;
extern tcti_gadget_t gadget_dmb;
extern tcti_gadget_t gadget_nop;
extern tcti_gadget_t gadget_clrex;
extern tcti_gadget_t gadget_fadd;
extern tcti_gadget_t gadget_dc_zva;
extern tcti_gadget_t gadget_sysreg_unsupported;
extern tcti_gadget_t gadget_ccmp_native_reg[2][2][16];
extern tcti_gadget_t gadget_ccmp_native_imm[2][2][16];
extern tcti_gadget_t gadget_simd_dup_gpr;
extern tcti_gadget_t gadget_simd_mov_gpr_from_vec;
extern tcti_gadget_t gadget_simd_movi_imm;
extern tcti_gadget_t gadget_simd_fmov_gpr;
extern tcti_gadget_t gadget_simd_ext;
extern tcti_gadget_t gadget_simd_cnt;
extern tcti_gadget_t gadget_simd_ins_gpr;
extern tcti_gadget_t gadget_simd_tbl;
extern tcti_gadget_t gadget_simd_tbx;
extern tcti_gadget_t gadget_simd_xtn;
extern tcti_gadget_t gadget_simd_xtn2;
extern tcti_gadget_t gadget_simd_zip1;
extern tcti_gadget_t gadget_simd_zip2;
extern tcti_gadget_t gadget_simd_trn1;
extern tcti_gadget_t gadget_simd_trn2;
extern tcti_gadget_t gadget_simd_uzp1;
extern tcti_gadget_t gadget_simd_uzp2;
extern tcti_gadget_t gadget_simd_bic;
extern tcti_gadget_t gadget_simd_and;
extern tcti_gadget_t gadget_simd_orr;
extern tcti_gadget_t gadget_simd_eor;
extern tcti_gadget_t gadget_simd_add;
extern tcti_gadget_t gadget_simd_sub;
extern tcti_gadget_t gadget_simd_mul;
extern tcti_gadget_t gadget_simd_orn;
extern tcti_gadget_t gadget_simd_bsl;
extern tcti_gadget_t gadget_simd_bit;
extern tcti_gadget_t gadget_simd_bif;
extern tcti_gadget_t gadget_simd_cmeq;
extern tcti_gadget_t gadget_simd_cmgt;
extern tcti_gadget_t gadget_simd_mla;
extern tcti_gadget_t gadget_simd_mls;

@interface TCTISystemAndFPGadgetSurfaceTests : XCTestCase
@end

#define TCTI_DECLARE_GADGET_POINTER_TEST(_name, _symbol)                                          \
- (void)testGadgetSurface_##_name                                                                  \
{                                                                                                  \
    XCTAssertNotEqual((uintptr_t)(_symbol), (uintptr_t)0,                                          \
                      @"%s must stay populated because generator/runtime consumes it", #_symbol);  \
}

#define TCTI_DECLARE_GADGET_MATRIX3_TEST(_name, _table)                                            \
- (void)testGadgetSurface_##_name                                                                  \
{                                                                                                  \
    XCTAssertNotEqual((uintptr_t)(_table[0][0][0]), (uintptr_t)0,                                  \
                      @"%s[0][0][0] must exist for condition-native lowering", #_table);           \
    XCTAssertNotEqual((uintptr_t)(_table[1][1][15]), (uintptr_t)0,                                 \
                      @"%s[1][1][15] must exist for condition-native lowering", #_table);          \
}

@implementation TCTISystemAndFPGadgetSurfaceTests

TCTI_DECLARE_GADGET_POINTER_TEST(Svc, gadget_svc)
TCTI_DECLARE_GADGET_POINTER_TEST(Mrs, gadget_mrs)
TCTI_DECLARE_GADGET_POINTER_TEST(Msr, gadget_msr)
TCTI_DECLARE_GADGET_POINTER_TEST(Isb, gadget_isb)
TCTI_DECLARE_GADGET_POINTER_TEST(Dsb, gadget_dsb)
TCTI_DECLARE_GADGET_POINTER_TEST(Dmb, gadget_dmb)
TCTI_DECLARE_GADGET_POINTER_TEST(Nop, gadget_nop)
TCTI_DECLARE_GADGET_POINTER_TEST(Clrex, gadget_clrex)
TCTI_DECLARE_GADGET_POINTER_TEST(Fadd, gadget_fadd)
TCTI_DECLARE_GADGET_POINTER_TEST(DcZva, gadget_dc_zva)
TCTI_DECLARE_GADGET_POINTER_TEST(SysregUnsupported, gadget_sysreg_unsupported)
TCTI_DECLARE_GADGET_MATRIX3_TEST(CcmpNativeReg, gadget_ccmp_native_reg)
TCTI_DECLARE_GADGET_MATRIX3_TEST(CcmpNativeImm, gadget_ccmp_native_imm)
TCTI_DECLARE_GADGET_POINTER_TEST(SimdDupGpr, gadget_simd_dup_gpr)
TCTI_DECLARE_GADGET_POINTER_TEST(SimdMovGprFromVec, gadget_simd_mov_gpr_from_vec)
TCTI_DECLARE_GADGET_POINTER_TEST(SimdMoviImm, gadget_simd_movi_imm)
TCTI_DECLARE_GADGET_POINTER_TEST(SimdFmovGpr, gadget_simd_fmov_gpr)
TCTI_DECLARE_GADGET_POINTER_TEST(SimdExt, gadget_simd_ext)
TCTI_DECLARE_GADGET_POINTER_TEST(SimdCnt, gadget_simd_cnt)
TCTI_DECLARE_GADGET_POINTER_TEST(SimdInsGpr, gadget_simd_ins_gpr)
TCTI_DECLARE_GADGET_POINTER_TEST(SimdTbl, gadget_simd_tbl)
TCTI_DECLARE_GADGET_POINTER_TEST(SimdTbx, gadget_simd_tbx)
TCTI_DECLARE_GADGET_POINTER_TEST(SimdXtn, gadget_simd_xtn)
TCTI_DECLARE_GADGET_POINTER_TEST(SimdXtn2, gadget_simd_xtn2)
TCTI_DECLARE_GADGET_POINTER_TEST(SimdZip1, gadget_simd_zip1)
TCTI_DECLARE_GADGET_POINTER_TEST(SimdZip2, gadget_simd_zip2)
TCTI_DECLARE_GADGET_POINTER_TEST(SimdTrn1, gadget_simd_trn1)
TCTI_DECLARE_GADGET_POINTER_TEST(SimdTrn2, gadget_simd_trn2)
TCTI_DECLARE_GADGET_POINTER_TEST(SimdUzp1, gadget_simd_uzp1)
TCTI_DECLARE_GADGET_POINTER_TEST(SimdUzp2, gadget_simd_uzp2)
TCTI_DECLARE_GADGET_POINTER_TEST(SimdBic, gadget_simd_bic)
TCTI_DECLARE_GADGET_POINTER_TEST(SimdAnd, gadget_simd_and)
TCTI_DECLARE_GADGET_POINTER_TEST(SimdOrr, gadget_simd_orr)
TCTI_DECLARE_GADGET_POINTER_TEST(SimdEor, gadget_simd_eor)
TCTI_DECLARE_GADGET_POINTER_TEST(SimdAdd, gadget_simd_add)
TCTI_DECLARE_GADGET_POINTER_TEST(SimdSub, gadget_simd_sub)
TCTI_DECLARE_GADGET_POINTER_TEST(SimdMul, gadget_simd_mul)
TCTI_DECLARE_GADGET_POINTER_TEST(SimdOrn, gadget_simd_orn)
TCTI_DECLARE_GADGET_POINTER_TEST(SimdBsl, gadget_simd_bsl)
TCTI_DECLARE_GADGET_POINTER_TEST(SimdBit, gadget_simd_bit)
TCTI_DECLARE_GADGET_POINTER_TEST(SimdBif, gadget_simd_bif)
TCTI_DECLARE_GADGET_POINTER_TEST(SimdCmeq, gadget_simd_cmeq)
TCTI_DECLARE_GADGET_POINTER_TEST(SimdCmgt, gadget_simd_cmgt)
TCTI_DECLARE_GADGET_POINTER_TEST(SimdMla, gadget_simd_mla)
TCTI_DECLARE_GADGET_POINTER_TEST(SimdMls, gadget_simd_mls)

@end
