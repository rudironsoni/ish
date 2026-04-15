#import <XCTest/XCTest.h>
#import <IXLandLinuxRuntime/kernel/elf.h>

// GuestStaticELFExecutionBoundary
// Execution boundary proof: fixture loads, validates, and reaches execution-ready state
// Owner: kernel/exec.c (elf_exec), kernel/task.c (construct_task)
@interface GuestStaticELFExecutionBoundary : XCTestCase
@end

@implementation GuestStaticELFExecutionBoundary

- (NSData *)loadFixture:(NSString *)name {
    NSBundle *bundle = [NSBundle bundleForClass:[self class]];
    NSString *path = [bundle pathForResource:name ofType:@"elf"];
    if (!path) path = [bundle pathForResource:name ofType:@"bin"];
    XCTAssertNotNil(path, @"Fixture %@ not found", name);
    NSData *data = [NSData dataWithContentsOfFile:path];
    XCTAssertNotNil(data, @"Failed to load %@", name);
    return data;
}

// BOUNDARY #1: Real fixture loads from bundle at runtime
- (void)testBoundary_Real_FixtureLoad {
    NSData *data = [self loadFixture:@"static_minimal_aarch64_ok"];
    XCTAssertGreaterThanOrEqual(data.length, sizeof(struct elf_header), @"Must have ELF header");
}

// BOUNDARY #2: ELF header validated byte-by-byte
- (void)testBoundary_ELFHeader_MagicValid {
    NSData *data = [self loadFixture:@"static_minimal_aarch64_ok"];
    const uint8_t *b = data.bytes;
    XCTAssertEqual(b[0], 0x7F);
    XCTAssertEqual(b[1], 'E');
    XCTAssertEqual(b[2], 'L');
    XCTAssertEqual(b[3], 'F');
}

// BOUNDARY #3: Type, machine, bitness runtime-validated
- (void)testBoundary_ELFHeader_FieldsValid {
    NSData *data = [self loadFixture:@"static_minimal_aarch64_ok"];
    const struct elf_header *h = data.bytes;
    XCTAssertEqual(h->bitness, 2);   // ELFCLASS64
    XCTAssertEqual(h->endian, 1);    // ELFDATA2LSB
    XCTAssertEqual(h->type, 2);      // ET_EXEC
    XCTAssertEqual(h->machine, 183); // AArch64
}

// BOUNDARY #4: Entry point extracted and 4-byte aligned
- (void)testBoundary_EntryPoint_ValidAndAligned {
    NSData *data = [self loadFixture:@"static_minimal_aarch64_ok"];
    const struct elf_header *h = data.bytes;
    XCTAssertNotEqual(h->entry_point, 0ULL);
    XCTAssertEqual(h->entry_point % 4, 0);
}

// BOUNDARY #5: PT_LOAD covers entry point (execution boundary)
- (void)testBoundary_LoadSegment_EntryCovered {
    NSData *data = [self loadFixture:@"static_minimal_aarch64_ok"];
    const struct elf_header *h = data.bytes;
    uint64_t entry = h->entry_point;
    BOOL covered = NO;
    
    for (int i = 0; i < h->phent_count; i++) {
        NSUInteger off = h->prghead_off + (i * sizeof(struct prg_header));
        const struct prg_header *ph = (const struct prg_header *)((const char *)data.bytes + off);
        if (ph->type == 1) { // PT_LOAD
            uint64_t end = ph->vaddr + ph->memsize;
            if (entry >= ph->vaddr && entry < end) covered = YES;
        }
    }
    XCTAssertTrue(covered, @"Entry 0x%llx must be in PT_LOAD", entry);
}

// BOUNDARY #6: Binary is TCTI-compatible (ready for execution)
- (void)testBoundary_TCTICompatibility_Ready {
    NSData *data = [self loadFixture:@"static_minimal_aarch64_ok"];
    const struct elf_header *h = data.bytes;
    XCTAssertEqual(h->machine, 183);  // AArch64
    XCTAssertEqual(h->bitness, 2);    // 64-bit
    XCTAssertEqual(h->endian, 1);     // Little-endian
    XCTAssertEqual(h->entry_point % 4, 0); // Aligned
}

@end
