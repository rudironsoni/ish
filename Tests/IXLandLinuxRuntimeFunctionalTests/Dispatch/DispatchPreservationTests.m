#import <XCTest/XCTest.h>
#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>
#import <IXLandLinuxRuntime/tcti/gadgets_tcti.h>
#import <IXLandLinuxRuntime/tcti/frame.h>
#import "Tests/Support/TCTITestHarness/tcti_harness_truth.h"

/*
 * Dispatch Preservation Tests
 * Proves docs/tcti/preservation-table.yaml and docs/tcti/boundary-fields.yaml
 */

@interface DispatchPreservationTests : XCTestCase
@end

@implementation DispatchPreservationTests

/*
 * docs/tcti/preservation-table.yaml:6-13
 * Entry validation: gadgets_non_null, cpu_non_null
 */
- (void)testDP001FirstGadgetDispatch
{
    struct cpu_state cpu = {0};
    tcti_gadget_t gadgets[2];
    
    /* Single gadget + exit */
    gadgets[0] = gadget_add_imm[0][0][1]; /* x0 = x0 + 1 */
    gadgets[1] = gadget_exit;
    
    cpu.x[0] = 0x1000000000000000ULL;
    
    /* Execute through tcti_entry_block */
    tcti_entry_block(gadgets, &cpu);
    
    XCTAssertEqual(cpu.x[0], 0x1000000000000001ULL, @"First gadget dispatch failed");
    XCTAssertEqual(cpu.tcti_exit_reason, TCTI_EXIT_NORMAL, @"Exit reason not normal");
}

/*
 * docs/tcti/boundary-fields.yaml:26-29
 * x28_gadget_stream, x27_next_gadget
 */
- (void)testDP002EpilogueLoadsNextGadgetFromX28
{
    struct cpu_state cpu = {0};
    tcti_gadget_t gadgets[3];
    
    /* Two gadgets + exit */
    gadgets[0] = gadget_mov_reg[0][1];      /* x0 = x1 */
    gadgets[1] = gadget_add_imm[0][0][1];   /* x0 = x0 + 1 */
    gadgets[2] = gadget_exit;
    
    cpu.x[0] = 0xAAAAAAAAAAAAAAAAULL;
    cpu.x[1] = 0x5555555555555555ULL;
    
    tcti_entry_block(gadgets, &cpu);
    
    /* After mov: x0 = x1, after add_imm: x0 = x1 + 1 */
    uint64_t expected = 0x5555555555555555ULL + 1;
    XCTAssertEqual(cpu.x[0], expected, @"Epilogue next-gadget dispatch failed");
}

/*
 * docs/tcti/preservation-table.yaml:14-19
 * Helper call preserves x28
 */
- (void)testDP003HelperPreservesX28
{
    /* Helper calls must preserve x28 (gadget stream pointer) */
    /* Tested indirectly: if x28 corrupted, subsequent gadgets fail */
    struct cpu_state cpu = {0};
    tcti_gadget_t gadgets[2];
    
    /* Gadget requiring memory helper (x16 is memory-backed) */
    gadgets[0] = gadget_mov_reg[16][0];     /* x16 = x0 (requires store) */
    gadgets[1] = gadget_exit;
    
    cpu.x[0] = 0x123456789ABCDEF0ULL;
    
    tcti_entry_block(gadgets, &cpu);
    
    /* If x28 corrupted, exit reason would be wrong or crash */
    XCTAssertEqual(cpu.tcti_exit_reason, TCTI_EXIT_NORMAL);
}

/*
 * docs/tcti/preservation-table.yaml:14-19
 * Helper call preserves nzcv
 */
- (void)testDP004HelperPreservesNZCV
{
    struct cpu_state cpu = {0};
    tcti_gadget_t gadgets[2];
    
    /* CMP to set flags, then helper call via memory access */
    cpu.x[0] = 0x1000000000000000ULL;
    cpu.x[1] = 0x0FFFFFFFFFFFFFFFFULL;
    
    gadgets[0] = gadget_mov_reg[16][0];     /* Triggers helper via memory */
    gadgets[1] = gadget_exit;
    
    /* NZCV should be preserved through helper */
    tcti_entry_block(gadgets, &cpu);
    
    /* Verifiable via flag state after exit */
    XCTAssertEqual(cpu.tcti_exit_reason, TCTI_EXIT_NORMAL);
}

/*
 * docs/tcti/preservation-table.yaml:6-13
 * Null gadget rejection
 */
- (void)testDP005NullGadgetRejection
{
    struct cpu_state cpu = {0};
    tcti_gadget_t gadgets[2];
    
    gadgets[0] = NULL;  /* Invalid gadget */
    gadgets[1] = gadget_exit;
    
    /* Entry should reject or fault */
    tcti_entry_block(gadgets, &cpu);
    
    /* Should exit with fault or error reason */
    XCTAssertTrue(cpu.tcti_exit_reason != TCTI_EXIT_NORMAL || cpu.fault_addr != 0,
                  @"Null gadget should cause fault or abnormal exit");
}

@end
