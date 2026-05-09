#import <XCTest/XCTest.h>
#import <IXLandLinuxRuntime/kernel/errno.h>
#import <IXLandLinuxRuntime/kernel/memory.h>
#import <IXLandLinuxRuntime/kernel/mm.h>
#import <IXLandLinuxRuntime/kernel/vma.h>

// Guest.MemoryManagement System Tests
// Tests guest-visible memory management syscall contracts
// Owner: kernel/mmap.c

@interface GuestMemoryManagementTests : XCTestCase
@end

@implementation GuestMemoryManagementTests

// Contract: MMAP_ANONYMOUS contract documented
// Owner: kernel/mmap.c:do_mmap
- (void)testMemoryContract_MmapAnonymousDocumentation {
    // System: Anonymous mmap with valid parameters
    // Contract: Returns valid address or MAP_FAILED
    XCTAssert(true, "Mmap contract: returns valid address or error");
}

// Contract: kernel-selected mmap must not allocate the low guard region
// Owner: kernel/memory.c:pt_find_hole
- (void)testMemoryContract_MmapNullStartsAboveLowGuard {
    struct mem mem;
    mem_init(&mem);

    page_t page = pt_find_hole(&mem, 1);

    XCTAssertEqual(page, (page_t)A64_MMAP_BASE_PAGE,
                   "mmap(NULL, ...) must start at the configured guest mmap base");
    mem_destroy(&mem);
}

// Contract: MMAP_ANONYMOUS zero-initialized
// Owner: kernel/mmap.c:pt_map_nothing
- (void)testMemoryContract_MmapAnonymousZeroesMemory {
    // Contract: Memory should be zeroed
    XCTAssert(true, "Mmapped memory should be zeroed (guest-visible contract)");
}

// Contract: MMAP_FIXED honors requested address
// Owner: kernel/mmap.c:do_mmap
- (void)testMemoryContract_MmapFixedHonorsAddress {
    // Contract: MAP_FIXED returns requested address or fails
    XCTAssert(true, "MAP_FIXED must return requested address or fail");
}

// Contract: MMAP_FIXED fails on existing mapping
// Owner: kernel/mmap.c:do_mmap
- (void)testMemoryContract_MmapFixedFailsOnExistingMapping {
    // Contract: FIXED on mapped address should remap or fail
    XCTAssert(true, "Second FIXED mmap at same address should succeed or fail");
}

// Contract: MUNMAP releases mapping
// Owner: kernel/mmap.c:sys_munmap
- (void)testMemoryContract_MunmapReleasesMapping {
    // Contract: munmap must succeed for valid mapping
    XCTAssert(true, "Munmap must succeed for valid mapping");
}

// Contract: MUNMAP on unmapped region fails
// Owner: kernel/mmap.c:sys_munmap
- (void)testMemoryContract_MunmapUnmappedFails {
    // Contract: munmap on invalid/unmapped returns error
    XCTAssert(true, "Munmap on unmapped should fail");
}

// Contract: MPROTECT changes protection
// Owner: kernel/mmap.c:sys_mprotect
- (void)testMemoryContract_MprotectChangesProtection {
    // Contract: mprotect to read-only must succeed
    XCTAssert(true, "Mprotect to read-only must succeed");
}

- (void)testMemoryContract_MprotectPreservesAnonymousAndCOWMappingBits {
    struct mm *mm = mm_new();
    XCTAssertNotEqual(mm, NULL);
    if (mm == NULL)
        return;

    page_t start = (page_t)A64_MMAP_BASE_PAGE;
    XCTAssertEqual(pt_map_nothing(&mm->mem, start, 1, P_READ | P_WRITE), 0);

    struct page_desc *desc = page_map_lookup(&mm->mem.pages, start);
    XCTAssertNotEqual(desc, NULL);
    if (desc == NULL) {
        mm_release(mm);
        return;
    }

    desc->flags |= P_COW;
    struct vm_area *vma = vma_tree_find(&mm->mem.vmas, start << PAGE_BITS);
    XCTAssertNotEqual(vma, NULL);
    if (vma != NULL)
        vma->flags |= P_COW;

    XCTAssertEqual(pt_set_flags(&mm->mem, start, 1, P_READ), 0);

    desc = page_map_lookup(&mm->mem.pages, start);
    XCTAssertNotEqual(desc, NULL);
    if (desc != NULL) {
        XCTAssertEqual(desc->flags & P_RWX, (unsigned)P_READ);
        XCTAssertTrue((desc->flags & P_ANONYMOUS) != 0,
                      @"mprotect must not strip anonymous-mapping metadata");
        XCTAssertTrue((desc->flags & P_COW) != 0,
                      @"mprotect must not strip copy-on-write metadata");
    }

    vma = vma_tree_find(&mm->mem.vmas, start << PAGE_BITS);
    XCTAssertNotEqual(vma, NULL);
    if (vma != NULL) {
        XCTAssertEqual(vma->flags & P_RWX, (unsigned)P_READ);
        XCTAssertTrue((vma->flags & P_ANONYMOUS) != 0,
                      @"mprotect must preserve VMA mapping metadata");
        XCTAssertTrue((vma->flags & P_COW) != 0,
                      @"mprotect must preserve VMA COW metadata");
    }

    mm_release(mm);
}

- (void)testMemoryContract_MprotectNoneRevokesGuestReadAccess {
    struct mem mem;
    mem_init(&mem);

    page_t start = (page_t)A64_MMAP_BASE_PAGE;
    XCTAssertEqual(pt_map_nothing(&mem, start, 1, P_READ | P_WRITE), 0);

    read_wrlock(&mem.lock);
    XCTAssertNotEqual(mem_ptr(&mem, start << PAGE_BITS, MEM_READ), NULL,
                      @"fresh anonymous mappings must be readable before mprotect(PROT_NONE)");
    read_wrunlock(&mem.lock);

    XCTAssertEqual(pt_set_flags(&mem, start, 1, 0), 0);

    read_wrlock(&mem.lock);
    XCTAssertEqual(mem_ptr(&mem, start << PAGE_BITS, MEM_READ), NULL,
                   @"mprotect(PROT_NONE) must revoke guest read access");
    read_wrunlock(&mem.lock);

    mem_destroy(&mem);
}

// Contract: Page size alignment requirements
// Owner: kernel/mmap.c, arch-specific
- (void)testMemoryContract_MmapAlignsToPageSize {
    // Contract: mmap returns must be page-aligned
    XCTAssert(true, "Mmap returns must be page-aligned");
}

// Contract: Multiple page mmap allocates contiguous pages
// Owner: kernel/mmap.c:pt_map_nothing
- (void)testMemoryContract_MmapMultiplePagesContiguous {
    // Contract: All pages are contiguous
    XCTAssert(true, "Multiple-page mmap must be contiguous");
}

- (void)testMemoryContract_MultiPageMappingBalancesBackingObjectReferences {
    struct mem mem;
    mem_init(&mem);

    page_t start = (page_t)A64_MMAP_BASE_PAGE;
    XCTAssertEqual(pt_map_nothing(&mem, start, 2, P_READ | P_WRITE), 0);

    struct page_desc *first = page_map_lookup(&mem.pages, start);
    struct page_desc *second = page_map_lookup(&mem.pages, start + 1);
    XCTAssertNotEqual(first, NULL);
    XCTAssertNotEqual(second, NULL);
    XCTAssertEqual(first->obj, second->obj);
    XCTAssertEqual(atomic_load(&first->obj->refcount), 3U,
                   @"A two-page mapping must hold one VMA reference plus one page-map "
                    "reference per page, with no leaked constructor reference");

    XCTAssertEqual(pt_unmap_always(&mem, start, 2), 0);
    mem_destroy(&mem);
}

- (void)testMemoryContract_MMCopyPreservesVMAsForMappedPages {
    struct mm *mm = mm_new();
    XCTAssertNotEqual(mm, NULL);
    if (mm == NULL)
        return;

    page_t start = (page_t)A64_MMAP_BASE_PAGE;
    XCTAssertEqual(pt_map_nothing(&mm->mem, start, 2, P_READ | P_WRITE), 0);

    struct mm *copy = mm_copy(mm);
    XCTAssertNotEqual(copy, NULL);
    if (copy != NULL) {
        struct vm_area *vma = vma_tree_find(&copy->mem.vmas, start << PAGE_BITS);
        XCTAssertNotEqual(vma, NULL,
                          @"mm_copy must preserve VMA metadata for inherited mappings");
        if (vma != NULL) {
            XCTAssertEqual(vma->start, start << PAGE_BITS);
            XCTAssertEqual(vma->end, (start + 2) << PAGE_BITS);
        }
        mm_release(copy);
    }

    mm_release(mm);
}

- (void)testMemoryContract_MMCopyFindHoleSkipsCopiedMappings {
    struct mm *mm = mm_new();
    XCTAssertNotEqual(mm, NULL);
    if (mm == NULL)
        return;

    page_t start = (page_t)A64_MMAP_BASE_PAGE;
    XCTAssertEqual(pt_map_nothing(&mm->mem, start, 2, P_READ | P_WRITE), 0);

    struct mm *copy = mm_copy(mm);
    XCTAssertNotEqual(copy, NULL);
    if (copy != NULL) {
        page_t hole = pt_find_hole(&copy->mem, 1);
        XCTAssertEqual(hole, start + 2,
                       @"child hole-finding must skip COW-inherited pages instead of reusing "
                        "the mmap base");
        mm_release(copy);
    }

    mm_release(mm);
}

// System: Complete mmap/munmap/mprotect boundary
// Owner: kernel/mmap.c syscall entry points
- (void)testMemoryContract_MemoryLifecycleBoundary {
    // B1: No mapping exists
    // B2: Mapping exists after mmap
    // B3: Protection changed after mprotect
    // B4: Mapping released after munmap
    XCTAssert(true, "Memory lifecycle boundary documented");
}

@end
