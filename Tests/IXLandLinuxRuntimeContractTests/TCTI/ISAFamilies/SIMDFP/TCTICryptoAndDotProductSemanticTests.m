#import <XCTest/XCTest.h>

#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>

@interface TCTICryptoAndDotProductSemanticTests : XCTestCase
@end

#define TCTI_DECLARE_CRYPTO_REJECT_TEST(_name, _insn, _pc)                                         \
- (void)testSemanticExecutionContract_##_name                                                      \
{                                                                                                  \
    enum {                                                                                         \
        textPC = _pc,                                                                              \
    };                                                                                             \
    static const uint32_t insn = _insn;                                                            \
    struct cpu_state cpu;                                                                          \
    memset(&cpu, 0, sizeof(cpu));                                                                  \
    XCTAssertLessThan(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,                      \
                      @"Crypto-family instruction `%s` must reject explicitly until real TCTI "    \
                       @"ownership exists", #_name);                                               \
}

@implementation TCTICryptoAndDotProductSemanticTests

- (void)testSemanticExecutionContract_AESERejectsUntilCryptoFamilyIsImplemented
{
    enum {
        textPC = 0x94000,
    };

    static const uint32_t insn = 0x4e284820; // aese v0.16b, v1.16b

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));

    XCTAssertLessThan(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                      @"Crypto instructions must not disappear by omission. Until AESE is "
                       "implemented, the real TCTI path must reject it explicitly.");
}

- (void)testSemanticExecutionContract_PMULLRejectsUntilCryptoFamilyIsImplemented
{
    enum {
        textPC = 0x94020,
    };

    static const uint32_t insn = 0x0ee2e020; // pmull v0.1q, v1.1d, v2.1d

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));

    XCTAssertLessThan(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                      @"PMULL must have an explicit reject policy until the crypto/dot-product "
                       "family is implemented through TCTI.");
}

TCTI_DECLARE_CRYPTO_REJECT_TEST(AESDRejectsUntilCryptoFamilyIsImplemented, 0x4e285862, 0x94040)
TCTI_DECLARE_CRYPTO_REJECT_TEST(AESMCRejectsUntilCryptoFamilyIsImplemented, 0x4e2868a4, 0x94044)
TCTI_DECLARE_CRYPTO_REJECT_TEST(AESIMCRejectsUntilCryptoFamilyIsImplemented, 0x4e2878e6, 0x94048)
TCTI_DECLARE_CRYPTO_REJECT_TEST(PMULRejectsUntilCryptoFamilyIsImplemented, 0x2e229c20, 0x9404c)
TCTI_DECLARE_CRYPTO_REJECT_TEST(SHA1CRejectsUntilCryptoFamilyIsImplemented, 0x5e020020, 0x94050)
TCTI_DECLARE_CRYPTO_REJECT_TEST(SHA1MRejectsUntilCryptoFamilyIsImplemented, 0x5e022020, 0x94051)
TCTI_DECLARE_CRYPTO_REJECT_TEST(SHA1PRejectsUntilCryptoFamilyIsImplemented, 0x5e051083, 0x94052)
TCTI_DECLARE_CRYPTO_REJECT_TEST(SHA1SU0RejectsUntilCryptoFamilyIsImplemented, 0x5e0830e6, 0x94053)
TCTI_DECLARE_CRYPTO_REJECT_TEST(SHA1SU1RejectsUntilCryptoFamilyIsImplemented, 0x5e281949, 0x94054)
TCTI_DECLARE_CRYPTO_REJECT_TEST(SHA256HRejectsUntilCryptoFamilyIsImplemented, 0x5e024020, 0x94054)
TCTI_DECLARE_CRYPTO_REJECT_TEST(SHA256H2RejectsUntilCryptoFamilyIsImplemented, 0x5e1051ee, 0x94055)
TCTI_DECLARE_CRYPTO_REJECT_TEST(SHA256SU0RejectsUntilCryptoFamilyIsImplemented, 0x5e282a51, 0x94056)
TCTI_DECLARE_CRYPTO_REJECT_TEST(SHA256SU1RejectsUntilCryptoFamilyIsImplemented, 0x5e156293, 0x94057)
TCTI_DECLARE_CRYPTO_REJECT_TEST(SM3PARTW1RejectsUntilCryptoFamilyIsImplemented, 0xce62c020, 0x94058)
TCTI_DECLARE_CRYPTO_REJECT_TEST(SM3PARTW2RejectsUntilCryptoFamilyIsImplemented, 0xce65c483, 0x94059)
TCTI_DECLARE_CRYPTO_REJECT_TEST(SM3SS1RejectsUntilCryptoFamilyIsImplemented, 0xce4824e6, 0x9405a)
TCTI_DECLARE_CRYPTO_REJECT_TEST(SM3TT1ARejectsUntilCryptoFamilyIsImplemented, 0xce4c816a, 0x9405b)
TCTI_DECLARE_CRYPTO_REJECT_TEST(SM3TT1BRejectsUntilCryptoFamilyIsImplemented, 0xce4f95cd, 0x9405c)
TCTI_DECLARE_CRYPTO_REJECT_TEST(SM3TT2ARejectsUntilCryptoFamilyIsImplemented, 0xce52aa30, 0x9405d)
TCTI_DECLARE_CRYPTO_REJECT_TEST(SM3TT2BRejectsUntilCryptoFamilyIsImplemented, 0xce55be93, 0x9405e)
TCTI_DECLARE_CRYPTO_REJECT_TEST(SM4ERejectsUntilCryptoFamilyIsImplemented, 0xcec086f6, 0x9405f)
TCTI_DECLARE_CRYPTO_REJECT_TEST(SM4EKEYRejectsUntilCryptoFamilyIsImplemented, 0xce7acb38, 0x94060)
TCTI_DECLARE_CRYPTO_REJECT_TEST(SDOTRejectsUntilCryptoFamilyIsImplemented, 0x4e829420, 0x94061)
TCTI_DECLARE_CRYPTO_REJECT_TEST(UDOTRejectsUntilCryptoFamilyIsImplemented, 0x6e859483, 0x94062)
TCTI_DECLARE_CRYPTO_REJECT_TEST(USDOTRejectsUntilCryptoFamilyIsImplemented, 0x4e889ce6, 0x94063)
TCTI_DECLARE_CRYPTO_REJECT_TEST(SUDOTRejectsUntilCryptoFamilyIsImplemented, 0x0f0ef1ac, 0x94064)
TCTI_DECLARE_CRYPTO_REJECT_TEST(FDOTRejectsUntilCryptoFamilyIsImplemented, 0x0e02fc20, 0x94065)
TCTI_DECLARE_CRYPTO_REJECT_TEST(BFDOTRejectsUntilCryptoFamilyIsImplemented, 0x2e48fce6, 0x94066)
TCTI_DECLARE_CRYPTO_REJECT_TEST(BCAXRejectsUntilCryptoFamilyIsImplemented, 0xce2d398b, 0x94058)
TCTI_DECLARE_CRYPTO_REJECT_TEST(XARRejectsUntilCryptoFamilyIsImplemented, 0xce911e0f, 0x9405c)

@end
