#import <IXLandInstrumentationTracing/trace.h>
#import <IXLandLinuxRuntime/fs/fd.h>
#import <IXLandLinuxRuntime/kernel/calls.h>
#import <IXLandLinuxRuntime/kernel/errno.h>
#import <IXLandLinuxRuntime/kernel/guest_trace_context.h>
#import <IXLandLinuxRuntime/kernel/memory.h>
#import <IXLandLinuxRuntime/kernel/mm.h>
#import <IXLandLinuxRuntime/kernel/task.h>
#import <IXLandLinuxRuntime/util/debug.h>
#include <string.h>

static void trace_mm_copy_failure(const char *reason, int64_t err)
{
    ixland_guest_trace_field_t fields[] = {
        { .key = "reason", .kind = IXLAND_GUEST_TRACE_FIELD_STRING, .string_value = reason },
        { .key = "return_value", .kind = IXLAND_GUEST_TRACE_FIELD_I64_DEC, .i64_value = err },
        { .key = "src_mm",
          .kind = IXLAND_GUEST_TRACE_FIELD_U64_HEX,
          .u64_value = (uint64_t)(current ? current->mm : NULL) },
    };
    ixland_guest_trace_emit_structured(IXLAND_INSTRUMENTATION_ORIGIN_KERNEL, "mem.mm_copy.failure",
                                       fields, sizeof(fields) / sizeof(fields[0]));
}

static int mm_clone_vmas(struct mm *src, struct mm *dst)
{
    for (struct vm_area *vma = src->mem.vmas.head; vma != NULL; vma = vma->next) {
        struct vm_area *clone = vma_alloc();
        if (clone == NULL) {
            trace_mm_copy_failure("vma_alloc_failed", _ENOMEM);
            return _ENOMEM;
        }
        clone->start = vma->start;
        clone->end = vma->end;
        clone->flags = vma->flags;
        clone->obj = vma->obj;
        if (clone->obj != NULL)
            mem_object_retain(clone->obj);
        clone->obj_offset = vma->obj_offset;
        vma_tree_insert(&dst->mem.vmas, clone);
    }
    return 0;
}

struct mm *mm_new(void)
{
    struct mm *mm = malloc(sizeof(struct mm));
    if (mm == NULL)
        return NULL;
    trace_emit_mm_new((uint64_t)mm);
    mem_init(&mm->mem);
    mm->start_brk = mm->brk = 0; // should get overwritten by exec
    mm->exefile = NULL;
    mm->refcount = 1;
    return mm;
}

struct mm *mm_copy(struct mm *mm)
{
    trace_emit_mm_copy((uint64_t)mm, 0);
    struct mm *new_mm = malloc(sizeof(struct mm));
    if (new_mm == NULL) {
        trace_mm_copy_failure("mm_alloc_failed", _ENOMEM);
        return NULL;
    }
    trace_emit_mm_copy((uint64_t)mm, (uint64_t)new_mm);
    *new_mm = *mm;
    // Fix wrlock_init failing because it thinks it's reinitializing the same lock
    memset(&new_mm->mem.lock, 0, sizeof(new_mm->mem.lock));
    new_mm->refcount = 1;
    mem_init(&new_mm->mem);
    if (new_mm->exefile != NULL)
        fd_retain(new_mm->exefile);
    write_wrlock(&mm->mem.lock);
    int err = mm_clone_vmas(mm, new_mm);
    if (err == 0)
        err = pt_copy_on_write(&mm->mem, &new_mm->mem, 0, A64_USER_TOP >> PAGE_BITS);
    write_wrunlock(&mm->mem.lock);
    if (err < 0) {
        trace_mm_copy_failure(err == _ENOMEM ? "clone_failed" : "copy_on_write_failed", err);
        mm_release(new_mm);
        return NULL;
    }
    return new_mm;
}

void mm_retain(struct mm *mm)
{
    mm->refcount++;
    trace_emit_mm_retain((uint64_t)mm, mm->refcount);
}

void mm_release(struct mm *mm)
{
    if (mm == NULL)
        return;
    uint32_t old_refcount = mm->refcount;
    trace_emit_mm_release((uint64_t)mm, old_refcount);
    if (--mm->refcount == 0) {
        trace_emit_mm_release_freed((uint64_t)mm);
        if (mm->exefile != NULL)
            fd_close(mm->exefile);
        mem_destroy(&mm->mem);
        free(mm);
    }
}

static addr_t do_mmap(addr_t addr, uint32_t len, uint32_t prot, uint32_t flags, fd_t fd_no,
                      off_t_ offset)
{
    int err;
    pages_t pages = PAGE_ROUND_UP(len);
    if (!pages)
        return _EINVAL;
    page_t page = BAD_PAGE;
    if (addr != 0) {
        if (PGOFFSET(addr) != 0)
            return _EINVAL;
        page = PAGE(addr);
        if (!(flags & MMAP_FIXED) && !pt_is_hole(current->mem, page, pages)) {
            addr = 0;
        }
    }
    if (addr == 0) {
        page = pt_find_hole(current->mem, pages);
        if (page == BAD_PAGE)
            return _ENOMEM;
    }

    if (flags & MMAP_SHARED)
        prot |= P_SHARED;

    if (flags & MMAP_ANONYMOUS) {
        if ((err = pt_map_nothing(current->mem, page, pages, prot)) < 0)
            return err;
    } else {
        // fd must be valid
        struct fd *fd = f_get(fd_no);
        if (fd == NULL)
            return _EBADF;
        if (fd->ops->mmap == NULL)
            return _ENODEV;
        if ((err = fd->ops->mmap(fd, current->mem, page, pages, (off_t)offset, prot, flags)) < 0)
            return err;
        /* Set file-backed metadata on the first page's descriptor */
        struct page_desc *first_desc = page_map_lookup(&current->mem->pages, page);
        if (first_desc) {
            first_desc->obj->fd = fd_retain(fd);
            first_desc->obj->file_offset = offset;
        }
    }
    return page << PAGE_BITS;
}

static addr_t mmap_common(addr_t addr, uint32_t len, uint32_t prot, uint32_t flags, fd_t fd_no,
                          off_t_ offset)
{
    STRACE("mmap(0x%x, 0x%x, 0x%x, 0x%x, %d, %lld)", addr, len, prot, flags, fd_no,
           (long long)offset);
    if (len == 0)
        return _EINVAL;
    if (prot & ~P_RWX)
        return _EINVAL;
    if ((flags & MMAP_PRIVATE) && (flags & MMAP_SHARED))
        return _EINVAL;

    write_wrlock(&current->mem->lock);
    addr_t res = do_mmap(addr, len, prot, flags, fd_no, offset);
    write_wrunlock(&current->mem->lock);
    if (res == _ENOMEM)
        trace_record_event(TRACE_ORIGIN_KERNEL, "mmap.fail.enomem");
    return res;
}

addr_t sys_mmap_native(addr_t addr, uint32_t len, uint32_t prot, uint32_t flags, fd_t fd_no,
                       off_t_ offset)
{
    return mmap_common(addr, len, prot, flags, fd_no, offset);
}

addr_t sys_mmap2(addr_t addr, uint32_t len, uint32_t prot, uint32_t flags, fd_t fd_no,
                 uint32_t offset)
{
    return mmap_common(addr, len, prot, flags, fd_no, (off_t_)offset << PAGE_BITS);
}

struct mmap_arg_struct {
    uint32_t addr, len, prot, flags, fd, offset;
};

addr_t sys_mmap(addr_t args_addr)
{
    struct mmap_arg_struct args;
    if (user_get(args_addr, args))
        return _EFAULT;
    return mmap_common(args.addr, args.len, args.prot, args.flags, args.fd, args.offset);
}

int64_t sys_munmap(addr_t addr, uint64_t len)
{
    STRACE("munmap(0x%x, 0x%x)", addr, len);
    if (PGOFFSET(addr) != 0)
        return _EINVAL;
    if (len == 0)
        return _EINVAL;
    write_wrlock(&current->mem->lock);
    int err = pt_unmap_always(current->mem, PAGE(addr), PAGE_ROUND_UP(len));
    write_wrunlock(&current->mem->lock);
    if (err < 0)
        return _EINVAL;
    return 0;
}

#define MREMAP_MAYMOVE_ 1
#define MREMAP_FIXED_   2

int64_t sys_mremap(addr_t addr, uint32_t old_len, uint32_t new_len, uint32_t flags)
{
    STRACE("mremap(%#x, %#x, %#x, %d)", addr, old_len, new_len, flags);
    if (PGOFFSET(addr) != 0)
        return _EINVAL;
    if (old_len == 0 || new_len == 0)
        return _EINVAL;
    if (flags & ~(MREMAP_MAYMOVE_ | MREMAP_FIXED_))
        return _EINVAL;
    if (flags & MREMAP_FIXED_) {
        FIXME("missing MREMAP_FIXED");
        return _EINVAL;
    }
    pages_t old_pages = PAGE_ROUND_UP(old_len);
    pages_t new_pages = PAGE_ROUND_UP(new_len);

    write_wrlock(&current->mem->lock);

    // shrinking always works
    if (new_pages <= old_pages) {
        int err = pt_unmap(current->mem, PAGE(addr) + new_pages, old_pages - new_pages);
        if (err < 0) {
            write_wrunlock(&current->mem->lock);
            return _EFAULT;
        }
        write_wrunlock(&current->mem->lock);
        return addr;
    }

    struct vm_area *vma = vma_tree_find(&current->mem->vmas, addr);
    if (!vma || vma->start != addr) {
        write_wrunlock(&current->mem->lock);
        return _EFAULT;
    }
    unsigned pt_flags = vma->flags;
    for (page_t page = PAGE(addr); page < PAGE(addr) + old_pages; page++) {
        struct page_desc *desc = page_map_lookup(&current->mem->pages, page);
        if (!desc || desc->obj != vma->obj || desc->flags != pt_flags) {
            write_wrunlock(&current->mem->lock);
            return _EFAULT;
        }
    }
    if (!(pt_flags & P_ANONYMOUS)) {
        FIXME("mremap grow on file mappings");
        write_wrunlock(&current->mem->lock);
        return _EFAULT;
    }
    page_t extra_start = PAGE(addr) + old_pages;
    pages_t extra_pages = new_pages - old_pages;
    if (pt_is_hole(current->mem, extra_start, extra_pages)) {
        int err = pt_map_nothing(current->mem, extra_start, extra_pages, pt_flags);
        write_wrunlock(&current->mem->lock);
        if (err < 0)
            return err;
        return addr;
    }

    if (!(flags & MREMAP_MAYMOVE_)) {
        write_wrunlock(&current->mem->lock);
        return _ENOMEM;
    }

    page_t new_page = pt_find_hole(current->mem, new_pages);
    if (new_page == BAD_PAGE) {
        write_wrunlock(&current->mem->lock);
        return _ENOMEM;
    }

    int err = pt_map_nothing(current->mem, new_page, new_pages, pt_flags);
    if (err < 0) {
        write_wrunlock(&current->mem->lock);
        return err;
    }

    for (pages_t page = 0; page < old_pages; page++) {
        struct page_desc *old_desc = page_map_lookup(&current->mem->pages, PAGE(addr) + page);
        struct page_desc *new_desc = page_map_lookup(&current->mem->pages, new_page + page);
        if (!old_desc || !new_desc) {
            pt_unmap_always(current->mem, new_page, new_pages);
            write_wrunlock(&current->mem->lock);
            return _EFAULT;
        }

        memcpy((char *)new_desc->obj->host_base + new_desc->offset,
               (char *)old_desc->obj->host_base + old_desc->offset, PAGE_SIZE);
    }

    pt_unmap_always(current->mem, PAGE(addr), old_pages);
    write_wrunlock(&current->mem->lock);
    return new_page << PAGE_BITS;
}

int64_t sys_mprotect(addr_t addr, uint64_t len, int64_t prot)
{
    STRACE("mprotect(0x%x, 0x%x, 0x%x)", addr, len, prot);
    if (PGOFFSET(addr) != 0)
        return _EINVAL;
    if (prot & ~P_RWX)
        return _EINVAL;
    pages_t pages = PAGE_ROUND_UP(len);
    write_wrlock(&current->mem->lock);
    int err = (int)pt_set_flags(current->mem, PAGE(addr), pages, (int)prot);
    write_wrunlock(&current->mem->lock);
    return err;
}

uint32_t sys_madvise(addr_t UNUSED(addr), uint32_t UNUSED(len), uint32_t UNUSED(advice))
{
    // portable applications should not rely on linux's destructive semantics for MADV_DONTNEED.
    return 0;
}

uint32_t sys_mbind(addr_t UNUSED(addr), uint32_t UNUSED(len), int64_t UNUSED(mode),
                   addr_t UNUSED(nodemask), uint32_t UNUSED(maxnode), uint64_t UNUSED(flags))
{
    return 0;
}

int64_t sys_mlock(addr_t UNUSED(addr), uint32_t UNUSED(len))
{
    return 0;
}

int64_t sys_msync(addr_t UNUSED(addr), uint32_t UNUSED(len), int64_t UNUSED(flags))
{
    return 0;
}

addr_t sys_brk(addr_t new_brk)
{
    STRACE("brk(0x%x)", new_brk);
    trace_record_event(TRACE_ORIGIN_KERNEL, "brk.call");
    struct mm *mm = current->mm;
    addr_t old_brk = mm->brk;

    write_wrlock(&mm->mem.lock);
    if (new_brk < mm->start_brk)
        goto out;

    if (new_brk > old_brk) {
        // expand heap: map region from old_brk to new_brk
        // round up because of the definition of brk: "the first location after the end of the
        // uninitialized data segment." (brk(2)) if the brk is 0x2000, page 0x2000 shouldn't be
        // mapped, but it should be if the brk is 0x2001.
        page_t start = PAGE_ROUND_UP(old_brk);
        pages_t size = PAGE_ROUND_UP(new_brk) - PAGE_ROUND_UP(old_brk);
        if (!pt_is_hole(&mm->mem, start, size)) {
            trace_record_event(TRACE_ORIGIN_KERNEL, "brk.fail.no_hole");
            goto out;
        }
        int err = pt_map_nothing(&mm->mem, start, size, P_READ | P_WRITE);
        if (err < 0) {
            trace_record_event(TRACE_ORIGIN_KERNEL, "brk.fail.map");
            goto out;
        }
    } else if (new_brk < old_brk) {
        // shrink heap: unmap region from new_brk to old_brk
        // first page to unmap is the page after the last byte below the new brk
        // last page to unmap is PAGE(old_brk)
        page_t start = PAGE_ROUND_UP(new_brk);
        page_t end = PAGE_ROUND_UP(old_brk);
        if (end > start)
            pt_unmap_always(&mm->mem, start, end - start);
    }

    mm->brk = new_brk;
    if (new_brk > old_brk)
        trace_record_event(TRACE_ORIGIN_KERNEL, "brk.grow.ok");
out:;
    addr_t brk = mm->brk;
    {
        char ev[160];
        snprintf(ev, sizeof(ev), "brk.state:start=0x%llx,old=0x%llx,new=0x%llx,res=0x%llx",
                 (unsigned long long)mm->start_brk, (unsigned long long)old_brk,
                 (unsigned long long)new_brk, (unsigned long long)brk);
        trace_record_event(TRACE_ORIGIN_KERNEL, ev);
    }
    write_wrunlock(&mm->mem.lock);
    return brk;
}
