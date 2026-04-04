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

// increment the change count
static void mem_changed(struct mem *mem);
static struct mmu_ops mem_mmu_ops;

void mem_init(struct mem *mem)
{
    mem->pgdir = calloc(MEM_PGDIR_SIZE, sizeof(struct pt_entry *));
    mem->pgdir_used = 0;
    mem->mmu.ops = &mem_mmu_ops;
    mem->mmu.changes = 0;
    wrlock_init(&mem->lock);
}

void mem_destroy(struct mem *mem)
{
    if (mem == NULL) {
        return;
    }
    write_wrlock(&mem->lock);
    pt_unmap_always(mem, 0, MEM_PAGES);
    for (int i = 0; i < MEM_PGDIR_SIZE; i++) {
        if (mem->pgdir[i] != NULL)
            free(mem->pgdir[i]);
    }
    free(mem->pgdir);
    write_wrunlock(&mem->lock);
    wrlock_destroy(&mem->lock);
}

#define PGDIR_TOP(page)    ((page) >> 10)
#define PGDIR_BOTTOM(page) ((page) & (MEM_PGDIR_SIZE - 1))

static struct pt_entry *mem_pt_new(struct mem *mem, page_t page)
{
    struct pt_entry *pgdir = mem->pgdir[PGDIR_TOP(page)];
    if (pgdir == NULL) {
        pgdir = mem->pgdir[PGDIR_TOP(page)] = calloc(MEM_PGDIR_SIZE, sizeof(struct pt_entry));
        mem->pgdir_used++;
    }
    return &pgdir[PGDIR_BOTTOM(page)];
}

struct pt_entry *mem_pt(struct mem *mem, page_t page)
{
    struct pt_entry *pgdir = mem->pgdir[PGDIR_TOP(page)];
    trace_emit_mem_pgdir_lookup(page, (uint64_t)pgdir);
    if (pgdir == NULL)
        return NULL;
    struct pt_entry *entry = &pgdir[PGDIR_BOTTOM(page)];
    if (entry->data == NULL)
        return NULL;
    return entry;
}

static void mem_pt_del(struct mem *mem, page_t page)
{
    struct pt_entry *entry = mem_pt(mem, page);
    if (entry != NULL)
        entry->data = NULL;
}

void mem_next_page(struct mem *mem, page_t *page)
{
    (*page)++;
    if (*page >= MEM_PAGES)
        return;
    while (*page < MEM_PAGES && mem->pgdir[PGDIR_TOP(*page)] == NULL)
        *page = (*page - PGDIR_BOTTOM(*page)) + MEM_PGDIR_SIZE;
}

page_t pt_find_hole(struct mem *mem, pages_t size)
{
    page_t hole_end = 0; // this can never be used before initializing but gcc doesn't realize
    bool in_hole = false;
    for (page_t page = 0xf7ffd; page > 0x40000; page--) {
        // I don't know how this works but it does
        if (!in_hole && mem_pt(mem, page) == NULL) {
            in_hole = true;
            hole_end = page + 1;
        }
        if (mem_pt(mem, page) != NULL)
            in_hole = false;
        else if (hole_end - page == size)
            return page;
    }
    return BAD_PAGE;
}

bool pt_is_hole(struct mem *mem, page_t start, pages_t pages)
{
    for (page_t page = start; page < start + pages; page++) {
        if (mem_pt(mem, page) != NULL)
            return false;
    }
    return true;
}

int pt_map(struct mem *mem, page_t start, pages_t pages, void *memory, size_t offset,
           unsigned flags)
{
    if (memory == MAP_FAILED)
        return errno_map();

    // If this fails, the munmap in pt_unmap would probably fail.
    assert((uintptr_t)memory % real_page_size == 0 || memory == vdso_data);

    struct data *data = malloc(sizeof(struct data));
    if (data == NULL) {
        return _ENOMEM;
    }
    size_t guest_bytes = pages * PAGE_SIZE + offset;
    size_t host_bytes = HOST_ROUND_UP(guest_bytes);
    *data = (struct data){
        .data = memory,
        .host_size = host_bytes,

#if LEAK_DEBUG
        .pid = current ? current->pid : 0,
        .dest = start << PAGE_BITS,
#endif
    };

    for (page_t page = start; page < start + pages; page++) {
        if (mem_pt(mem, page) != NULL)
            pt_unmap(mem, page, 1);
        data->refcount++;
        struct pt_entry *pt = mem_pt_new(mem, page);
        pt->data = data;
        pt->offset = ((page - start) << PAGE_BITS) + offset;
        pt->flags = flags;
    }
    return 0;
}

int pt_unmap(struct mem *mem, page_t start, pages_t pages)
{
    for (page_t page = start; page < start + pages; page++)
        if (mem_pt(mem, page) == NULL)
            return -1;
    return pt_unmap_always(mem, start, pages);
}

int pt_unmap_always(struct mem *mem, page_t start, pages_t pages)
{
    // Invalidate all TCTI blocks when memory changes
    // Per-MMU cache invalidation via current task
    struct task *task = current;
    if (task && task->cpu.mmu && task->cpu.mmu->block_cache) {
        a64_cache_invalidate_all(task->cpu.mmu->block_cache);
    }

    for (page_t page = start; page < start + pages; mem_next_page(mem, &page)) {
        struct pt_entry *pt = mem_pt(mem, page);
        if (pt == NULL)
            continue;
        struct data *data = pt->data;
        mem_pt_del(mem, page);
        if (--data->refcount == 0) {
            // vdso wasn't allocated with mmap, it's just in our data segment
            if (data->data != vdso_data) {
                int err = munmap(data->data, data->host_size);
                if (err != 0)
                    die("munmap(%p, %lu) failed: %s", data->data, data->host_size, strerror(errno));
            }
            if (data->fd != NULL) {
                fd_close(data->fd);
            }
            free(data);
        }
    }
    mem_changed(mem);
    return 0;
}

int pt_map_nothing(struct mem *mem, page_t start, pages_t pages, unsigned flags)
{
    if (pages == 0)
        return 0;
    void *memory = mmap(NULL, HOST_ROUND_UP(pages * PAGE_SIZE), PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
    return pt_map(mem, start, pages, memory, 0, flags | P_ANONYMOUS);
}

int pt_set_flags(struct mem *mem, page_t start, pages_t pages, int flags)
{
    for (page_t page = start; page < start + pages; page++)
        if (mem_pt(mem, page) == NULL)
            return _ENOMEM;
    for (page_t page = start; page < start + pages; page++) {
        struct pt_entry *entry = mem_pt(mem, page);
        int old_flags = entry->flags;
        entry->flags = flags;
        // check if protection is increasing
        if ((flags & ~old_flags) & (P_READ | P_WRITE)) {
            void *data = (char *)entry->data->data + entry->offset;
            // force to be page aligned
            data = (void *)((uintptr_t)data & ~(real_page_size - 1));
            int prot = PROT_READ;
            if (flags & P_WRITE)
                prot |= PROT_WRITE;
            if (mprotect(data, real_page_size, prot) < 0)
                return errno_map();
        }
    }
    mem_changed(mem);
    return 0;
}

int pt_copy_on_write(struct mem *src, struct mem *dst, page_t start, page_t pages)
{
    for (page_t page = start; page < start + pages; mem_next_page(src, &page)) {
        struct pt_entry *entry = mem_pt(src, page);
        if (entry == NULL)
            continue;
        if (pt_unmap_always(dst, page, 1) < 0)
            return -1;
        if (!(entry->flags & P_SHARED))
            entry->flags |= P_COW;
        entry->data->refcount++;
        struct pt_entry *dst_entry = mem_pt_new(dst, page);
        dst_entry->data = entry->data;
        dst_entry->offset = entry->offset;
        dst_entry->flags = entry->flags;
    }
    mem_changed(src);
    mem_changed(dst);
    return 0;
}

static void mem_changed(struct mem *mem)
{
    // PR 5: Use atomic increment for thread safety on 64-bit counter
    __atomic_fetch_add(&mem->mmu.changes, 1, __ATOMIC_SEQ_CST);
}

// This version will return NULL instead of making necessary pagetable changes.
// Used by the emulator to avoid deadlocks.
static void *mem_ptr_nofault(struct mem *mem, addr_t addr, int type)
{
    // EMIT MEMORY TRANSLATION ATTEMPT (at entry)
    uint64_t size = (type == MEM_WRITE) ? 1 : 0;
    trace_emit_mem_translate_attempt(addr, size);

    struct pt_entry *entry = mem_pt(mem, PAGE(addr));
    if (entry == NULL) {
        // EMIT MEMORY TRANSLATION RESULT (failure path)
        trace_emit_mem_translate_result(0, 0);
        return NULL;
    }
    if (type == MEM_WRITE && !P_WRITABLE(entry->flags)) {
        // EMIT MEMORY TRANSLATION RESULT (failure path - permission denied)
        trace_emit_mem_translate_result(0, 0);
        return NULL;
    }

    void *host_ptr = entry->data->data + entry->offset + PGOFFSET(addr);

    // EMIT MEMORY TRANSLATION RESULT (success path)
    trace_emit_mem_translate_result((uint64_t)host_ptr, 1);

    return host_ptr;
}

void *mem_ptr(struct mem *mem, addr_t addr, int type)
{
    void *old_ptr = mem_ptr_nofault(mem, addr, type); // just for an assert

    page_t page = PAGE(addr);
    struct pt_entry *entry = mem_pt(mem, page);

    if (entry == NULL) {
        // page does not exist
        // look to see if the next VM region is willing to grow down
        // For stack growth down, check pages ABOVE the fault (page+1, page+2, ...)
        // Only a mapped page above the fault with P_GROWSDOWN justifies mapping the fault page
        // Stack grows downward, so a fault at page X can only be resolved if page X+1
        // (or higher) is mapped and has P_GROWSDOWN set
        page_t p = page + 1;
        while (p < MEM_PAGES && mem_pt(mem, p) == NULL)
            p++;
        if (p >= MEM_PAGES)
            return NULL;
        if (!(mem_pt(mem, p)->flags & P_GROWSDOWN))
            return NULL;

        // Changing memory maps must be done with the write lock. But this is
        // called with the read lock.
        // This locking stuff is copy/pasted for all the code in this function
        // which changes memory maps.
        // TODO: factor the lock/unlock code here into a new function. Do this
        // next time you touch this function.
        read_wrunlock(&mem->lock);
        write_wrlock(&mem->lock);
        pt_map_nothing(mem, page, 1, P_WRITE | P_GROWSDOWN);
        write_wrunlock(&mem->lock);
        read_wrlock(&mem->lock);

        entry = mem_pt(mem, page);
    }

    if (entry != NULL && (type == MEM_WRITE || type == MEM_WRITE_PTRACE)) {
        // if page is unwritable, well tough luck
        if (type != MEM_WRITE_PTRACE && !(entry->flags & P_WRITE))
            return NULL;
        if (type == MEM_WRITE_PTRACE) {
            // TODO: Is P_WRITE really correct? The page shouldn't be writable without ptrace.
            entry->flags |= P_WRITE | P_COW;
        }
        // Invalidate TCTI blocks when memory is modified
        // Per-MMU cache invalidation via current task
        struct task *task = current;
        if (task && task->cpu.mmu && task->cpu.mmu->block_cache) {
            a64_cache_invalidate_all(task->cpu.mmu->block_cache);
        }
        // if page is cow, ~~milk~~ copy it
        if (entry->flags & P_COW) {
            void *guest_ptr = (char *)entry->data->data + entry->offset;
            void *host_base = (void *)HOST_ROUND_DOWN((uintptr_t)guest_ptr);
            size_t host_off = (uintptr_t)guest_ptr - (uintptr_t)host_base;
            void *copy = mmap(NULL, real_page_size, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);

            // copy/paste from above
            read_wrunlock(&mem->lock);
            write_wrlock(&mem->lock);
            memcpy(copy, host_base, real_page_size);
            pt_map(mem, page, 1, copy, host_off, entry->flags & ~P_COW);
            write_wrunlock(&mem->lock);
            read_wrlock(&mem->lock);
        }
    }

    void *ptr = mem_ptr_nofault(mem, addr, type);
    assert(old_ptr == NULL || old_ptr == ptr || type == MEM_WRITE_PTRACE);
    return ptr;
}

static void *mem_mmu_translate(struct mmu *mmu, addr_t addr, int type)
{
    return mem_ptr_nofault(container_of(mmu, struct mem, mmu), addr, type);
}

static struct mmu_ops mem_mmu_ops = {
    .translate = mem_mmu_translate,
};

int mem_segv_reason(struct mem *mem, addr_t addr)
{
    struct pt_entry *pt = mem_pt(mem, PAGE(addr));
    if (pt == NULL)
        return SEGV_MAPERR_;
    return SEGV_ACCERR_;
}

size_t real_page_size;
__attribute__((constructor)) static void get_real_page_size()
{
    real_page_size = sysconf(_SC_PAGESIZE);
}

void mem_coredump(struct mem *mem, const char *file)
{
    int fd = open(file, O_CREAT | O_RDWR | O_TRUNC, 0666);
    if (fd < 0) {
        perror("open");
        return;
    }
    if (ftruncate(fd, 0xffffffff) < 0) {
        perror("ftruncate");
        return;
    }

    int pages = 0;
    for (page_t page = 0; page < MEM_PAGES; page++) {
        struct pt_entry *entry = mem_pt(mem, page);
        if (entry == NULL)
            continue;
        pages++;
        if (lseek(fd, page << PAGE_BITS, SEEK_SET) < 0) {
            perror("lseek");
            return;
        }
        if (write(fd, entry->data->data, PAGE_SIZE) < 0) {
            perror("write");
            return;
        }
    }
    printk("dumped %d pages\n", pages);
    close(fd);
}
