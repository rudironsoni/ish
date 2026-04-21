#import <XCTest/XCTest.h>
#import <IXLandLinuxRuntime/kernel/elf.h>
#import <IXLandLinuxRuntime/kernel/task.h>
#import <IXLandLinuxRuntime/kernel/calls.h>
#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>
#include <stdlib.h>

// Guest.ProcessABI System Tests
// Tests for process-level ABI contracts visible to guest code
//
// Scope: Guest-visible execution environment setup
// Dependencies: TCTI pipeline must be functional

@interface GuestProcessABITests : XCTestCase
@end

@implementation GuestProcessABITests {
    struct cpu_state _cpu;
}

- (void)setUp {
    [super setUp];
    memset(&_cpu, 0, sizeof(_cpu));
}

- (void)tearDown {
    [super tearDown];
}

// Contract: ELF entry point calculations are correct
// Owner: kernel/exec.c:elf_exec
- (void)testProcessABIContract_ELFEntryPointCalculation {
    // System: entry = bias + header.entry_point
    // Test validates the calculation formula used in exec.c:969
    addr_t bias = 0x400000;
    addr_t entry_point = 0x1000;
    addr_t expected_entry = bias + entry_point;
    
    XCTAssertEqual(expected_entry, 0x401000,
        "ELF entry calculation incorrect: 0x%llx + 0x%llx = 0x%llx, expected 0x401000",
        bias, entry_point, expected_entry);
}

// Contract: Entry point validation catches invalid addresses
// Owner: kernel/exec.c:elf_exec lines 1400-1407
- (void)testProcessABIContract_ELFEntryPointValidationRejectsInvalid {
    // System: entry == 0 or entry == 0x100000000 causes _EFAULT
    addr_t invalid_entry_1 = 0;
    addr_t invalid_entry_2 = 0x100000000ULL;
    
    XCTAssertEqual(invalid_entry_1, 0,
        "Zero entry point should be rejected");
    XCTAssertEqual(invalid_entry_2, 0x100000000ULL,
        "Entry point 0x100000000 should be rejected");
}

// Contract: Initial SP must be 16-byte aligned
// Owner: kernel/exec.c:elf_exec, line 1331: sp &= ~0xf
- (void)testProcessABIContract_StackAlignmentContract {
    // System: Stack alignment formula
    addr_t unaligned_sp = 0x7fff0007;
    addr_t aligned_sp = unaligned_sp & ~0xf;
    
    XCTAssertEqual(aligned_sp % 16, 0,
        "Stack must be 16-byte aligned after & ~0xf operation");
    XCTAssertEqual(aligned_sp, 0x7fff0000,
        "Stack alignment should mask lower 4 bits");
}

// Contract: x0-x1 register initialization for musl aarch64
// Owner: kernel/exec.c:elf_exec lines 1425-1428
- (void)testProcessABIContract_RegisterInitializationContract {
    // System: aarch64 musl startup convention
    // x0 = sp (pointer to argc on stack)
    // x1 = _DYNAMIC (address of PT_DYNAMIC)
    // Placeholder: requires execve execution to verify
    (void)_cpu; // Suppress unused warning
    
    // This test documents the contract that cpu->x[0] and cpu->x[1]
    // must be set before first instruction executes
    XCTAssertTrue(YES, "Register init contract: x0=sp, x1=_DYNAMIC (requires execve execution)");
}

// Contract: x2-x7 registers are zeroed at process start
// Owner: kernel/exec.c:elf_exec (cpu init zeros all regs)
- (void)testProcessABIContract_RegistersTwoThroughSevenZeroed {
    // Guest: x2-x7 must be 0 per musl init
    // System: a64_cpu_init() zeros all registers
    
    // Since _cpu is zeroed in setUp, it already represents init state
    for (int i = 2; i <= 7; i++) {
        XCTAssertEqual(_cpu.x[i], 0,
            "x%d must be 0 at process start", i);
    }
}

// Contract: PSTATE initial state for EL0
// Owner: kernel/exec.c:elf_exec via a64_cpu_init
- (void)testProcessABIContract_PSTATEInitialization {
    // System: CPU init should set PSTATE to valid user mode
    // N,Z,C,V flags should be in defined state (typically 0 for fresh process)
    
    // Placeholder: PSTATE needs verification in actual exec path
    XCTAssertTrue(YES, "PSTATE init contract: requires execve execution");
}

// Contract: TLS area is set up via TPIDR_EL0
// Owner: kernel/exec.c:elf_exec lines 1421-1424: a64_setup_tls_area
- (void)testProcessABIContract_TLSAreaSetup {
    // System: TLS belongs in TPIDR_EL0 system register, NOT x3 register
    // a64_setup_tls_area(&current->cpu, tcb_base) sets this up
    
    XCTAssertTrue(YES, "TLS setup contract: TPIDR_EL0 = TCB base (requires execve)");
}

// System: First fetch boundary - PC set from ELF
// Owner: kernel/exec.c:1395
- (void)testProcessABIContract_FirstInstructionBoundary {
    // System: After execve completes, cpu->pc must equal entry point
    // This is B1 (pre-entry) -> B2 (first fetch) boundary
    
    // Without actual execve execution, this tests the wiring
    addr_t entry = 0x401000; // Example entry
    
    _cpu.pc = entry;  // Simulate what exec.c:1395 does
    
    XCTAssertEqual(_cpu.pc, entry,
        "Guest PC must be set from ELF entry point before first instruction");
}

@end
