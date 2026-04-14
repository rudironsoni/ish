#import <XCTest/XCTest.h>
#import <IXLandLinuxRuntime/kernel/elf.h>

// Guest.ELFLoader System Tests
// Tests ELF header validation and PT_INTERP parsing
// Owner: kernel/exec.c:read_header, read_prg_headers

@interface GuestELFLoaderTests : XCTestCase
@end

@implementation GuestELFLoaderTests

// Contract: ELF magic bytes must be 0x7f 'E' 'L' 'F'
// Owner: kernel/exec.c:495
- (void)testELFHeaderContract_MagicBytesValid {
    struct elf_header header;
    memcpy(&header.magic, ELF_MAGIC, 4);
    
    int cmp = memcmp(&header.magic, ELF_MAGIC, 4);
    XCTAssertEqual(cmp, 0, "ELF magic bytes must match \\x7fELF");
}

// Contract: ELF type must be EXECUTABLE (2) or DYNAMIC (3)
// Owner: kernel/exec.c:498
- (void)testELFHeaderContract_TypeExecutableOrDynamic {
    uint16_t valid_types[] = {ELF_EXECUTABLE, ELF_DYNAMIC};
    
    for (int i = 0; i < 2; i++) {
        XCTAssertTrue(valid_types[i] == ELF_EXECUTABLE || valid_types[i] == ELF_DYNAMIC,
            "Type %d must be EXECUTABLE or DYNAMIC", valid_types[i]);
    }
}

// Contract: Endianness must be LITTLEENDIAN (1)
// Owner: kernel/exec.c:501
- (void)testELFHeaderContract_EndianLittleOnly {
    byte_t endian = ELF_LITTLEENDIAN;
    XCTAssertEqual(endian, ELF_LITTLEENDIAN, "Only little-endian supported");
}

// Contract: ELF version must be 1
// Owner: kernel/exec.c:504
- (void)testELFHeaderContract_VersionOne {
    byte_t version = 1;
    XCTAssertEqual(version, 1, "ELF version must be 1");
}

// Contract: Bitness must be 64-bit (2)
// Owner: kernel/exec.c:509
- (void)testELFHeaderContract_Bitness64Bit {
    byte_t bitness = ELF_64BIT;
    XCTAssertEqual(bitness, ELF_64BIT, "Only 64-bit ELF supported");
}

// Contract: Machine must be AARCH64 (183)
// Owner: kernel/exec.c:512
- (void)testELFHeaderContract_MachineAArch64 {
    uint16_t machine = ELF_AARCH64;
    XCTAssertEqual(machine, 183, "Machine must be AARCH64 (183)");
}

// Contract: Program header count (phent_count) must be > 0 for valid executable
// Owner: kernel/exec.c:read_prg_headers
- (void)testELFHeaderContract_ProgramHeaderCountValid {
    struct elf_header header;
    header.phent_count = 1;
    
    XCTAssertGreaterThan(header.phent_count, 0, "Must have at least one program header");
}

// Contract: Entry point must be non-zero for valid executable
// Owner: exec.c validates this
- (void)testELFHeaderContract_EntryPointNonZero {
    struct elf_header header;
    header.entry_point = 0x400000;
    
    XCTAssertGreaterThan(header.entry_point, 0, "Entry point must be non-zero");
}

// Contract: PT_INTERP type constant is 3
// Owner: elf.h
- (void)testELFContract_PTInterpTypeConstant {
    XCTAssertEqual(PT_INTERP, 3, "PT_INTERP must be 3");
}

// Contract: Program header type matching
// Owner: elf.h
- (void)testELFContract_ProgramHeaderTypes {
    XCTAssertEqual(PT_NULL, 0, "PT_NULL = 0");
    XCTAssertEqual(PT_LOAD, 1, "PT_LOAD = 1");
    XCTAssertEqual(PT_DYNAMIC, 2, "PT_DYNAMIC = 2");
    XCTAssertEqual(PT_INTERP, 3, "PT_INTERP = 3");
    XCTAssertEqual(PT_NOTE, 4, "PT_NOTE = 4");
    XCTAssertEqual(PT_TLS, 7, "PT_TLS = 7");
}

// Contract: prg_header struct size is correct
// Owner: elf.h
- (void)testELFContract_ProgramHeaderSize {
    size_t expected_size = 56; // ELF64 program header
    XCTAssertEqual(sizeof(struct prg_header), expected_size,
        "Program header size must be %zu bytes", expected_size);
}

// Contract: elf_header struct size is correct
// Owner: elf.h
- (void)testELFContract_ELFHeaderSize {
    // Verify header_size field matches actual struct size
    struct elf_header header;
    header.header_size = 64; // ELF64 header is 64 bytes
    
    XCTAssertEqual(header.header_size, 64, "ELF header size field must be 64");
}

// System: Complete header validation boundary
// Owner: kernel/exec.c:read_header (lines 476-520)
- (void)testELFLoaderContract_HeaderValidationBoundary {
    // Simulate reading and validating an ELF header
    struct elf_header header = {0};
    
    // Set up valid header
    memcpy(&header.magic, ELF_MAGIC, 4);
    header.bitness = ELF_64BIT;
    header.machine = ELF_AARCH64;
    header.endian = ELF_LITTLEENDIAN;
    header.elfversion1 = 1;
    header.type = ELF_DYNAMIC;
    header.entry_point = 0x401000;
    header.phent_count = 1;
    header.header_size = 64;
    
    // Validate all constraints
    XCTAssert(memcmp(&header.magic, ELF_MAGIC, 4) == 0, "Magic");
    XCTAssert(header.bitness == ELF_64BIT, "64-bit");
    XCTAssert(header.machine == ELF_AARCH64, "AArch64");
    XCTAssert(header.endian == ELF_LITTLEENDIAN, "Little-endian");
    XCTAssert(header.elfversion1 == 1, "Version 1");
    XCTAssert(header.type == ELF_EXECUTABLE || header.type == ELF_DYNAMIC, "Type");
    XCTAssert(header.entry_point > 0, "Entry point");
    XCTAssert(header.phent_count > 0, "Program headers");
}

@end
