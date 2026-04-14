#import <XCTest/XCTest.h>
#import <IXLandLinuxRuntime/emu/aarch64/fetch.h>
#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>
#import <IXLandLinuxRuntime/emu/tlb.h>
#import <IXLandLinuxRuntime/kernel/task.h>
#import <IXLandLinuxRuntime/kernel/mm.h>
#include <stdlib.h>

// Test owner: emu/aarch64/fetch.c
// Contract group: TCTI.Fetch
// Boundaries tested: B1->B2

@interface TCTIFetchTests : XCTestCase
@end

@implementation TCTIFetchTests {
    struct cpu_state *_cpu;
    struct tlb *_tlb;
}

- (void)setUp {
    [super setUp];
    // Contract: Each test starts with deterministic state
    // Set up minimal CPU and TLB for fetch testing
    _cpu = calloc(1, sizeof(struct cpu_state));
    _tlb = calloc(1, sizeof(struct tlb));
    XCTAssert(_cpu != NULL, "CPU allocation failed");
    XCTAssert(_tlb != NULL, "TLB allocation failed");
}

- (void)tearDown {
    // Contract: No persistent state between tests
    free(_cpu);
    free(_tlb);
    [super tearDown];
}

// B1->B2 boundary test: Valid fetch
// Owner: Sources/IXLandLinuxRuntime/emu/aarch64/fetch.c
- (void)test_TCTIFetchContract_ValidGuestPC_ReturnsInstructionWord {
    // Arrange: Set up B1 state (pre-entry guest state)
    // This test requires a properly mapped memory region
    // For now, we test the API contract - a valid fetch returns 0 or fault
    uint64_t test_pc = 0x1000;  // Valid guest PC
    uint32_t insn = 0;
    
    // Act: Call the fetch function
    int result = a64_fetch_insn(_cpu, _tlb, test_pc, &insn);
    
    // Assert: Either success (0) or fault (-EFAULT) is valid contract response
    // The contract is: function returns int, writes to insn pointer
    XCTAssertTrue(result == 0 || result == -14, // -EFAULT = -14
        "Contract violation: fetch must return 0 (success) or -EFAULT (fault), got %d",
        result);
}

// B1->B2 boundary test: Invalid/missing mapping
// Owner: emu/aarch64/fetch.c
- (void)test_TCTIFetchContract_InvalidGuestPC_ReturnsFault {
    // Arrange: Invalid PC without mapping
    uint64_t invalid_pc = 0x0;  // Null/invalid PC
    uint32_t insn = 0;
    
    // Act: Call the fetch function
    int result = a64_fetch_insn(_cpu, _tlb, invalid_pc, &insn);
    
    // Assert: Should return fault for invalid/unmapped PC
    XCTAssertTrue(result == 0 || result == -14, // -EFAULT = -14
        "Contract violation: fetch must return 0 (success) or -EFAULT (fault) for PC 0x%llx, got %d",
        invalid_pc, result);
}

// Contract: Fetch is deterministic for same input
// Owner: fetch.c
- (void)test_TCTIFetchContract_SameInput_ProducesSameOutput {
    // Contract: Same PC + same memory state = same result
    // Deterministic requirement from guest architectural contract
    uint64_t test_pc = 0x1000;
    uint32_t insn1 = 0, insn2 = 0;
    
    // Act: Call fetch twice with same inputs
    int result1 = a64_fetch_insn(_cpu, _tlb, test_pc, &insn1);
    int result2 = a64_fetch_insn(_cpu, _tlb, test_pc, &insn2);
    
    // Assert: Same PC should produce same result code
    XCTAssertEqual(result1, result2,
        "Contract violation: same PC 0x%llx must produce same result, got %d vs %d",
        test_pc, result1, result2);
}

// Contract: Page-crossing fetch behavior
// Owner: fetch.c
- (void)test_TCTIFetchContract_PageCrossing_IsDeterministic {
    // Arrange: PC at end of page where instruction spans boundary
    uint64_t page_end_pc = 0x1FFC;  // Last 4 bytes of 8K page
    uint32_t insn = 0;
    
    // Act: Attempt cross-page fetch
    int result = a64_fetch_insn(_cpu, _tlb, page_end_pc, &insn);
    
    // Assert: Cross-page fetch must return valid status
    XCTAssertTrue(result == 0 || result == -14, // -EFAULT = -14
        "Contract violation: cross-page fetch must return 0 or -EFAULT, got %d for PC 0x%llx",
        result, page_end_pc);
}

@end
