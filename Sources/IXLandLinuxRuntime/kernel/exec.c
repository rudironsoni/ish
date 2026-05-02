#import <IXLandLinuxRuntime/kernel/memory.h>
#import <IXLandLinuxRuntime/kernel/page_map.h>
#import <IXLandLinuxRuntime/kernel/signal.h>
#import <IXLandLinuxRuntime/kernel/task.h>
#import <IXLandLinuxRuntime/kernel/vma.h>
#define _GNU_SOURCE
#import <IXLandInstrumentationTracing/trace.h>
#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>
#import <IXLandLinuxRuntime/emu/aarch64/tls.h>
#import <IXLandLinuxRuntime/fs/fd.h>
#import <IXLandLinuxRuntime/kernel/calls.h>
#import <IXLandLinuxRuntime/kernel/elf.h>
#import <IXLandLinuxRuntime/kernel/errno.h>
#import <IXLandLinuxRuntime/kernel/personality.h>
#import <IXLandLinuxRuntime/kernel/random.h>
#import <IXLandLinuxRuntime/kernel/vdso.h>
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
static inline int user_memset(addr_t start, uint8_t val, addr_t len);
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

static void trace_elf_header_role_checkpoint(const char *name, const char *role,
                                             struct elf_header *header)
{
    char role_buf[24];
    char class_buf[8];
    char machine_buf[8];
    char entry_buf[24];
    char type_buf[8];

    snprintf(role_buf, sizeof(role_buf), "%s", role ? role : "unknown");
    snprintf(class_buf, sizeof(class_buf), "%u", header->bitness);
    snprintf(machine_buf, sizeof(machine_buf), "%u", header->machine);
    snprintf(entry_buf, sizeof(entry_buf), "0x%llx", (unsigned long long)header->entry_point);
    snprintf(type_buf, sizeof(type_buf), "%u", header->type);

    trace_attribute_t attrs[] = {
        { "role", role_buf },       { "elf_class", class_buf }, { "elf_machine", machine_buf },
        { "elf_entry", entry_buf }, { "elf_type", type_buf },
    };

    (void)trace_begin_interval(TRACE_ORIGIN_TASK, name, attrs, sizeof(attrs) / sizeof(attrs[0]));

    static int budget = 4;
    if (budget > 0) {
        char ev[224];
        snprintf(ev, sizeof(ev),
                 "loader.%s_elf.header=class:%u,machine:%u,entry:0x%llx,e_ident:7f454c46%02x%02x%"
                 "02x%02x%02x",
                 role ? role : "unknown", header->bitness, header->machine,
                 (unsigned long long)header->entry_point, (unsigned)header->bitness,
                 (unsigned)header->endian, (unsigned)header->elfversion1, (unsigned)header->abi,
                 (unsigned)header->abi_version);
        trace_record_event(TRACE_ORIGIN_KERNEL, ev);
        budget--;
    }
}

static const char *trace_mem_object_kind_name(enum mem_object_kind kind)
{
    switch (kind) {
    case MEM_OBJ_RAM:
        return "ram";
    case MEM_OBJ_FILE:
        return "file";
    case MEM_OBJ_VDSO:
        return "vdso";
    case MEM_OBJ_SPECIAL:
        return "special";
    default:
        return "unknown";
    }
}

static bool trace_read_guest_bytes_locked(addr_t guest_addr, uint8_t *out, size_t len)
{
    if (current == NULL || current->mem == NULL || out == NULL)
        return false;

    for (size_t i = 0; i < len; i++) {
        addr_t addr = guest_addr + i;
        struct page_desc *desc = page_map_lookup(&current->mem->pages, PAGE(addr));
        if (desc == NULL || desc->obj == NULL || desc->obj->host_base == NULL)
            return false;

        size_t host_off = desc->offset + PGOFFSET(addr);
        if (host_off >= desc->obj->host_size)
            return false;

        const uint8_t *host = (const uint8_t *)desc->obj->host_base;
        out[i] = host[host_off];
    }
    return true;
}

static void trace_loader_page_zero_locked(const char *name)
{
    struct page_desc *desc0 = NULL;
    struct mem_object *obj0 = NULL;
    bool mapped = false;
    uint8_t bytes[32] = { 0 };
    bool have_bytes = false;

    if (current && current->mem) {
        desc0 = page_map_lookup(&current->mem->pages, 0);
        if (desc0 && desc0->obj) {
            mapped = true;
            obj0 = desc0->obj;
            have_bytes = trace_read_guest_bytes_locked(0, bytes, sizeof(bytes));
        }
    }

    char mapped_buf[8];
    char kind_buf[16];
    char obj_name_buf[96];
    char host_base_buf[32];
    char host_size_buf[32];
    char file_off_buf[32];
    char page_desc_off_buf[32];
    char bytes_buf[96];
    char is_elf_buf[8];
    char elf_class_buf[8];

    int is_elf = 0;
    int elf_class = 0;

    snprintf(mapped_buf, sizeof(mapped_buf), "%d", mapped ? 1 : 0);
    snprintf(kind_buf, sizeof(kind_buf), "%s",
             obj0 ? trace_mem_object_kind_name(obj0->kind) : "none");
    snprintf(obj_name_buf, sizeof(obj_name_buf), "%s", (obj0 && obj0->name) ? obj0->name : "none");
    snprintf(host_base_buf, sizeof(host_base_buf), "%p", obj0 ? obj0->host_base : NULL);
    snprintf(host_size_buf, sizeof(host_size_buf), "%zu", obj0 ? obj0->host_size : 0);
    snprintf(file_off_buf, sizeof(file_off_buf), "0x%zx", obj0 ? obj0->file_offset : 0);
    snprintf(page_desc_off_buf, sizeof(page_desc_off_buf), "0x%zx", desc0 ? desc0->offset : 0);

    if (!have_bytes) {
        snprintf(bytes_buf, sizeof(bytes_buf), "unavailable");
    } else {
        if (bytes[0] == 0x7f && bytes[1] == 'E' && bytes[2] == 'L' && bytes[3] == 'F') {
            is_elf = 1;
            elf_class = bytes[4];
        }
        size_t pos = 0;
        for (size_t i = 0; i < sizeof(bytes); i++) {
            int written = snprintf(bytes_buf + pos, sizeof(bytes_buf) - pos, "%02x", bytes[i]);
            if (written <= 0 || (size_t)written >= sizeof(bytes_buf) - pos)
                break;
            pos += (size_t)written;
        }
    }

    snprintf(is_elf_buf, sizeof(is_elf_buf), "%d", is_elf);
    snprintf(elf_class_buf, sizeof(elf_class_buf), "%d", elf_class);

    trace_attribute_t attrs[] = {
        { "page0_mapped", mapped_buf },
        { "backing_kind", kind_buf },
        { "backing_name", obj_name_buf },
        { "host_base", host_base_buf },
        { "host_size", host_size_buf },
        { "file_offset", file_off_buf },
        { "page_desc_offset", page_desc_off_buf },
        { "bytes_0_1f_hex", bytes_buf },
        { "page0_is_elf", is_elf_buf },
        { "page0_elf_class", elf_class_buf },
    };

    (void)trace_begin_interval(TRACE_ORIGIN_KERNEL, name, attrs, sizeof(attrs) / sizeof(attrs[0]));

    static int budget = 1;
    if (budget <= 0)
        return;

    char ev[320];
    void *host_ptr0 = NULL;
    if (obj0 && obj0->host_base && desc0 && desc0->offset < obj0->host_size)
        host_ptr0 = (void *)((uint8_t *)obj0->host_base + desc0->offset);

    snprintf(ev, sizeof(ev), "loader.page0.mapped=mapped:%s,guest_page:0x0,host_ptr:%p",
             mapped ? "yes" : "no", host_ptr0);
    trace_record_event(TRACE_ORIGIN_KERNEL, ev);

    snprintf(ev, sizeof(ev),
             "mm.page0.state.loader_final=mapped:%s,guest_page:0x0,backing:%s,host_ptr:%p,"
             "range:[0x%llx..0x%llx],task:%p,mm:%p,mem:%p,cpu:%p,tlb:%p,page_desc:%p,obj:%p",
             mapped ? "yes" : "no",
             (obj0 && obj0->name) ? obj0->name
                                  : (obj0 ? trace_mem_object_kind_name(obj0->kind) : "none"),
             host_ptr0, 0ULL, (unsigned long long)(PAGE_SIZE - 1), (void *)current,
             current ? (void *)current->mm : NULL, current ? (void *)current->mem : NULL,
             current ? (void *)&current->cpu : NULL, NULL, (void *)desc0, (void *)obj0);
    trace_record_event(TRACE_ORIGIN_KERNEL, ev);

    snprintf(ev, sizeof(ev),
             "loader.page0.backing_object=obj:%p,kind:%s,name:%s,fd:%p,host_base:%p,host_size:%zu,"
             "file_off:0x%zx,page_desc_off:0x%zx",
             (void *)obj0, obj0 ? trace_mem_object_kind_name(obj0->kind) : "none",
             (obj0 && obj0->name) ? obj0->name : "none", obj0 ? (void *)obj0->fd : NULL,
             obj0 ? obj0->host_base : NULL, obj0 ? obj0->host_size : 0,
             obj0 ? obj0->file_offset : 0, desc0 ? desc0->offset : 0);
    trace_record_event(TRACE_ORIGIN_KERNEL, ev);

    if (have_bytes) {
        char low16[48];
        char high16[48];
        for (size_t i = 0; i < 16; i++) {
            snprintf(low16 + i * 2, sizeof(low16) - i * 2, "%02x", bytes[i]);
            snprintf(high16 + i * 2, sizeof(high16) - i * 2, "%02x", bytes[i + 16]);
        }
        snprintf(ev, sizeof(ev), "loader.page0.bytes=range:0x00-0x0f,hex:%s", low16);
        trace_record_event(TRACE_ORIGIN_KERNEL, ev);
        snprintf(ev, sizeof(ev), "loader.page0.bytes=range:0x10-0x1f,hex:%s", high16);
        trace_record_event(TRACE_ORIGIN_KERNEL, ev);
    } else {
        trace_record_event(TRACE_ORIGIN_KERNEL, "loader.page0.bytes=unavailable");
    }

    budget--;
}

static void trace_loader_bias_checkpoint(const char *name, addr_t main_bias, addr_t interp_bias,
                                         addr_t main_first_vaddr, addr_t main_first_file_off,
                                         addr_t interp_first_vaddr, addr_t interp_first_file_off,
                                         const char *interp_name)
{
    char main_bias_buf[32];
    char interp_bias_buf[32];
    char interp_name_buf[160];
    char personality_buf[32];

    snprintf(main_bias_buf, sizeof(main_bias_buf), "0x%llx", (unsigned long long)main_bias);
    snprintf(interp_bias_buf, sizeof(interp_bias_buf), "0x%llx", (unsigned long long)interp_bias);
    snprintf(interp_name_buf, sizeof(interp_name_buf), "%s", interp_name ? interp_name : "none");
    snprintf(personality_buf, sizeof(personality_buf), "0x%x",
             current && current->group ? current->group->personality : 0);

    trace_attribute_t attrs[] = {
        { "main_load_bias", main_bias_buf },
        { "interp_load_bias", interp_bias_buf },
        { "interp_path", interp_name_buf },
        { "personality", personality_buf },
    };

    (void)trace_begin_interval(TRACE_ORIGIN_KERNEL, name, attrs, sizeof(attrs) / sizeof(attrs[0]));

    static int budget = 4;
    if (budget <= 0)
        return;

    char ev[320];
    uint32_t persona = (current && current->group) ? current->group->personality : 0;

    snprintf(ev, sizeof(ev),
             "loader.main_load_bias=bias:0x%llx,first_pt_load_vaddr:0x%llx,first_pt_load_file_off:"
             "0x%llx",
             (unsigned long long)main_bias, (unsigned long long)main_first_vaddr,
             (unsigned long long)main_first_file_off);
    trace_record_event(TRACE_ORIGIN_KERNEL, ev);

    snprintf(ev, sizeof(ev),
             "loader.interp_load_bias=bias:0x%llx,first_pt_load_vaddr:0x%llx,first_pt_load_file_"
             "off:0x%llx,present:%s",
             (unsigned long long)interp_bias, (unsigned long long)interp_first_vaddr,
             (unsigned long long)interp_first_file_off,
             (interp_name && interp_name[0]) ? "yes" : "no");
    trace_record_event(TRACE_ORIGIN_KERNEL, ev);

    snprintf(ev, sizeof(ev), "loader.interpreter_path=path:%s",
             (interp_name && interp_name[0]) ? interp_name : "none");
    trace_record_event(TRACE_ORIGIN_KERNEL, ev);

    snprintf(ev, sizeof(ev),
             "loader.personality.flags=raw:0x%x,addr_no_randomize:%d,mmap_page_zero_like:%d",
             persona, (persona & ADDR_NO_RANDOMIZE_) ? 1 : 0, 0);
    trace_record_event(TRACE_ORIGIN_KERNEL, ev);

    budget--;
}

static void trace_auxv_essentials_checkpoint(const char *name, struct aux_ent *aux)
{
    addr_t at_phdr = 0;
    addr_t at_base = 0;
    addr_t at_entry = 0;
    addr_t at_phnum = 0;
    addr_t at_phent = 0;

    for (size_t i = 0; aux[i].type != 0; i++) {
        switch (aux[i].type) {
        case AX_PHDR:
            at_phdr = aux[i].value;
            break;
        case AX_BASE:
            at_base = aux[i].value;
            break;
        case AX_ENTRY:
            at_entry = aux[i].value;
            break;
        case AX_PHNUM:
            at_phnum = aux[i].value;
            break;
        case AX_PHENT:
            at_phent = aux[i].value;
            break;
        default:
            break;
        }
    }

    char at_phdr_buf[32];
    char at_base_buf[32];
    char at_entry_buf[32];
    char at_phnum_buf[32];
    char at_phent_buf[32];

    snprintf(at_phdr_buf, sizeof(at_phdr_buf), "0x%llx", (unsigned long long)at_phdr);
    snprintf(at_base_buf, sizeof(at_base_buf), "0x%llx", (unsigned long long)at_base);
    snprintf(at_entry_buf, sizeof(at_entry_buf), "0x%llx", (unsigned long long)at_entry);
    snprintf(at_phnum_buf, sizeof(at_phnum_buf), "%llu", (unsigned long long)at_phnum);
    snprintf(at_phent_buf, sizeof(at_phent_buf), "%llu", (unsigned long long)at_phent);

    trace_attribute_t attrs[] = {
        { "AT_PHDR", at_phdr_buf },   { "AT_BASE", at_base_buf },   { "AT_ENTRY", at_entry_buf },
        { "AT_PHNUM", at_phnum_buf }, { "AT_PHENT", at_phent_buf },
    };

    (void)trace_begin_interval(TRACE_ORIGIN_KERNEL, name, attrs, sizeof(attrs) / sizeof(attrs[0]));

    static int budget = 1;
    if (budget > 0) {
        char ev[256];
        snprintf(ev, sizeof(ev),
                 "loader.auxv.core=AT_BASE:0x%llx,AT_PHDR:0x%llx,AT_ENTRY:0x%llx,AT_PHNUM:%llu,AT_"
                 "PHENT:%llu",
                 (unsigned long long)at_base, (unsigned long long)at_phdr,
                 (unsigned long long)at_entry, (unsigned long long)at_phnum,
                 (unsigned long long)at_phent);
        trace_record_event(TRACE_ORIGIN_KERNEL, ev);
        budget--;
    }
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

    trace_emit_u32(TRACE_EVENT_BLOCK_COMPILE_START, 0x1002, (uint32_t)header.prghead_off);
    if (fd->ops->lseek(fd, header.prghead_off, LSEEK_SET) < 0) {
        trace_emit_u32(TRACE_EVENT_BLOCK_COMPILE_START, 0x1003, 1);
        free(ph);
        return _EIO;
    }
    trace_emit_u32(TRACE_EVENT_BLOCK_COMPILE_START, 0x1004, (uint32_t)ph_size);
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

static void trace_interp_phdr_event(int index, const struct prg_header *ph)
{
    static int budget = 16;
    if (budget <= 0 || ph == NULL)
        return;
    char ev[320];
    snprintf(ev, sizeof(ev),
             "loader.interp.phdr=index:%d,p_type:%u,p_offset:0x%llx,p_vaddr:0x%llx,p_filesz:%llu,p_"
             "memsz:%llu,p_align:0x%llx,flags:0x%x",
             index, ph->type, (unsigned long long)ph->offset, (unsigned long long)ph->vaddr,
             (unsigned long long)ph->filesize, (unsigned long long)ph->memsize,
             (unsigned long long)ph->alignment, ph->flags);
    trace_record_event(TRACE_ORIGIN_KERNEL, ev);
    budget--;
}

static void trace_interp_pt_load_map_event(int index, const struct prg_header *ph,
                                           addr_t interp_base)
{
    static int budget = 12;
    if (budget <= 0 || ph == NULL)
        return;
    addr_t chosen_start = interp_base + ph->vaddr;
    addr_t chosen_end = chosen_start + ph->memsize;
    addr_t aligned_guest_vaddr = PAGE(chosen_start) << PAGE_BITS;
    addr_t aligned_file_off = ph->offset - PGOFFSET(chosen_start);
    addr_t page_start = PAGE(chosen_start);
    addr_t page_end = PAGE(chosen_end);

    char ev[384];
    snprintf(
        ev, sizeof(ev),
        "loader.interp.pt_load.map=index:%d,guest_map_start:0x%llx,guest_map_end:0x%llx,page_"
        "aligned_file_off:0x%llx,page_aligned_guest_vaddr:0x%llx,guest_page_range:[0x%llx..0x%llx]",
        index, (unsigned long long)chosen_start, (unsigned long long)chosen_end,
        (unsigned long long)aligned_file_off, (unsigned long long)aligned_guest_vaddr,
        (unsigned long long)page_start, (unsigned long long)page_end);
    trace_record_event(TRACE_ORIGIN_KERNEL, ev);
    budget--;
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
    if (ph.flags & PH_X)
        flags |= P_EXEC;

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

    // PROOF: Verify the mapped memory content at key offsets for interpreter first PT_LOAD
    // This proves whether the file content was correctly mapped or if mmap returned wrong data
    // The interpreter's first PT_LOAD maps guest page 0x1 (0x1000) from file offset 0
    if (start_page == 1 && filesize > 0x40) {
        struct page_desc *verify_desc = page_map_lookup(&current->mem->pages, start_page);
        if (verify_desc && verify_desc->obj && verify_desc->obj->host_base) {
            uint16_t *host_short = (uint16_t *)((char *)verify_desc->obj->host_base + 0x36);
            char ev[256];
            snprintf(ev, sizeof(ev),
                     "loader.interp.first_pt_load_verify=bias:0x%lx,host_base:%p,bytes_at_0x36:0x%04x,expected:0x0038",
                     (unsigned long)bias, verify_desc->obj->host_base, (unsigned int)*host_short);
            trace_record_event(TRACE_ORIGIN_KERNEL, ev);

            uint32_t *host_int = (uint32_t *)((char *)verify_desc->obj->host_base + 0x38);
            snprintf(ev, sizeof(ev),
                     "loader.interp.first_pt_load_verify2=bytes_at_0x38:0x%08x",
                     (unsigned int)*host_int);
            trace_record_event(TRACE_ORIGIN_KERNEL, ev);
        }
    }

    // TODO find a better place for these to avoid code duplication
    struct page_desc *first_desc = page_map_lookup(&current->mem->pages, start_page);
    if (first_desc == NULL || first_desc->obj == NULL) {
        return _ENOMEM;
    }
    first_desc->obj->fd = fd_retain(fd);
    first_desc->obj->file_offset = map_offset;

    if (memsize > filesize) {
        // put zeroes between addr + filesize and addr + memsize, call that bss
        uint32_t bss_size = (uint32_t)(memsize - filesize);

        // first zero the tail from the end of the file mapping to the end
        // of the load entry or the end of the page, whichever comes first
        addr_t file_end = addr + filesize;
        uint32_t tail_size = PAGE_SIZE - PGOFFSET(file_end);
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

static addr_t align_up_addr(addr_t value, addr_t alignment)
{
    if (alignment <= 1)
        return value;
    return (value + alignment - 1) & ~(alignment - 1);
}

static addr_t elf_load_alignment(struct elf_header *header, struct prg_header *ph)
{
    addr_t alignment = PAGE_SIZE;
    for (int i = 0; i < header->phent_count; i++) {
        if (ph[i].type == PT_LOAD && ph[i].alignment > alignment)
            alignment = ph[i].alignment;
    }
    return alignment;
}

static addr_t align_elf_load_bias(struct elf_header *header, struct prg_header *ph, addr_t bias)
{
    return align_up_addr(bias, elf_load_alignment(header, ph));
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
    addr_t base = 0;
    if (first != NULL) {
        addr_t alignment = elf_load_alignment(header, ph);
        pages_t a = PAGE_ROUND_UP(last->vaddr + last->memsize);
        pages_t b = PAGE(first->vaddr);
        size = a - b;
        page_t min_page = PAGE(first->vaddr);
        page_t align_slack = PAGE_ROUND_UP(alignment);
        page_t hole_page =
            vma_tree_find_hole_above(&current->mem->vmas, min_page, size + align_slack);
        if (hole_page == (page_t)-1)
            hole_page = min_page;
        base = (hole_page << PAGE_BITS) - first->vaddr;
        base = align_up_addr(base, alignment);
    }
    return base;
}

static int elf_exec(struct fd *fd, const char *file, struct exec_args argv, struct exec_args envp)
{
    int err = 0;
    trace_exec_checkpoint("task.proof.do_execve.before_elf_exec", err);

    // DIAGNOSTIC: Confirm elf_exec is reached
    trace_record_event(TRACE_ORIGIN_KERNEL, "loader.elf_exec.reached");

    // Trace: Entry to elf_exec (POINT 4 - elf_exec_entry)
    trace_emit_exec_path_boundary((uint64_t)current, current->pid, (uint64_t)current->mm,
                                  (uint64_t)current->mem, EXEC_PATH_ELF_EXEC_ENTRY, 0);

    // read the headers
    struct elf_header header;
    if ((err = read_header(fd, &header)) < 0) {
        return err;
    }
    trace_elf_header_role_checkpoint("task.proof.loader.elf_header", "main", &header);
    trace_record_event(TRACE_ORIGIN_KERNEL, "loader.main_elf.header=accepted:1");

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

        interp_name = malloc(ph[i].filesize + 1);
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

        // PT_INTERP semantic validation: path must be null-terminated within p_filesz bytes per
        // Linux ELF spec
        size_t term_idx = ph[i].filesize;
        for (size_t j = 0; j < ph[i].filesize; j++) {
            if (interp_name[j] == '\0') {
                term_idx = j;
                break;
            }
        }
        if (term_idx == ph[i].filesize) {
            // No null terminator found within PT_INTERP payload - malformed ELF
            err = _EINVAL;
            goto out_free_interp;
        }

        // Defensive terminator at p_filesz boundary (already null-terminated per validation above)
        interp_name[ph[i].filesize] = '\0';

        // open interpreter and read headers
        interp_fd = generic_open(interp_name, O_RDONLY, 0);
        {
            char ev[256];
            int open_err = IS_ERR(interp_fd) ? (int)PTR_ERR(interp_fd) : 0;
            snprintf(ev, sizeof(ev), "loader.interp.open.result=err:%d,path:%s,present:1",
                     open_err, interp_name ? interp_name : "none");
            trace_record_event(TRACE_ORIGIN_KERNEL, ev);
        }
        if (IS_ERR(interp_fd)) {
            err = (int)PTR_ERR(interp_fd);
            goto out_free_interp;
        }
        if ((err = read_header(interp_fd, &interp_header)) < 0) {
            if (err == _ENOEXEC)
                err = _ELIBBAD;
            goto out_free_interp;
        }
        trace_elf_header_role_checkpoint("task.proof.loader.elf_header", "interp", &interp_header);
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
    bool main_first_load_seen = false;
    addr_t main_first_load_vaddr = 0;
    addr_t main_first_load_off = 0;

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

        if (!main_first_load_seen) {
            main_first_load_seen = true;
            main_first_load_vaddr = ph[i].vaddr;
            main_first_load_off = ph[i].offset;
        }

        if (!load_addr_set && header.type == ELF_DYNAMIC) {
            // see giant comment in linux/fs/binfmt_elf.c, around line 950
            if (interp_name)
                bias = align_elf_load_bias(&header, ph, 0x56555000);
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

        // file is already a valid path string; pass directly to trace attributes
        snprintf(data_buf, sizeof(data_buf), "%p",
                 (void *)page_map_lookup(&current->mem->pages, PAGE(bias + ph[i].vaddr)));
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
            { "path", file },
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
            current->mm->start_brk = current->mm->brk = brk;
    }

    addr_t entry = bias + header.entry_point;
    addr_t interp_base = 0;
    addr_t dynamic_addr = 0; // _DYNAMIC section address for x1
    bool interp_first_load_seen = false;
    bool interp_lowest_pt_load_seen = false;
    addr_t interp_first_load_vaddr = 0;
    addr_t interp_first_load_off = 0;
    addr_t interp_dynamic_vaddr = 0;
    addr_t interp_lowest_pt_load_vaddr = 0;
    addr_t interp_lowest_pt_load_off = 0;

    // Find PT_DYNAMIC in main executable (used if no interpreter)
    for (unsigned i = 0; i < header.phent_count; i++) {
        if (ph[i].type == PT_DYNAMIC) {
            dynamic_addr = bias + ph[i].vaddr;
            break;
        }
    }
    {
        char ev[256];
        snprintf(ev, sizeof(ev), "loader.dynamic_addr=0x%llx,interp_present:%s",
                 (unsigned long long)dynamic_addr, interp_name ? "yes" : "no");
        trace_record_event(TRACE_ORIGIN_KERNEL, ev);
    }

    if (interp_name) {
        for (int i = 0; i < interp_header.phent_count; i++) {
            trace_interp_phdr_event(i, &interp_ph[i]);
            if (interp_ph[i].type == PT_DYNAMIC)
                interp_dynamic_vaddr = interp_ph[i].vaddr;
            if (interp_ph[i].type != PT_LOAD)
                continue;
            if (!interp_lowest_pt_load_seen ||
                interp_ph[i].vaddr < interp_lowest_pt_load_vaddr) {
                interp_lowest_pt_load_seen = true;
                interp_lowest_pt_load_vaddr = interp_ph[i].vaddr;
                interp_lowest_pt_load_off = interp_ph[i].offset;
            }
        }

        // map dat shit! interpreter edition
        interp_base = find_hole_for_elf(&interp_header, interp_ph);
        {
            addr_t interp_first_map =
                interp_base + (PAGE(interp_lowest_pt_load_vaddr) << PAGE_BITS);
            if (interp_first_map < PAGE_SIZE)
                interp_base = align_elf_load_bias(&interp_header, interp_ph, PAGE_SIZE);

            char ev[320];
            snprintf(
                ev, sizeof(ev),
                "loader.interp.bias.compute=first_pt_load_vaddr:0x%llx,first_pt_load_file_off:0x%"
                "llx,computed_load_bias:0x%llx,formula:interp_base+vaddr,interp_present_path:1",
                (unsigned long long)interp_lowest_pt_load_vaddr,
                (unsigned long long)interp_lowest_pt_load_off, (unsigned long long)interp_base);
            trace_record_event(TRACE_ORIGIN_KERNEL, ev);
        }

        for (int i = interp_header.phent_count - 1; i >= 0; i--) {
            if (interp_ph[i].type != PT_LOAD)
                continue;
            if (!interp_first_load_seen) {
                interp_first_load_seen = true;
                interp_first_load_vaddr = interp_ph[i].vaddr;
                interp_first_load_off = interp_ph[i].offset;
            }
            // musl's ld-musl self-relocates by writing to its own text segment.
            // The kernel normally maps the first PT_LOAD of the interpreter as
            // writable (PF_W) even if ELF flags say read+execute only, because
            // self-relocation requires writing to the GOT/text.  We do the same.
            struct prg_header interp_ph_entry = interp_ph[i];
            if (i == 0 || (interp_ph[i].flags & PH_X))
                interp_ph_entry.flags |= PH_W;
            if ((err = load_entry(interp_ph_entry, interp_base, interp_fd)) < 0)
                goto beyond_hope;
            trace_interp_pt_load_map_event(i, &interp_ph[i], interp_base);

            // Trace PT_LOAD mapping for APPSIM-004 diagnosis (interpreter)
            char role_buf[32] = "interpreter";
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

            // interp_name is now a valid runtime-owned string; pass directly
            snprintf(data_buf, sizeof(data_buf), "%p",
                     (void *)page_map_lookup(&current->mem->pages,
                                             PAGE(interp_base + interp_ph[i].vaddr)));
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
                { "path", interp_name },
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

// Note: x1/AT_DYNAMIC must point to the MAIN EXECUTABLE's _DYNAMIC, not the
// interpreter's. The dynamic linker uses this to find the main program's
// relocation table. dynamic_addr was already set from main's PT_DYNAMIC at
// lines 1015-1020. Do not overwrite it with interpreter's _DYNAMIC.

// Trace interpreter mapping for APPSIM-004 diagnosis
        char interp_base_buf[32];
        char interp_entry_buf[32];

        // interp_name is already a valid runtime-owned string; pass directly
        snprintf(interp_base_buf, sizeof(interp_base_buf), "0x%lx", (unsigned long)interp_base);
        snprintf(interp_entry_buf, sizeof(interp_entry_buf), "0x%lx", (unsigned long)entry);

        trace_attribute_t interp_attrs[] = {
            { "name", interp_name },
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
                struct page_desc *desc = page_map_lookup(&current->mem->pages, pg);
                if (desc && desc->obj && !desc->obj->name) {
                    desc->obj->name = "[interpreter]";
                }
            }
        }

        {
            struct page_desc *page0_desc = page_map_lookup(&current->mem->pages, 0);
            char ev[320];
            snprintf(ev, sizeof(ev),
                     "loader.interp.map.final=final_interp_base:0x%llx,final_interp_entry:0x%llx,"
                     "page0_mapped:%s,page0_backing:%s",
                     (unsigned long long)interp_base,
                     (unsigned long long)(interp_base + interp_header.entry_point),
                     page0_desc ? "yes" : "no",
                     (page0_desc && page0_desc->obj && page0_desc->obj->name)
                         ? page0_desc->obj->name
                         : "none");
            trace_record_event(TRACE_ORIGIN_KERNEL, ev);
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
                (void)seg_addr;
            }
        }
    }

    trace_loader_bias_checkpoint("task.proof.loader.biases", bias, interp_base,
                                 main_first_load_vaddr, main_first_load_off,
                                 interp_first_load_vaddr, interp_first_load_off, interp_name);

    // map vdso
    err = _ENOMEM;
    pages_t vdso_pages = sizeof(vdso_data) >> PAGE_BITS;
    pages_t aux_pages = vdso_pages + VVAR_PAGES + 2;
    page_t aux_region = pt_find_hole(current->mem, aux_pages);
    if (aux_region == BAD_PAGE)
        goto beyond_hope;

    // Never allow the vvar/vdso helper region to claim guest page 0.
    // Keep one guard page between vvar and vdso mappings.
    page_t vvar_page = aux_region + 1;
    page_t vdso_page = vvar_page + VVAR_PAGES + 1;

    bool vdso_elf_available = vdso_has_elf_image();
    if ((err = pt_map(current->mem, vdso_page, vdso_pages, (void *)vdso_data, 0, 0)) < 0)
        goto beyond_hope;
    page_map_lookup(&current->mem->pages, vdso_page)->obj->name = "[vdso]";
    current->mm->vdso = vdso_elf_available ? (vdso_page << PAGE_BITS) : 0;
    addr_t vdso_entry =
        vdso_elf_available ? current->mm->vdso + ((struct elf_header *)vdso_data)->entry_point : 0;

    // map empty "vvar" pages for VDSO compatibility
    if ((err = pt_map_nothing(current->mem, vvar_page, VVAR_PAGES, 0)) < 0)
        goto beyond_hope;
    page_map_lookup(&current->mem->pages, vvar_page)->obj->name = "[vvar]";

    trace_loader_page_zero_locked("task.proof.loader.page0");

// STACK TIME!

// Map sufficient stack pages to accommodate initial stack setup.
// AArch64 startup layout - two-sided budget inside mapped stack region
// Derived from USER_TOP with explicit upward headroom and downward reserve
#define USER_TOP          A64_USER_TOP               /* 48-bit VA = 256TB */
#define STARTUP_HEADROOM  ((addr_t)16 * 1024 * 1024) // 16 MB gap
#define STACK_MAPPED_SIZE ((addr_t)4 * 1024 * 1024)  // 4 MB mapped stack
#define TLS_TCB_SIZE      ((addr_t)256 * 1024)       // 256 KB
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

    struct aux_ent aux[24];
    size_t aux_count = 0;
#define ADD_AUX(type_, value_)                                                                    \
    do {                                                                                           \
        aux[aux_count++] = (struct aux_ent){ (type_), (value_) };                                  \
    } while (0)
    if (vdso_elf_available) {
        ADD_AUX(AX_SYSINFO, vdso_entry);
        ADD_AUX(AX_SYSINFO_EHDR, current->mm->vdso);
    }
    ADD_AUX(AX_HWCAP, 0x00000000);
    ADD_AUX(AX_PAGESZ, PAGE_SIZE);
    ADD_AUX(AX_CLKTCK, 0x64);
    ADD_AUX(AX_PHDR, load_addr + header.prghead_off);
    ADD_AUX(AX_PHENT, sizeof(struct prg_header));
    ADD_AUX(AX_PHNUM, header.phent_count);
    ADD_AUX(AX_BASE, interp_base);
    ADD_AUX(AX_FLAGS, 0);
    ADD_AUX(AX_ENTRY, bias + header.entry_point);
    ADD_AUX(AX_UID, 0);
    ADD_AUX(AX_EUID, 0);
    ADD_AUX(AX_GID, 0);
    ADD_AUX(AX_EGID, 0);
    ADD_AUX(AX_SECURE, 0);
    ADD_AUX(AX_RANDOM, random_addr);
    ADD_AUX(AX_HWCAP2, 0);
    ADD_AUX(AX_EXECFN, file_addr);
    ADD_AUX(AX_PLATFORM, platform_addr);
    ADD_AUX(0, 0);
#undef ADD_AUX
    size_t aux_size = aux_count * sizeof(aux[0]);
    {
        char ev[256];
        snprintf(ev, sizeof(ev),
                 "loader.auxv.at_base.write=value:0x%llx,source:interp_base,interp_present:%s,"
                 "reason:interpreter_base_for_dynamic_linker",
                 (unsigned long long)interp_base, interp_name ? "yes" : "no");
        trace_record_event(TRACE_ORIGIN_KERNEL, ev);
    }
    trace_auxv_essentials_checkpoint("task.proof.loader.auxv", aux);
    // AArch64 user stacks are LP64: argc/argv/envp slots are 64-bit wide.
    sp -= ((argv.count + 1) + (envp.count + 1) + 1) * stack_slot_size;
    sp -= aux_size;
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
    addr_t null_addr = 0;
    size_t argc = argv.count;
    while (argc-- > 0) {
        if (user_put(p, argv_addr))
            return _EFAULT;
        argv_addr += user_strlen(argv_addr) + 1;
        p += stack_slot_size;
    }
    if (user_put(p, null_addr))
        return _EFAULT;
    p += stack_slot_size;

    // envp
    size_t envc = envp.count;
    while (envc-- > 0) {
        if (user_put(p, envp_addr))
            return _EFAULT;
        envp_addr += user_strlen(envp_addr) + 1;
        p += stack_slot_size;
    }
    if (user_put(p, null_addr))
        return _EFAULT;
    p += stack_slot_size;

    // copy auxv
    current->mm->auxv_start = p;
    trace_exec_checkpoint("task.proof.elf_exec.before_write_auxv", err);
    if (user_write(p, aux, aux_size)) {
        trace_exec_checkpoint("task.proof.elf_exec.write_auxv_failed", err);
        goto beyond_hope;
    }
    trace_exec_checkpoint("task.proof.elf_exec.after_write_auxv", err);
    p += aux_size;
    current->mm->auxv_end = p;

    current->mm->stack_start = sp;
    trace_exec_checkpoint("task.proof.elf_exec.after_stack_setup", err);
    
    // STACK PROOF: Log stack layout before CPU setup
    {
        char stack_proof[512];
        snprintf(stack_proof, sizeof(stack_proof),
                 "stack.proof.layout=sp:0x%lx,argc:%zu,argv_addr:0x%lx,envp_addr:0x%lx,auxv_start:0x%lx,auxv_end:0x%lx",
                 (unsigned long)sp, (size_t)argv.count, (unsigned long)argv_addr, 
                 (unsigned long)envp_addr, (unsigned long)current->mm->auxv_start, 
                 (unsigned long)current->mm->auxv_end);
        trace_record_event(TRACE_ORIGIN_KERNEL, stack_proof);
    }

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
    // x1 = loader _DYNAMIC when entering an interpreter, otherwise the
    //      executable _DYNAMIC for direct static-pie startup.
    // x2-x7 = 0 (not used for startup)
    // x8 = 0 (syscall number register)
    // TPIDR_EL0 = TCB base (TLS pointer, accessed via system register)
    //
    // musl ldso startup records x1 as its own dynv before walking auxv to
    // discover the main executable. Passing the main executable _DYNAMIC here
    // makes ld-musl relocate against the wrong object and report its own libc
    // symbols as missing.

    // Set up TCB (Thread Control Block) for TLS
    // TLS belongs in TPIDR_EL0, NOT in x3
    addr_t tcb_base = TCB_BASE; // Start of mapped TCB pages (256 KB)
    a64_setup_tls_area(&current->cpu, tcb_base);

    // Correct AArch64 musl startup: pass stack pointer in x0
    current->cpu.x[0] = sp; // Points to argc on stack
    current->cpu.x[1] =
        interp_name && interp_dynamic_vaddr ? interp_base + interp_dynamic_vaddr : dynamic_addr;
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

static inline int user_memset(addr_t start, uint8_t val, addr_t len)
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
    // DIAGNOSTIC: Confirm format_exec is reached and calling elf_exec
    trace_record_event(TRACE_ORIGIN_KERNEL, "loader.format_exec.called");
    // DIAGNOSTIC: Confirm format_exec is calling elf_exec
    trace_record_event(TRACE_ORIGIN_KERNEL, "loader.format_exec.calling_elf_exec");
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
    int size = (int)fd->ops->read(fd, header, sizeof(header) - 1);
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

    snprintf(new_argv_buf, sizeof(new_argv_buf), "%s", interpreter);
    new_argv.count = 1;
    size_t n = strlen(interpreter) + 1;

    snprintf(new_argv_buf + n, sizeof(new_argv_buf) - n, "%s", file);
    n += strlen(file) + 1;
    new_argv.count++;

    memcpy(new_argv_buf + n, argv_rest.args, args_rest_size);
    new_argv.count += argv_rest.count;

    struct fd *interpreter_fd = generic_open(interpreter, O_RDONLY_, 0);
    if (IS_ERR(interpreter_fd))
        return (int)PTR_ERR(interpreter_fd);
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
    int size = (int)fd->ops->read(fd, header, sizeof(header) - 1);
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
    size_t interp_len = strlen(interpreter);
    snprintf(new_argv_buf, sizeof(new_argv_buf), "%s", interpreter);
    new_argv.count = 1;
    size_t n = interp_len + 1;
    if (argument) {
        size_t arg_len = strlen(argument);
        snprintf(new_argv_buf + n, sizeof(new_argv_buf) - n, "%s", argument);
        new_argv.count++;
        n += arg_len + 1;
    }
    snprintf(new_argv_buf + n, sizeof(new_argv_buf) - n, "%s", file);
    n += strlen(file) + 1;
    new_argv.count++;
    memcpy(new_argv_buf + n, argv_rest.args, args_rest_size);
    new_argv.count += argv_rest.count;

    struct fd *interpreter_fd = generic_open(interpreter, O_RDONLY_, 0);
    if (IS_ERR(interpreter_fd))
        return (int)PTR_ERR(interpreter_fd);
    int err = format_exec(interpreter_fd, interpreter, new_argv, envp);
    fd_close(interpreter_fd);
    return err;
}

int __do_execve(const char *file, struct exec_args argv, struct exec_args envp)
{
    // DIAGNOSTIC: Confirm __do_execve is reached
    trace_record_event(TRACE_ORIGIN_KERNEL, "loader.__do_execve.reached");
    // TRACE[1]: __do_execve_entry
    trace_emit_exec_path_boundary(
        (uint64_t)current, current ? current->pid : 0, (uint64_t)(current ? current->mm : NULL),
        (uint64_t)(current ? current->mem : NULL), EXEC_PATH_DO_EXECVE_ENTRY_RET, 0);
    struct fd *fd = generic_open(file, O_RDONLY, 0);
    if (IS_ERR(fd)) {
        return (int)PTR_ERR(fd);
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
    (void)fd->ops->read(fd, debug_buf, 16);
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
    // Safe copy without strncpy - compute length manually
    size_t bnlen = 0;
    while (basename[bnlen] && bnlen < sizeof(current->comm) - 1) {
        bnlen++;
    }
    memcpy(current->comm, basename, bnlen);
    current->comm[bnlen] = '\0';
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

uint32_t sys_execve(addr_t filename_addr, addr_t argv_addr, addr_t envp_addr)
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
        err = (int)argc;
        goto err_free_argv;
    }

    char *envp = malloc(ARGV_MAX);
    if (envp == NULL)
        goto err_free_envp;
    if (envp_addr != 0) {
        err = (int)user_read_string_array(envp_addr, envp, ARGV_MAX);
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
