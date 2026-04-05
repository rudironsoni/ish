#ifndef PAGE_MAP_H
#define PAGE_MAP_H

#include <IXLandLinuxRuntime/kernel/mem_object.h>
#include <IXLandLinuxRuntime/util/sync.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Sparse radix page map for 64-bit aarch64 guest address translation.
 *
 * Replaces the old two-level pgdir (10+10 bits = 4GB cap) with a
 * 4-level radix tree using 9-bit fanout at each level.
 *
 * Layout for 48-bit VA with 4KB pages:
 *   bits 47:39 - level 0 index (512 entries)
 *   bits 38:30 - level 1 index (512 entries)
 *   bits 29:21 - level 2 index (512 entries)
 *   bits 20:12 - level 3 index (512 entries, leaf)
 *   bits 11:0  - page offset (4KB)
 *
 * This matches arm64 hardware page table thinking and supports the full
 * 48-bit user VA space (256TB).
 *
 * The page map is sparse: intermediate nodes are allocated on demand.
 * All operations are protected by the caller's mem->lock (rwlock).
 */

#define PAGE_MAP_LEVELS  4
#define PAGE_MAP_FANOUT  9
#define PAGE_MAP_ENTRIES (1ULL << PAGE_MAP_FANOUT) /* 512 */

/* Extract index at a given level from a 64-bit guest page number */
#define PAGE_MAP_INDEX(page, level)                                                                \
    (((page) >> (PAGE_MAP_FANOUT * (PAGE_MAP_LEVELS - 1 - (level)))) & (PAGE_MAP_ENTRIES - 1))

/* Page descriptor stored in leaf entries */
struct page_desc {
    struct mem_object *obj; /* backing object (refcounted) */
    size_t offset;          /* byte offset into obj->host_base */
    unsigned flags;         /* P_READ, P_WRITE, P_EXEC, P_COW, etc. */
};

/* Internal radix tree node */
struct page_map_node {
    union {
        struct page_map_node *children[PAGE_MAP_ENTRIES]; /* internal node */
        struct page_desc *leaves[PAGE_MAP_ENTRIES];       /* leaf node (level 3) */
    };
    unsigned level; /* 0 = root, 3 = leaf */
};

struct page_map {
    struct page_map_node *root;
};

/* Initialize an empty page map */
void page_map_init(struct page_map *map);

/* Destroy a page map, releasing all page descriptors.
   Does NOT release mem_object references; caller handles those. */
void page_map_destroy(struct page_map *map, void (*on_release)(struct page_desc *));

/*
 * Look up a page descriptor for the given guest page number.
 * Returns NULL if no mapping exists.
 */
struct page_desc *page_map_lookup(struct page_map *map, uint64_t page);

/*
 * Install a page descriptor at the given guest page number.
 * Allocates intermediate nodes as needed.
 * Takes a reference on desc->obj.
 * Returns 0 on success, -1 on allocation failure.
 */
int page_map_install(struct page_map *map, uint64_t page, struct page_desc *desc);

/*
 * Remove a page descriptor at the given guest page number.
 * Returns the removed descriptor (caller must release obj reference),
 * or NULL if no mapping existed.
 * Does NOT free intermediate nodes (they stay allocated for future use).
 */
struct page_desc *page_map_remove(struct page_map *map, uint64_t page);

/*
 * Iterate over all mapped pages in the range [start_page, start_page + num_pages).
 * Callback receives (page, desc, ctx); return 0 to continue, non-zero to stop.
 */
typedef int (*page_map_iter_cb)(uint64_t page, struct page_desc *desc, void *ctx);
void page_map_iterate_range(struct page_map *map, uint64_t start_page, uint64_t num_pages,
                            page_map_iter_cb cb, void *ctx);

/* Iterate over ALL mapped pages */
void page_map_iterate_all(struct page_map *map, page_map_iter_cb cb, void *ctx);

#endif
