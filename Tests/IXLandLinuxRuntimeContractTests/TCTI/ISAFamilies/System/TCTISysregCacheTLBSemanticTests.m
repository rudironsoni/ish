#import <XCTest/XCTest.h>

#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>

#include "../../Support/ISAFamilies/BaseScalar/tcti_control_and_flags_semantic_scenarios.h"
#include "../../Support/ISAFamilies/BaseScalar/tcti_scalar_runtime_semantic_scenarios.h"

@interface TCTISysregCacheTLBSemanticTests : XCTestCase
@end

@implementation TCTISysregCacheTLBSemanticTests

- (void)testSemanticExecutionContract_TPIDREL0RoundtripsThroughFullSysregEncoding
{
    XCTAssertEqual(tcti_semantic_case_tpidr_el0_roundtrips_through_full_sysreg_encoding(), 0ULL,
                   @"TCTI must honor the full decoded TPIDR_EL0 sysreg encoding so MSR/MRS "
                    @"roundtrip the guest thread pointer in live ldso paths");
}

- (void)testSemanticExecutionContract_DCZVAZeroesCacheBlock
{
    XCTAssertEqual(tcti_semantic_case_dc_zva_zeroes_cache_block(), 0ULL,
                   @"TCTI must implement DC ZVA so musl memset zeroes large allocations used "
                    "during BusyBox shell startup");
}

- (void)testSemanticExecutionContract_FPCRRoundtripsThroughSysregPath
{
    enum {
        textPC = 0x97000,
    };

    static const uint32_t insns[] = {
        0xd51b4401, // msr fpcr, x1
        0xd53b4402, // mrs x2, fpcr
    };

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.x[1] = 0x00c00000ULL;

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, insns, sizeof(insns) / sizeof(insns[0])),
                   0,
                   @"FPCR must roundtrip through the real sysreg TCTI path so guest FP control "
                    "state is architectural rather than host ambient state");
    XCTAssertEqual(cpu.fpcr, 0x00c00000U);
    XCTAssertEqual(cpu.x[2], 0x00c00000ULL);
}

- (void)testSemanticExecutionContract_CTREL0ReadPublishesArchitecturalValue
{
    enum {
        textPC = 0x97020,
    };

    static const uint32_t insn = 0xd53b0020; // mrs x0, ctr_el0

    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));

    XCTAssertEqual(a64_cpu_execute_code_block(&cpu, textPC, &insn, 1), 0,
                   @"CTR_EL0 reads must come through the sysreg table so guest cacheline "
                    "introspection stays architectural");
    XCTAssertNotEqual(cpu.x[0], 0ULL, @"CTR_EL0 should publish a non-zero architectural value");
}

@end
