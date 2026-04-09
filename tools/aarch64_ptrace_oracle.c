#define _GNU_SOURCE

#ifndef __linux__
#include <stdio.h>
int main(void)
{
    fprintf(stderr, "tools/aarch64_ptrace_oracle.c must be built and run on Linux AArch64\n");
    return 2;
}
#else

#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/elf.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/personality.h>
#include <sys/ptrace.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef NT_PRSTATUS
#define NT_PRSTATUS 1
#endif

struct map_entry {
    uint64_t start;
    uint64_t end;
    char perms[5];
    uint64_t offset;
    char path[512];
};

struct map_list {
    struct map_entry *entries;
    size_t len;
    size_t cap;
};

static void die(const char *msg)
{
    perror(msg);
    exit(1);
}

static void maps_push(struct map_list *maps, struct map_entry entry)
{
    if (maps->len == maps->cap) {
        size_t next = maps->cap ? maps->cap * 2 : 64;
        struct map_entry *new_entries = realloc(maps->entries, next * sizeof(*new_entries));
        if (!new_entries)
            die("realloc maps");
        maps->entries = new_entries;
        maps->cap = next;
    }
    maps->entries[maps->len++] = entry;
}

static struct map_list read_maps(pid_t pid)
{
    struct map_list maps = { 0 };
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/maps", pid);
    FILE *fp = fopen(path, "r");
    if (!fp)
        die("open maps");
    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        struct map_entry entry = { 0 };
        unsigned long long start = 0;
        unsigned long long end = 0;
        unsigned long long offset = 0;
        char dev[32] = { 0 };
        unsigned long inode = 0;
        char raw_path[512] = { 0 };
        int n = sscanf(line, "%llx-%llx %4s %llx %31s %lu %511[^\n]", &start, &end, entry.perms,
                       &offset, dev, &inode, raw_path);
        if (n < 6)
            continue;
        entry.start = (uint64_t)start;
        entry.end = (uint64_t)end;
        entry.offset = (uint64_t)offset;
        if (n == 7) {
            const char *p = raw_path;
            while (*p == ' ' || *p == '\t')
                p++;
            strncpy(entry.path, p, sizeof(entry.path) - 1);
        } else {
            entry.path[0] = '\0';
        }
        maps_push(&maps, entry);
    }
    fclose(fp);
    return maps;
}

static bool readlink_str(const char *path, char *out, size_t out_len)
{
    ssize_t n = readlink(path, out, out_len - 1);
    if (n < 0)
        return false;
    out[n] = '\0';
    return true;
}

struct elf_load_info {
    bool valid;
    int elf_class;
    int machine;
    uint64_t entry;
    uint64_t min_load_vaddr;
    bool has_interp;
    char interp[512];
    Elf64_Phdr load_headers[3];
    size_t load_count;
};

static struct elf_load_info read_elf_info(const char *path)
{
    struct elf_load_info info = { 0 };
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return info;
    Elf64_Ehdr eh = { 0 };
    if (read(fd, &eh, sizeof(eh)) != sizeof(eh)) {
        close(fd);
        return info;
    }
    if (memcmp(eh.e_ident, ELFMAG, SELFMAG) != 0) {
        close(fd);
        return info;
    }
    if (eh.e_ident[EI_CLASS] != ELFCLASS64) {
        close(fd);
        return info;
    }
    info.valid = true;
    info.elf_class = eh.e_ident[EI_CLASS];
    info.machine = eh.e_machine;
    info.entry = eh.e_entry;
    info.min_load_vaddr = UINT64_MAX;

    if (lseek(fd, eh.e_phoff, SEEK_SET) < 0) {
        close(fd);
        return info;
    }

    for (int i = 0; i < eh.e_phnum; i++) {
        Elf64_Phdr ph = { 0 };
        if (read(fd, &ph, sizeof(ph)) != sizeof(ph))
            break;
        if (ph.p_type == PT_LOAD) {
            if (ph.p_vaddr < info.min_load_vaddr)
                info.min_load_vaddr = ph.p_vaddr;
            if (info.load_count < 3)
                info.load_headers[info.load_count++] = ph;
        }
        if (ph.p_type == PT_INTERP && ph.p_filesz > 0 && ph.p_filesz < sizeof(info.interp)) {
            off_t pos = lseek(fd, 0, SEEK_CUR);
            if (lseek(fd, ph.p_offset, SEEK_SET) >= 0) {
                ssize_t got = read(fd, info.interp, ph.p_filesz);
                if (got > 0) {
                    info.interp[ph.p_filesz - 1] = '\0';
                    info.has_interp = true;
                }
            }
            if (pos >= 0)
                lseek(fd, pos, SEEK_SET);
        }
    }
    if (info.min_load_vaddr == UINT64_MAX)
        info.min_load_vaddr = 0;
    close(fd);
    return info;
}

static uint64_t parse_auxv_u64(pid_t pid, uint64_t key)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/auxv", pid);
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;
    struct {
        uint64_t a_type;
        uint64_t a_val;
    } item;
    while (read(fd, &item, sizeof(item)) == sizeof(item)) {
        if (item.a_type == key) {
            close(fd);
            return item.a_val;
        }
        if (item.a_type == AT_NULL)
            break;
    }
    close(fd);
    return 0;
}

static bool read_personality(pid_t pid, unsigned long *value)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/personality", pid);
    FILE *fp = fopen(path, "r");
    if (!fp)
        return false;
    int ok = fscanf(fp, "%lx", value);
    fclose(fp);
    return ok == 1;
}

static bool is_page_zero_mapped(const struct map_list *maps, struct map_entry *out)
{
    for (size_t i = 0; i < maps->len; i++) {
        const struct map_entry *m = &maps->entries[i];
        if (m->start == 0 || (m->start < 0x1000 && m->end > 0)) {
            if (out)
                *out = *m;
            return true;
        }
    }
    return false;
}

static bool ptrace_read_bytes(pid_t pid, uint64_t addr, uint8_t *buf, size_t len)
{
    size_t copied = 0;
    while (copied < len) {
        errno = 0;
        long word = ptrace(PTRACE_PEEKDATA, pid, (void *)(uintptr_t)(addr + copied), 0);
        if (word == -1 && errno != 0)
            return false;
        size_t n = sizeof(word);
        if (copied + n > len)
            n = len - copied;
        memcpy(buf + copied, &word, n);
        copied += n;
    }
    return true;
}

static uint64_t compute_load_bias(const struct map_list *maps, const char *path, uint64_t min_vaddr)
{
    for (size_t i = 0; i < maps->len; i++) {
        const struct map_entry *m = &maps->entries[i];
        if (m->path[0] == '\0')
            continue;
        if (strcmp(m->path, path) != 0)
            continue;
        if (strchr(m->perms, 'x') == NULL)
            continue;
        return m->start - min_vaddr;
    }
    return 0;
}

static void print_load_headers(const char *name, const struct elf_load_info *info)
{
    printf("%s PT_LOAD headers (first up to 3):\n", name);
    for (size_t i = 0; i < info->load_count; i++) {
        const Elf64_Phdr *ph = &info->load_headers[i];
        printf(
            "  [%zu] off=0x%llx vaddr=0x%llx filesz=0x%llx memsz=0x%llx flags=0x%x align=0x%llx\n",
            i, (unsigned long long)ph->p_offset, (unsigned long long)ph->p_vaddr,
            (unsigned long long)ph->p_filesz, (unsigned long long)ph->p_memsz, ph->p_flags,
            (unsigned long long)ph->p_align);
    }
}

static void print_hex(const uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        printf("%02x", buf[i]);
        if (i + 1 < len)
            printf(" ");
    }
    printf("\n");
}

static void single_step_trace(pid_t pid, int max_steps)
{
    struct iovec iov = { 0 };
    struct user_pt_regs regs = { 0 };
    iov.iov_base = &regs;
    iov.iov_len = sizeof(regs);
    printf("single-step trace (max %d):\n", max_steps);
    for (int i = 0; i < max_steps; i++) {
        if (ptrace(PTRACE_GETREGSET, pid, (void *)NT_PRSTATUS, &iov) < 0)
            die("PTRACE_GETREGSET");
        printf(
            "  step=%d pc=0x%llx sp=0x%llx x0=0x%llx x1=0x%llx x2=0x%llx x3=0x%llx nzcv=0x%llx\n",
            i, (unsigned long long)regs.pc, (unsigned long long)regs.sp,
            (unsigned long long)regs.regs[0], (unsigned long long)regs.regs[1],
            (unsigned long long)regs.regs[2], (unsigned long long)regs.regs[3],
            (unsigned long long)regs.pstate);
        if (ptrace(PTRACE_SINGLESTEP, pid, 0, 0) < 0)
            die("PTRACE_SINGLESTEP");
        int status = 0;
        if (waitpid(pid, &status, 0) < 0)
            die("waitpid single-step");
        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            printf("  trace stop: exited status=%d signaled=%d\n", WEXITSTATUS(status),
                   WTERMSIG(status));
            return;
        }
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <elf-path> [args...]\n", argv[0]);
        return 2;
    }

    const char *target = argv[1];
    pid_t pid = fork();
    if (pid < 0)
        die("fork");
    if (pid == 0) {
        if (ptrace(PTRACE_TRACEME, 0, 0, 0) < 0)
            die("PTRACE_TRACEME");
        kill(getpid(), SIGSTOP);
        execv(target, &argv[1]);
        die("execv");
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0)
        die("waitpid stop");
    if (!WIFSTOPPED(status)) {
        fprintf(stderr, "child did not stop\n");
        return 1;
    }

    if (ptrace(PTRACE_SETOPTIONS, pid, 0, PTRACE_O_TRACEEXEC) < 0)
        die("PTRACE_SETOPTIONS");

    if (ptrace(PTRACE_CONT, pid, 0, 0) < 0)
        die("PTRACE_CONT");
    if (waitpid(pid, &status, 0) < 0)
        die("waitpid exec trap");
    if (!WIFSTOPPED(status)) {
        fprintf(stderr, "child did not stop at exec\n");
        return 1;
    }

    char exe_path[1024] = { 0 };
    char exe_link[64];
    snprintf(exe_link, sizeof(exe_link), "/proc/%d/exe", pid);
    if (!readlink_str(exe_link, exe_path, sizeof(exe_path)))
        die("readlink exe");

    struct map_list maps = read_maps(pid);
    struct elf_load_info main_elf = read_elf_info(exe_path);
    struct elf_load_info interp_elf = { 0 };
    if (main_elf.has_interp)
        interp_elf = read_elf_info(main_elf.interp);

    printf("target_exe=%s\n", exe_path);
    printf("main_elf.class=%d machine=%d entry=0x%llx\n", main_elf.elf_class, main_elf.machine,
           (unsigned long long)main_elf.entry);
    if (main_elf.has_interp)
        printf("interp_path=%s\n", main_elf.interp);
    else
        printf("interp_path=<none>\n");

    if (interp_elf.valid) {
        printf("interp_elf.class=%d machine=%d entry=0x%llx\n", interp_elf.elf_class,
               interp_elf.machine, (unsigned long long)interp_elf.entry);
    }

    print_load_headers("main", &main_elf);
    if (interp_elf.valid)
        print_load_headers("interp", &interp_elf);

    uint64_t at_entry = parse_auxv_u64(pid, AT_ENTRY);
    uint64_t at_base = parse_auxv_u64(pid, AT_BASE);
    uint64_t at_phdr = parse_auxv_u64(pid, AT_PHDR);
    printf("auxv: AT_ENTRY=0x%llx AT_BASE=0x%llx AT_PHDR=0x%llx\n", (unsigned long long)at_entry,
           (unsigned long long)at_base, (unsigned long long)at_phdr);

    unsigned long personality_flags = 0;
    if (read_personality(pid, &personality_flags))
        printf("personality=0x%lx\n", personality_flags);
    else
        printf("personality=<unavailable>\n");

    uint64_t main_bias = compute_load_bias(&maps, exe_path, main_elf.min_load_vaddr);
    printf("main_load_bias=0x%llx\n", (unsigned long long)main_bias);
    if (main_elf.has_interp) {
        uint64_t interp_bias = compute_load_bias(&maps, main_elf.interp, interp_elf.min_load_vaddr);
        printf("interp_load_bias=0x%llx\n", (unsigned long long)interp_bias);
    }

    struct map_entry page0 = { 0 };
    bool page0_mapped = is_page_zero_mapped(&maps, &page0);
    printf("page0_mapped=%s\n", page0_mapped ? "YES" : "NO");
    if (page0_mapped) {
        printf("page0_backing=%s perms=%s range=0x%llx-0x%llx\n",
               page0.path[0] ? page0.path : "<anonymous>", page0.perms,
               (unsigned long long)page0.start, (unsigned long long)page0.end);
    }

    uint8_t page0_bytes[32] = { 0 };
    bool page0_read_ok = ptrace_read_bytes(pid, 0, page0_bytes, sizeof(page0_bytes));
    printf("bytes_0x0_0x1f_readable=%s\n", page0_read_ok ? "YES" : "NO");
    if (page0_read_ok) {
        printf("bytes_0x0_0x1f=");
        print_hex(page0_bytes, sizeof(page0_bytes));
    }

    bool page0_elf_match = false;
    if (page0_read_ok && page0_bytes[0] == 0x7f && page0_bytes[1] == 'E' && page0_bytes[2] == 'L' &&
        page0_bytes[3] == 'F') {
        page0_elf_match = true;
    }
    printf("bytes_match_elf_header=%s\n", page0_elf_match ? "YES" : "NO");

    single_step_trace(pid, 8);

    if (ptrace(PTRACE_DETACH, pid, 0, 0) < 0)
        perror("PTRACE_DETACH");

    free(maps.entries);
    return 0;
}

#endif
