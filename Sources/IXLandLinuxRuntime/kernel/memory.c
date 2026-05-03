#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define DEFAULT_CHANNEL memory
#import <IXLandInstrumentationTracing/trace_internal.h>
#import <IXLandLinuxRuntime/emu/aarch64/block-cache.h>
#import <IXLandLinuxRuntime/fs/fd.h>
#import <IXLandLinuxRuntime/kernel/errno.h>
#import <IXLandLinuxRuntime/kernel/memory.h>
#import <IXLandLinuxRuntime/kernel/signal.h>
#import <IXLandLinuxRuntime/kernel/task.h>
#import <IXLandLinuxRuntime/kernel/vdso.h>
#import <IXLandLinuxRuntime/util/debug.h>

static struct mmu_ops mem_mmu_ops;

void mem_init(struct mem *mem)
{
    memset(mem, 0, sizeof(*mem));
    vma_tree_init(&mem->vmas);
    page_map_init(&mem->pages);
    mem->mmu.ops = &mem_mmu_ops;
    mem->mmu.generation = 0;
    list_init(&mem->retire_list);
    mem->retire_tail = NULL;
    mem->retire_count = 0;
    wrlock_init(&mem->lock);
}

static void retire_page_desc(struct mem *mem, struct page_desc *desc)
{
    (void)mem;
    if (desc == NULL)
        return;
    mem_object_release(desc->obj);
    free(desc);
}

void mem_drain_retired(struct mem *mem)
{
    mem_generation_t current = mem->mmu.generation;
    struct list *head = &mem->retire_list;
    struct list *cur = head->next;
    while (cur != head) {
        struct mem_object *obj = container_of(cur, struct mem_object, retire_link);
        struct list *next = cur->next;
        if (obj->retire_generation < current) {
            list_remove(cur);
            obj->retire_generation = 0;
            if (mem->retire_tail == obj) {
                mem->retire_tail = cur->prev != head
                                       ? container_of(cur->prev, struct mem_object, retire_link)
                                       : NULL;
            }
            mem->retire_count--;
            mem_object_release(obj);
        }
        cur = next;
    }
}

static int mem_destroy_page_cb(uint64_t page, struct page_desc *desc, void *ctx)
{
    (void)page;
    struct mem *mem = (struct mem *)ctx;
    retire_page_desc(mem, desc);
    return 0;
}

static int mem_coredump_max_page_cb(uint64_t page, struct page_desc *desc, void *ctx)
{
    (void)desc;
    page_t *max = (page_t *)ctx;
    if (page > *max)
        *max = page;
    return 0;
}

static int mem_coredump_write_cb(uint64_t page, struct page_desc *desc, void *ctx)
{
    int *fd_ptr = (int *)ctx;
    if (lseek(*fd_ptr, page << PAGE_BITS, SEEK_SET) < 0) {
        perror("lseek");
        return 1;
    }
    if (write(*fd_ptr, desc->obj->host_base + desc->offset, PAGE_SIZE) < 0) {
        perror("write");
        return 1;
    }
    return 0;
}

void mem_destroy(struct mem *mem)
{
    if (!mem)
        return;
    write_wrlock(&mem->lock);

    page_map_iterate_all(&mem->pages, mem_destroy_page_cb, mem);

    page_map_destroy(&mem->pages, NULL);
    vma_tree_destroy(&mem->vmas);

    mem->mmu.generation++;
    mem_drain_retired(mem);

    write_wrunlock(&mem->lock);
    wrlock_destroy(&mem->lock);
}

page_t pt_find_hole(struct mem *mem, pages_t size)
{
    uint64_t page = vma_tree_find_hole_above(&mem->vmas, A64_MMAP_BASE_PAGE, size);
    return (page_t)page;
}

bool pt_is_hole(struct mem *mem, page_t start, pages_t pages)
{
    if (pages == 0)
        return true;

    uint64_t addr = start << PAGE_BITS;
    uint64_t end_addr = (start + pages) << PAGE_BITS;

    for (struct vm_area *vma = mem->vmas.head; vma; vma = vma->next) {
        if (vma->start >= end_addr)
            return true;
        if (vma->end > addr)
            return false;
    }

    return true;
}

int pt_map(struct mem *mem, page_t start, pages_t pages, void *memory, size_t offset,
           unsigned flags)
{
    if (memory == MAP_FAILED)
        return errno_map();

    assert((uintptr_t)memory % real_page_size == 0 || memory == vdso_data);

    enum mem_object_kind kind = MEM_OBJ_RAM;
    if (flags & P_ANONYMOUS)
        kind = MEM_OBJ_RAM;
    else if (memory == vdso_data)
        kind = MEM_OBJ_VDSO;
    else
        kind = MEM_OBJ_FILE;

    struct mem_object *obj =
        mem_object_new(memory, HOST_ROUND_UP((size_t)pages * PAGE_SIZE + offset), kind);
    if (!obj)
        return _ENOMEM;

    struct vm_area *removed[64];
    int n_removed = vma_tree_remove_range(&mem->vmas, start, pages, removed, 64);
    for (int i = 0; i < n_removed; i++) {
        page_t pg_start = removed[i]->start >> PAGE_BITS;
        page_t pg_end = removed[i]->end >> PAGE_BITS;
        for (page_t pg = pg_start; pg < pg_end; pg++) {
            struct page_desc *old_desc = page_map_remove(&mem->pages, pg);
            retire_page_desc(mem, old_desc);
        }
        mem_object_release(removed[i]->obj);
        vma_free(removed[i]);
    }

    struct vm_area *vma = vma_alloc();
    if (!vma) {
        mem_object_release(obj);
        return _ENOMEM;
    }
    vma->start = start << PAGE_BITS;
    vma->end = (start + pages) << PAGE_BITS;
    vma->flags = flags;
    vma->obj = obj;
    vma->obj_offset = offset;
    vma_tree_insert(&mem->vmas, vma);

    for (page_t page = start; page < start + pages; page++) {
        struct page_desc *desc = malloc(sizeof(struct page_desc));
        if (!desc) {
            for (page_t pg = start; pg < page; pg++) {
                struct page_desc *d = page_map_remove(&mem->pages, pg);
                retire_page_desc(mem, d);
            }
            vma_tree_remove(&mem->vmas, vma);
            mem_object_release(obj);
            vma_free(vma);
            return _ENOMEM;
        }
        desc->obj = obj;
        desc->offset = ((page - start) << PAGE_BITS) + offset;
        desc->flags = flags;
        page_map_install(&mem->pages, page, desc);
    }

    mem_bump_generation(mem);
    return 0;
}

int pt_map_nothing(struct mem *mem, page_t start, pages_t pages, unsigned flags)
{
    if (pages == 0)
        return 0;
    void *memory = mmap(NULL, HOST_ROUND_UP((size_t)pages * PAGE_SIZE), PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
    return pt_map(mem, start, pages, memory, 0, flags | P_ANONYMOUS);
}

int pt_unmap(struct mem *mem, page_t start, pages_t pages)
{
    for (page_t page = start; page < start + pages; page++) {
        if (!page_map_lookup(&mem->pages, page))
            return -1;
    }
    return pt_unmap_always(mem, start, pages);
}

int pt_unmap_always(struct mem *mem, page_t start, pages_t pages)
{
    struct task *task = current;
    if (task && task->cpu.mmu && task->cpu.mmu->block_cache) {
        a64_cache_invalidate_all(task->cpu.mmu->block_cache);
    }

    for (page_t page = start; page < start + pages; page++) {
        struct page_desc *desc = page_map_remove(&mem->pages, page);
        retire_page_desc(mem, desc);
    }

    struct vm_area *removed[64];
    int n_removed = vma_tree_remove_range(&mem->vmas, start, pages, removed, 64);
    for (int i = 0; i < n_removed; i++) {
        mem_object_release(removed[i]->obj);
        vma_free(removed[i]);
    }

    mem_bump_generation(mem);
    return 0;
}

int pt_set_flags(struct mem *mem, page_t start, pages_t pages, int flags)
{
    for (page_t page = start; page < start + pages; page++) {
        struct page_desc *desc = page_map_lookup(&mem->pages, page);
        if (!desc)
            return _ENOMEM;

        int old_flags = desc->flags;
        desc->flags = flags;

        struct vm_area *vma = vma_tree_find(&mem->vmas, page << PAGE_BITS);
        if (vma)
            vma->flags = flags;

        if ((flags & ~old_flags) & (P_READ | P_WRITE)) {
            void *host_ptr = (char *)desc->obj->host_base + desc->offset;
            host_ptr = (void *)((uintptr_t)host_ptr & ~(real_page_size - 1));
            int prot = PROT_READ;
            if (flags & P_WRITE)
                prot |= PROT_WRITE;
            if (mprotect(host_ptr, real_page_size, prot) < 0)
                return errno_map();
        }
    }

    mem_bump_generation(mem);
    return 0;
}

struct cow_copy_ctx {
    struct mem *src;
    struct mem *dst;
    int err;
};

static int pt_copy_on_write_page(uint64_t page, struct page_desc *src_desc, void *opaque)
{
    struct cow_copy_ctx *ctx = opaque;
    struct mem *src = ctx->src;
    struct mem *dst = ctx->dst;

    struct page_desc *old_dst = page_map_remove(&dst->pages, page);
    retire_page_desc(dst, old_dst);

    if (!(src_desc->flags & P_SHARED))
        src_desc->flags |= P_COW;

    struct page_desc *dst_desc = malloc(sizeof(struct page_desc));
    if (!dst_desc) {
        ctx->err = _ENOMEM;
        return 1;
    }
    dst_desc->obj = src_desc->obj;
    dst_desc->offset = src_desc->offset;
    dst_desc->flags = src_desc->flags;
    if (page_map_install(&dst->pages, page, dst_desc) < 0) {
        free(dst_desc);
        ctx->err = _ENOMEM;
        return 1;
    }

    struct vm_area *src_vma = vma_tree_find(&src->vmas, page << PAGE_BITS);
    if (src_vma) {
        struct vm_area *dst_vma = vma_tree_find(&dst->vmas, page << PAGE_BITS);
        if (!dst_vma) {
            dst_vma = vma_alloc();
            if (!dst_vma) {
                ctx->err = _ENOMEM;
                return 1;
            }
            dst_vma->start = src_vma->start;
            dst_vma->end = src_vma->end;
            dst_vma->flags = src_vma->flags;
            if (!(src_desc->flags & P_SHARED))
                dst_vma->flags |= P_COW;
            dst_vma->obj = src_vma->obj;
            dst_vma->obj_offset = src_vma->obj_offset;
            vma_tree_insert(&dst->vmas, dst_vma);
        }
    }

    return 0;
}

int pt_copy_on_write(struct mem *src, struct mem *dst, page_t start, pages_t pages)
{
    struct cow_copy_ctx ctx = { .src = src, .dst = dst, .err = 0 };
    page_map_iterate_range(&src->pages, start, pages, pt_copy_on_write_page, &ctx);
    if (ctx.err < 0)
        return ctx.err;

    mem_bump_generation(src);
    mem_bump_generation(dst);
    return 0;
}

void *mem_ptr(struct mem *mem, addr_t addr, int type)
{
    page_t page = PAGE(addr);

    struct page_desc *desc = page_map_lookup(&mem->pages, page);
    if (!desc) {
        struct vm_area *grow = NULL;
        for (struct vm_area *vma = mem->vmas.head; vma; vma = vma->next) {
            if (vma->start > addr) {
                grow = vma;
                break;
            }
        }
        if (!grow || !(grow->flags & P_GROWSDOWN))
            return NULL;
        unsigned grow_flags = grow->flags;

        read_wrunlock(&mem->lock);
        write_wrlock(&mem->lock);

        if (!page_map_lookup(&mem->pages, page)) {
            unsigned flags = grow_flags;
            if (!(flags & P_WRITE))
                flags |= P_WRITE;
            (void)pt_map_nothing(mem, page, 1, flags);
        }

        write_wrunlock(&mem->lock);
        read_wrlock(&mem->lock);

        desc = page_map_lookup(&mem->pages, page);
        if (!desc)
            return NULL;
    }

    if (type == MEM_WRITE || type == MEM_WRITE_PTRACE) {
        if (type != MEM_WRITE_PTRACE && !(desc->flags & P_WRITE))
            return NULL;

        if (type == MEM_WRITE_PTRACE && !(desc->flags & P_WRITE)) {
            read_wrunlock(&mem->lock);
            write_wrlock(&mem->lock);
            desc = page_map_lookup(&mem->pages, page);
            if (desc)
                desc->flags |= P_WRITE | P_COW;
            mem_bump_generation(mem);
            write_wrunlock(&mem->lock);
            read_wrlock(&mem->lock);
            desc = page_map_lookup(&mem->pages, page);
            if (!desc)
                return NULL;
        }

        if (desc->flags & P_COW) {
            void *guest_ptr = (char *)desc->obj->host_base + desc->offset;
            void *host_base = (void *)HOST_ROUND_DOWN((uintptr_t)guest_ptr);
            size_t host_off = (uintptr_t)guest_ptr - (uintptr_t)host_base;
            unsigned flags = desc->flags & ~P_COW;
            void *copy = mmap(NULL, real_page_size, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
            if (copy == MAP_FAILED)
                return NULL;
            memcpy(copy, host_base, real_page_size);

            read_wrunlock(&mem->lock);
            write_wrlock(&mem->lock);

            desc = page_map_lookup(&mem->pages, page);
            if (desc && (desc->flags & P_COW)) {
                (void)pt_map(mem, page, 1, copy, host_off, flags);
                copy = MAP_FAILED;
            }

            write_wrunlock(&mem->lock);
            read_wrlock(&mem->lock);

            if (copy != MAP_FAILED)
                munmap(copy, real_page_size);

            desc = page_map_lookup(&mem->pages, page);
            if (!desc)
                return NULL;
        }

        struct task *task = current;
        if (task && task->cpu.mmu && task->cpu.mmu->block_cache)
            a64_cache_invalidate_all(task->cpu.mmu->block_cache);
    }

    if (desc->obj->kind == MEM_OBJ_SPECIAL)
        return NULL;

    void *host_ptr = (char *)desc->obj->host_base + desc->offset + PGOFFSET(addr);

    return host_ptr;
}

int mem_segv_reason(struct mem *mem, addr_t addr)
{
    struct page_desc *pt = page_map_lookup(&mem->pages, PAGE(addr));
    if (!pt)
        return SEGV_MAPERR_;
    return SEGV_ACCERR_;
}

void mem_coredump(struct mem *mem, const char *file)
{
    int fd = open(file, O_CREAT | O_RDWR | O_TRUNC, 0666);
    if (fd < 0) {
        perror("open");
        return;
    }

    page_t max_page = 0;
    page_map_iterate_all(&mem->pages, mem_coredump_max_page_cb, &max_page);

    if (max_page > 0) {
        off_t file_size = ((max_page + 1) << PAGE_BITS);
        if (ftruncate(fd, file_size) < 0) {
            perror("ftruncate");
            close(fd);
            return;
        }
    }

    page_map_iterate_all(&mem->pages, mem_coredump_write_cb, &fd);

    printk("dumped pages\n");
    close(fd);
}

static void *mem_mmu_translate(struct mmu *mmu, addr_t addr, int type)
{
    struct mem *mem = container_of(mmu, struct mem, mmu);
    page_t page = PAGE(addr);
    struct page_desc *desc = page_map_lookup(&mem->pages, page);
    if (!desc)
        return NULL;
    if (type == MEM_WRITE && !P_WRITABLE(desc->flags))
        return NULL;
    if (desc->obj->kind == MEM_OBJ_SPECIAL)
        return NULL;
    return (char *)desc->obj->host_base + desc->offset + PGOFFSET(addr);
}

static struct mmu_ops mem_mmu_ops = {
    .translate = mem_mmu_translate,
};

size_t real_page_size;
__attribute__((constructor)) static void get_real_page_size(void)
{
    real_page_size = sysconf(_SC_PAGESIZE);
}
