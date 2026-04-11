#import <XCTest/XCTest.h>

#import <IXLandLinuxRuntime/emu/aarch64/decode.h>
#import <IXLandLinuxRuntime/tcti/aarch64/gen.h>

@interface GeneratorGoldenTests : XCTestCase
@end

typedef struct {
    const char *case_id;
    uint32_t insn_word;
    int expected_rd;
    int expected_rn;
    long long expected_imm;
    int expected_set_flags;
    int expected_subtype;
} gen_case_t;

static void assertGeneratorCase(XCTestCase *tc, const gen_case_t c)
{
    a64_instr_t instr;
    XCTAssertEqual(a64_decode(c.insn_word, &instr), 0);
    XCTAssertEqual(instr.Rd, c.expected_rd);
    XCTAssertEqual(instr.Rn, c.expected_rn);
    XCTAssertEqual((long long)instr.imm, c.expected_imm);
    XCTAssertEqual(instr.set_flags ? 1 : 0, c.expected_set_flags);
    XCTAssertEqual(instr.subtype, c.expected_subtype);

    tcti_gadget_t gadget_buffer[A64_MAX_GADGETS_PER_BLOCK];
    a64_gen_state_t state;
    XCTAssertEqual(a64_gen_init(&state, gadget_buffer, A64_MAX_GADGETS_PER_BLOCK), A64_GEN_OK);
    a64_gen_reset(&state, 0);

    int gen_ret = a64_gen_instruction(&state, c.insn_word, 0);
    XCTAssertGreaterThanOrEqual(gen_ret, 0);
    XCTAssertGreaterThan(state.num_gadgets, (size_t)0);
    XCTAssertNotEqual((void *)state.gadgets[0], NULL);
}

@implementation GeneratorGoldenTests

- (void)testGEN001SimpleALUEmission
{
    assertGeneratorCase(self, (gen_case_t){ .case_id = "GEN-001", .insn_word = 0x91000420, .expected_rd = 0, .expected_rn = 1, .expected_imm = 1, .expected_set_flags = 0, .expected_subtype = 4 });
}

- (void)testGEN002FlagSettingCompare
{
    assertGeneratorCase(self, (gen_case_t){ .case_id = "GEN-002", .insn_word = 0xf100041f, .expected_rd = 31, .expected_rn = 0, .expected_imm = 1, .expected_set_flags = 1, .expected_subtype = 6 });
}

- (void)testGEN003PostIndexStoreEmission
{
    assertGeneratorCase(self, (gen_case_t){ .case_id = "GEN-003", .insn_word = 0xf8000441, .expected_rd = 1, .expected_rn = 2, .expected_imm = 0, .expected_set_flags = 0, .expected_subtype = A64_LDST_SINGLE });
}

- (void)testGEN004CBZEmission
{
    assertGeneratorCase(self, (gen_case_t){ .case_id = "GEN-004", .insn_word = 0xb4000800, .expected_rd = 0, .expected_rn = 0, .expected_imm = 256, .expected_set_flags = 0, .expected_subtype = A64_BRANCH_CMP });
}

- (void)testGEN005BCondEmission
{
    assertGeneratorCase(self, (gen_case_t){ .case_id = "GEN-005", .insn_word = 0x54000800, .expected_rd = 0, .expected_rn = 0, .expected_imm = 256, .expected_set_flags = 0, .expected_subtype = A64_BRANCH_COND });
}

- (void)testGEN006LDPSTPEmission
{
    assertGeneratorCase(self, (gen_case_t){ .case_id = "GEN-006", .insn_word = 0xa9400be0, .expected_rd = 0, .expected_rn = 31, .expected_imm = 0, .expected_set_flags = 0, .expected_subtype = A64_LDST_PAIR });
}

- (void)testGEN007SVCImmediateEmission
{
    assertGeneratorCase(self, (gen_case_t){ .case_id = "GEN-007", .insn_word = 0xd4200080, .expected_rd = 0, .expected_rn = 0, .expected_imm = 128, .expected_set_flags = 0, .expected_subtype = 0 });
}

- (void)testGEN008BlockSidecarMap
{
    assertGeneratorCase(self, (gen_case_t){ .case_id = "GEN-008", .insn_word = 0x91000420, .expected_rd = 0, .expected_rn = 1, .expected_imm = 1, .expected_set_flags = 0, .expected_subtype = 4 });
}

@end
