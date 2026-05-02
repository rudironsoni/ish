#include <IXLandLinuxRuntime/kernel/page_map.h>
#include <stdlib.h>
#include <string.h>

/*
 * Sparse radix page map implementation.
 *
 * 4-level radix tree with 9-bit fanout (512 entries per level).
 * Supports full 48-bit aarch64 user VA space.
 *
 * All operations assume the caller holds the appropriate lock.
 */

void page_map_init(struct page_map *map)
{
    map->root = NULL;
}

static void destroy_node(struct page_map_node *node, void (*on_release)(struct page_desc *))
{
    if (!node)
        return;
    if (node->level < PAGE_MAP_LEVELS - 1) {
        for (unsigned i = 0; i < PAGE_MAP_ENTRIES; i++) {
            if (node->children[i])
                destroy_node(node->children[i], on_release);
        }
    } else {
        for (unsigned i = 0; i < PAGE_MAP_ENTRIES; i++) {
            if (node->leaves[i] && on_release)
                on_release(node->leaves[i]);
        }
    }
    free(node);
}

void page_map_destroy(struct page_map *map, void (*on_release)(struct page_desc *))
{
    destroy_node(map->root, on_release);
    map->root = NULL;
}

struct page_desc *page_map_lookup(struct page_map *map, uint64_t page)
{
    struct page_map_node *node = map->root;
    if (!node)
        return NULL;

    for (unsigned level = 0; level < PAGE_MAP_LEVELS - 1; level++) {
        unsigned idx = PAGE_MAP_INDEX(page, level);
        if (!node->children[idx])
            return NULL;
        node = node->children[idx];
    }

    unsigned leaf_idx = PAGE_MAP_INDEX(page, PAGE_MAP_LEVELS - 1);
    return node->leaves[leaf_idx];
}

static struct page_map_node *alloc_node(unsigned level)
{
    struct page_map_node *node = calloc(1, sizeof(struct page_map_node));
    if (!node)
        return NULL;
    node->level = level;
    return node;
}

int page_map_install(struct page_map *map, uint64_t page, struct page_desc *desc)
{
    if (!map->root) {
        map->root = alloc_node(0);
        if (!map->root)
            return -1;
    }

    struct page_map_node *node = map->root;

    for (unsigned level = 0; level < PAGE_MAP_LEVELS - 1; level++) {
        unsigned idx = PAGE_MAP_INDEX(page, level);
        if (!node->children[idx]) {
            node->children[idx] = alloc_node(level + 1);
            if (!node->children[idx])
                return -1;
        }
        node = node->children[idx];
    }

    unsigned leaf_idx = PAGE_MAP_INDEX(page, PAGE_MAP_LEVELS - 1);

    /* Replace existing entry if present */
    struct page_desc *old = node->leaves[leaf_idx];
    if (old) {
        mem_object_release(old->obj);
        free(old);
    }

    node->leaves[leaf_idx] = desc;
    mem_object_retain(desc->obj);
    return 0;
}

struct page_desc *page_map_remove(struct page_map *map, uint64_t page)
{
    struct page_map_node *node = map->root;
    if (!node)
        return NULL;

    struct page_map_node *path[PAGE_MAP_LEVELS];
    unsigned path_idx[PAGE_MAP_LEVELS];

    for (unsigned level = 0; level < PAGE_MAP_LEVELS - 1; level++) {
        unsigned idx = PAGE_MAP_INDEX(page, level);
        if (!node->children[idx])
            return NULL;
        path[level] = node;
        path_idx[level] = idx;
        node = node->children[idx];
    }

    unsigned leaf_idx = PAGE_MAP_INDEX(page, PAGE_MAP_LEVELS - 1);
    struct page_desc *desc = node->leaves[leaf_idx];
    if (!desc)
        return NULL;

    node->leaves[leaf_idx] = NULL;

    /* Note: We do NOT free empty intermediate nodes.
       They stay allocated for future use. This avoids the complexity
       of tracking node liveness during lockless translation.
       Memory overhead is bounded: worst case 512 * 4KB = 2MB per
       fully-populated level, but sparse usage is far smaller. */

    return desc;
}

static void iterate_node(struct page_map_node *node, uint64_t base_page, page_map_iter_cb cb,
                         void *ctx, int *stop)
{
    if (!node || *stop)
        return;

    if (node->level < PAGE_MAP_LEVELS - 1) {
        uint64_t shift = PAGE_MAP_FANOUT * (PAGE_MAP_LEVELS - 1 - node->level);
        for (unsigned i = 0; i < PAGE_MAP_ENTRIES && !*stop; i++) {
            if (node->children[i])
                iterate_node(node->children[i], base_page | ((uint64_t)i << shift), cb, ctx, stop);
        }
    } else {
        for (unsigned i = 0; i < PAGE_MAP_ENTRIES && !*stop; i++) {
            if (node->leaves[i]) {
                uint64_t page = base_page | i;
                *stop = cb(page, node->leaves[i], ctx);
            }
        }
    }
}

void page_map_iterate_range(struct page_map *map, uint64_t start_page, uint64_t num_pages,
                            page_map_iter_cb cb, void *ctx)
{
    uint64_t end_page = start_page + num_pages;
    int stop = 0;

    struct {
        struct page_map_node *node;
        uint64_t base_page;
    } stack[PAGE_MAP_LEVELS + 1];
    int sp = 0;

    if (!map->root)
        return;

    stack[sp].node = map->root;
    stack[sp].base_page = 0;
    sp++;

    while (sp > 0 && !stop) {
        sp--;
        struct page_map_node *node = stack[sp].node;
        uint64_t base = stack[sp].base_page;

        if (node->level < PAGE_MAP_LEVELS - 1) {
            uint64_t shift = PAGE_MAP_FANOUT * (PAGE_MAP_LEVELS - 1 - node->level);
            uint64_t span = 1ULL << shift;
            for (unsigned i = 0; i < PAGE_MAP_ENTRIES; i++) {
                if (!node->children[i])
                    continue;
                uint64_t child_base = base | ((uint64_t)i << shift);
                uint64_t child_end = child_base + span;
                if (child_end <= start_page || child_base >= end_page)
                    continue;
                stack[sp].node = node->children[i];
                stack[sp].base_page = child_base;
                sp++;
            }
        } else {
            for (unsigned i = 0; i < PAGE_MAP_ENTRIES; i++) {
                if (!node->leaves[i])
                    continue;
                uint64_t page = base | i;
                if (page < start_page || page >= end_page)
                    continue;
                stop = cb(page, node->leaves[i], ctx);
                if (stop)
                    break;
            }
        }
    }
}

void page_map_iterate_all(struct page_map *map, page_map_iter_cb cb, void *ctx)
{
    int stop = 0;
    iterate_node(map->root, 0, cb, ctx, &stop);
}
