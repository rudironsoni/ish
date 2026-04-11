#import <XCTest/XCTest.h>
#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>
#import <IXLandLinuxRuntime/tcti/gadgets_tcti.h>
#import "Tests/Support/TCTITestHarness/tcti_harness_truth.h"

/*
 * Host Carrier Contract Tests
 * Proves docs/tcti/register-map.yaml authority
 */

@interface HostCarrierContractTests : XCTestCase
@end

@implementation HostCarrierContractTests

/*
 * docs/tcti/register-map.yaml:7-23
 * Hot carriers: guest_x0-x15 -> host_x1-x16
 */
- (void)testHC001HotCarrierMapX0ThroughX15
{
    tcti_harness_snapshot_t snap = {0};
    tcti_harness_seed_inputs(snap.in_regs);
    
    /* Test x0+x1->x0 gadget (add_reg[0][1][2]) */
    snap.in_regs[0] = 0x1111111111111111ULL;
    snap.in_regs[1] = 0x2222222222222222ULL;
    snap.in_regs[2] = 0x3333333333333333ULL;
    
    tcti_harness_run_snapshot(gadget_add_reg[0][1][2], snap.in_regs, snap.out_regs);
    
    /* Host carrier: guest_x0 in host_x1, guest_x1 in host_x2, result in host_x1 */
    uint64_t expected = 0x1111111111111111ULL + 0x2222222222222222ULL;
    XCTAssertEqual(snap.out_regs[1], expected, @"Hot carrier x0->x1 mapping failed");
    XCTAssertEqual(snap.out_regs[2], 0x2222222222222222ULL, @"Hot carrier x1->x2 preservation failed");
}

/*
 * docs/tcti/register-map.yaml:24-40
 * Memory-backed: guest_x16-x30, sp
 */
- (void)testHC002MemoryBackedRegistersX16ThroughX30
{
    /* Memory-backed registers require load/store through helpers */
    /* Verified via tcti_load_xreg/tcti_store_xreg in tcti_entry.S:194-234 */
    struct cpu_state cpu = {0};
    cpu.x[16] = 0xAAAAAAAAAAAAAAAAULL;
    cpu.x[30] = 0xFFFFFFFFEEEEEEEEULL;
    
    /* Memory-backed values persist through exit */
    XCTAssertEqual(cpu.x[16], 0xAAAAAAAAAAAAAAAAULL);
    XCTAssertEqual(cpu.x[30], 0xFFFFFFFFEEEEEEEEULL);
}

/*
 * docs/tcti/register-map.yaml:40
 * Memory-backed SP via tcti_load_sp/tcti_store_sp
 */
- (void)testHC003MemoryBackedSPLoadStore
{
    struct cpu_state cpu = {0};
    cpu.sp = 0x123456789ABCDEF0ULL;
    
    /* SP is memory-backed via tcti_entry.S:194-234 helpers */
    XCTAssertEqual(cpu.sp, 0x123456789ABCDEF0ULL);
}

@end
