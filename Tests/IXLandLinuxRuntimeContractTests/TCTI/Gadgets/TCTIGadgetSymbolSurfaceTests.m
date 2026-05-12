#import <XCTest/XCTest.h>

#include <dlfcn.h>

@interface TCTIGadgetSymbolSurfaceTests : XCTestCase
@end

#define TCTI_DECLARE_GADGET_SYMBOL_TEST(_name, _symbol)                                           \
- (void)testGadgetSurface_##_name                                                                  \
{                                                                                                  \
    XCTAssertNotEqual(dlsym(RTLD_DEFAULT, _symbol), NULL,                                          \
                      @"%s must be link-visible because the TCTI gadget surface is an explicit "   \
                       @"emulator proof boundary", _symbol);                                       \
}

@implementation TCTIGadgetSymbolSurfaceTests

TCTI_DECLARE_GADGET_SYMBOL_TEST(AddImm, "gadget_add_imm")
TCTI_DECLARE_GADGET_SYMBOL_TEST(AddReg, "gadget_add_reg")
TCTI_DECLARE_GADGET_SYMBOL_TEST(SubImm, "gadget_sub_imm")
TCTI_DECLARE_GADGET_SYMBOL_TEST(SubReg, "gadget_sub_reg")
TCTI_DECLARE_GADGET_SYMBOL_TEST(AndImm, "gadget_and_imm")
TCTI_DECLARE_GADGET_SYMBOL_TEST(AndReg, "gadget_and_reg")
TCTI_DECLARE_GADGET_SYMBOL_TEST(OrrImm, "gadget_orr_imm")
TCTI_DECLARE_GADGET_SYMBOL_TEST(OrrReg, "gadget_orr_reg")
TCTI_DECLARE_GADGET_SYMBOL_TEST(EorImm, "gadget_eor_imm")
TCTI_DECLARE_GADGET_SYMBOL_TEST(EorReg, "gadget_eor_reg")
TCTI_DECLARE_GADGET_SYMBOL_TEST(MovReg, "gadget_mov_reg")
TCTI_DECLARE_GADGET_SYMBOL_TEST(MovImm, "gadget_mov_imm")
TCTI_DECLARE_GADGET_SYMBOL_TEST(MvnReg, "gadget_mvn_reg")
TCTI_DECLARE_GADGET_SYMBOL_TEST(LslImm, "gadget_lsl_imm")
TCTI_DECLARE_GADGET_SYMBOL_TEST(LsrImm, "gadget_lsr_imm")
TCTI_DECLARE_GADGET_SYMBOL_TEST(AsrImm, "gadget_asr_imm")
TCTI_DECLARE_GADGET_SYMBOL_TEST(CmpImm, "gadget_cmp_imm")
TCTI_DECLARE_GADGET_SYMBOL_TEST(CmpReg, "gadget_cmp_reg")
TCTI_DECLARE_GADGET_SYMBOL_TEST(TstImm, "gadget_tst_imm")
TCTI_DECLARE_GADGET_SYMBOL_TEST(TstReg, "gadget_tst_reg")
TCTI_DECLARE_GADGET_SYMBOL_TEST(B, "gadget_b")
TCTI_DECLARE_GADGET_SYMBOL_TEST(BCond, "gadget_b_cond")
TCTI_DECLARE_GADGET_SYMBOL_TEST(Bl, "gadget_bl")
TCTI_DECLARE_GADGET_SYMBOL_TEST(Br, "gadget_br")
TCTI_DECLARE_GADGET_SYMBOL_TEST(Blr, "gadget_blr")
TCTI_DECLARE_GADGET_SYMBOL_TEST(Ret, "gadget_ret")
TCTI_DECLARE_GADGET_SYMBOL_TEST(Cbz, "gadget_cbz")
TCTI_DECLARE_GADGET_SYMBOL_TEST(Cbnz, "gadget_cbnz")
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
TCTI_DECLARE_GADGET_SYMBOL_TEST(Svc, "gadget_svc")
TCTI_DECLARE_GADGET_SYMBOL_TEST(Mrs, "gadget_mrs")
TCTI_DECLARE_GADGET_SYMBOL_TEST(Msr, "gadget_msr")
TCTI_DECLARE_GADGET_SYMBOL_TEST(Isb, "gadget_isb")
TCTI_DECLARE_GADGET_SYMBOL_TEST(Dsb, "gadget_dsb")
TCTI_DECLARE_GADGET_SYMBOL_TEST(Dmb, "gadget_dmb")
TCTI_DECLARE_GADGET_SYMBOL_TEST(Nop, "gadget_nop")
TCTI_DECLARE_GADGET_SYMBOL_TEST(Clrex, "gadget_clrex")
TCTI_DECLARE_GADGET_SYMBOL_TEST(Fadd, "gadget_fadd")
TCTI_DECLARE_GADGET_SYMBOL_TEST(Fsub, "gadget_fsub")
TCTI_DECLARE_GADGET_SYMBOL_TEST(Fmul, "gadget_fmul")
TCTI_DECLARE_GADGET_SYMBOL_TEST(Fdiv, "gadget_fdiv")
TCTI_DECLARE_GADGET_SYMBOL_TEST(Fcmp, "gadget_fcmp")
TCTI_DECLARE_GADGET_SYMBOL_TEST(LdrX, "gadget_ldr_x")
TCTI_DECLARE_GADGET_SYMBOL_TEST(StrX, "gadget_str_x")
TCTI_DECLARE_GADGET_SYMBOL_TEST(AtomicLdst, "gadget_atomic_ldst")
TCTI_DECLARE_GADGET_SYMBOL_TEST(SimdLdst, "gadget_simd_ldst")

@end
