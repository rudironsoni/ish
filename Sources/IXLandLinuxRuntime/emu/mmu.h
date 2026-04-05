#ifndef EMU_CPU_MEM_H
#define EMU_CPU_MEM_H

#include <IXLandLinuxRuntime/util/misc.h>

/*
 * MMU and page constants for 64-bit aarch64 userspace.
 *
 * ARCHITECTURAL INVARIANTS:
 * - Guest virtual addresses are 64-bit only. No 32-bit caps.
 * - Guest page numbers are 64-bit only.
 * - Page counts are 64-bit (uint64_t) where they represent sizes.
 * - The address space is bounded by A64_USER_TOP (48-bit VA = 256TB).
 * - There is NO global MEM_PAGES constant limiting the address space.
 * - Translation is generation-based for TLB and TCTI block cache correctness.
 */

/* Page geometry (4KB pages, standard for aarch64 Linux) */
#define PAGE_BITS 12
#undef PAGE_SIZE
#define PAGE_SIZE            (1 << PAGE_BITS)
#define PAGE(addr)           ((addr) >> PAGE_BITS)
#define PGOFFSET(addr)       ((addr) & (PAGE_SIZE - 1))
#define PAGE_ROUND_UP(bytes) (PAGE((bytes) + PAGE_SIZE - 1))

/* Page and address types - all 64-bit */
typedef qword_t page_t;
typedef qword_t pages_t; /* Was dword_t (uint32_t) -- widened for 64-bit */

/* Sentinel for "no page found" -- outside valid VA range */
#define BAD_PAGE ((page_t)~0ULL)

/* Forward declarations */
struct a64_block_cache;
struct mmu;

/*
 * Translation generation counter.
 *
 * Every time the address space mapping changes (map, unmap, protect, CoW),
 * the generation is atomically incremented. TLB entries and TCTI blocks
 * store the generation at the time they were created. On lookup, if the
 * stored generation does not match the current generation, the entry is
 * treated as stale and the translation is re-resolved.
 *
 * This replaces the old "changes" counter with an explicit contract:
 * - Read-side translation MUST check generation after resolving.
 * - Write-side mutations MUST bump generation.
 * - Stale entries are rejected, not dereferenced.
 */
typedef uint64_t mem_generation_t;

struct mmu {
    struct mmu_ops *ops;
    struct a64_block_cache *block_cache;
    mem_generation_t generation; /* Was "changes" -- now the translation generation */
};

#define MEM_READ         0
#define MEM_WRITE        1
#define MEM_WRITE_PTRACE 2

struct mmu_ops {
    void *(*translate)(struct mmu *mmu, addr_t addr, int type);
};

static inline void *mmu_translate(struct mmu *mmu, addr_t addr, int type)
{
    return mmu->ops->translate(mmu, addr, type);
}

#endif
