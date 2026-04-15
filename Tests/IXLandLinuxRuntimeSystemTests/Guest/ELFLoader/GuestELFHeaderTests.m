#import <XCTest/XCTest.h>
#import <IXLandLinuxRuntime/kernel/elf.h>

// GuestELFHeaderTests - Fixture validation without app bootstrap
// Owner: kernel/exec.c, kernel/elf.c
@interface GuestELFHeaderTests : XCTestCase
@property (nonatomic, strong) NSData *staticMinimalData;
@property (nonatomic, strong) NSData *badMagicData;
@property (nonatomic, strong) NSData *wrongMachineData;
@property (nonatomic, strong) NSData *dynamicInterpData;
@end

@implementation GuestELFHeaderTests

- (void)setUp {
    [super setUp];
    
    NSBundle *bundle = [NSBundle bundleForClass:[self class]];
    
    // Load fixtures from bundle
    NSString *staticPath = [bundle pathForResource:@"static_minimal_aarch64_ok" ofType:@"elf"];
    NSString *badMagicPath = [bundle pathForResource:@"static_bad_magic" ofType:@"bin"];
    NSString *wrongMachinePath = [bundle pathForResource:@"static_wrong_machine" ofType:@"bin"];
    NSString *dynamicPath = [bundle pathForResource:@"dynamic_ptinterp_sample" ofType:@"elf"];
    
    self.staticMinimalData = [NSData dataWithContentsOfFile:staticPath];
    self.badMagicData = [NSData dataWithContentsOfFile:badMagicPath];
    self.wrongMachineData = [NSData dataWithContentsOfFile:wrongMachinePath];
    self.dynamicInterpData = [NSData dataWithContentsOfFile:dynamicPath];
}

- (void)tearDown {
    self.staticMinimalData = nil;
    self.badMagicData = nil;
    self.wrongMachineData = nil;
    self.dynamicInterpData = nil;
    [super tearDown];
}

// Validate fixtures are loadable
- (void)testFixtures_AreAllLoadable {
    XCTAssertNotNil(self.staticMinimalData, @"static_minimal_aarch64_ok.elf must load");
    XCTAssertNotNil(self.badMagicData, @"static_bad_magic.bin must load");
    XCTAssertNotNil(self.wrongMachineData, @"static_wrong_machine.bin must load");
    XCTAssertNotNil(self.dynamicInterpData, @"dynamic_ptinterp_sample.elf must load");
}

// Static minimal ELF has valid header size
- (void)testStaticELF_HasValidHeaderSize {
    XCTAssertGreaterThanOrEqual(self.staticMinimalData.length, sizeof(struct elf_header),
                                @"ELF file must be at least header size (64 bytes)");
}

// Static ELF has valid AArch64 machine type
- (void)testStaticELF_HasValidMachineType {
    const struct elf_header *header = (const struct elf_header *)self.staticMinimalData.bytes;
    XCTAssertEqual(header->machine, ELF_AARCH64,
                  @"Machine type must be AArch64 (183)");
}

// Static ELF has executable type
- (void)testStaticELF_HasExecutableType {
    const struct elf_header *header = (const struct elf_header *)self.staticMinimalData.bytes;
    XCTAssertEqual(header->type, ELF_EXECUTABLE,
                  @"Type must be ELF_EXECUTABLE (2)");
}

// Bad magic fixture has invalid header
- (void)testBadMagic_HasInvalidMagic {
    const uint8_t *magic = (const uint8_t *)self.badMagicData.bytes;
    XCTAssertNotEqual(magic[0], 0x7F,
                     @"First byte should not be ELF magic (0x7F)");
}

// Wrong machine fixture has non-AArch64 machine field
- (void)testWrongMachine_HasNonAArch64Machine {
    const struct elf_header *header = (const struct elf_header *)self.wrongMachineData.bytes;
    XCTAssertNotEqual(header->machine, ELF_AARCH64,
                     @"Machine type should NOT be AArch64");
}

// Dynamic ELF has PT_INTERP type
- (void)testDynamicELF_HasPTInterpType {
    const struct elf_header *header = (const struct elf_header *)self.dynamicInterpData.bytes;
    XCTAssertEqual(header->type, ELF_DYNAMIC,
                  @"Type must be ELF_DYNAMIC (3)");
}

// Verify PT_LOAD segment exists in static ELF
- (void)testStaticELF_HasPTLoadSegment {
    const struct elf_header *header = (const struct elf_header *)self.staticMinimalData.bytes;
    
    BOOL foundLoad = NO;
    const void *phdrBytes = (const char *)self.staticMinimalData.bytes + header->prghead_off;
    
    for (int i = 0; i < header->phent_count && !foundLoad; i++) {
        const struct prg_header *ph = (const struct prg_header *)((const char *)phdrBytes +
                                    i * sizeof(struct prg_header));
        if (ph->type == PT_LOAD) {
            foundLoad = YES;
        }
    }
    
    XCTAssertTrue(foundLoad, @"Static ELF must have at least one PT_LOAD segment");
}

// Validate entry point is within LOAD segment
- (void)testStaticELF_EntryPointInLoadSegment {
    const struct elf_header *header = (const struct elf_header *)self.staticMinimalData.bytes;
    XCTAssertNotEqual(header->entry_point, 0, @"Entry point must not be zero");
    
    BOOL entryInLoad = NO;
    const void *phdrBytes = (const char *)self.staticMinimalData.bytes + header->prghead_off;
    
    for (int i = 0; i < header->phent_count && !entryInLoad; i++) {
        const struct prg_header *ph = (const struct prg_header *)((const char *)phdrBytes +
                                    i * sizeof(struct prg_header));
        if (ph->type == PT_LOAD) {
            uint64_t seg_start = ph->vaddr;
            uint64_t seg_end = ph->vaddr + ph->memsize;
            if (header->entry_point >= seg_start && header->entry_point < seg_end) {
                entryInLoad = YES;
            }
        }
    }
    
    XCTAssertTrue(entryInLoad, @"Entry point must fall within a LOAD segment");
}

@end
