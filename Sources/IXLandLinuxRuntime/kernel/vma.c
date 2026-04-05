#include <IXLandLinuxRuntime/kernel/vma.h>
#include <stdlib.h>
#include <string.h>

/*
 * VMA sorted linked list implementation.
 *
 * Simple, correct, and fast enough for typical VMA counts (< 100).
 * All operations are O(n) in number of VMAs, not pages.
 */

void vma_tree_init(struct vma_tree *tree)
{
    tree->head = NULL;
    tree->count = 0;
}

void vma_tree_destroy(struct vma_tree *tree)
{
    struct vm_area *vma = tree->head;
    while (vma) {
        struct vm_area *next = vma->next;
        mem_object_release(vma->obj);
        free(vma);
        vma = next;
    }
    tree->head = NULL;
    tree->count = 0;
}

struct vm_area *vma_tree_find(struct vma_tree *tree, uint64_t addr)
{
    struct vm_area *vma = tree->head;
    while (vma) {
        if (addr >= vma->start && addr < vma->end)
            return vma;
        if (addr < vma->start)
            return NULL; /* past all possible matches */
        vma = vma->next;
    }
    return NULL;
}

struct vm_area *vma_tree_find_exact(struct vma_tree *tree, uint64_t addr)
{
    struct vm_area *vma = tree->head;
    while (vma) {
        if (addr == vma->start)
            return vma;
        if (addr < vma->start)
            return NULL;
        vma = vma->next;
    }
    return NULL;
}

uint64_t vma_tree_find_hole(struct vma_tree *tree, uint64_t pages)
{
    return vma_tree_find_hole_above(tree, 0, pages);
}

uint64_t vma_tree_find_hole_above(struct vma_tree *tree, uint64_t min_page, uint64_t pages)
{
    uint64_t scan = min_page;
    struct vm_area *vma = tree->head;

    while (vma) {
        uint64_t vma_start_page = vma->start >> 12;
        uint64_t vma_end_page = vma->end >> 12;

        if (vma_start_page > scan) {
            uint64_t gap = vma_start_page - scan;
            if (gap >= pages)
                return scan;
        }
        if (vma_end_page > scan)
            scan = vma_end_page;
        vma = vma->next;
    }

    /* Gap after last VMA up to USER_TOP */
    uint64_t top_page = A64_USER_TOP >> 12;
    if (top_page > scan && (top_page - scan) >= pages)
        return scan;

    return (uint64_t)-1;
}

void vma_tree_insert(struct vma_tree *tree, struct vm_area *vma)
{
    vma->next = NULL;

    if (!tree->head || vma->start < tree->head->start) {
        vma->next = tree->head;
        tree->head = vma;
        tree->count++;
        mem_object_retain(vma->obj);
        return;
    }

    struct vm_area *cur = tree->head;
    while (cur->next && cur->next->start < vma->start)
        cur = cur->next;

    vma->next = cur->next;
    cur->next = vma;
    tree->count++;
    mem_object_retain(vma->obj);
}

void vma_tree_remove(struct vma_tree *tree, struct vm_area *vma)
{
    if (tree->head == vma) {
        tree->head = vma->next;
    } else {
        struct vm_area *cur = tree->head;
        while (cur && cur->next != vma)
            cur = cur->next;
        if (cur)
            cur->next = vma->next;
    }
    tree->count--;
}

int vma_tree_remove_range(struct vma_tree *tree, uint64_t start_page, uint64_t pages,
                          struct vm_area **out_removed, int max_removed)
{
    uint64_t start_addr = start_page << 12;
    uint64_t end_addr = (start_page + pages) << 12;
    int count = 0;

    struct vm_area *vma = tree->head;
    while (vma) {
        struct vm_area *next = vma->next;
        if (vma->end > start_addr && vma->start < end_addr) {
            if (count < max_removed)
                out_removed[count] = vma;
            count++;
            vma_tree_remove(tree, vma);
        }
        vma = next;
    }

    return count;
}

void vma_tree_iterate(struct vma_tree *tree, vma_iter_cb cb, void *ctx)
{
    struct vm_area *vma = tree->head;
    while (vma) {
        if (cb(vma, ctx))
            break;
        vma = vma->next;
    }
}

struct vm_area *vma_alloc(void)
{
    return calloc(1, sizeof(struct vm_area));
}

void vma_free(struct vm_area *vma)
{
    free(vma);
}
