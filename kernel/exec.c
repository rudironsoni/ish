#include "kernel/signal.h"
#include "kernel/memory.h"
#include "task.h"
#define _GNU_SOURCE
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "misc.h"
#include "kernel/calls.h"
#include "kernel/random.h"
#include "kernel/errno.h"
#include "fs/fd.h"
#include "kernel/elf.h"
#include "kernel/vdso.h"
#include "emu/aarch64/tls.h"
#include "emu/aarch64/cpu.h"
#include "trace/trace.h"
// Simple debug logging - outputs to system console
#define exec_log(fmt, ...) printk("[iSH-exec] " fmt, ##__VA_ARGS__)

// Logging macros - set ISH_EXEC_DEBUG=1 for verbose logging
#ifndef ISH_EXEC_DEBUG
#define ISH_EXEC_DEBUG 0
#endif

#if ISH_EXEC_DEBUG
#if ISH_APPLE
    #include <os/log.h>
    #define ISH_LOG_ERROR(fmt, ...) do { \
        printk("[exec] ERROR: " fmt "\n", ##__VA_ARGS__); \
        os_log_error(OS_LOG_DEFAULT, "[iSH] [exec] ERROR: " fmt, ##__VA_ARGS__); \
    } while(0)
    #define ISH_LOG(fmt, ...) do { \
        printk("[exec] " fmt "\n", ##__VA_ARGS__); \
        os_log(OS_LOG_DEFAULT, "[iSH] [exec] " fmt, ##__VA_ARGS__); \
    } while(0)
    #define ISH_LOG_DEBUG(fmt, ...) do { \
        printk("[exec] DEBUG: " fmt "\n", ##__VA_ARGS__); \
        os_log_debug(OS_LOG_DEFAULT, "[iSH] [exec] DEBUG: " fmt, ##__VA_ARGS__); \
    } while(0)
#else
    #define ISH_LOG_ERROR(fmt, ...) printk("[exec] ERROR: " fmt "\n", ##__VA_ARGS__)
    #define ISH_LOG(fmt, ...) printk("[exec] " fmt "\n", ##__VA_ARGS__)
    #define ISH_LOG_DEBUG(fmt, ...) printk("[exec] DEBUG: " fmt "\n", ##__VA_ARGS__)
#endif
#else
#define ISH_LOG_ERROR(fmt, ...) printk("[exec] ERROR: " fmt "\n", ##__VA_ARGS__)
#define ISH_LOG(fmt, ...) do {} while(0)
#define ISH_LOG_DEBUG(fmt, ...) do {} while(0)
#endif

#define ARGV_MAX 32 * PAGE_SIZE

struct exec_args {
    // number of arguments
    size_t count;
    // series of count null-terminated strings, plus an extra null for good measure
    const char *args;
};

static inline addr_t align_stack(addr_t sp);
static inline ssize_t user_strlen(addr_t p);
static inline int user_memset(addr_t start, byte_t val, addr_t len);
static inline addr_t copy_string(addr_t sp, const char *string);
static inline addr_t args_copy(addr_t sp, struct exec_args args);
static size_t args_size(struct exec_args args);

static int read_header(struct fd *fd, struct elf_header *header) {
    // Reset file position to beginning
    fd->ops->lseek(fd, 0, LSEEK_SET);
    
    // Read raw bytes first to debug
    unsigned char raw_header[64];
    ssize_t err = fd->ops->read(fd, raw_header, 64);
    if (err < 0) {
        exec_log("[exec] read_header: read error %d\n", (int)err);
        return (int) err;
    }
    if (err != 64) {
        exec_log("[exec] read_header: short read (%d bytes), returning ENOEXEC\n", (int)err);
        return _ENOEXEC;
    }
    
    // Log raw bytes
    exec_log("[exec] RAW BYTES: ");
    for (int i = 0; i < 16; i++) {
        exec_log("%02x ", raw_header[i]);
    }
    exec_log("\n");
    
    // Copy to header struct
    memcpy(header, raw_header, sizeof(*header));
    
    // Debug: print parsed values
    uint8_t *magic_bytes = (uint8_t*)&header->magic;
    exec_log("[exec] PARSED: magic=%02x %02x %02x %02x\n",
           magic_bytes[0], magic_bytes[1], magic_bytes[2], magic_bytes[3]);
    exec_log("[exec] PARSED: bitness=%d endian=%d elfv1=%d type=%d machine=%d\n",
           header->bitness, header->endian, header->elfversion1, header->type, header->machine);
    // Validate ELF header with detailed error codes
    if (memcmp(&header->magic, ELF_MAGIC, sizeof(header->magic)) != 0) {
        exec_log("[exec] read_header: magic mismatch, returning ENOEXEC\n");
        return _ENOEXEC;  // Error -8: Magic bytes wrong
    }
    if (header->type != ELF_EXECUTABLE && header->type != ELF_DYNAMIC) {
        exec_log("[exec] read_header: type mismatch (type=%d, expected %d or %d), returning ENOEXEC\n",
               header->type, ELF_EXECUTABLE, ELF_DYNAMIC);
        return _ENOEXEC;  // Error -8: Type not executable/dynamic
    }
    if (header->endian != ELF_LITTLEENDIAN) {
        exec_log("[exec] read_header: endian mismatch (endian=%d, expected %d), returning ENOEXEC\n",
               header->endian, ELF_LITTLEENDIAN);
        return _ENOEXEC;  // Error -8: Wrong endian
    }
    if (header->elfversion1 != 1) {
        exec_log("[exec] read_header: elfversion mismatch (elfversion1=%d, expected 1), returning ENOEXEC\n",
               header->elfversion1);
        return _ENOEXEC;  // Error -8: Wrong ELF version
    }

    // Architecture-specific validation (aarch64 only)
    if (header->bitness != ELF_64BIT) {
        exec_log("[exec] read_header: bitness mismatch (bitness=%d, expected %d), returning ENOEXEC\n",
               header->bitness, ELF_64BIT);
        return _ENOEXEC;  // Error -8: Not 64-bit
    }
    if (header->machine != ELF_AARCH64) {
        exec_log("[exec] read_header: machine mismatch (machine=%d, expected %d), returning ENOEXEC\n",
               header->machine, ELF_AARCH64);
        return _ENOEXEC;  // Error -8: Not aarch64
    }
    exec_log("[exec] read_header: ELF validation PASSED\n");
    return 0;
}

static int read_prg_headers(struct fd *fd, struct elf_header header, struct prg_header **ph_out) {
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

static int load_entry(struct prg_header ph, addr_t bias, struct fd *fd) {
    int err;

    addr_t addr = ph.vaddr + bias;
    addr_t offset = ph.offset;
    addr_t memsize = ph.memsize;
    addr_t filesize = ph.filesize;

    ISH_LOG("load_entry: addr=%llx, offset=%llx, memsize=%llx, filesize=%llx", 
            (unsigned long long)addr, (unsigned long long)offset, 
            (unsigned long long)memsize, (unsigned long long)filesize);

    int flags = P_READ;
    if (ph.flags & PH_W) flags |= P_WRITE;

    pages_t map_pages = PAGE_ROUND_UP(filesize + PGOFFSET(addr));
    page_t start_page = PAGE(addr);
    off_t map_offset = offset - PGOFFSET(addr);
    ISH_LOG("load_entry: mapping %u pages at page %u, offset=%lld", map_pages, start_page, (long long)map_offset);
    
    if (fd->ops->mmap == NULL) {
        ISH_LOG_ERROR("load_entry: fd->ops->mmap is NULL!");
        return _EINVAL;
    }
    
    if ((err = fd->ops->mmap(fd, current->mem, start_page,
                    map_pages,
                    map_offset, flags, MMAP_PRIVATE)) < 0) {
        ISH_LOG_ERROR("load_entry: mmap failed: %d", err);
        return err;
    }
    ISH_LOG("load_entry: mmap succeeded");
    // TODO find a better place for these to avoid code duplication
    struct pt_entry *first_pt = mem_pt(current->mem, start_page);
    if (first_pt == NULL || first_pt->data == NULL) {
        ISH_LOG_ERROR("load_entry: mem_pt returned NULL after mmap!");
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

static addr_t find_hole_for_elf(struct elf_header *header, struct prg_header *ph) {
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

static int elf_exec(struct fd *fd, const char *file, struct exec_args argv, struct exec_args envp) {
    int err = 0;
    
    ISH_LOG("elf_exec: loading %s", file);
    printk("[exec] elf_exec: ENTRY for file=%s\n", file);

    // read the headers
    struct elf_header header;
    printk("[exec] About to call read_header\n");
    if ((err = read_header(fd, &header)) < 0) {
        ISH_LOG_ERROR("read_header failed: %d", err);
        printk("[exec] read_header FAILED with err=%d\n", err);
        return err;
    }
    printk("[exec] read_header SUCCEEDED\n");
    ISH_LOG("ELF header OK: type=%d, machine=%d, entry=%llx", 
            header.type, header.machine, (unsigned long long)header.entry_point);
    
    struct prg_header *ph;
    printk("[exec] About to call read_prg_headers\n");
    if ((err = read_prg_headers(fd, header, &ph)) < 0) {
        ISH_LOG_ERROR("read_prg_headers failed: %d", err);
        printk("[exec] read_prg_headers FAILED with err=%d\n", err);
        return err;
    }
    printk("[exec] read_prg_headers SUCCEEDED, phent_count=%d\n", header.phent_count);
    ISH_LOG("Program headers OK: count=%d", header.phent_count);

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

        ISH_LOG("Allocating interpreter name: %zu bytes", (size_t)ph[i].filesize);
        interp_name = malloc(ph[i].filesize);
        if (interp_name == NULL) {
            ISH_LOG_ERROR("ENOMEM: Failed to allocate interpreter name (%zu bytes)", (size_t)ph[i].filesize);
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
            if (err == _ENOEXEC) err = _ELIBBAD;
            goto out_free_interp;
        }
        if ((err = read_prg_headers(interp_fd, interp_header, &interp_ph)) < 0) {
            if (err == _ENOEXEC) err = _ELIBBAD;
            goto out_free_interp;
        }
    }

    printk("[exec] About to release old mm and create new mm\n");
    // free the process's memory.
    // from this point on, if any error occurs the process will have to be
    // killed before it even starts. please don't be too sad about it, it's
    // just a process.
    //
    // general_lock protects current->mm. otherwise procfs might read the
    // pointer before it's released and then try to lock it after it's
    // released.
    ISH_LOG("Releasing old mm and creating new mm...");
    lock(&current->general_lock);
    printk("[exec] Calling mm_release, current->mm=%p\n", current->mm);
    if (current->mm == NULL) {
        printk("[exec] ERROR: current->mm is NULL!\n");
        unlock(&current->general_lock);
        err = _EINVAL;
        goto out_free_interp;
    }
    mm_release(current->mm);
    printk("[exec] mm_release done, calling mm_new\n");
    struct mm *new_mm = mm_new();
    if (new_mm == NULL) {
        ISH_LOG_ERROR("ENOMEM: mm_new() failed");
        unlock(&current->general_lock);
        err = _ENOMEM;
        goto out_free_interp;
    }
    task_set_mm(current, new_mm);
    unlock(&current->general_lock);
    write_wrlock(&current->mem->lock);
    printk("[exec] write_wrlock done, mm->exefile set\n");
    ISH_LOG("New mm created successfully");

    current->mm->exefile = fd_retain(fd);

    addr_t load_addr = 0; // used for AX_PHDR
    bool load_addr_set = false;
    addr_t bias = 0; // offset for loading shared libraries as executables

    // map dat shit!
    printk("[exec] About to map %d program headers\n", header.phent_count);
    ISH_LOG("Mapping %d program headers...", header.phent_count);
    for (unsigned i = 0; i < header.phent_count; i++) {
        if (ph[i].type != PT_LOAD)
            continue;

        if (!load_addr_set && header.type == ELF_DYNAMIC) {
            // see giant comment in linux/fs/binfmt_elf.c, around line 950
            if (interp_name)
                bias = 0x56555000; // I have no idea how this number was arrived at
            else
                bias = find_hole_for_elf(&header, ph);
        }

        ISH_LOG_DEBUG("Loading segment %u: vaddr=%llx, memsize=%llx, filesize=%llx", 
                i, (unsigned long long)ph[i].vaddr, 
                (unsigned long long)ph[i].memsize, (unsigned long long)ph[i].filesize);
        ISH_LOG("About to load segment %u...", i);
        if ((err = load_entry(ph[i], bias, fd)) < 0) {
            ISH_LOG_ERROR("load_entry failed for segment %u: %d", i, err);
            goto beyond_hope;
        }
        ISH_LOG("Segment %u loaded successfully", i);

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
    addr_t dynamic_addr = 0;  // _DYNAMIC section address for x1

    // Find PT_DYNAMIC in main executable (used if no interpreter)
    for (unsigned i = 0; i < header.phent_count; i++) {
        if (ph[i].type == PT_DYNAMIC) {
            dynamic_addr = bias + ph[i].vaddr;
            printk("[exec] Found PT_DYNAMIC in main executable at 0x%llx\n", (unsigned long long)dynamic_addr);
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
        }
        entry = interp_base + interp_header.entry_point;
        
        // For dynamically linked executables, x1 must point to loader's _DYNAMIC
        dynamic_addr = 0;
        addr_t interp_dyn_fileoffset = 0;
        for (int i = 0; i < interp_header.phent_count; i++) {
            if (interp_ph[i].type == PT_DYNAMIC) {
                dynamic_addr = interp_base + interp_ph[i].vaddr;
                interp_dyn_fileoffset = interp_ph[i].offset;
                printk("[exec] Found PT_DYNAMIC in interpreter at 0x%llx (using this for x1)\n", (unsigned long long)dynamic_addr);
                printk("[exec]   file offset=0x%llx, vaddr=0x%llx, interp_base=0x%llx\n",
                       (unsigned long long)interp_ph[i].offset,
                       (unsigned long long)interp_ph[i].vaddr,
                       (unsigned long long)interp_base);
                break;
            }
        }
        
        // Dump _DYNAMIC table from memory
        if (dynamic_addr != 0) {
            printk("[exec] DUMPING _DYNAMIC table at 0x%llx:\n", (unsigned long long)dynamic_addr);
            write_wrunlock(&current->mem->lock);  // Unlock for user_get
            for (int i = 0; i < 20; i++) {  // Dump first 20 entries
                uint64_t tag, val;
                if (user_get(dynamic_addr + i*16, tag) == 0 && 
                    user_get(dynamic_addr + i*16 + 8, val) == 0) {
                    printk("[exec]   _DYNAMIC[%d]: tag=%llu (0x%llx) val=0x%llx\n", 
                           i, (unsigned long long)tag, (unsigned long long)tag, (unsigned long long)val);
                    if (tag == 0) break;  // DT_NULL terminator
                } else {
                    printk("[exec]   _DYNAMIC[%d]: failed to read\n", i);
                    break;
                }
            }
            write_wrlock(&current->mem->lock);  // Re-lock
        }
        
        // Log interpreter PT_LOAD segments
        printk("[exec] Interpreter PT_LOAD segments:\n");
        for (int i = 0; i < interp_header.phent_count; i++) {
            if (interp_ph[i].type == PT_LOAD) {
                addr_t seg_addr = interp_base + interp_ph[i].vaddr;
                addr_t seg_end = seg_addr + interp_ph[i].memsize;
                printk("[exec]   PT_LOAD: vaddr=0x%llx->0x%llx (memsize=0x%llx, filesize=0x%llx) flags=%s%s%s\n",
                       (unsigned long long)seg_addr,
                       (unsigned long long)seg_end,
                       (unsigned long long)interp_ph[i].memsize,
                       (unsigned long long)interp_ph[i].filesize,
                       (interp_ph[i].flags & PH_R) ? "R" : "",
                       (interp_ph[i].flags & PH_W) ? "W" : "",
                       (interp_ph[i].flags & PH_X) ? "X" : "");
            }
        }
    }

    // map vdso
    printk("[exec] About to map vdso\n");
    err = _ENOMEM;
    pages_t vdso_pages = sizeof(vdso_data) >> PAGE_BITS;
    // FIXME disgusting hack: musl's dynamic linker has a one-page hole, and
    // I'd rather not put the vdso in that hole. so find a two-page hole and
    // add one.
    page_t vdso_page = pt_find_hole(current->mem, vdso_pages + 1);
    if (vdso_page == BAD_PAGE)
        goto beyond_hope;
    vdso_page += 1;
    if ((err = pt_map(current->mem, vdso_page, vdso_pages, (void *) vdso_data, 0, 0)) < 0)
        goto beyond_hope;
    mem_pt(current->mem, vdso_page)->data->name = "[vdso]";
    current->mm->vdso = vdso_page << PAGE_BITS;
    addr_t vdso_entry = current->mm->vdso + ((struct elf_header *) vdso_data)->entry_point;
    printk("[exec] vdso mapped successfully at page %d\n", vdso_page);

    // map 3 empty "vvar" pages for VDSO compatibility
    page_t vvar_page = pt_find_hole(current->mem, VVAR_PAGES);
    if (vvar_page == BAD_PAGE)
        goto beyond_hope;
    if ((err = pt_map_nothing(current->mem, vvar_page, VVAR_PAGES, 0)) < 0)
        goto beyond_hope;
    mem_pt(current->mem, vvar_page)->data->name = "[vvar]";

    // STACK TIME!

    // Map sufficient stack pages to accommodate initial stack setup.
    // Stack grows downward from high addresses. We need space for:
    // - argv/envp strings and pointers
    // - auxv array (~320 bytes)
    // - platform string, random bytes
    // - alignment padding
    // - musl loader runtime stack usage (can be significant)
    // Total: ~1-2KB for args, but musl needs 20-40KB for initialization
    // Use 16 pages (64KB) to be safe.
    // Map pages 0xffff0-0xfffff (addresses 0xffff0000 - 0xffffffff).
    // Initial SP will be at 0xffffffff (top of mapped region).
    if ((err = pt_map_nothing(current->mem, 0xffff0, 16, P_WRITE | P_GROWSDOWN)) < 0)
        goto beyond_hope;
    
    // Map TCB (Thread Control Block) pages for TLS
    // aarch64 musl expects x3 to point to TCB at startup
    // TCB is placed just below the stack pages at 0xfffed-0xfffef
    // These pages are at 0xfffed000-0xfffeffff
    // We need 3 pages (12KB) for TCB + TLS data (musl can use offsets up to ~17KB)
    if ((err = pt_map_nothing(current->mem, 0xfffed, 3, P_WRITE)) < 0)
        goto beyond_hope;
    
    // that was the last memory mapping
    write_wrunlock(&current->mem->lock);
    // Start SP at 0xffffffff (top of mapped region, aligned to 16 bytes)
    // Stack grows down into 0xfffff, 0xffffe, 0xffffd as data is pushed.
    addr_t sp = 0xfffffff0ULL;
    const size_t stack_slot_size = sizeof(addr_t);
    // on 32-bit linux, there's 4 empty bytes at the very bottom of the stack.
    // on 64-bit linux, there's 8. make ptraceomatic happy. (a major theme in this file)
    sp -= sizeof(void *);

    err = _EFAULT;
    // first, copy stuff pointed to by argv/envp/auxv
    // filename, argc, argv
    addr_t file_addr = sp = copy_string(sp, file);
    if (sp == 0)
        goto beyond_hope;
    addr_t envp_addr = sp = args_copy(sp, envp);
    if (sp == 0)
        goto beyond_hope;
    current->mm->argv_end = sp;
    addr_t argv_addr = sp = args_copy(sp, argv);
    if (sp == 0)
        goto beyond_hope;
    current->mm->argv_start = sp;
    sp = align_stack(sp);

    addr_t platform_addr = sp = copy_string(sp, "aarch64");
    if (sp == 0)
        goto beyond_hope;
    // 16 random bytes so no system call is needed to seed a userspace RNG
    char random[16] = {};
    get_random(random, sizeof(random)); // if this fails, eh, no one's really using it
    addr_t random_addr = sp -= sizeof(random);
    if (user_put(sp, random))
        goto beyond_hope;

    // the way linux aligns the stack at this point is kinda funky
    // calculate how much space is needed for argv, envp, and auxv, subtract
    // that from sp, then align, then copy argv/envp/auxv from that down

    // declare elf aux now so we can know how big it is
    struct aux_ent aux[] = {
        {AX_SYSINFO, vdso_entry},
        {AX_SYSINFO_EHDR, current->mm->vdso},
        {AX_HWCAP, 0x00000000}, // suck that
        {AX_PAGESZ, PAGE_SIZE},
        {AX_CLKTCK, 0x64},
        {AX_PHDR, load_addr + header.prghead_off},
        {AX_PHENT, sizeof(struct prg_header)},
        {AX_PHNUM, header.phent_count},
        {AX_BASE, interp_base},
        {AX_FLAGS, 0},
        {AX_ENTRY, bias + header.entry_point},
        {AX_UID, 0},
        {AX_EUID, 0},
        {AX_GID, 0},
        {AX_EGID, 0},
        {AX_SECURE, 0},
        {AX_RANDOM, random_addr},
        {AX_HWCAP2, 0}, // suck that too
        {AX_EXECFN, file_addr},
        {AX_PLATFORM, platform_addr},
        {0, 0}
    };
    // AArch64 user stacks are LP64: argc/argv/envp slots are 64-bit wide.
    sp -= ((argv.count + 1) + (envp.count + 1) + 1) * stack_slot_size;
    sp -= sizeof(aux);
    sp &=~ 0xf;

    // now copy down, start using p so sp is preserved
    addr_t p = sp;

    // argc
    if (user_put(p, argv.count))
        return _EFAULT;
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
    if (user_put(p, aux))
        goto beyond_hope;
    p += sizeof(aux);
    current->mm->auxv_end = p;

    current->mm->stack_start = sp;

    // Initialize CPU state properly before setting up registers
    // This zeros all X registers, PSTATE, and other state to prevent garbage values
    // CRITICAL: Save and restore mmu pointer since a64_cpu_init zeros all fields
    struct mmu *saved_mmu = current->cpu.mmu;
    a64_cpu_init(&current->cpu);
    current->cpu.mmu = saved_mmu;

    current->cpu.sp = sp;
    current->cpu.pc = entry;
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
    addr_t tcb_base = 0xfffed000;  // Start of mapped TCB pages (3 pages = 12KB)
    a64_setup_tls_area(&current->cpu, tcb_base);
    
    // Correct AArch64 musl startup: pass stack pointer in x0
    current->cpu.x[0] = sp;  // Points to argc on stack
    current->cpu.x[1] = dynamic_addr;   // _DYNAMIC - address of PT_DYNAMIC section
    // x[2-7] already zeroed by a64_cpu_init()
    // x[8-30] also zeroed by a64_cpu_init()
    
    printk("[exec] CPU REGISTERS INITIALIZED - about to start execution\n");
    printk("[exec] x0=%llu (argc), x1=0x%llx (argv), x2=0x%llx (envp), x3=0x%llx (auxv)\n",
           (unsigned long long)current->cpu.x[0],
           (unsigned long long)current->cpu.x[1],
           (unsigned long long)current->cpu.x[2],
           (unsigned long long)current->cpu.x[3]);
    printk("[exec] sp=0x%llx, pc=0x%llx, entry=0x%llx\n",
           (unsigned long long)current->cpu.sp,
           (unsigned long long)current->cpu.pc,
           (unsigned long long)entry);
    
    ISH_LOG("aarch64 init: x0=%llu (argc), x1=%llx (argv), x2=%llx (envp), x3=%llx (auxv), sp=%llx, pc=%llx",
            (unsigned long long)current->cpu.x[0],
            (unsigned long long)current->cpu.x[1],
            (unsigned long long)current->cpu.x[2],
            (unsigned long long)current->cpu.x[3],
            (unsigned long long)current->cpu.sp,
            (unsigned long long)current->cpu.pc);
    
    // aarch64 PSTATE (no eflags register)
    // current->cpu.eflags = 0;

    printk("[exec] elf_exec COMPLETING SUCCESSFULLY, err=0\n");
    err = 0;
out_free_interp:
    if (interp_name != NULL)
        free(interp_name);
    if (interp_fd != NULL && !IS_ERR(interp_fd))
        fd_close(interp_fd);
    if (interp_ph != NULL)
        free(interp_ph);
out_free_ph:
    free(ph);
    printk("[exec] elf_exec RETURNING err=%d\n", err);
    return err;

beyond_hope:
    // TODO force sigsegv
    write_wrunlock(&current->mem->lock);
    goto out_free_interp;
}

static size_t args_size(struct exec_args args) {
    const char *args_end = args.args;
    for (size_t i = 0; i < args.count; i++) {
        args_end += strlen(args_end) + 1;
    }
    // don't forget the very last null terminator
    assert(args_end[0] == '\0');
    args_end++;
    return args_end - args.args;
}

static inline addr_t align_stack(addr_t sp) {
    return sp &~ 0xf;
}

static inline addr_t copy_string(addr_t sp, const char *string) {
    sp -= strlen(string) + 1;
    if (user_write_string(sp, string))
        return 0;
    return sp;
}

static inline addr_t args_copy(addr_t sp, struct exec_args args) {
    size_t size = args_size(args);
    sp -= size;
    if (user_write(sp, args.args, size))
        return 0;
    return sp;
}

static inline ssize_t user_strlen(addr_t p) {
    size_t i = 0;
    char c;
    do {
        if (user_get(p + i, c))
            return -1;
        i++;
    } while (c != '\0');
    return i - 1;
}

static inline int user_memset(addr_t start, byte_t val, addr_t len) {
    while (len--)
        if (user_put(start++, val))
            return 1;
    return 0;
}

static int format_exec(struct fd *fd, const char *file, struct exec_args argv, struct exec_args envp) {
    int err = elf_exec(fd, file, argv, envp);
    if (err != _ENOEXEC)
        return err;
    // other formats would go here
    return _ENOEXEC;
}

// Execute a text file that contains an interpreter path (like /bin/busybox)
// This handles files that are just a path to the interpreter, not a shebang
static int text_interpreter_exec(struct fd *fd, const char *file, struct exec_args argv, struct exec_args envp) {
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
        return _ENOEXEC;  // It's a shebang, handled elsewhere
    if (size >= 4 && (header[0] == 0x7f && header[1] == 'E' && header[2] == 'L' && header[3] == 'F'))
        return _ENOEXEC;  // It's an ELF binary
    
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
        return _ENOEXEC;  // Not a valid path
    
    // Remove trailing whitespace
    char *end = interpreter + strlen(interpreter) - 1;
    while (end > interpreter && (*end == ' ' || *end == '\t' || *end == '\r'))
        *end-- = '\0';
    
    printk("[exec] Text interpreter exec: %s -> %s\n", file, interpreter);
    
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
    struct exec_args new_argv = {.args = new_argv_buf};
    
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

static int shebang_exec(struct fd *fd, const char *file, struct exec_args argv, struct exec_args envp) {
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
    struct exec_args new_argv = {.args = new_argv_buf};
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

int __do_execve(const char *file, struct exec_args argv, struct exec_args envp) {
    ISH_LOG("__do_execve: opening %s", file);
    
    printk("[iSH] EXEC: file=%s argc=%zu\n", file, argv.count);
    
    struct fd *fd = generic_open(file, O_RDONLY, 0);
    if (IS_ERR(fd)) {
        ISH_LOG_ERROR("generic_open failed for %s: %d", file, PTR_ERR(fd));
        printk("[iSH] EXEC: generic_open failed: %d\n", PTR_ERR(fd));
        return PTR_ERR(fd);
    }
    ISH_LOG("generic_open succeeded for %s", file);
    printk("[iSH] EXEC: generic_open succeeded\n");

    struct statbuf stat;
    int err = fd->mount->fs->fstat(fd, &stat);
    if (err < 0) {
        printk("[iSH] EXEC: fstat failed: %d\n", err);
        fd_close(fd);
        return err;
    }
    printk("[iSH] EXEC: fstat ok, mode=%o size=%llu\n", stat.mode, (unsigned long long)stat.size);

    // if nobody has permission to execute, it should be safe to not execute
    if (!(stat.mode & 0111)) {
        // TEMPORARY: Allow execution even without execute permissions
        printk("[iSH] EXEC: WARNING no exec perms (mode=%o), allowing\n", stat.mode);
    }

    // Debug: Read actual bytes from file
    char debug_buf[17] = {0};
    fd->ops->lseek(fd, 0, LSEEK_SET);
    ssize_t debug_read = fd->ops->read(fd, debug_buf, 16);
    fd->ops->lseek(fd, 0, LSEEK_SET);
    
    printk("[iSH] EXEC: Read %zd bytes\n", debug_read);
    printk("[iSH] EXEC: BYTES: %02x %02x %02x %02x %02x %02x %02x %02x\n",
           (unsigned char)debug_buf[0], (unsigned char)debug_buf[1], 
           (unsigned char)debug_buf[2], (unsigned char)debug_buf[3],
           (unsigned char)debug_buf[4], (unsigned char)debug_buf[5],
           (unsigned char)debug_buf[6], (unsigned char)debug_buf[7]);
    
    err = format_exec(fd, file, argv, envp);
    printk("[iSH] EXEC: format_exec returned %d\n", err);
    
    if (err == _ENOEXEC) {
        err = shebang_exec(fd, file, argv, envp);
        printk("[iSH] EXEC: shebang_exec returned %d\n", err);
    }
    if (err == _ENOEXEC) {
        err = text_interpreter_exec(fd, file, argv, envp);
        printk("[iSH] EXEC: text_interpreter_exec returned %d\n", err);
    }
    
    if (err == _ENOEXEC) {
        printk("[iSH] EXEC: FAILED - All methods failed\n");
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
        send_signal(current, SIGTRAP_, (struct siginfo_) {
            .code = SI_USER_,
            .kill.pid = current->pid,
            .kill.uid = current->uid,
        });
        unlock(&pids_lock);
    }

    return 0;
}

int do_execve(const char *file, size_t argc, const char *argv_p, const char *envp_p) {
    struct exec_args argv = {.count = argc, .args = argv_p};
    struct exec_args envp = {.args = envp_p};
    while (*envp_p != '\0') {
        envp_p += strlen(envp_p) + 1;
        envp.count++;
    }
    return __do_execve(file, argv, envp);
}

static ssize_t user_read_string_array(addr_t addr, char *buf, size_t max) {
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

dword_t sys_execve(addr_t filename_addr, addr_t argv_addr, addr_t envp_addr) {
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
