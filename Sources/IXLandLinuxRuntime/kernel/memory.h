#ifndef MEMORY_H
#define MEMORY_H

#include <IXLandLinuxRuntime/emu/mmu.h>
#include <IXLandLinuxRuntime/kernel/mem_object.h>
#include <IXLandLinuxRuntime/kernel/page_map.h>
#include <IXLandLinuxRuntime/kernel/vma.h>
#include <IXLandLinuxRuntime/util/sync.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <unistd.h>

/*
 * 64-bit aarch64 memory management subsystem.
 *
 * ARCHITECTURAL INVARIANTS:
 * - Guest VA space is 64-bit only, bounded by A64_USER_TOP (48-bit = 256TB).
 * - NO MEM_PAGES global cap. The address space is sparse and unbounded.
 * - VMA tree manages regions (hole-finding, split, merge, permissions).
 * - Sparse radix page map (4-level, 9-bit fanout) handles per-page translation.
 * - Backing objects (mem_object) are refcounted and NEVER freed immediately on unmap.
 * - Deferred reclamation via epoch-based retire lists prevents UAF.
 * - Translation is generation-based: every translation mutation bumps mmu.generation.
 * - Executable block reuse is code-generation-based: only executable layout or
 *   code-byte changes bump mmu.code_generation.
 * - TLB entries and TCTI block cache entries reject stale generations.
 *
 * OLD MODEL (deleted):
 * - struct pt_entry **pgdir (two-level 10+10 bit page table = 4GB cap)
 * - struct data with immediate free on refcount==0 (UAF-prone)
 * - MEM_PAGES = (1 << 20) hardcoded limit
 * - pt_find_hole linear scan over 0x40000-0xf7ffd
 * - mem_ptr_nofault naked entry->data->data dereference (no lock, no generation)
 */

/* Page flags (preserved from old model for syscall compatibility) */
#define P_READ  (1 << 0)
#define P_WRITE (1 << 1)
#undef P_EXEC /* defined in sys/proc.h on darwin */
#define P_EXEC            (1 << 2)
#define P_RWX             (P_READ | P_WRITE | P_EXEC)
#define P_GROWSDOWN       (1 << 3)
#define P_COW             (1 << 4)
#define P_WRITABLE(flags) (flags & P_WRITE && !(flags & P_COW))
#define P_ANONYMOUS       (1 << 6)
#define P_SHARED          (1 << 7)

/* Host page helpers (for mmap/munmap operations on the host) */
extern size_t real_page_size;
#define HOST_PAGE_SIZE     real_page_size
#define HOST_ROUND_DOWN(x) ((x) & ~(real_page_size - 1))
#define HOST_ROUND_UP(x)   (((x) + real_page_size - 1) & ~(real_page_size - 1))

/*
 * struct mem - the address space descriptor.
 *
 * Replaces the old struct mem { pgdir, pgdir_used, mmu, lock }.
 * Now contains:
 * - vma_tree: ordered interval tree for region management
 * - page_map: sparse radix tree for per-page translation
 * - mmu: MMU ops + generation counter
 * - lock: rwlock protecting all mutations
 * - retire_list: deferred reclamation list for freed mem_objects
 * - retire_count: number of objects on retire list
 */
struct mem {
    struct vma_tree vmas;  /* VMA interval tree */
    struct page_map pages; /* sparse radix page map */
    struct mmu mmu;        /* MMU ops + generation */
    wrlock_t lock;         /* protects all mutations */

    /* Deferred reclamation */
    struct list retire_list;        /* doubly-linked list head for retired objects */
    struct mem_object *retire_tail; /* last object in retire list (for O(1) append) */
    int retire_count;
};

/* Initialize the address space */
void mem_init(struct mem *mem);

/* Uninitialize the address space (frees all VMAs, page map entries, retires) */
void mem_destroy(struct mem *mem);

/*
 * Map memory into the address space.
 * Takes ownership of `memory` (will be munmap'd on final release).
 * Creates a VMA and installs page descriptors for each page.
 */
int pt_map(struct mem *mem, page_t start, pages_t pages, void *memory, size_t offset,
           unsigned flags);

/* Map anonymous (zero-filled) pages */
int pt_map_nothing(struct mem *mem, page_t page, pages_t pages, unsigned flags);

/* Unmap pages. Returns -1 if any part of the range isn't mapped. */
int pt_unmap(struct mem *mem, page_t start, pages_t pages);

/* Unmap pages, ignoring unmapped regions. */
int pt_unmap_always(struct mem *mem, page_t start, pages_t pages);

/* Set flags on a range of pages. */
int pt_set_flags(struct mem *mem, page_t start, pages_t pages, int flags);

/* Copy pages from src to dst using copy-on-write (for fork). */
int pt_copy_on_write(struct mem *src, struct mem *dst, page_t start, pages_t pages);

/*
 * Translate a guest virtual address to a host pointer.
 * Returns NULL if the page is not mapped or permissions don't allow access.
 * Must be called with mem read-locked.
 *
 * This replaces the old mem_ptr_nofault. It now:
 * - Looks up the VMA to get the mem_object and offset
 * - Checks the generation counter for staleness
 * - Returns a stable host pointer only for RAM-like mappings
 * - Rejects special mappings (returns NULL, caller must use helper path)
 */
void *mem_ptr(struct mem *mem, addr_t addr, int type);

/* Determine SIGSEGV reason (MAPERR vs ACCERR) */
int mem_segv_reason(struct mem *mem, addr_t addr);

/* Find a hole of at least `size` pages in the address space. */
page_t pt_find_hole(struct mem *mem, pages_t size);

/* Check if a range is unmapped. */
bool pt_is_hole(struct mem *mem, page_t start, pages_t pages);

/* Dump memory to a core file (64-bit correct). */
void mem_coredump(struct mem *mem, const char *file);

/* Bump the translation generation (called on every translation-visible mutation). */
static inline void mem_bump_generation(struct mem *mem)
{
    __atomic_fetch_add(&mem->mmu.generation, 1, __ATOMIC_SEQ_CST);
}

/* Bump the executable-code generation (only when executable mappings/bytes change). */
static inline void mem_bump_code_generation(struct mem *mem)
{
    __atomic_fetch_add(&mem->mmu.code_generation, 1, __ATOMIC_SEQ_CST);
}

/* Drain the retire list: free objects whose retire_generation is stale. */
void mem_drain_retired(struct mem *mem);

#endif
