//
//  GadgetTests.m
//  UnitTests
//
//  Tests for TCTI gadgets and CPU state
//

#import <XCTest/XCTest.h>
#include "emu/aarch64/cpu.h"
#include "asbestos/aarch64/gadgets_tcti.h"

@interface GadgetTests : XCTestCase
@end

@implementation GadgetTests

#pragma mark - CPU State Structure Tests

- (void)testCpuStateSize {
    XCTAssertLessThan(sizeof(struct cpu_state), (size_t)65536, 
        "CPU state should be less than 64KB");
}

- (void)testCpuStateAlignment {
    struct cpu_state cpu;
    XCTAssertEqual(((uintptr_t)&cpu.x[0] % 8), (uintptr_t)0,
        "Register x[0] should be 8-byte aligned");
}

- (void)testStackPointerAlignment {
    struct cpu_state cpu;
    XCTAssertEqual(((uintptr_t)&cpu.sp % 8), (uintptr_t)0,
        "Stack pointer should be 8-byte aligned");
}

- (void)testProgramCounterAlignment {
    struct cpu_state cpu;
    XCTAssertEqual(((uintptr_t)&cpu.pc % 8), (uintptr_t)0,
        "Program counter should be 8-byte aligned");
}

#pragma mark - CPU State Register Tests

- (void)testAllGeneralRegistersExist {
    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    
    for (int i = 0; i < 31; i++) {
        cpu.x[i] = i;
    }
    
    for (int i = 0; i < 31; i++) {
        XCTAssertEqual(cpu.x[i], (uint64_t)i, "Register X%d should hold value %d", i, i);
    }
}

- (void)testStackPointerWritable {
    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    
    cpu.sp = 0x7FFFFFFFUL;
    XCTAssertEqual(cpu.sp, 0x7FFFFFFFUL);
}

- (void)testProgramCounterWritable {
    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    
    cpu.pc = 0x1000;
    XCTAssertEqual(cpu.pc, 0x1000);
}

#pragma mark - PSTATE Tests

- (void)testPstateExists {
    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    
    cpu.pstate = 0;
    XCTAssertEqual(cpu.pstate, 0);
}

#pragma mark - Vector Register Tests

- (void)testVectorRegistersExist {
    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    
    for (int i = 0; i < 32; i++) {
        cpu.vregs[i].q = i;
    }
    
    for (int i = 0; i < 32; i++) {
        XCTAssertEqual(cpu.vregs[i].q, (__int128)i, "VREG %d should hold value", i);
    }
}

- (void)testVectorRegisterAlignment {
    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    
    XCTAssertEqual(((uintptr_t)&cpu.vregs[0] % 16), (uintptr_t)0,
        "Vector registers should be 16-byte aligned");
}

#pragma mark - TLS Register Tests

- (void)testTpidrEl0Exists {
    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    
    cpu.tpidr_el0 = 0x1234;
    XCTAssertEqual(cpu.tpidr_el0, 0x1234);
}

#pragma mark - Fault Tracking Tests

- (void)testFaultAddressExists {
    struct cpu_state cpu;
    memset(&cpu, 0, sizeof(cpu));
    
    cpu.fault_addr = 0xDEADBEEF;
    XCTAssertEqual(cpu.fault_addr, 0xDEADBEEF);
}

@end
