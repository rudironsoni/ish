#import <IXLandLinuxRuntime/kernel/task.h>

#import <IXLandLinuxRuntime/kernel/memory.h>
#import <IXLandLinuxRuntime/kernel/signal.h>
#define _GNU_SOURCE
#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>
#import <IXLandLinuxRuntime/emu/aarch64/tls.h>
#import <IXLandLinuxRuntime/fs/fd.h>
#import <IXLandLinuxRuntime/kernel/calls.h>
#import <IXLandLinuxRuntime/kernel/elf.h>
#import <IXLandLinuxRuntime/kernel/errno.h>
#import <IXLandLinuxRuntime/kernel/random.h>
#import <IXLandLinuxRuntime/kernel/vdso.h>
#import <IXLandLinuxRuntime/trace/trace.h>

#import <IXLandLinuxRuntime/util/misc.h>

#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define ARGV_MAX 32 * PAGE_SIZE

struct exec_args {
    // number of arguments
    size_t count;
    // series of count null-terminated strings, plus an extra null for good measure
    const char *args;
};

static void trace_exec_checkpoint(const char *name, int err)
{
    char pid_buf[32];
    char task_buf[32];
    char mm_buf[32];
    char mem_buf[32];
    char cpu_mmu_buf[32];
    char expected_mem_mmu_buf[32];
    char err_buf[32];

    snprintf(pid_buf, sizeof(pid_buf), "%u", (unsigned)(current ? current->pid : 0));
    snprintf(task_buf, sizeof(task_buf), "%p", (void *)current);
    snprintf(mm_buf, sizeof(mm_buf), "%p", current ? (void *)current->mm : NULL);
    snprintf(mem_buf, sizeof(mem_buf), "%p", current ? (void *)current->mem : NULL);
    snprintf(cpu_mmu_buf, sizeof(cpu_mmu_buf), "%p", current ? (void *)current->cpu.mmu : NULL);
    snprintf(expected_mem_mmu_buf, sizeof(expected_mem_mmu_buf), "%p",
             current && current->mem ? (void *)&current->mem->mmu : NULL);
    snprintf(err_buf, sizeof(err_buf), "%d", err);

    trace_attribute_t attrs[] = {
        { "pid", pid_buf },         { "task", task_buf },
        { "mm", mm_buf },           { "mem", mem_buf },
        { "cpu.mmu", cpu_mmu_buf }, { "expected.mem.mmu", expected_mem_mmu_buf },
        { "err", err_buf },
    };

    (void)trace_begin_interval(TRACE_ORIGIN_EXEC, name, attrs, sizeof(attrs) / sizeof(attrs[0]));
}

static void trace_exec_layout_checkpoint(const char *name, struct task *task, int err)
{
    char task_buf[32];
    char cpu_field_buf[32];
    char pid_field_buf[32];
    char mm_field_buf[32];
    char mem_field_buf[32];
    char pid_value_buf[32];
    char mm_value_buf[32];
    char mem_value_buf[32];
    char cpu_mmu_value_buf[32];
    char expected_mem_mmu_buf[32];
    char sizeof_task_buf[32];
    char sizeof_cpu_buf[32];
    char off_cpu_buf[32];
    char off_pid_buf[32];
    char off_mm_buf[32];
    char off_mem_buf[32];
    char err_buf[32];

    snprintf(task_buf, sizeof(task_buf), "%p", (void *)task);
    snprintf(cpu_field_buf, sizeof(cpu_field_buf), "%p", task ? (void *)&task->cpu : NULL);
    snprintf(pid_field_buf, sizeof(pid_field_buf), "%p", task ? (void *)&task->pid : NULL);
    snprintf(mm_field_buf, sizeof(mm_field_buf), "%p", task ? (void *)&task->mm : NULL);
    snprintf(mem_field_buf, sizeof(mem_field_buf), "%p", task ? (void *)&task->mem : NULL);
    snprintf(pid_value_buf, sizeof(pid_value_buf), "%u", (unsigned)(task ? task->pid : 0));
    snprintf(mm_value_buf, sizeof(mm_value_buf), "%p", task ? (void *)task->mm : NULL);
    snprintf(mem_value_buf, sizeof(mem_value_buf), "%p", task ? (void *)task->mem : NULL);
    snprintf(cpu_mmu_value_buf, sizeof(cpu_mmu_value_buf), "%p",
             task ? (void *)task->cpu.mmu : NULL);
    snprintf(expected_mem_mmu_buf, sizeof(expected_mem_mmu_buf), "%p",
             task && task->mem ? (void *)&task->mem->mmu : NULL);
    snprintf(sizeof_task_buf, sizeof(sizeof_task_buf), "%zu", sizeof(struct task));
    snprintf(sizeof_cpu_buf, sizeof(sizeof_cpu_buf), "%zu", sizeof(struct cpu_state));
    snprintf(off_cpu_buf, sizeof(off_cpu_buf), "%zu", __builtin_offsetof(struct task, cpu));
    snprintf(off_pid_buf, sizeof(off_pid_buf), "%zu", __builtin_offsetof(struct task, pid));
    snprintf(off_mm_buf, sizeof(off_mm_buf), "%zu", __builtin_offsetof(struct task, mm));
    snprintf(off_mem_buf, sizeof(off_mem_buf), "%zu", __builtin_offsetof(struct task, mem));
    snprintf(err_buf, sizeof(err_buf), "%d", err);

    trace_attribute_t attrs[] = {
        { "task", task_buf },
        { "addr.cpu", cpu_field_buf },
        { "addr.pid", pid_field_buf },
        { "addr.mm", mm_field_buf },
        { "addr.mem", mem_field_buf },
        { "pid", pid_value_buf },
        { "mm", mm_value_buf },
        { "mem", mem_value_buf },
        { "cpu.mmu", cpu_mmu_value_buf },
        { "expected.mem.mmu", expected_mem_mmu_buf },
        { "sizeof.task", sizeof_task_buf },
        { "sizeof.cpu", sizeof_cpu_buf },
        { "offsetof.cpu", off_cpu_buf },
        { "offsetof.pid", off_pid_buf },
        { "offsetof.mm", off_mm_buf },
        { "offsetof.mem", off_mem_buf },
        { "err", err_buf },
    };

    (void)trace_begin_interval(TRACE_ORIGIN_EXEC, name, attrs, sizeof(attrs) / sizeof(attrs[0]));
}

static inline addr_t align_stack(addr_t sp);
static inline ssize_t user_strlen(addr_t p);
static inline int user_memset(addr_t start, byte_t val, addr_t len);
static inline addr_t copy_string(addr_t sp, const char *string);
static inline addr_t args_copy(addr_t sp, struct exec_args args);
static size_t args_size(struct exec_args args);

static void trace_elf_header_checkpoint(const char *name, struct elf_header *header)
{
    char class_buf[8];
    char machine_buf[8];
    char entry_buf[24];
    char type_buf[8];

    snprintf(class_buf, sizeof(class_buf), "%u", header->bitness);
    snprintf(machine_buf, sizeof(machine_buf), "%u", header->machine);
    snprintf(entry_buf, sizeof(entry_buf), "0x%llx", (unsigned long long)header->entry_point);
    snprintf(type_buf, sizeof(type_buf), "%u", header->type);

    trace_attribute_t attrs[] = {
        { "elf_class", class_buf },
        { "elf_machine", machine_buf },
        { "elf_entry", entry_buf },
        { "elf_type", type_buf },
    };

    (void)trace_begin_interval(TRACE_ORIGIN_TASK, name, attrs, sizeof(attrs) / sizeof(attrs[0]));
}

static int read_header(struct fd *fd, struct elf_header *header)
{
    // Reset file position to beginning
    fd->ops->lseek(fd, 0, LSEEK_SET);

    // Read raw bytes first to debug
    unsigned char raw_header[64];
    ssize_t err = fd->ops->read(fd, raw_header, 64);
    if (err < 0) {
        return (int)err;
    }
    if (err != 64) {
        return _ENOEXEC;
    }

    // Copy to header struct
    memcpy(header, raw_header, sizeof(*header));

    // Validate ELF header with detailed error codes
    if (memcmp(&header->magic, ELF_MAGIC, sizeof(header->magic)) != 0) {
        return _ENOEXEC; // Error -8: Magic bytes wrong
    }
    if (header->type != ELF_EXECUTABLE && header->type != ELF_DYNAMIC) {
        return _ENOEXEC; // Error -8: Type not executable/dynamic
    }
    if (header->endian != ELF_LITTLEENDIAN) {
        return _ENOEXEC; // Error -8: Wrong endian
    }
    if (header->elfversion1 != 1) {
        return _ENOEXEC; // Error -8: Wrong ELF version
    }

    // Architecture-specific validation (aarch64 only)
    if (header->bitness != ELF_64BIT) {
        return _ENOEXEC; // Error -8: Not 64-bit
    }
    if (header->machine != ELF_AARCH64) {
        return _ENOEXEC; // Error -8: Not aarch64
    }

    // Emit ELF header facts via approved instrumentation
    trace_elf_header_checkpoint("task.proof.elf_header.validated", header);

    return 0;
}

static int read_prg_headers(struct fd *fd, struct elf_header header, struct prg_header **ph_out)
{
    ssize_t ph_size = sizeof(struct prg_header) * header.phent_count;
    trace_emit_u32(TRACE_EVENT_BLOCK_COMPILE_START, 0x1000, (uint32_t)ph_size);
    struct prg_header *ph = malloc(ph_size);
    if (ph == NULL) {
        trace_emit_u32(TRACE_EVENT_BLOCK_COMPILE_START, 0x1001, 0);
        return _ENOMEM;
    }

    trace_emit_u32(TRACE_EVENT_BLOCK_COMPILE_START, 0x1002, header.prghead_off);
    if (fd->ops->lseek(fd, header.prghead_off, LSEEK_SET) < 0) {
        trace_emit_u32(TRACE_EVENT_BLOCK_COMPILE_START, 0x1003, 1);
        free(ph);
        return _EIO;
    }
    trace_emit_u32(TRACE_EVENT_BLOCK_COMPILE_START, 0x1004, ph_size);
    ssize_t read_ret = fd->ops->read(fd, ph, ph_size);
    trace_emit_u32(TRACE_EVENT_BLOCK_COMPILE_START, 0x1005, (uint32_t)read_ret);
    if (read_ret != ph_size) {
        trace_emit_u32(TRACE_EVENT_BLOCK_COMPILE_START, 0x1006, errno);
        free(ph);
        if (errno != 0)
            return _EIO;
        return _ENOEXEC;
    }

    trace_emit_u32(TRACE_EVENT_BLOCK_COMPILE_START, 0x1007, 0);
    *ph_out = ph;
    return 0;
}

static int load_entry(struct prg_header ph, addr_t bias, struct fd *fd)
{
    int err;

    // Diagnostic: Prove load_entry reachability for APPSIM-004
    if (trace_get_level() >= TRACE_LEVEL_SUMMARY) {
        char ph_type_buf[32];
        char ph_vaddr_buf[32];
        char ph_offset_buf[32];
        char bias_buf[32];
        char fd_buf[32];

        snprintf(ph_type_buf, sizeof(ph_type_buf), "%lu", (unsigned long)ph.type);
        snprintf(ph_vaddr_buf, sizeof(ph_vaddr_buf), "0x%lx", (unsigned long)ph.vaddr);
        snprintf(ph_offset_buf, sizeof(ph_offset_buf), "0x%lx", (unsigned long)ph.offset);
        snprintf(bias_buf, sizeof(bias_buf), "0x%lx", (unsigned long)bias);
        snprintf(fd_buf, sizeof(fd_buf), "%p", (void *)fd);

        trace_attribute_t entry_attrs[] = {
            { "ph_type", ph_type_buf },
            { "ph_vaddr", ph_vaddr_buf },
            { "ph_offset", ph_offset_buf },
            { "bias", bias_buf },
            { "fd", fd_buf },
        };
        trace_begin_interval(TRACE_ORIGIN_KERNEL, "task.proof.exec.load_entry.reached", entry_attrs,
                             sizeof(entry_attrs) / sizeof(entry_attrs[0]));
    }

    addr_t addr = ph.vaddr + bias;
    addr_t offset = ph.offset;
    addr_t memsize = ph.memsize;
    addr_t filesize = ph.filesize;

    int flags = P_READ;
    if (ph.flags & PH_W)
        flags |= P_WRITE;

    pages_t map_pages = PAGE_ROUND_UP(filesize + PGOFFSET(addr));
    page_t start_page = PAGE(addr);
    off_t map_offset = offset - PGOFFSET(addr);

    if (fd->ops->mmap == NULL) {
        return _EINVAL;
    }

    if ((err = fd->ops->mmap(fd, current->mem, start_page, map_pages, map_offset, flags,
                             MMAP_PRIVATE)) < 0) {
        return err;
    }

    // TODO find a better place for these to avoid code duplication
    struct pt_entry *first_pt = mem_pt(current->mem, start_page);
    if (first_pt == NULL || first_pt->data == NULL) {
        return _ENOMEM;
    }
    first_pt->data->fd = fd_retain(fd);
    first_pt->data->file_offset = map_offset;

    if (memsize > filesize) {
        // put zeroes between addr + filesize and addr + memsize, call that bss
        dword_t bss_size = memsize - filesize;

        // first zero the tail from the end of the file mapping to the end
        // of the load entry or the end of the page, whichever comes first
        addr_t file_end = addr + filesize;
        dword_t tail_size = PAGE_SIZE - PGOFFSET(file_end);
        if (tail_size == PAGE_SIZE)
            // if you can calculate tail_size better and not have to do this please let me know
            tail_size = 0;

        if (tail_size != 0) {
            // Unlock and lock the mem because the user functions must be
            // called without locking mem.
            write_wrunlock(&current->mem->lock);
            user_memset(file_end, 0, tail_size);
            write_wrlock(&current->mem->lock);
        }
        if (tail_size > bss_size)
            tail_size = bss_size;

        // then map the pages from after the file mapping up to and including the end of bss
        if (bss_size - tail_size != 0)
            if ((err = pt_map_nothing(current->mem, PAGE_ROUND_UP(addr + filesize),
                                      PAGE_ROUND_UP(bss_size - tail_size), flags)) < 0)
                return err;
    }
    return 0;
}

static addr_t find_hole_for_elf(struct elf_header *header, struct prg_header *ph)
{
    struct prg_header *first = NULL, *last = NULL;
    for (int i = 0; i < header->phent_count; i++) {
        if (ph[i].type == PT_LOAD) {
            if (first == NULL)
                first = &ph[i];
            last = &ph[i];
        }
    }
    pages_t size = 0;
    if (first != NULL) {
        pages_t a = PAGE_ROUND_UP(last->vaddr + last->memsize);
        pages_t b = PAGE(first->vaddr);
        size = a - b;
    }
    return pt_find_hole(current->mem, size) << PAGE_BITS;
}

static int elf_exec(struct fd *fd, const char *file, struct exec_args argv, struct exec_args envp)
{
    int err = 0;
    trace_exec_checkpoint("task.proof.do_execve.before_elf_exec", err);

    // Trace: Entry to elf_exec (POINT 4 - elf_exec_entry)
    trace_emit_exec_path_boundary((uint64_t)current, current->pid, (uint64_t)current->mm,
                                  (uint64_t)current->mem, EXEC_PATH_ELF_EXEC_ENTRY, 0);

    // read the headers
    struct elf_header header;
    if ((err = read_header(fd, &header)) < 0) {
        return err;
    }

    struct prg_header *ph;
    if ((err = read_prg_headers(fd, header, &ph)) < 0) {
        return err;
    }

    // look for an interpreter
    char *interp_name = NULL;
    struct fd *interp_fd = NULL;
    struct elf_header interp_header;
    struct prg_header *interp_ph = NULL;
    for (unsigned i = 0; i < header.phent_count; i++) {
        if (ph[i].type != PT_INTERP)
            continue;
        if (interp_name) {
            // can't have two interpreters
            err = _EINVAL;
            goto out_free_interp;
        }

        interp_name = malloc(ph[i].filesize);
        if (interp_name == NULL) {
            err = _ENOMEM;
            goto out_free_ph;
        }

        // read the interpreter name out of the file
        err = _EIO;
        if (fd->ops->lseek(fd, ph[i].offset, LSEEK_SET) < 0)
            goto out_free_interp;
        if ((elf_off_t)fd->ops->read(fd, interp_name, ph[i].filesize) != ph[i].filesize)
            goto out_free_interp;

        // open interpreter and read headers
        interp_fd = generic_open(interp_name, O_RDONLY, 0);
        if (IS_ERR(interp_fd)) {
            err = PTR_ERR(interp_fd);
            goto out_free_interp;
        }
        if ((err = read_header(interp_fd, &interp_header)) < 0) {
            if (err == _ENOEXEC)
                err = _ELIBBAD;
            goto out_free_interp;
        }
        if ((err = read_prg_headers(interp_fd, interp_header, &interp_ph)) < 0) {
            if (err == _ENOEXEC)
                err = _ELIBBAD;
            goto out_free_interp;
        }
    }
    // free the process's memory.
    // from this point on, if any error occurs the process will have to be
    // killed before it even starts. please don't be too sad about it, it's
    // just a process.
    //
    // general_lock protects current->mm. otherwise procfs might read the
    // pointer before it's released and then try to lock it after it's
    // released.

    lock(&current->general_lock);
    if (current->mm == NULL) {
        unlock(&current->general_lock);
        err = _EINVAL;
        goto out_free_interp;
    }
    // FIX-001 (REVISED): Create new mm BEFORE releasing old mm
    // This eliminates the window where current->mm/mem are NULL.
    // If mm_new() fails, current->mm is still valid (pointing to old_mm).
    // Only after mm_new() succeeds do we release old mm and update current.
    struct mm *old_mm = current->mm;
    struct mm *new_mm = mm_new();
    if (new_mm == NULL) {
        // current->mm is still valid (old_mm), no need to restore
        unlock(&current->general_lock);
        err = _ENOMEM;
        goto out_free_interp;
    }
    trace_exec_checkpoint("task.proof.do_execve.before_mm_release", err);
    // Trace: Before mm_release (old_mm valid, new_mm ready)
    trace_emit_exec_mm_boundary((uint64_t)current, current->pid, (uint64_t)current->mm,
                                (uint64_t)current->mem, (uint64_t)old_mm, (uint64_t)new_mm,
                                EXEC_MM_OP_BEFORE_MM_RELEASE, 0);
    // Now safe to release old mm and update current
    mm_release(old_mm);
    trace_emit(TRACE_EVENT_MM_RELEASE, 0);
    trace_exec_checkpoint("task.proof.do_execve.after_mm_release", err);
    // Trace: After mm_release (old_mm released, new_mm not yet set)
    trace_emit_exec_mm_boundary((uint64_t)current, current->pid, (uint64_t)current->mm,
                                (uint64_t)current->mem, (uint64_t)old_mm, (uint64_t)new_mm,
                                EXEC_MM_OP_AFTER_MM_RELEASE, 0);
    // Trace: Before task_set_mm (critical transition point)
    trace_exec_checkpoint("task.proof.do_execve.before_task_set_mm", err);
    trace_emit_exec_mm_boundary((uint64_t)current, current->pid, (uint64_t)current->mm,
                                (uint64_t)current->mem, (uint64_t)old_mm, (uint64_t)new_mm,
                                EXEC_MM_OP_BEFORE_TASK_SET_MM, 0);
    task_set_mm(current, new_mm);
    trace_emit(TRACE_EVENT_TASK_SET_MM, 0);
    trace_exec_checkpoint("task.proof.do_execve.after_task_set_mm", err);
    trace_exec_checkpoint("task.proof.elf_exec.after_task_set_mm", err);
    // Trace: After task_set_mm (new_mm should be set)
    trace_emit_exec_mm_boundary((uint64_t)current, current->pid, (uint64_t)current->mm,
                                (uint64_t)current->mem, (uint64_t)old_mm, (uint64_t)new_mm,
                                EXEC_MM_OP_AFTER_TASK_SET_MM, 0);
    // DEFENSIVE: Verify current->mem was properly set by task_set_mm
    // This catches any header/include issues where task_set_mm might not work correctly
    if (current->mem == NULL) {
        // Force set current->mem to the correct value
        current->mem = &current->mm->mem;
    }
    // Additional safety: directly verify mem points to mm->mem
    if (current->mem != &current->mm->mem) {
        current->mem = &current->mm->mem;
    }
    unlock(&current->general_lock);
    write_wrlock(&current->mem->lock);

    current->mm->exefile = fd_retain(fd);
    trace_exec_checkpoint("task.proof.elf_exec.after_exefile_set", err);

    addr_t load_addr = 0; // used for AX_PHDR
    bool load_addr_set = false;
    addr_t bias = 0; // offset for loading shared libraries as executables

    // map dat shit!

    // Diagnostic: Prove header.phent_count BEFORE loop
    trace_exec_checkpoint("task.proof.exec.ph_count.before_loop", header.phent_count);

    for (unsigned i = 0; i < header.phent_count; i++) {
        // Diagnostic: Prove loop entry and ph[i].type BEFORE PT_LOAD filter
        char idx_buf[16];
        char type_buf[32];
        char vaddr_buf[32];
        char offset_buf[32];
        char filesize_buf[32];
        char memsize_buf[32];

        snprintf(idx_buf, sizeof(idx_buf), "%u", i);
        snprintf(type_buf, sizeof(type_buf), "%lu", (unsigned long)ph[i].type);
        snprintf(vaddr_buf, sizeof(vaddr_buf), "0x%lx", (unsigned long)ph[i].vaddr);
        snprintf(offset_buf, sizeof(offset_buf), "0x%lx", (unsigned long)ph[i].offset);
        snprintf(filesize_buf, sizeof(filesize_buf), "%lu", (unsigned long)ph[i].filesize);
        snprintf(memsize_buf, sizeof(memsize_buf), "%lu", (unsigned long)ph[i].memsize);

        trace_attribute_t loop_attrs[] = {
            { "index", idx_buf },
            { "ph_type", type_buf },
            { "ph_vaddr", vaddr_buf },
            { "ph_offset", offset_buf },
            { "ph_filesize", filesize_buf },
            { "ph_memsize", memsize_buf },
        };
        trace_begin_interval(TRACE_ORIGIN_KERNEL, "task.proof.exec.ph_loop.entry", loop_attrs,
                             sizeof(loop_attrs) / sizeof(loop_attrs[0]));

        // Prove we reach the PT_LOAD filter
        trace_exec_checkpoint("task.proof.exec.before_pt_load_filter", ph[i].type);

        if (ph[i].type != PT_LOAD)
            continue;

        if (!load_addr_set && header.type == ELF_DYNAMIC) {
            // see giant comment in linux/fs/binfmt_elf.c, around line 950
            if (interp_name)
                bias = 0x56555000; // I have no idea how this number was arrived at
            else
                bias = find_hole_for_elf(&header, ph);
        }

        // Prove PT_LOAD block is entered
        trace_exec_checkpoint("task.proof.exec.pt_load.block_entered", ph[i].type);

        if ((err = load_entry(ph[i], bias, fd)) < 0) {
            goto beyond_hope;
        }

        // Prove load_entry() was reached for PT_LOAD segments
        trace_exec_checkpoint("task.proof.exec.load_entry.reached", err);

        // Trace PT_LOAD mapping for APPSIM-004 diagnosis (main executable)
        char role_buf[32] = "main";
        char path_buf[256];
        char data_buf[32];
        char fd_buf[32];
        char map_start_buf[32];
        char map_end_buf[32];
        char file_offset_start_buf[32];
        char ph_vaddr_buf[32];
        char ph_offset_buf[32];
        char ph_filesize_buf[32];
        char ph_memsize_buf[32];
        char flags_buf[32];

        strncpy(path_buf, file, sizeof(path_buf) - 1);
        path_buf[sizeof(path_buf) - 1] = '\0';
        snprintf(data_buf, sizeof(data_buf), "%p",
                 (void *)mem_pt(current->mem, PAGE(bias + ph[i].vaddr)));
        snprintf(fd_buf, sizeof(fd_buf), "%p", (void *)fd);
        snprintf(map_start_buf, sizeof(map_start_buf), "0x%lx",
                 (unsigned long)(bias + ph[i].vaddr));
        snprintf(map_end_buf, sizeof(map_end_buf), "0x%lx",
                 (unsigned long)(bias + ph[i].vaddr + ph[i].memsize));
        snprintf(file_offset_start_buf, sizeof(file_offset_start_buf), "0x%lx",
                 (unsigned long)(ph[i].offset - PGOFFSET(ph[i].vaddr)));
        snprintf(ph_vaddr_buf, sizeof(ph_vaddr_buf), "0x%lx", (unsigned long)ph[i].vaddr);
        snprintf(ph_offset_buf, sizeof(ph_offset_buf), "0x%lx", (unsigned long)ph[i].offset);
        snprintf(ph_filesize_buf, sizeof(ph_filesize_buf), "%lu", (unsigned long)ph[i].filesize);
        snprintf(ph_memsize_buf, sizeof(ph_memsize_buf), "%lu", (unsigned long)ph[i].memsize);
        snprintf(flags_buf, sizeof(flags_buf), "0x%x", ph[i].flags);

        trace_attribute_t load_attrs[] = {
            { "role", role_buf },
            { "path", path_buf },
            { "data", data_buf },
            { "fd", fd_buf },
            { "map_start", map_start_buf },
            { "map_end", map_end_buf },
            { "file_offset_start", file_offset_start_buf },
            { "ph_vaddr", ph_vaddr_buf },
            { "ph_offset", ph_offset_buf },
            { "ph_filesize", ph_filesize_buf },
            { "ph_memsize", ph_memsize_buf },
            { "flags", flags_buf },
        };
        trace_begin_interval(TRACE_ORIGIN_KERNEL, "task.proof.exec.load.segment", load_attrs,
                             sizeof(load_attrs) / sizeof(load_attrs[0]));

        // load_addr is used to get a value for AX_PHDR et al
        if (!load_addr_set) {
            load_addr = bias + ph[i].vaddr - ph[i].offset;
            load_addr_set = true;
        }

        // we have to know where the brk starts
        addr_t brk = bias + ph[i].vaddr + ph[i].memsize;
        if (brk > current->mm->start_brk)
            current->mm->start_brk = current->mm->brk = BYTES_ROUND_UP(brk);
    }

    addr_t entry = bias + header.entry_point;
    addr_t interp_base = 0;
    addr_t dynamic_addr = 0; // _DYNAMIC section address for x1

    // Find PT_DYNAMIC in main executable (used if no interpreter)
    for (unsigned i = 0; i < header.phent_count; i++) {
        if (ph[i].type == PT_DYNAMIC) {
            dynamic_addr = bias + ph[i].vaddr;
            break;
        }
    }

    if (interp_name) {
        // map dat shit! interpreter edition
        interp_base = find_hole_for_elf(&interp_header, interp_ph);
        for (int i = interp_header.phent_count - 1; i >= 0; i--) {
            if (interp_ph[i].type != PT_LOAD)
                continue;
            if ((err = load_entry(interp_ph[i], interp_base, interp_fd)) < 0)
                goto beyond_hope;

            // Trace PT_LOAD mapping for APPSIM-004 diagnosis (interpreter)
            char role_buf[32] = "interpreter";
            char path_buf[256];
            char data_buf[32];
            char fd_buf[32];
            char map_start_buf[32];
            char map_end_buf[32];
            char file_offset_start_buf[32];
            char ph_vaddr_buf[32];
            char ph_offset_buf[32];
            char ph_filesize_buf[32];
            char ph_memsize_buf[32];
            char flags_buf[32];

            strncpy(path_buf, interp_name, sizeof(path_buf) - 1);
            path_buf[sizeof(path_buf) - 1] = '\0';
            snprintf(data_buf, sizeof(data_buf), "%p",
                     (void *)mem_pt(current->mem, PAGE(interp_base + interp_ph[i].vaddr)));
            snprintf(fd_buf, sizeof(fd_buf), "%p", (void *)interp_fd);
            snprintf(map_start_buf, sizeof(map_start_buf), "0x%lx",
                     (unsigned long)(interp_base + interp_ph[i].vaddr));
            snprintf(map_end_buf, sizeof(map_end_buf), "0x%lx",
                     (unsigned long)(interp_base + interp_ph[i].vaddr + interp_ph[i].memsize));
            snprintf(file_offset_start_buf, sizeof(file_offset_start_buf), "0x%lx",
                     (unsigned long)(interp_ph[i].offset - PGOFFSET(interp_ph[i].vaddr)));
            snprintf(ph_vaddr_buf, sizeof(ph_vaddr_buf), "0x%lx",
                     (unsigned long)interp_ph[i].vaddr);
            snprintf(ph_offset_buf, sizeof(ph_offset_buf), "0x%lx",
                     (unsigned long)interp_ph[i].offset);
            snprintf(ph_filesize_buf, sizeof(ph_filesize_buf), "%lu",
                     (unsigned long)interp_ph[i].filesize);
            snprintf(ph_memsize_buf, sizeof(ph_memsize_buf), "%lu",
                     (unsigned long)interp_ph[i].memsize);
            snprintf(flags_buf, sizeof(flags_buf), "0x%x", interp_ph[i].flags);

            trace_attribute_t load_attrs[] = {
                { "role", role_buf },
                { "path", path_buf },
                { "data", data_buf },
                { "fd", fd_buf },
                { "map_start", map_start_buf },
                { "map_end", map_end_buf },
                { "file_offset_start", file_offset_start_buf },
                { "ph_vaddr", ph_vaddr_buf },
                { "ph_offset", ph_offset_buf },
                { "ph_filesize", ph_filesize_buf },
                { "ph_memsize", ph_memsize_buf },
                { "flags", flags_buf },
            };
            trace_begin_interval(TRACE_ORIGIN_KERNEL, "task.proof.exec.load.segment", load_attrs,
                                 sizeof(load_attrs) / sizeof(load_attrs[0]));
        }
        entry = interp_base + interp_header.entry_point;

        // Trace interpreter mapping for APPSIM-004 diagnosis
        char interp_name_buf[128];
        char interp_base_buf[32];
        char interp_entry_buf[32];

        strncpy(interp_name_buf, interp_name, sizeof(interp_name_buf) - 1);
        interp_name_buf[sizeof(interp_name_buf) - 1] = '\0';
        snprintf(interp_base_buf, sizeof(interp_base_buf), "0x%lx", (unsigned long)interp_base);
        snprintf(interp_entry_buf, sizeof(interp_entry_buf), "0x%lx", (unsigned long)entry);

        trace_attribute_t interp_attrs[] = {
            { "name", interp_name_buf },
            { "base", interp_base_buf },
            { "entry", interp_entry_buf },
        };
        trace_begin_interval(TRACE_ORIGIN_KERNEL, "task.proof.elf_interp.mapped", interp_attrs,
                             sizeof(interp_attrs) / sizeof(interp_attrs[0]));

        // Name the interpreter mapping pages for APPSIM-004 diagnosis
        // This allows pc_mapping lookup to identify interpreter vs. executable
        for (int i = 0; i < interp_header.phent_count; i++) {
            if (interp_ph[i].type != PT_LOAD)
                continue;
            addr_t seg_start = interp_base + interp_ph[i].vaddr;
            addr_t seg_end = seg_start + interp_ph[i].memsize;
            page_t start_page = PAGE(seg_start);
            page_t end_page = PAGE(seg_end);
            for (page_t pg = start_page; pg <= end_page; pg++) {
                struct pt_entry *pt = mem_pt(current->mem, pg);
                if (pt && pt->data && !pt->data->name) {
                    pt->data->name = "[interpreter]";
                }
            }
        }

        // For dynamically linked executables, x1 must point to loader's _DYNAMIC
        dynamic_addr = 0;
        for (int i = 0; i < interp_header.phent_count; i++) {
            if (interp_ph[i].type == PT_DYNAMIC) {
                dynamic_addr = interp_base + interp_ph[i].vaddr;
                break;
            }
        }

        // Dump _DYNAMIC table from memory
        if (dynamic_addr != 0) {
            write_wrunlock(&current->mem->lock); // Unlock for user_get
            for (int i = 0; i < 20; i++) {       // Dump first 20 entries
                uint64_t tag, val;
                if (user_get(dynamic_addr + i * 16, tag) == 0 &&
                    user_get(dynamic_addr + i * 16 + 8, val) == 0) {
                    if (tag == 0)
                        break; // DT_NULL terminator
                } else {
                    break;
                }
            }
            write_wrlock(&current->mem->lock); // Re-lock
        }
        for (int i = 0; i < interp_header.phent_count; i++) {
            if (interp_ph[i].type == PT_LOAD) {
                addr_t seg_addr = interp_base + interp_ph[i].vaddr;
                (void) seg_addr;
            }
        }
    }

    // map vdso
    err = _ENOMEM;
    pages_t vdso_pages = sizeof(vdso_data) >> PAGE_BITS;
    // FIXME disgusting hack: musl's dynamic linker has a one-page hole, and
    // I'd rather not put the vdso in that hole. so find a two-page hole and
    // add one.
    page_t vdso_page = pt_find_hole(current->mem, vdso_pages + 1);
    if (vdso_page == BAD_PAGE)
        goto beyond_hope;
    vdso_page += 1;
    if ((err = pt_map(current->mem, vdso_page, vdso_pages, (void *)vdso_data, 0, 0)) < 0)
        goto beyond_hope;
    mem_pt(current->mem, vdso_page)->data->name = "[vdso]";
    current->mm->vdso = vdso_page << PAGE_BITS;
    addr_t vdso_entry = current->mm->vdso + ((struct elf_header *)vdso_data)->entry_point;

    // map 3 empty "vvar" pages for VDSO compatibility
    page_t vvar_page = pt_find_hole(current->mem, VVAR_PAGES);
    if (vvar_page == BAD_PAGE)
        goto beyond_hope;
    if ((err = pt_map_nothing(current->mem, vvar_page, VVAR_PAGES, 0)) < 0)
        goto beyond_hope;
    mem_pt(current->mem, vvar_page)->data->name = "[vvar]";

// STACK TIME!

// Map sufficient stack pages to accommodate initial stack setup.
// AArch64 startup layout - two-sided budget inside mapped stack region
// Derived from USER_TOP with explicit upward headroom and downward reserve
#define USER_TOP          (((addr_t)MEM_PAGES) << PAGE_BITS) // 0x100000000
#define STARTUP_HEADROOM  ((addr_t)16 * 1024 * 1024)         // 16 MB gap
#define STACK_MAPPED_SIZE ((addr_t)4 * 1024 * 1024)          // 4 MB mapped stack
#define TLS_TCB_SIZE      ((addr_t)256 * 1024)               // 256 KB
#define GUARD_PAGES       4
#define GUARD_SIZE        ((addr_t)GUARD_PAGES * PAGE_SIZE)

// Two-sided budget inside mapped stack
#define INITIAL_UPWARD_HEADROOM  ((addr_t)512 * 1024)      // 512 KB above SP
#define INITIAL_DOWNWARD_RESERVE ((addr_t)2 * 1024 * 1024) // 2 MB below SP

#define STACK_TOP  (USER_TOP - STARTUP_HEADROOM)
#define STACK_BASE (STACK_TOP - STACK_MAPPED_SIZE)

#define TCB_TOP  (STACK_BASE - GUARD_SIZE)
#define TCB_BASE (TCB_TOP - TLS_TCB_SIZE)

    // Verify two-sided budget fits inside mapped stack
    _Static_assert(INITIAL_UPWARD_HEADROOM + INITIAL_DOWNWARD_RESERVE <= STACK_MAPPED_SIZE,
                   "startup budget must fit inside mapped stack");

    // Map stack pages from STACK_BASE to STACK_TOP
    pages_t stack_base_page = PAGE(STACK_BASE);
    pages_t stack_size_pages = PAGE_ROUND_UP(STACK_MAPPED_SIZE);
    if ((err = pt_map_nothing(current->mem, stack_base_page, stack_size_pages,
                              P_WRITE | P_GROWSDOWN)) < 0)
        goto beyond_hope;

    // Map TCB/TLS pages below stack with guard gap
    pages_t tcb_base_page = PAGE(TCB_BASE);
    pages_t tcb_size_pages = PAGE_ROUND_UP(TLS_TCB_SIZE);
    if ((err = pt_map_nothing(current->mem, tcb_base_page, tcb_size_pages, P_WRITE)) < 0)
        goto beyond_hope;

    // that was the last memory mapping
    write_wrunlock(&current->mem->lock);

    // Start SP with explicit upward headroom inside mapped stack
    // argv/envp/auxv placed in downward reserve, X2 can walk in upward headroom
    addr_t sp = align_stack(STACK_TOP - INITIAL_UPWARD_HEADROOM);

    const size_t stack_slot_size = sizeof(addr_t);
    // on 32-bit linux, there's 4 empty bytes at the very bottom of the stack.
    // on 64-bit linux, there's 8. make ptraceomatic happy. (a major theme in this file)
    sp -= sizeof(void *);

    err = _EFAULT;
    // first, copy stuff pointed to by argv/envp/auxv
    // filename, argc, argv
    trace_exec_checkpoint("task.proof.elf_exec.before_copy_execfn", err);
    addr_t file_addr = sp = copy_string(sp, file);
    if (sp == 0) {
        trace_exec_checkpoint("task.proof.elf_exec.copy_execfn_failed", err);
        goto beyond_hope;
    }
    trace_exec_checkpoint("task.proof.elf_exec.after_copy_execfn", err);

    trace_exec_checkpoint("task.proof.elf_exec.before_copy_envp", err);
    addr_t envp_addr = sp = args_copy(sp, envp);
    if (sp == 0) {
        trace_exec_checkpoint("task.proof.elf_exec.copy_envp_failed", err);
        goto beyond_hope;
    }
    trace_exec_checkpoint("task.proof.elf_exec.after_copy_envp", err);
    current->mm->argv_end = sp;

    trace_exec_checkpoint("task.proof.elf_exec.before_copy_argv", err);
    addr_t argv_addr = sp = args_copy(sp, argv);
    if (sp == 0) {
        trace_exec_checkpoint("task.proof.elf_exec.copy_argv_failed", err);
        goto beyond_hope;
    }
    trace_exec_checkpoint("task.proof.elf_exec.after_copy_argv", err);
    current->mm->argv_start = sp;
    sp = align_stack(sp);

    trace_exec_checkpoint("task.proof.elf_exec.before_copy_platform", err);
    addr_t platform_addr = sp = copy_string(sp, "aarch64");
    if (sp == 0) {
        trace_exec_checkpoint("task.proof.elf_exec.copy_platform_failed", err);
        goto beyond_hope;
    }
    trace_exec_checkpoint("task.proof.elf_exec.after_copy_platform", err);
    // 16 random bytes so no system call is needed to seed a userspace RNG
    char random[16] = {};
    get_random(random, sizeof(random)); // if this fails, eh, no one's really using it
    addr_t random_addr = sp -= sizeof(random);
    trace_exec_checkpoint("task.proof.elf_exec.before_copy_random", err);
    if (user_put(sp, random)) {
        trace_exec_checkpoint("task.proof.elf_exec.copy_random_failed", err);
        goto beyond_hope;
    }
    trace_exec_checkpoint("task.proof.elf_exec.after_copy_random", err);

    // the way linux aligns the stack at this point is kinda funky
    // calculate how much space is needed for argv, envp, and auxv, subtract
    // that from sp, then align, then copy argv/envp/auxv from that down

    // declare elf aux now so we can know how big it is
    struct aux_ent aux[] = { { AX_SYSINFO, vdso_entry },
                             { AX_SYSINFO_EHDR, current->mm->vdso },
                             { AX_HWCAP, 0x00000000 }, // suck that
                             { AX_PAGESZ, PAGE_SIZE },
                             { AX_CLKTCK, 0x64 },
                             { AX_PHDR, load_addr + header.prghead_off },
                             { AX_PHENT, sizeof(struct prg_header) },
                             { AX_PHNUM, header.phent_count },
                             { AX_BASE, interp_base },
                             { AX_FLAGS, 0 },
                             { AX_ENTRY, bias + header.entry_point },
                             { AX_UID, 0 },
                             { AX_EUID, 0 },
                             { AX_GID, 0 },
                             { AX_EGID, 0 },
                             { AX_SECURE, 0 },
                             { AX_RANDOM, random_addr },
                             { AX_HWCAP2, 0 }, // suck that too
                             { AX_EXECFN, file_addr },
                             { AX_PLATFORM, platform_addr },
                             { 0, 0 } };
    // AArch64 user stacks are LP64: argc/argv/envp slots are 64-bit wide.
    sp -= ((argv.count + 1) + (envp.count + 1) + 1) * stack_slot_size;
    sp -= sizeof(aux);
    sp &= ~0xf;

    // now copy down, start using p so sp is preserved
    addr_t p = sp;

    // argc
    trace_exec_checkpoint("task.proof.elf_exec.before_write_argc", err);
    if (user_put(p, argv.count)) {
        trace_exec_checkpoint("task.proof.elf_exec.write_argc_failed", err);
        return _EFAULT;
    }
    trace_exec_checkpoint("task.proof.elf_exec.after_write_argc", err);
    p += stack_slot_size;

    // argv
    size_t argc = argv.count;
    while (argc-- > 0) {
        if (user_put(p, argv_addr))
            return _EFAULT;
        argv_addr += user_strlen(argv_addr) + 1;
        p += stack_slot_size;
    }
    p += stack_slot_size;

    // envp
    size_t envc = envp.count;
    while (envc-- > 0) {
        if (user_put(p, envp_addr))
            return _EFAULT;
        envp_addr += user_strlen(envp_addr) + 1;
        p += stack_slot_size;
    }
    p += stack_slot_size;

    // copy auxv
    current->mm->auxv_start = p;
    trace_exec_checkpoint("task.proof.elf_exec.before_write_auxv", err);
    if (user_put(p, aux)) {
        trace_exec_checkpoint("task.proof.elf_exec.write_auxv_failed", err);
        goto beyond_hope;
    }
    trace_exec_checkpoint("task.proof.elf_exec.after_write_auxv", err);
    p += sizeof(aux);
    current->mm->auxv_end = p;

    current->mm->stack_start = sp;
    trace_exec_checkpoint("task.proof.elf_exec.after_stack_setup", err);

    // Initialize CPU state properly before setting up registers
    // This zeros all X registers, PSTATE, and other state to prevent garbage values
    // CRITICAL: Save and restore mmu pointer since a64_cpu_init zeros all fields
    trace_exec_layout_checkpoint("task.proof.exec.layout_before_cpu_init", current, err);
    trace_exec_checkpoint("task.proof.elf_exec.before_cpu_init_probe", err);
    a64_cpu_init_probe(current, &current->cpu, err);
    trace_exec_checkpoint("task.proof.elf_exec.after_cpu_init_probe", err);
    trace_exec_layout_checkpoint("task.proof.exec.layout_after_cpu_init_probe", current, err);
    trace_exec_checkpoint("task.proof.elf_exec.before_cpu_init", err);
    struct mmu *saved_mmu = current->cpu.mmu;
    a64_cpu_init(current, &current->cpu, err);
    trace_exec_checkpoint("task.proof.elf_exec.after_cpu_init", err);
    current->cpu.mmu = saved_mmu;
    trace_exec_checkpoint("task.proof.elf_exec.after_restore_cpu_mmu", err);

    current->cpu.sp = sp;
    current->cpu.pc = entry;

    // DEBUG: Verify CPU state after initialization

    // CRITICAL: Validate entry point before returning
    if (current->cpu.pc == 0) {
        err = _EFAULT;
        goto beyond_hope;
    }
    if (current->cpu.pc == 0x100000000ULL) {
        err = _EFAULT;
        goto beyond_hope;
    }
    // aarch64 doesn't have x87 FPU control word
    // current->cpu.fcw = 0x37f;

    // aarch64 musl process startup convention:
    // x0 = sp (pointer to stack with argc/argv/envp/auxv layout)
    // x1 = _DYNAMIC (pointer to dynamic section, or 0 if not modeled)
    // x2-x7 = 0 (not used for startup)
    // x8 = 0 (syscall number register)
    // TPIDR_EL0 = TCB base (TLS pointer, accessed via system register)
    //
    // musl _start moves sp into x0, sets x1 to _DYNAMIC, aligns sp,
    // then calls _start_c(sp) which reads argc from p[0] and argv from p+1

    // Set up TCB (Thread Control Block) for TLS
    // TLS belongs in TPIDR_EL0, NOT in x3
    addr_t tcb_base = TCB_BASE; // Start of mapped TCB pages (256 KB)
    a64_setup_tls_area(&current->cpu, tcb_base);

    // Correct AArch64 musl startup: pass stack pointer in x0
    current->cpu.x[0] = sp;           // Points to argc on stack
    current->cpu.x[1] = dynamic_addr; // _DYNAMIC - address of PT_DYNAMIC section
    // x[2-7] already zeroed by a64_cpu_init()
    // x[8-30] also zeroed by a64_cpu_init()

    // aarch64 PSTATE (no eflags register)
    // current->cpu.eflags = 0;
    trace_exec_checkpoint("task.proof.elf_exec.after_cpu_setup", err);
    err = 0;
    trace_exec_checkpoint("task.proof.elf_exec.before_return", err);
    trace_exec_checkpoint("task.proof.do_execve.after_elf_exec", err);
out_free_interp:
    if (interp_name != NULL)
        free(interp_name);
    if (interp_fd != NULL && !IS_ERR(interp_fd))
        fd_close(interp_fd);
    if (interp_ph != NULL)
        free(interp_ph);
out_free_ph:
    free(ph);
    return err;

beyond_hope:
    // TODO force sigsegv
    trace_exec_checkpoint("task.proof.elf_exec.error_branch_entered", err);
    trace_exec_checkpoint("task.proof.elf_exec.before_cleanup_unlock", err);
    write_wrunlock(&current->mem->lock);
    trace_exec_checkpoint("task.proof.elf_exec.after_cleanup_unlock", err);
    goto out_free_interp;
}

static size_t args_size(struct exec_args args)
{
    const char *args_end = args.args;
    for (size_t i = 0; i < args.count; i++) {
        args_end += strlen(args_end) + 1;
    }
    // don't forget the very last null terminator
    assert(args_end[0] == '\0');
    args_end++;
    return args_end - args.args;
}

static inline addr_t align_stack(addr_t sp)
{
    return sp & ~0xf;
}

static inline addr_t copy_string(addr_t sp, const char *string)
{
    sp -= strlen(string) + 1;
    if (user_write_string(sp, string))
        return 0;
    return sp;
}

static inline addr_t args_copy(addr_t sp, struct exec_args args)
{
    size_t size = args_size(args);
    sp -= size;
    if (user_write(sp, args.args, size))
        return 0;
    return sp;
}

static inline ssize_t user_strlen(addr_t p)
{
    size_t i = 0;
    char c;
    do {
        if (user_get(p + i, c))
            return -1;
        i++;
    } while (c != '\0');
    return i - 1;
}

static inline int user_memset(addr_t start, byte_t val, addr_t len)
{
    while (len--)
        if (user_put(start++, val))
            return 1;
    return 0;
}

static int format_exec(struct fd *fd, const char *file, struct exec_args argv,
                       struct exec_args envp)
{
    trace_exec_checkpoint("task.proof.do_execve.before_format_exec", 0);
    int err = elf_exec(fd, file, argv, envp);
    trace_exec_checkpoint("task.proof.elf_exec.after_return_to_caller", err);
    trace_exec_checkpoint("task.proof.do_execve.after_format_exec", err);
    if (err != _ENOEXEC)
        return err;
    // other formats would go here
    return _ENOEXEC;
}

// Execute a text file that contains an interpreter path (like /bin/busybox)
// This handles files that are just a path to the interpreter, not a shebang
static int text_interpreter_exec(struct fd *fd, const char *file, struct exec_args argv,
                                 struct exec_args envp)
{
    if (fd->ops->lseek(fd, 0, LSEEK_SET))
        return _EIO;
    char header[256];
    int size = fd->ops->read(fd, header, sizeof(header) - 1);
    if (size < 0)
        return _EIO;
    header[size] = '\0';

    // Check if it's a plain text path (no shebang, no ELF magic)
    // Must not start with #! and not be binary
    if (size >= 2 && header[0] == '#' && header[1] == '!')
        return _ENOEXEC; // It's a shebang, handled elsewhere
    if (size >= 4 &&
        (header[0] == 0x7f && header[1] == 'E' && header[2] == 'L' && header[3] == 'F'))
        return _ENOEXEC; // It's an ELF binary

    // Find end of first line (path to interpreter)
    char *newline = strchr(header, '\n');
    if (newline)
        *newline = '\0';

    // Trim whitespace
    char *interpreter = header;
    while (*interpreter == ' ' || *interpreter == '\t')
        interpreter++;

    // Check if it looks like a path
    if (*interpreter != '/')
        return _ENOEXEC; // Not a valid path

    // Remove trailing whitespace
    char *end = interpreter + strlen(interpreter) - 1;
    while (end > interpreter && (*end == ' ' || *end == '\t' || *end == '\r'))
        *end-- = '\0';

    // Build new argv: interpreter [original argv0] [original args...]
    struct exec_args argv_rest = {
        .count = argv.count > 0 ? argv.count - 1 : 0,
        .args = argv.count > 0 ? argv.args + strlen(argv.args) + 1 : argv.args,
    };

    size_t args_rest_size = args_size(argv_rest);
    size_t extra_args_size = strlen(interpreter) + 1 + strlen(file) + 1;
    if (args_rest_size + extra_args_size >= ARGV_MAX)
        return _E2BIG;

    char new_argv_buf[ARGV_MAX];
    struct exec_args new_argv = { .args = new_argv_buf };

    strcpy(new_argv_buf, interpreter);
    new_argv.count = 1;
    size_t n = strlen(interpreter) + 1;

    strcpy(new_argv_buf + n, file);
    n += strlen(file) + 1;
    new_argv.count++;

    memcpy(new_argv_buf + n, argv_rest.args, args_rest_size);
    new_argv.count += argv_rest.count;

    struct fd *interpreter_fd = generic_open(interpreter, O_RDONLY_, 0);
    if (IS_ERR(interpreter_fd))
        return PTR_ERR(interpreter_fd);
    int result = format_exec(interpreter_fd, interpreter, new_argv, envp);
    fd_close(interpreter_fd);
    return result;
}

static int shebang_exec(struct fd *fd, const char *file, struct exec_args argv,
                        struct exec_args envp)
{
    // read the first 128 bytes to get the shebang line out of
    if (fd->ops->lseek(fd, 0, LSEEK_SET))
        return _EIO;
    char header[128];
    int size = fd->ops->read(fd, header, sizeof(header) - 1);
    if (size < 0)
        return _EIO;
    header[size] = '\0';

    // only look at the first line
    char *newline = strchr(header, '\n');
    if (newline == NULL)
        return _ENOEXEC;
    *newline = '\0';

    // format: #![spaces]interpreter[spaces]argument[spaces]
    char *p = header;
    if (p[0] != '#' || p[1] != '!')
        return _ENOEXEC;
    p += 2;
    while (*p == ' ')
        p++;
    if (*p == '\0')
        return _ENOEXEC;

    char *interpreter = p;
    while (*p != ' ' && *p != '\0')
        p++;
    if (*p != '\0') {
        *p++ = '\0';
        while (*p == ' ')
            p++;
    }

    char *argument = p;
    // strip trailing whitespace
    p = strchr(p, '\0') - 1;
    while (*p == ' ')
        *p-- = '\0';
    if (*argument == '\0')
        argument = NULL;

    struct exec_args argv_rest = {
        .count = argv.count - 1,
        .args = argv.args + strlen(argv.args) + 1,
    };
    size_t args_rest_size = args_size(argv_rest);
    size_t extra_args_size = strlen(interpreter) + 1 + strlen(file) + 1;
    if (argument)
        extra_args_size += strlen(argument) + 1;
    if (args_rest_size + extra_args_size >= ARGV_MAX)
        return _E2BIG;

    char new_argv_buf[ARGV_MAX];
    struct exec_args new_argv = { .args = new_argv_buf };
    size_t n = 0;
    strcpy(new_argv_buf, interpreter);
    new_argv.count++;
    n += strlen(interpreter) + 1;
    if (argument) {
        strcpy(new_argv_buf + n, argument);
        new_argv.count++;
        n += strlen(argument) + 1;
    }
    strcpy(new_argv_buf + n, file);
    n += strlen(file) + 1;
    new_argv.count++;
    memcpy(new_argv_buf + n, argv_rest.args, args_rest_size);
    new_argv.count += argv_rest.count;

    struct fd *interpreter_fd = generic_open(interpreter, O_RDONLY_, 0);
    if (IS_ERR(interpreter_fd))
        return PTR_ERR(interpreter_fd);
    int err = format_exec(interpreter_fd, interpreter, new_argv, envp);
    fd_close(interpreter_fd);
    return err;
}

int __do_execve(const char *file, struct exec_args argv, struct exec_args envp)
{
    // TRACE[1]: __do_execve_entry
    trace_emit_exec_path_boundary(
        (uint64_t)current, current ? current->pid : 0, (uint64_t)(current ? current->mm : NULL),
        (uint64_t)(current ? current->mem : NULL), EXEC_PATH_DO_EXECVE_ENTRY_RET, 0);
    struct fd *fd = generic_open(file, O_RDONLY, 0);
    if (IS_ERR(fd)) {
        return PTR_ERR(fd);
    }

    struct statbuf stat;
    int err = fd->mount->fs->fstat(fd, &stat);
    if (err < 0) {
        fd_close(fd);
        return err;
    }

    // if nobody has permission to execute, it should be safe to not execute
    if (!(stat.mode & 0111)) {
        // TEMPORARY: Allow execution even without execute permissions
    }

    char debug_buf[17] = { 0 };
    fd->ops->lseek(fd, 0, LSEEK_SET);
    (void) fd->ops->read(fd, debug_buf, 16);
    fd->ops->lseek(fd, 0, LSEEK_SET);

    // TRACE[3]: before_format_exec
    trace_emit_exec_path_boundary(
        (uint64_t)current, current ? current->pid : 0, (uint64_t)(current ? current->mm : NULL),
        (uint64_t)(current ? current->mem : NULL), EXEC_PATH_BEFORE_FORMAT_EXEC, 0);
    err = format_exec(fd, file, argv, envp);
    // TRACE[4]: after_format_exec
    trace_emit_exec_path_boundary(
        (uint64_t)current, current ? current->pid : 0, (uint64_t)(current ? current->mm : NULL),
        (uint64_t)(current ? current->mem : NULL), EXEC_PATH_AFTER_FORMAT_EXEC, err);

    if (err == _ENOEXEC) {
        err = shebang_exec(fd, file, argv, envp);
    }
    if (err == _ENOEXEC) {
        err = text_interpreter_exec(fd, file, argv, envp);
    }

    if (err == _ENOEXEC) {
    }
    fd_close(fd);
    if (err < 0)
        return err;

    // setuid/setgid
    if (stat.mode & S_ISUID) {
        current->suid = current->euid;
        current->euid = stat.uid;
    }
    if (stat.mode & S_ISGID) {
        current->sgid = current->egid;
        current->egid = stat.gid;
    }

    // save current->comm
    lock(&current->general_lock);
    const char *basename = strrchr(file, '/');
    if (basename == NULL)
        basename = file;
    else
        basename++;
    strncpy(current->comm, basename, sizeof(current->comm));
    unlock(&current->general_lock);

    update_thread_name();

    // cloexec
    // consider putting this in fd.c?
    fdtable_do_cloexec(current->files);

    // reset signal handlers
    lock(&current->sighand->lock);
    for (int sig = 0; sig < NUM_SIGS; sig++) {
        struct sigaction_ *action = &current->sighand->action[sig];
        if (action->handler != SIG_IGN_)
            action->handler = SIG_DFL_;
    }
    current->sighand->altstack = 0;
    unlock(&current->sighand->lock);

    current->did_exec = true;
    vfork_notify(current);

    if (current->ptrace.traced) {
        lock(&pids_lock);
        send_signal(current, SIGTRAP_,
                    (struct siginfo_){
                        .code = SI_USER_,
                        .kill.pid = current->pid,
                        .kill.uid = current->uid,
                    });
        unlock(&pids_lock);
    }

    return 0;
}

int do_execve(const char *file, size_t argc, const char *argv_p, const char *envp_p)
{
    // TRACE[0]: do_execve_entry
    trace_exec_checkpoint("task.proof.do_execve.entry", 0);
    trace_emit_exec_path_boundary(
        (uint64_t)current, current ? current->pid : 0, (uint64_t)(current ? current->mm : NULL),
        (uint64_t)(current ? current->mem : NULL), EXEC_PATH_DO_EXECVE_ENTRY, 0);

    // APPSIM-004 Stage 3A: Trace exec target path
    trace_attribute_t exec_target_attrs[] = {
        { "path", file },
        { "pid", "unknown" }, // Will be updated below if current is valid
    };
    if (current != NULL) {
        char pid_buf[32];
        snprintf(pid_buf, sizeof(pid_buf), "%u", (unsigned)current->pid);
        exec_target_attrs[1].value = pid_buf;
    }
    (void)trace_begin_interval(TRACE_ORIGIN_EXEC, "task.proof.guest.exec.target", exec_target_attrs,
                               sizeof(exec_target_attrs) / sizeof(exec_target_attrs[0]));

    // APPSIM-004 Stage 1: Check if this is /bin/login exec
    int is_login = (strstr(file, "login") != NULL);
    if (is_login) {
        trace_exec_checkpoint("task.proof.login.exec.entry.kernel", 0);
    }

    struct exec_args argv = { .count = argc, .args = argv_p };
    struct exec_args envp = { .args = envp_p };
    while (*envp_p != '\0') {
        envp_p += strlen(envp_p) + 1;
        envp.count++;
    }
    int err = __do_execve(file, argv, envp);

    // APPSIM-004 Stage 3A: Trace exec success/failure
    char err_buf[32];
    snprintf(err_buf, sizeof(err_buf), "%d", err);
    trace_attribute_t exec_result_attrs[] = {
        { "path", file },
        { "return", err_buf },
    };
    if (err < 0) {
        (void)trace_begin_interval(TRACE_ORIGIN_EXEC, "task.proof.guest.exec.failure",
                                   exec_result_attrs,
                                   sizeof(exec_result_attrs) / sizeof(exec_result_attrs[0]));
    } else {
        (void)trace_begin_interval(TRACE_ORIGIN_EXEC, "task.proof.guest.exec.success",
                                   exec_result_attrs,
                                   sizeof(exec_result_attrs) / sizeof(exec_result_attrs[0]));
    }

    // APPSIM-004 Stage 1: Trace exec result for /bin/login
    if (is_login) {
        if (err < 0) {
            trace_exec_checkpoint("task.proof.login.exec.failure.kernel", err);
        } else {
            trace_exec_checkpoint("task.proof.login.exec.success.kernel", 0);
            // Verify PID is still valid after successful exec
            if (current != NULL && current->pid != 0) {
                trace_exec_checkpoint("task.proof.login.pid.alive.kernel", current->pid);
            }
        }
    }

    // APPSIM-004 Stage 3A: Verify guest PID alive after exec
    if (err == 0 && current != NULL && current->pid != 0) {
        char alive_pid_buf[32];
        snprintf(alive_pid_buf, sizeof(alive_pid_buf), "%u", (unsigned)current->pid);
        trace_attribute_t pid_alive_attrs[] = {
            { "pid", alive_pid_buf },
            { "path", file },
        };
        (void)trace_begin_interval(TRACE_ORIGIN_EXEC, "task.proof.guest.pid.alive_after_exec",
                                   pid_alive_attrs,
                                   sizeof(pid_alive_attrs) / sizeof(pid_alive_attrs[0]));
    }

    trace_exec_checkpoint("task.proof.do_execve.before_return", err);
    return err;
}

static ssize_t user_read_string_array(addr_t addr, char *buf, size_t max)
{
    size_t i = 0;
    size_t p = 0;
    for (;;) {
        addr_t str_addr;
        if (user_get(addr + i * sizeof(addr_t), str_addr))
            return _EFAULT;
        if (str_addr == 0)
            break;
        size_t str_p = 0;
        for (;;) {
            if (p >= max)
                return _E2BIG;
            if (user_get(str_addr + str_p, buf[p]))
                return _EFAULT;
            str_p++;
            p++;
            if (buf[p - 1] == '\0')
                break;
        }
        i++;
    }
    if (p >= max)
        return _E2BIG;
    buf[p] = '\0';
    return i;
}

dword_t sys_execve(addr_t filename_addr, addr_t argv_addr, addr_t envp_addr)
{
    char filename[MAX_PATH];
    if (user_read_string(filename_addr, filename, sizeof(filename)))
        return _EFAULT;

    int err = _ENOMEM;
    char *argv = malloc(ARGV_MAX);
    if (argv == NULL)
        goto err_free_argv;
    ssize_t argc = user_read_string_array(argv_addr, argv, ARGV_MAX);
    if (argc < 0) {
        err = argc;
        goto err_free_argv;
    }

    char *envp = malloc(ARGV_MAX);
    if (envp == NULL)
        goto err_free_envp;
    if (envp_addr != 0) {
        err = user_read_string_array(envp_addr, envp, ARGV_MAX);
        if (err < 0)
            goto err_free_envp;
    } else {
        // Do not take advantage of this nonstandard and nonportable misfeature!
        // - Michael Kerrisk, execve(2)
        envp[0] = envp[1] = '\0';
    }

    STRACE("execve(\"%.1000s\", {", filename);
    const char *args = argv;
    while (*args != '\0') {
        STRACE("\"%.1000s\", ", args);
        args += strlen(args) + 1;
    }
    STRACE("}, {");
    args = envp;
    while (*args != '\0') {
        STRACE("\"%.1000s\", ", args);
        args += strlen(args) + 1;
    }
    STRACE("})");

    err = do_execve(filename, argc, argv, envp);

err_free_envp:
    free(envp);
err_free_argv:
    free(argv);
    return err;
}
