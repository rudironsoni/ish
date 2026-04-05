#import <XCTest/XCTest.h>

#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>
#import <IXLandLinuxRuntime/emu/aarch64/sysreg.h>
#import <IXLandLinuxRuntime/tcti/gadgets_tcti.h>

@interface SysregXCTests : XCTestCase
@end

@implementation SysregXCTests

- (struct cpu_state)freshCPU
{
    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.pc = 0x4000;
    return cpu;
}

- (void)testRouteMatrix
{
    const a64_sysreg_spec_t *nzcv = a64_sysreg_lookup(A64_SYSREG_NZCV);
    XCTAssertNotEqual(nzcv, NULL);
    XCTAssertEqual(nzcv->tier, A64_SYSREG_TIER0);
    XCTAssertEqual(nzcv->read_route, A64_SYSREG_ROUTE_TCTI_FASTPATH);
    XCTAssertEqual(nzcv->write_route, A64_SYSREG_ROUTE_TCTI_FASTPATH);

    const a64_sysreg_spec_t *ctr = a64_sysreg_lookup(A64_SYSREG_CTR_EL0);
    XCTAssertNotEqual(ctr, NULL);
    XCTAssertEqual(ctr->tier, A64_SYSREG_TIER1);
    XCTAssertEqual(ctr->read_route, A64_SYSREG_ROUTE_TCTI_FASTPATH);
    XCTAssertEqual(ctr->write_route, A64_SYSREG_ROUTE_UNSUPPORTED);

    const a64_sysreg_spec_t *daif = a64_sysreg_lookup(A64_SYSREG_DAIF);
    XCTAssertNotEqual(daif, NULL);
    XCTAssertEqual(daif->tier, A64_SYSREG_TIER2);
    XCTAssertEqual(daif->read_route, A64_SYSREG_ROUTE_UNSUPPORTED);
    XCTAssertEqual(daif->write_route, A64_SYSREG_ROUTE_UNSUPPORTED);
}

- (void)testNZCVRoundTrip
{
    struct cpu_state cpu = [self freshCPU];
    cpu.x[0] = 0xA0000000ULL;

    XCTAssertEqual(a64_sysreg_write(&cpu, A64_SYSREG_NZCV, 0), TCTI_EXIT_NORMAL);
    XCTAssertEqual(cpu.pstate & 0xF0000000ULL, 0xA0000000ULL);

    XCTAssertEqual(a64_sysreg_read(&cpu, A64_SYSREG_NZCV, 1), TCTI_EXIT_NORMAL);
    XCTAssertEqual(cpu.x[1], 0xA0000000ULL);
}

- (void)testTPIDREl0RoundTrip
{
    struct cpu_state cpu = [self freshCPU];
    cpu.x[3] = 0x123456789ABCDEF0ULL;

    XCTAssertEqual(a64_sysreg_write(&cpu, A64_SYSREG_TPIDR_EL0, 3), TCTI_EXIT_NORMAL);
    XCTAssertEqual(cpu.tpidr_el0, 0x123456789ABCDEF0ULL);

    XCTAssertEqual(a64_sysreg_read(&cpu, A64_SYSREG_TPIDR_EL0, 4), TCTI_EXIT_NORMAL);
    XCTAssertEqual(cpu.x[4], 0x123456789ABCDEF0ULL);
}

- (void)testReadOnlyFastPathRegisters
{
    struct cpu_state cpu = [self freshCPU];
    cpu.tpidr_el0 = 0xCAFEBABE12340000ULL;

    XCTAssertEqual(a64_sysreg_read(&cpu, A64_SYSREG_TPIDRRO_EL0, 5), TCTI_EXIT_NORMAL);
    XCTAssertEqual(cpu.x[5], 0xCAFEBABE12340000ULL);
    XCTAssertEqual(a64_sysreg_write(&cpu, A64_SYSREG_TPIDRRO_EL0, 5), TCTI_EXIT_UNSUPPORTED_SYSREG);

    XCTAssertEqual(a64_sysreg_read(&cpu, A64_SYSREG_CTR_EL0, 6), TCTI_EXIT_NORMAL);
    XCTAssertEqual(cpu.x[6], A64_SYSREG_CTR_EL0_VALUE);
    XCTAssertEqual(a64_sysreg_write(&cpu, A64_SYSREG_CTR_EL0, 6), TCTI_EXIT_UNSUPPORTED_SYSREG);

    XCTAssertEqual(a64_sysreg_read(&cpu, A64_SYSREG_DCZID_EL0, 7), TCTI_EXIT_NORMAL);
    XCTAssertEqual(cpu.x[7], A64_SYSREG_DCZID_EL0_VALUE);
    XCTAssertEqual(a64_sysreg_write(&cpu, A64_SYSREG_DCZID_EL0, 7), TCTI_EXIT_UNSUPPORTED_SYSREG);
}

- (void)testUnsupportedRoutes
{
    struct cpu_state cpu = [self freshCPU];
    XCTAssertEqual(a64_sysreg_read(&cpu, A64_SYSREG_DAIF, 0), TCTI_EXIT_UNSUPPORTED_SYSREG);
    XCTAssertEqual(a64_sysreg_write(&cpu, A64_SYSREG_DAIF, 0), TCTI_EXIT_UNSUPPORTED_SYSREG);
    XCTAssertEqual(a64_sysreg_read(&cpu, A64_SYSREG_CNTFRQ_EL0, 0), TCTI_EXIT_UNSUPPORTED_SYSREG);
    XCTAssertEqual(a64_sysreg_read(&cpu, A64_SYSREG_CNTVCT_EL0, 0), TCTI_EXIT_UNSUPPORTED_SYSREG);
}

@end
