#ifndef VMA_H
#define VMA_H

#include <IXLandLinuxRuntime/kernel/mem_object.h>
#include <IXLandLinuxRuntime/util/sync.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * VMA (Virtual Memory Area) list for 64-bit aarch64 userspace.
 *
 * Replaces the old linear pgdir scan with an ordered linked list.
 * Each VMA represents a contiguous guest virtual address range with
 * uniform permissions and a shared backing mem_object.
 *
 * Invariants:
 * - VMAs never overlap; adjacent VMAs with identical properties are merged.
 * - The list is ordered by vma->start.
 * - Hole finding walks the gaps between VMAs.
 * - All operations are protected by the mem->lock (rwlock).
 *
 * Using a sorted linked list instead of an RB tree because:
 * - Typical processes have < 100 VMAs
 * - O(n) for n<100 is faster than O(log n) with RB tree overhead
 * - Much simpler to get correct (no rotation bugs)
 */

/* AArch64 userspace VA policy: 48-bit user VA (standard for arm64 Linux) */
#define A64_USER_VA_BITS 48
#define A64_TASK_SIZE    ((1ULL << A64_USER_VA_BITS) - 1)
#define A64_USER_TOP     (1ULL << A64_USER_VA_BITS)

struct vm_area {
    uint64_t start;         /* inclusive guest VA start (page-aligned) */
    uint64_t end;           /* exclusive guest VA end (page-aligned) */
    unsigned flags;         /* P_READ, P_WRITE, P_EXEC, etc. */
    struct mem_object *obj; /* backing object (refcounted) */
    size_t obj_offset;      /* offset into obj->host_base for this VMA's start */

    /* Sorted linked list */
    struct vm_area *next;
};

struct vma_tree {
    struct vm_area *head;
    unsigned count;
};

/* Initialize an empty VMA tree */
void vma_tree_init(struct vma_tree *tree);

/* Destroy a VMA tree, releasing all VMAs and their backing objects */
void vma_tree_destroy(struct vma_tree *tree);

/*
 * Find a VMA that contains the given guest address.
 * Returns NULL if no VMA covers addr.
 */
struct vm_area *vma_tree_find(struct vma_tree *tree, uint64_t addr);

/*
 * Find a VMA that starts at exactly the given address.
 * Returns NULL if no VMA starts at addr.
 */
struct vm_area *vma_tree_find_exact(struct vma_tree *tree, uint64_t addr);

/*
 * Allocate a hole of at least `pages` contiguous unmapped pages.
 * Returns the start page number, or (uint64_t)-1 if no hole found.
 */
uint64_t vma_tree_find_hole(struct vma_tree *tree, uint64_t pages);

/*
 * Allocate a hole at or above `min_page` of at least `pages` pages.
 * Returns the start page number, or (uint64_t)-1 if no hole found.
 */
uint64_t vma_tree_find_hole_above(struct vma_tree *tree, uint64_t min_page, uint64_t pages);

/*
 * Insert a new VMA into the list in sorted order.
 * Takes a reference on obj.
 */
void vma_tree_insert(struct vma_tree *tree, struct vm_area *vma);

/*
 * Remove a VMA from the list.
 * Does NOT release the backing object reference; caller must handle that.
 */
void vma_tree_remove(struct vma_tree *tree, struct vm_area *vma);

/*
 * Remove all VMAs in the range [start_page, start_page + pages).
 * VMAs are split if partially covered.
 * Returns the number of VMAs removed.
 */
int vma_tree_remove_range(struct vma_tree *tree, uint64_t start_page, uint64_t pages,
                          struct vm_area **out_removed, int max_removed);

/*
 * Iterate over all VMAs in address order.
 * Callback receives each VMA; return 0 to continue, non-zero to stop.
 */
typedef int (*vma_iter_cb)(struct vm_area *vma, void *ctx);
void vma_tree_iterate(struct vma_tree *tree, vma_iter_cb cb, void *ctx);

/* Allocate a new vm_area (caller must fill fields and insert) */
struct vm_area *vma_alloc(void);

/* Free a vm_area (does NOT release obj reference) */
void vma_free(struct vm_area *vma);

#endif
