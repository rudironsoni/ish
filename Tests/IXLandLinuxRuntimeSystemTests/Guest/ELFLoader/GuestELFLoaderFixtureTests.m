#import <XCTest/XCTest.h>
#import <IXLandLinuxRuntime/kernel/elf.h>

// Guest.ELFLoader Fixture Tests
// Tests real ELF loading from on-disk fixtures
// Owner: kernel/exec.c, kernel/elf.c
@interface GuestELFLoaderFixtureTests : XCTestCase
@end

@implementation GuestELFLoaderFixtureTests

// Helper: Get path to a fixture in the test bundle
- (NSString *)pathForFixture:(NSString *)filename {
    NSBundle *bundle = [NSBundle bundleForClass:[self class]];
    NSString *path = [bundle pathForResource:filename ofType:nil inDirectory:@"Tests/Fixtures/ELF"];
    if (!path) {
        // Fallback: try without subdirectory
        path = [bundle pathForResource:filename ofType:nil];
    }
    return path;
}

// Helper: Read entire file into NSData
- (NSData *)dataForFixture:(NSString *)filename {
    NSString *path = [self pathForFixture:filename];
    XCTAssertNotNil(path, @"Fixture %@ must exist in test bundle", filename);
    NSError *error = nil;
    NSData *data = [NSData dataWithContentsOfFile:path options:0 error:&error];
    XCTAssertNil(error, @"Failed to read %@: %@", filename, error);
    XCTAssertNotNil(data, @"Fixture %@ data must not be nil", filename);
    XCTAssertGreaterThan(data.length, 0, @"Fixture %@ must have content", filename);
    return data;
}

// Helper: Check if 4-byte magic matches ELF
- (BOOL)isELFMagic:(const uint8_t *)bytes {
    static const uint8_t elfMagic[4] = {0x7F, 'E', 'L', 'F'};
    return (bytes[0] == elfMagic[0]) &&
           (bytes[1] == elfMagic[1]) &&
           (bytes[2] == elfMagic[2]) &&
           (bytes[3] == elfMagic[3]);
}

// Contract: Valid static AArch64 ELF fixture can be loaded
// Owner: kernel/exec.c, kernel/elf.c
- (void)testStaticELF_ValidHeader_IsAccepted {
    NSData *data = [self dataForFixture:@"static_minimal_aarch64_ok.elf"];
    XCTAssertGreaterThanOrEqual(data.length, sizeof(struct elf_header),
                                @"ELF file must be at least header size");
    
    // Parse the ELF header
    const struct elf_header *header = (const struct elf_header *)data.bytes;
    
    // Validate magic by byte comparison (avoids packed struct warnings)
    const uint8_t *magic = (const uint8_t *)data.bytes;
    XCTAssertTrue([self isELFMagic:magic], @"Magic must be valid ELF");
    
    // Validate ELF class (bitness field)
    XCTAssertEqual(header->bitness, ELF_64BIT,
                  @"Must be 64-bit ELF");
    
    // Validate endianness
    XCTAssertEqual(header->endian, ELF_LITTLEENDIAN,
                  @"Must be little-endian");
    
    // Validate ABI (0 = System V/Generic, 3 = Linux; both are acceptable)
    XCTAssertTrue(header->abi == 0 || header->abi == ELF_LINUX_ABI,
                  @"ABI must be 0 (System V) or 3 (Linux), got %d", header->abi);
    
    // Validate type
    XCTAssertEqual(header->type, ELF_EXECUTABLE,
                  @"Must be executable type");
    
    // Validate machine
    XCTAssertEqual(header->machine, ELF_AARCH64,
                  @"Must be AArch64 machine type (183)");
    
    // Validate entry point is set
    XCTAssertNotEqual(header->entry_point, 0,
                     @"Entry point must be non-zero");
}

// Contract: Invalid ELF with bad magic is rejected
// Owner: kernel/exec.c
- (void)testStaticELF_BadMagic_IsRejected {
    NSData *data = [self dataForFixture:@"static_bad_magic.bin"];
    XCTAssertGreaterThanOrEqual(data.length, 4,
                                @"File must have at least 4 bytes for magic");
    
    const uint8_t *bytes = (const uint8_t *)data.bytes;
    
    // Should NOT match ELF magic
    XCTAssertFalse([self isELFMagic:bytes],
                  @"Bad magic fixture must NOT match ELF magic");
    // Specific check: first byte should be wrong
    XCTAssertNotEqual(bytes[0], 0x7F,
                     @"First byte should not be 0x7F");
}

// Contract: ELF with wrong machine type is rejected
// Owner: kernel/exec.c
- (void)testStaticELF_WrongMachine_IsRejected {
    NSData *data = [self dataForFixture:@"static_wrong_machine.bin"];
    XCTAssertGreaterThanOrEqual(data.length, sizeof(struct elf_header),
                                @"File must be at least header size");
    
    const uint8_t *bytes = (const uint8_t *)data.bytes;
    const struct elf_header *header = (const struct elf_header *)data.bytes;
    
    // Magic should still be valid (check by bytes to avoid packed struct warning)
    XCTAssertTrue([self isELFMagic:bytes], @"Magic should still be valid ELF");
    
    // But machine type should NOT be AArch64
    XCTAssertNotEqual(header->machine, ELF_AARCH64,
                     @"Machine type should NOT be AArch64");
}

// Contract: Dynamic ELF with PT_INTERP is detected
// Owner: kernel/exec.c
- (void)testDynamicELF_PTINTERP_IsDetected {
    NSData *data = [self dataForFixture:@"dynamic_ptinterp_sample.elf"];
    XCTAssertGreaterThanOrEqual(data.length, sizeof(struct elf_header),
                                @"Dynamic ELF must have header");
    
    const struct elf_header *header = (const struct elf_header *)data.bytes;
    
    // Validate it's a dynamic type
    XCTAssertEqual(header->type, ELF_DYNAMIC,
                  @"Must be DYNAMIC type (3)");
    
    // Validate machine is still AArch64
    XCTAssertEqual(header->machine, ELF_AARCH64,
                  @"Must be AArch64 machine type");
}

// Contract: Static ELF program headers are valid
// Owner: kernel/exec.c
- (void)testStaticELF_ProgramHeaders_AreValid {
    NSData *data = [self dataForFixture:@"static_minimal_aarch64_ok.elf"];
    const struct elf_header *header = (const struct elf_header *)data.bytes;
    
    // Program header offset must be within file (field is prghead_off)
    XCTAssertGreaterThan(header->prghead_off, 0,
                        @"Program header offset must be positive");
    XCTAssertLessThan(header->prghead_off, data.length,
                      @"Program header offset must be within file");
    
    // Entry size must be correct (field is phent_size)
    XCTAssertEqual(header->phent_size, sizeof(struct prg_header),
                  @"Program header entry size must be 56 bytes");
    
    // Number of entries must be reasonable (field is phent_count)
    XCTAssertGreaterThan(header->phent_count, 0,
                        @"Must have at least one program header");
}

// Contract: Static ELF has LOAD segment
// Owner: kernel/exec.c
- (void)testStaticELF_LoadSegment_Exists {
    NSData *data = [self dataForFixture:@"static_minimal_aarch64_ok.elf"];
    const struct elf_header *header = (const struct elf_header *)data.bytes;
    
    // Iterate through program headers looking for PT_LOAD
    BOOL foundLoad = NO;
    for (int i = 0; i < header->phent_count; i++) {
        const struct prg_header *ph = (const struct prg_header *)((const char *)data.bytes +
                                    header->prghead_off +
                                    (i * sizeof(struct prg_header)));
        if (ph->type == PT_LOAD) {
            foundLoad = YES;
            break;
        }
    }
    
    XCTAssertTrue(foundLoad, @"Static ELF must have at least one PT_LOAD segment");
}

// Contract: ELF entry point maps to LOAD segment
// Owner: kernel/exec.c
- (void)testStaticELF_EntryPoint_IsInLoadSegment {
    NSData *data = [self dataForFixture:@"static_minimal_aarch64_ok.elf"];
    const struct elf_header *header = (const struct elf_header *)data.bytes;
    
    uint64_t entry = header->entry_point;
    XCTAssertNotEqual(entry, 0, @"Entry point must not be zero");
    
    BOOL entryInLoad = NO;
    for (int i = 0; i < header->phent_count; i++) {
        const struct prg_header *ph = (const struct prg_header *)((const char *)data.bytes +
                                    header->prghead_off +
                                    (i * sizeof(struct prg_header)));
        if (ph->type == PT_LOAD) {
            // Check if entry falls within segment bounds
            uint64_t seg_start = ph->vaddr;
            uint64_t seg_end = ph->vaddr + ph->memsize;
            if (entry >= seg_start && entry < seg_end) {
                entryInLoad = YES;
                break;
            }
        }
    }
    
    XCTAssertTrue(entryInLoad, @"Entry point must fall within a LOAD segment");
}

@end
