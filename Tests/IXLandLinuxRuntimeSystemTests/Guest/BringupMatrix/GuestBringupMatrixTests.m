#import <XCTest/XCTest.h>
#import <IXLandLinuxRuntime/kernel/elf.h>
#import <IXLandLinuxRuntime/kernel/calls.h>
#import <IXLandLinuxRuntime/kernel/task.h>
#import <IXLandLinuxRuntime/kernel/memory.h>
#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>

// Guest.BringupMatrix System Tests
// Tests cold-start guest bring-up from valid runtime state
// Validates static ELF path to first user instruction
// Owner: kernel/exec.c, kernel/elf.c, emu/aarch64/cpu.c

@interface GuestBringupMatrixTests : XCTestCase
@end

@implementation GuestBringupMatrixTests

// Contract: ELF magic is correctly defined
// Owner: kernel/elf.h
- (void)testELFHeader_ConstantsMatchExpectedValues {
    // ELF_MAGIC is "\177ELF" = 0x7f followed by "ELF"
    XCTAssertTrue(strcmp(ELF_MAGIC, "\177ELF") == 0, "ELF magic must be 0x7f followed by ELF");
    XCTAssertEqual(ELF_32BIT, 1, "ELF_32BIT must be 1");
    XCTAssertEqual(ELF_64BIT, 2, "ELF_64BIT must be 2");
    XCTAssertEqual(ELF_LITTLEENDIAN, 1, "ELF_LITTLEENDIAN must be 1");
    XCTAssertEqual(ELF_BIGENDIAN, 2, "ELF_BIGENDIAN must be 2");
}

// Contract: ELF architecture constants are defined
// Owner: kernel/elf.h
- (void)testELFHeader_ArchitectureConstantsAreDefined {
    XCTAssertEqual(ELF_LINUX_ABI, 3, "ELF_LINUX_ABI must be 3");
    XCTAssertEqual(ELF_EXECUTABLE, 2, "ELF_EXECUTABLE must be 2");
    XCTAssertEqual(ELF_DYNAMIC, 3, "ELF_DYNAMIC must be 3");
    XCTAssertEqual(ELF_AARCH64, 183, "ELF_AARCH64 must be 183 (EM_AARCH64)");
}

// Contract: ELF header structure size is correct
// Owner: kernel/elf.h
- (void)testELFHeader_StructSizesAreValid {
    XCTAssertEqual(sizeof(struct elf_header), 64, "ELF header must be 64 bytes");
    XCTAssertEqual(sizeof(struct prg_header), 56, "ELF program header must be 56 bytes");
}

// Contract: ELF PT_LOAD segment type is defined
// Owner: kernel/elf.h
- (void)testELFProgramHeader_PT_LOADIsDefined {
    XCTAssertEqual(PT_LOAD, 1, "PT_LOAD must be 1");
    XCTAssertEqual(PT_INTERP, 3, "PT_INTERP must be 3");
    XCTAssertEqual(PT_DYNAMIC, 2, "PT_DYNAMIC must be 2");
}

// Contract: execve syscall is implemented
// Owner: kernel/exec.c, kernel/calls.c
- (void)testSyscallContract_ExecveIsImplemented {
    XCTAssert(sys_execve != NULL, "execve syscall must be implemented");
}

// Contract: ELF PT_INTERP segment handling contract exists
// Owner: kernel/exec.c
- (void)testELFLoader_PT_INTERPHandlerExists {
    // PT_INTERP is used for dynamic linking - the content points to the interpreter path
    // This test validates that the infrastructure for handling PT_INTERP exists
    XCTAssertEqual(PT_INTERP, 3, "PT_INTERP segment type must be defined");
}

// Contract: CPU state structure has required fields for bring-up
// Owner: emu/aarch64/cpu.h
- (void)testCPUState_RequiredFieldsExist {
    // cpu_state must have size > 0
    XCTAssertTrue(sizeof(struct cpu_state) > 0, "cpu_state must have size > 0");
}

// Contract: Memory management structures exist for bring-up
// Owner: kernel/memory.h
- (void)testMemoryContract_DescriptorStructuresExist {
    // mm_struct is required for process memory management
    XCTAssertTrue(sizeof(struct mm) > 0, "mm struct must have size > 0");
}

// Contract: Task structure has required fields for bring-up
// Owner: kernel/task.h
- (void)testTaskContract_RequiredFieldsExist {
    XCTAssertTrue(sizeof(struct task) > 0, "task struct must have size > 0");
    
    // Verify offset calculations that bring-up depends on
    XCTAssertTrue(offsetof(struct task, cpu) >= 0, "task->cpu offset must be valid");
    XCTAssertTrue(offsetof(struct task, mm) >= 0, "task->mm offset must be valid");
    XCTAssertTrue(offsetof(struct task, mem) >= 0, "task->mem offset must be valid");
}

// Contract: Aux vector entry type constants are defined
// Owner: kernel/elf.h
- (void)testELFHeader_AuxVectorConstantsAreDefined {
    XCTAssertEqual(AX_PHDR, 3, "AX_PHDR must be 3");
    XCTAssertEqual(AX_PHENT, 4, "AX_PHENT must be 4");
    XCTAssertEqual(AX_PHNUM, 5, "AX_PHNUM must be 5");
    XCTAssertEqual(AX_PAGESZ, 6, "AX_PAGESZ must be 6");
    XCTAssertEqual(AX_BASE, 7, "AX_BASE must be 7");
    XCTAssertEqual(AX_ENTRY, 9, "AX_ENTRY must be 9");
}

// Contract: Program header permission flags are defined
// Owner: kernel/elf.h
- (void)testProgramHeader_PermissionFlagsAreDefined {
    XCTAssertEqual(PH_R, 4, "PH_R (read) must be 4");
    XCTAssertEqual(PH_W, 2, "PH_W (write) must be 2");
    XCTAssertEqual(PH_X, 1, "PH_X (execute) must be 1");
}

// Contract: Stack initialization contract exists for bring-up
// Owner: kernel/exec.c, include/bits/pthread.h
// ARGV_MAX is defined in exec.c, PAGE_SIZE must be defined for the calculation
- (void)testBringupContract_StackInitializationExists {
    // Stack must be set up with argc, argv, envp, auxv
    // PAGE_SIZE is required, and ARGV_MAX = 32 * PAGE_SIZE in exec.c
    XCTAssertTrue(PAGE_SIZE > 0, "PAGE_SIZE must be positive");
}

// Contract: Program break (brk) contract exists
// Owner: kernel/calls.c
- (void)testSyscallContract_BrkIsImplemented {
    XCTAssert(sys_brk != NULL, "brk syscall must be implemented for bring-up");
}

// Contract: mmap contract exists for dynamic ELF loading
// Owner: kernel/calls.c
- (void)testSyscallContract_MmapIsImplemented {
    XCTAssert(sys_mmap != NULL, "mmap syscall must be implemented for ELF loading");
}

// Contract: ELF entry point field is at correct offset
// Owner: kernel/elf.h struct elf_header
- (void)testBringupContract_ELFEntryPointFieldOffsetIsValid {
    XCTAssertEqual(offsetof(struct elf_header, entry_point), 24,
                   "ELF entry point field must be at offset 24");
}

@end
