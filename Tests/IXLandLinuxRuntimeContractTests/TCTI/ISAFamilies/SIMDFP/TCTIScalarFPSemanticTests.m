#import <XCTest/XCTest.h>

#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>

@interface TCTIScalarFPSemanticTests : XCTestCase
@end

@implementation TCTIScalarFPSemanticTests

- (void)testSemanticExecutionContract_FMOVGPRToScalarAndBackRoundTripsBits
{
    enum {
        textPC = 0x92000,
    };

    static const uint32_t insns[] = {
        0x9e670000, // fmov d0, x0
        0x9e660002, // fmov x2, d0
    };

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.x[0] = 0x400e000000000000ULL;

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, insns, sizeof(insns) / sizeof(insns[0])),
                   0,
                   @"Scalar FMOV between GPR and FP registers must roundtrip raw bits through "
                    "the real TCTI SIMD/FP path");
    XCTAssertEqual(cpu.vregs[0].d[0], 0x400e000000000000ULL);
    XCTAssertEqual(cpu.x[2], 0x400e000000000000ULL);
}

- (void)testSemanticExecutionContract_FMOVGPRToSecondScalarRegisterAndBackRoundTripsBits
{
    enum {
        textPC = 0x92010,
    };

    static const uint32_t insns[] = {
        0x9e670021, // fmov d1, x1
        0x9e660022, // fmov x2, d1
    };

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.x[1] = 0x4002000000000000ULL;

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, insns, sizeof(insns) / sizeof(insns[0])),
                   0,
                   @"Scalar FMOV must roundtrip through non-zero FP register indices, not only v0");
    XCTAssertEqual(cpu.vregs[1].d[0], 0x4002000000000000ULL);
    XCTAssertEqual(cpu.x[2], 0x4002000000000000ULL);
}

- (void)testSemanticExecutionContract_FADDDoubleAddsIEEE64Operands
{
    enum {
        textPC = 0x92020,
    };

    static const uint32_t insns[] = {
        0x9e670000, // fmov d0, x0
        0x9e670021, // fmov d1, x1
        0x1e612800, // fadd d0, d0, d1
        0x9e660002, // fmov x2, d0
    };

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.x[0] = 0x3ff8000000000000ULL; // 1.5
    cpu.x[1] = 0x4002000000000000ULL; // 2.25

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, insns, sizeof(insns) / sizeof(insns[0])),
                   0,
                   @"Scalar FP arithmetic must execute through TCTI so guest ldso/libc code can "
                    "consume real IEEE64 results without a fallback engine");
    XCTAssertEqual(cpu.vregs[1].d[0], 0x4002000000000000ULL,
                   @"The second scalar FMOV must populate d1 before FADD consumes it");
    XCTAssertEqual(cpu.vregs[0].d[0], 0x400e000000000000ULL,
                   @"FADD d0,d0,d1 must update the destination scalar FP register in place");
    XCTAssertEqual(cpu.x[2], 0x400e000000000000ULL, @"1.5 + 2.25 must produce 3.75");
}

@end
