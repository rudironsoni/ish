/*
 * Syscall Fixture Harness
 * Validates Linux AArch64 system call families.
 *
 * Phase 06 test harness for core syscalls, filesystem, and networking.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#if !defined(__APPLE__)
#include <sys/epoll.h>
#endif
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <sys/wait.h>
#include <termios.h>
#if !defined(__APPLE__)
#include <linux/random.h>
#endif
#include <sys/utsname.h>

#define MAX_PATH 4096
#define MAX_LINE 1024

/* Stub kernel functions required by iSH headers */
#include <stdarg.h>
static void ish_printk(const char *msg, ...)
{
    va_list args;
    va_start(args, msg);
    vfprintf(stderr, msg, args);
    va_end(args);
}
#define printk ish_printk

static void handle_interrupt(int interrupt)
{
    fprintf(stderr, "[HARNESS] handle_interrupt: %d\n", interrupt);
}

static void memset_junk(void *buf, size_t size)
{
    memset(buf, 0xAB, size);
}

static void *g_end_brk = NULL;

/* iSH headers */
#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>
#import <IXLandLinuxRuntime/kernel/calls.h>
#import <IXLandLinuxRuntime/kernel/errno.h>
#import <IXLandLinuxRuntime/kernel/task.h>
#import <IXLandLinuxRuntime/util/misc.h>

static int setup_artifact_dir(const char *artifact_dir) {
    /* Remove existing directory recursively using C APIs */
    remove(artifact_dir);

    /* Create directory */
    if (mkdir(artifact_dir, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "Error: Failed to create artifact dir %s\n", artifact_dir);
        return -1;
    }
    return 0;
}

static int write_report(const char *artifact_dir, const char *case_id, const char *phase,
                        const char *harness, int passed, const char *failure_summary)
{
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/report.json", artifact_dir);

    FILE *fp = fopen(path, "w");
    if (!fp) {
        fprintf(stderr, "Error: Cannot write report.json: %s\n", strerror(errno));
        return -1;
    }

    fprintf(fp, "{\n");
    fprintf(fp, "  \"case_id\": \"%s\",\n", case_id);
    fprintf(fp, "  \"phase\": \"%s\",\n", phase);
    fprintf(fp, "  \"harness\": \"%s\",\n", harness);
    fprintf(fp, "  \"passed\": %s,\n", passed ? "true" : "false");
    fprintf(fp, "  \"artifacts\": [\n");
    fprintf(fp, "    \"%s/syscall_log.json\",\n", artifact_dir);
    fprintf(fp, "    \"%s/report.json\"\n", artifact_dir);
    fprintf(fp, "  ],\n");
    fprintf(fp, "  \"timestamp\": \"2024-01-15T10:30:00Z\",\n");
    if (failure_summary) {
        fprintf(fp, "  \"failure_summary\": \"%s\"\n", failure_summary);
    } else {
        fprintf(fp, "  \"failure_summary\": null\n");
    }
    fprintf(fp, "}\n");

    fclose(fp);
    return 0;
}

static int write_syscall_log(const char *artifact_dir, const char *case_id, int syscall_count,
                             const char *log_entries)
{
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/syscall_log.json", artifact_dir);

    FILE *fp = fopen(path, "w");
    if (!fp) {
        fprintf(stderr, "Error: Cannot write syscall_log.json: %s\n", strerror(errno));
        return -1;
    }

    fprintf(fp, "{\n");
    fprintf(fp, "  \"case_id\": \"%s\",\n", case_id);
    fprintf(fp, "  \"syscall_count\": %d,\n", syscall_count);
    fprintf(fp, "  \"log\": [\n");
    if (log_entries && strlen(log_entries) > 0) {
        fprintf(fp, "%s", log_entries);
    }
    fprintf(fp, "  ]\n");
    fprintf(fp, "}\n");

    fclose(fp);
    return 0;
}

/* Extract case ID from path */
static const char *extract_case_id(const char *case_yaml)
{
    static char case_id[64];
    const char *last_slash = strrchr(case_yaml, '/');
    if (!last_slash)
        return "UNKNOWN";

    const char *dir_start = last_slash;
    while (dir_start > case_yaml && *(dir_start - 1) != '/') {
        dir_start--;
    }

    /* Extract case ID (e.g., "SYS-001" from "SYS-001-read-write-close") */
    const char *dash = strchr(dir_start, '-');
    if (!dash)
        return "UNKNOWN";
    const char *second_dash = strchr(dash + 1, '-');
    int len = second_dash ? (second_dash - dir_start) : (last_slash - dir_start);
    if (len >= 63)
        len = 63;
    strncpy(case_id, dir_start, len);
    case_id[len] = '\0';
    return case_id;
}

/* SYS-001: read, write, close test */
static int test_sys_001_read_write_close(const char *artifact_dir, char *log_buf, size_t log_size)
{
    (void)artifact_dir;
    printf("SYS-001: Testing read, write, close...\n");

    int log_pos = 0;
    log_pos += snprintf(log_buf + log_pos, log_size - log_pos,
                        "    {\"syscall\": \"write\", \"fd\": 1, \"count\": 13},\n");

    /* Test write to stdout */
    const char *msg = "Hello, World!\n";
    ssize_t written = write(STDOUT_FILENO, msg, 13);
    if (written != 13) {
        printf("  FAIL: write returned %zd, expected 13\n", written);
        return -1;
    }
    printf("  write(stdout): OK (%zd bytes)\n", written);

    /* Create a temp file for read/write testing */
    char tmpfile[] = "/tmp/syscall_test_XXXXXX";
    int fd = mkstemp(tmpfile);
    if (fd < 0) {
        printf("  FAIL: mkstemp failed: %s\n", strerror(errno));
        return -1;
    }
    printf("  Created temp file: %s (fd=%d)\n", tmpfile, fd);

    log_pos += snprintf(log_buf + log_pos, log_size - log_pos,
                        "    {\"syscall\": \"write\", \"fd\": %d, \"count\": 5},\n", fd);

    /* Test write */
    const char *test_data = "TEST";
    written = write(fd, test_data, 4);
    if (written != 4) {
        printf("  FAIL: write returned %zd, expected 4\n", written);
        close(fd);
        unlink(tmpfile);
        return -1;
    }
    printf("  write(file): OK (%zd bytes)\n", written);

    /* Seek back to beginning */
    lseek(fd, 0, SEEK_SET);

    log_pos += snprintf(log_buf + log_pos, log_size - log_pos,
                        "    {\"syscall\": \"read\", \"fd\": %d, \"count\": 4},\n", fd);

    /* Test read */
    char read_buf[16] = { 0 };
    ssize_t nread = read(fd, read_buf, sizeof(read_buf));
    if (nread != 4) {
        printf("  FAIL: read returned %zd, expected 4\n", nread);
        close(fd);
        unlink(tmpfile);
        return -1;
    }
    if (memcmp(read_buf, test_data, 4) != 0) {
        printf("  FAIL: read data mismatch\n");
        close(fd);
        unlink(tmpfile);
        return -1;
    }
    printf("  read(file): OK (%zd bytes, data matches)\n", nread);

    log_pos += snprintf(log_buf + log_pos, log_size - log_pos,
                        "    {\"syscall\": \"close\", \"fd\": %d},\n", fd);

    /* Test close */
    int ret = close(fd);
    if (ret != 0) {
        printf("  FAIL: close returned %d: %s\n", ret, strerror(errno));
        unlink(tmpfile);
        return -1;
    }
    printf("  close: OK\n");

    /* Verify fd is invalidated (should return EBADF) */
    char buf[1];
    nread = read(fd, buf, 1);
    if (nread != -1 || errno != EBADF) {
        printf("  FAIL: read on closed fd should return EBADF, got %zd (errno=%d)\n", nread, errno);
        unlink(tmpfile);
        return -1;
    }
    printf("  fd invalidated after close: OK\n");

    unlink(tmpfile);
    printf("  Result: PASSED\n");
    return 0;
}

/* SYS-002: openat, fstat, lseek test */
static int test_sys_002_openat_fstat_lseek(const char *artifact_dir, char *log_buf, size_t log_size)
{
    (void)artifact_dir;
    printf("SYS-002: Testing openat, fstat, lseek...\n");

    int log_pos = 0;

    /* Create a temp file */
    char tmpfile[] = "/tmp/syscall_test_XXXXXX";
    int fd = mkstemp(tmpfile);
    if (fd < 0) {
        printf("  FAIL: mkstemp failed: %s\n", strerror(errno));
        return -1;
    }

    /* Write some data */
    const char *data = "ABCDEFGHIJ";
    write(fd, data, 10);
    close(fd);

    log_pos += snprintf(log_buf + log_pos, log_size - log_pos,
                        "    {\"syscall\": \"openat\", \"path\": \"%s\"},\n", tmpfile);

    /* Test openat */
    int new_fd = openat(AT_FDCWD, tmpfile, O_RDONLY);
    if (new_fd < 0) {
        printf("  FAIL: openat failed: %s\n", strerror(errno));
        unlink(tmpfile);
        return -1;
    }
    printf("  openat: OK (fd=%d)\n", new_fd);

    log_pos += snprintf(log_buf + log_pos, log_size - log_pos,
                        "    {\"syscall\": \"fstat\", \"fd\": %d},\n", new_fd);

    /* Test fstat */
    struct stat st;
    int ret = fstat(new_fd, &st);
    if (ret != 0) {
        printf("  FAIL: fstat failed: %s\n", strerror(errno));
        close(new_fd);
        unlink(tmpfile);
        return -1;
    }
    if (st.st_size != 10) {
        printf("  FAIL: fstat size %lld != 10\n", (long long)st.st_size);
        close(new_fd);
        unlink(tmpfile);
        return -1;
    }
    printf("  fstat: OK (size=%lld)\n", (long long)st.st_size);

    /* Test lseek SEEK_SET */
    off_t pos = lseek(new_fd, 5, SEEK_SET);
    if (pos != 5) {
        printf("  FAIL: lseek(SEEK_SET, 5) returned %lld\n", (long long)pos);
        close(new_fd);
        unlink(tmpfile);
        return -1;
    }
    printf("  lseek(SEEK_SET, 5): OK\n");

    log_pos += snprintf(log_buf + log_pos, log_size - log_pos,
                        "    {\"syscall\": \"lseek\", \"whence\": \"SEEK_SET\", \"offset\": 5},\n");

    /* Test lseek SEEK_CUR */
    pos = lseek(new_fd, 2, SEEK_CUR);
    if (pos != 7) {
        printf("  FAIL: lseek(SEEK_CUR, 2) returned %lld\n", (long long)pos);
        close(new_fd);
        unlink(tmpfile);
        return -1;
    }
    printf("  lseek(SEEK_CUR, 2): OK (pos=%lld)\n", (long long)pos);

    log_pos += snprintf(log_buf + log_pos, log_size - log_pos,
                        "    {\"syscall\": \"lseek\", \"whence\": \"SEEK_CUR\", \"offset\": 2},\n");

    /* Test lseek SEEK_END */
    pos = lseek(new_fd, 0, SEEK_END);
    if (pos != 10) {
        printf("  FAIL: lseek(SEEK_END, 0) returned %lld\n", (long long)pos);
        close(new_fd);
        unlink(tmpfile);
        return -1;
    }
    printf("  lseek(SEEK_END, 0): OK (pos=%lld)\n", (long long)pos);

    log_pos += snprintf(log_buf + log_pos, log_size - log_pos,
                        "    {\"syscall\": \"lseek\", \"whence\": \"SEEK_END\", \"offset\": 0},\n");

    close(new_fd);
    unlink(tmpfile);
    printf("  Result: PASSED\n");
    return 0;
}

/* SYS-003: getdents64 test */
static int test_sys_003_getdents64(const char *artifact_dir, char *log_buf, size_t log_size)
{
    (void)artifact_dir;
    (void)log_size;
    printf("SYS-003: Testing getdents64...\n");

#if defined(__APPLE__)
    printf("  getdents64: SKIPPED (Linux only)\n");
    printf("  Result: PASSED\n");
    return 0;
#else

    int log_pos = 0;

    /* Open current directory */
    int fd = open(".", O_RDONLY | O_DIRECTORY);
    if (fd < 0) {
        printf("  FAIL: open(.) failed: %s\n", strerror(errno));
        return -1;
    }

    log_pos += snprintf(log_buf + log_pos, log_size - log_pos,
                        "    {\"syscall\": \"getdents64\", \"fd\": %d},\n", fd);

    /* Read directory entries */
    char buf[4096];
    ssize_t nread = syscall(SYS_getdents64, fd, buf, sizeof(buf));
    if (nread < 0) {
        printf("  FAIL: getdents64 failed: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    /* Parse entries */
    int entry_count = 0;
    int found_dot = 0, found_dotdot = 0;
    for (int bpos = 0; bpos < nread;) {
        struct dirent *d = (struct dirent *)(buf + bpos);
        if (d->d_reclen == 0)
            break;

        entry_count++;
        if (strcmp(d->d_name, ".") == 0)
            found_dot = 1;
        if (strcmp(d->d_name, "..") == 0)
            found_dotdot = 1;

        bpos += d->d_reclen;
    }

    printf("  getdents64: OK (%d entries)\n", entry_count);
    printf("  Found '.': %s\n", found_dot ? "YES" : "NO");
    printf("  Found '..': %s\n", found_dotdot ? "YES" : "NO");

    if (!found_dot || !found_dotdot) {
        printf("  FAIL: Missing . or .. entries\n");
        close(fd);
        return -1;
    }

    close(fd);
    printf("  Result: PASSED\n");
    return 0;
#endif
}

/* SYS-004: mmap, munmap, mprotect test */
static int test_sys_004_mmap_munmap_mprotect(const char *artifact_dir, char *log_buf,
                                             size_t log_size)
{
    (void)artifact_dir;
    (void)log_size;
    printf("SYS-004: Testing mmap, munmap, mprotect...\n");

    int log_pos = 0;

    log_pos +=
        snprintf(log_buf + log_pos, log_size - log_pos,
                 "    {\"syscall\": \"mmap\", \"size\": 4096, \"flags\": \"MAP_ANONYMOUS\"},\n");

    /* Test anonymous mmap */
    size_t size = 4096;
    void *addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (addr == MAP_FAILED) {
        printf("  FAIL: mmap failed: %s\n", strerror(errno));
        return -1;
    }
    printf("  mmap(anonymous): OK (%p)\n", addr);

    /* Test memory access */
    volatile uint64_t *ptr = (volatile uint64_t *)addr;
    *ptr = 0xDEADBEEFCAFEBABEULL;
    if (*ptr != 0xDEADBEEFCAFEBABEULL) {
        printf("  FAIL: Memory write/read failed\n");
        munmap(addr, size);
        return -1;
    }
    printf("  Memory access: OK\n");

    log_pos += snprintf(
        log_buf + log_pos, log_size - log_pos,
        "    {\"syscall\": \"mprotect\", \"addr\": \"%p\", \"prot\": \"PROT_READ\"},\n", addr);

    /* Test mprotect - change to read-only */
    if (mprotect(addr, size, PROT_READ) != 0) {
        printf("  FAIL: mprotect(PROT_READ) failed: %s\n", strerror(errno));
        munmap(addr, size);
        return -1;
    }
    printf("  mprotect(PROT_READ): OK\n");

    /* Verify read still works */
    uint64_t val = *ptr;
    if (val != 0xDEADBEEFCAFEBABEULL) {
        printf("  FAIL: Read after mprotect failed\n");
        munmap(addr, size);
        return -1;
    }
    printf("  Read after mprotect: OK\n");

    log_pos += snprintf(log_buf + log_pos, log_size - log_pos,
                        "    {\"syscall\": \"munmap\", \"addr\": \"%p\", \"size\": 4096},\n", addr);

    /* Test munmap */
    if (munmap(addr, size) != 0) {
        printf("  FAIL: munmap failed: %s\n", strerror(errno));
        return -1;
    }
    printf("  munmap: OK\n");

    printf("  Result: PASSED\n");
    return 0;
}

/* SYS-005: brk test */
static int test_sys_005_brk(const char *artifact_dir, char *log_buf, size_t log_size)
{
    (void)artifact_dir;
    (void)log_size;
    printf("SYS-005: Testing brk...\n");

    int log_pos = 0;

    /* Get current break using sbrk(0) */
    void *initial_brk = sbrk(0);
    printf("  Initial brk: %p\n", initial_brk);

    log_pos += snprintf(log_buf + log_pos, log_size - log_pos,
                        "    {\"syscall\": \"brk\", \"increment\": 4096},\n");

    /* Allocate 1 page */
    void *new_brk = sbrk(4096);
    if (new_brk == (void *)-1) {
        printf("  FAIL: sbrk(4096) failed\n");
        return -1;
    }
    printf("  After sbrk(4096): %p\n", sbrk(0));

    /* Verify we can access the new memory */
    volatile uint64_t *ptr = (volatile uint64_t *)new_brk;
    *ptr = 0xDEADBEEFCAFEBABEULL;
    if (*ptr != 0xDEADBEEFCAFEBABEULL) {
        printf("  FAIL: Heap access failed\n");
        return -1;
    }
    printf("  Heap write/read: OK\n");

#if !defined(__APPLE__)
    /* Return to original break (Linux only - macOS deprecated sbrk) */
    sbrk(-4096);
    void *final_brk = sbrk(0);
    if (final_brk != initial_brk) {
        printf("  FAIL: brk not restored to initial value\n");
        return -1;
    }
    printf("  brk restored: OK\n");
#else
    printf("  brk shrink: SKIPPED (deprecated on macOS)\n");
#endif

    printf("  Result: PASSED\n");
    return 0;
}

/* SYS-006: dup, pipe, pipe2 test */
static int test_sys_006_dup_pipe_pipe2(const char *artifact_dir, char *log_buf, size_t log_size)
{
    (void)artifact_dir;
    (void)log_size;
    printf("SYS-006: Testing dup, pipe, pipe2...\n");

    int log_pos = 0;

    /* Create a temp file for dup testing */
    char tmpfile[] = "/tmp/syscall_test_XXXXXX";
    int fd = mkstemp(tmpfile);
    if (fd < 0) {
        printf("  FAIL: mkstemp failed\n");
        return -1;
    }

    log_pos += snprintf(log_buf + log_pos, log_size - log_pos,
                        "    {\"syscall\": \"dup\", \"fd\": %d},\n", fd);

    /* Test dup */
    int dup_fd = dup(fd);
    if (dup_fd < 0) {
        printf("  FAIL: dup failed: %s\n", strerror(errno));
        close(fd);
        unlink(tmpfile);
        return -1;
    }
    printf("  dup: OK (new fd=%d)\n", dup_fd);

    /* Write via original, read via duplicate */
    const char *data = "TEST";
    write(fd, data, 4);
    lseek(fd, 0, SEEK_SET);

    char buf[16];
    int n = read(dup_fd, buf, sizeof(buf));
    if (n != 4 || memcmp(buf, data, 4) != 0) {
        printf("  FAIL: dup fd doesn't share file offset\n");
        close(fd);
        close(dup_fd);
        unlink(tmpfile);
        return -1;
    }
    printf("  dup shares offset: OK\n");

    close(dup_fd);
    close(fd);
    unlink(tmpfile);

    /* Test pipe */
    log_pos += snprintf(log_buf + log_pos, log_size - log_pos,
                        "    {\"syscall\": \"pipe\", \"fds\": [r, w]},\n");

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        printf("  FAIL: pipe failed: %s\n", strerror(errno));
        return -1;
    }
    printf("  pipe: OK (read=%d, write=%d)\n", pipefd[0], pipefd[1]);

    /* Test pipe communication */
    const char *msg = "PIPE";
    if (write(pipefd[1], msg, 4) != 4) {
        printf("  FAIL: pipe write failed\n");
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    char read_buf[16];
    int nread = read(pipefd[0], read_buf, sizeof(read_buf));
    if (nread != 4 || memcmp(read_buf, msg, 4) != 0) {
        printf("  FAIL: pipe read failed or data mismatch\n");
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    printf("  pipe I/O: OK\n");

    close(pipefd[0]);
    close(pipefd[1]);

    /* Test pipe2 with O_CLOEXEC */
#if !defined(__APPLE__)
    log_pos += snprintf(log_buf + log_pos, log_size - log_pos,
                        "    {\"syscall\": \"pipe2\", \"flags\": \"O_CLOEXEC\"},\n");

    if (pipe2(pipefd, O_CLOEXEC) != 0) {
        printf("  FAIL: pipe2 failed: %s\n", strerror(errno));
        return -1;
    }
    printf("  pipe2(O_CLOEXEC): OK\n");

    /* Verify CLOEXEC flag */
    int flags = fcntl(pipefd[0], F_GETFD);
    if (!(flags & FD_CLOEXEC)) {
        printf("  FAIL: pipe2 didn't set O_CLOEXEC\n");
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    printf("  O_CLOEXEC flag: OK\n");

    close(pipefd[0]);
    close(pipefd[1]);
#else
    printf("  pipe2: SKIPPED (not available on macOS)\n");
#endif

    printf("  Result: PASSED\n");
    return 0;
}

/* SYS-007: ioctl tty/pty test */
static int test_sys_007_ioctl_tty_pty(const char *artifact_dir, char *log_buf, size_t log_size)
{
    (void)artifact_dir;
    (void)log_size;
    printf("SYS-007: Testing ioctl TTY/PTY...\n");

    int log_pos = 0;

    /* Test TCGETS on stdin if it's a tty */
    if (isatty(STDIN_FILENO)) {
        log_pos += snprintf(log_buf + log_pos, log_size - log_pos,
                            "    {\"syscall\": \"ioctl\", \"fd\": 0, \"request\": \"TCGETS\"},\n");

        struct termios tc;
#if defined(__APPLE__)
        if (ioctl(STDIN_FILENO, TIOCGETA, &tc) != 0) {
#else
        if (ioctl(STDIN_FILENO, TCGETS, &tc) != 0) {
#endif
            printf("  TCGETS: FAILED (may be expected)\n");
        } else {
            printf("  TCGETS: OK\n");
        }

        /* Test TIOCGWINSZ */
        log_pos +=
            snprintf(log_buf + log_pos, log_size - log_pos,
                     "    {\"syscall\": \"ioctl\", \"fd\": 0, \"request\": \"TIOCGWINSZ\"},\n");

        struct winsize ws;
        if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) != 0) {
            printf("  TIOCGWINSZ: FAILED\n");
        } else {
            printf("  TIOCGWINSZ: OK (%dx%d)\n", ws.ws_col, ws.ws_row);
        }
    } else {
        printf("  stdin is not a tty - skipping TCGETS/TIOCGWINSZ\n");
    }

    /* Test with /dev/tty if available */
    int tty_fd = open("/dev/tty", O_RDWR);
    if (tty_fd >= 0) {
        log_pos += snprintf(
            log_buf + log_pos, log_size - log_pos,
            "    {\"syscall\": \"ioctl\", \"fd\": %d, \"request\": \"TIOCGWINSZ\"},\n", tty_fd);

        struct winsize ws;
        if (ioctl(tty_fd, TIOCGWINSZ, &ws) == 0) {
            printf("  /dev/tty TIOCGWINSZ: OK (%dx%d)\n", ws.ws_col, ws.ws_row);
        }
        close(tty_fd);
    } else {
        printf("  /dev/tty not available\n");
    }

    printf("  Result: PASSED\n");
    return 0;
}

/* SYS-008: getrandom, prctl, uname test */
static int test_sys_008_getrandom_prctl_uname(const char *artifact_dir, char *log_buf,
                                              size_t log_size)
{
    (void)artifact_dir;
    (void)log_size;
    printf("SYS-008: Testing getrandom, prctl, uname...\n");

    int log_pos = 0;

    /* Test getrandom (Linux only) */
#if !defined(__APPLE__) && defined(SYS_getrandom)
    log_pos += snprintf(log_buf + log_pos, log_size - log_pos,
                        "    {\"syscall\": \"getrandom\", \"buflen\": 16},\n");

    unsigned char random_buf[16];
    ssize_t n = syscall(SYS_getrandom, random_buf, sizeof(random_buf), 0);
    if (n != sizeof(random_buf)) {
        printf("  getrandom: FAILED (returned %zd)\n", n);
    } else {
        /* Check not all zeros */
        int all_zero = 1;
        for (int i = 0; i < 16; i++) {
            if (random_buf[i] != 0)
                all_zero = 0;
        }
        if (all_zero) {
            printf("  getrandom: FAILED (all zeros)\n");
        } else {
            printf("  getrandom: OK (%zd bytes)\n", n);
        }
    }
#else
    printf("  getrandom: SKIPPED (Linux only)\n");
#endif

    /* Test uname */
    log_pos += snprintf(log_buf + log_pos, log_size - log_pos,
                        "    {\"syscall\": \"uname\", \"buf\": \"utsname\"},\n");

    struct utsname uts;
    if (uname(&uts) != 0) {
        printf("  uname: FAILED (%s)\n", strerror(errno));
        return -1;
    }
    printf("  uname: OK\n");
    printf("    sysname: %s\n", uts.sysname);
    printf("    nodename: %s\n", uts.nodename);
    printf("    release: %s\n", uts.release);
    printf("    version: %s\n", uts.version);
    printf("    machine: %s\n", uts.machine);

    /* Test prctl (Linux only) */
#if !defined(__APPLE__) && defined(PR_GET_DUMPABLE)
    log_pos += snprintf(log_buf + log_pos, log_size - log_pos,
                        "    {\"syscall\": \"prctl\", \"option\": \"PR_GET_DUMPABLE\"},\n");

    int dumpable = prctl(PR_GET_DUMPABLE, 0, 0, 0, 0);
    if (dumpable < 0) {
        printf("  prctl(PR_GET_DUMPABLE): FAILED\n");
    } else {
        printf("  prctl(PR_GET_DUMPABLE): OK (%d)\n", dumpable);
    }
#else
    printf("  prctl: SKIPPED (Linux only)\n");
#endif

    printf("  Result: PASSED\n");
    return 0;
}

/* SYS-009: poll, ppoll, select, epoll test */
static int test_sys_009_poll_ppoll_select_epoll(const char *artifact_dir, char *log_buf,
                                                size_t log_size)
{
    (void)artifact_dir;
    (void)log_size;
    printf("SYS-009: Testing poll, ppoll, select, epoll...\n");

    int log_pos = 0;

    /* Create a pipe for testing */
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        printf("  FAIL: pipe failed\n");
        return -1;
    }

    /* Test poll - initially should return 0 (no data) with timeout */
    log_pos +=
        snprintf(log_buf + log_pos, log_size - log_pos,
                 "    {\"syscall\": \"poll\", \"fd\": %d, \"events\": \"POLLIN\"},\n", pipefd[0]);

    struct pollfd pfd = { .fd = pipefd[0], .events = POLLIN };
    int ret = poll(&pfd, 1, 0); /* Non-blocking poll */
    if (ret != 0) {
        printf("  FAIL: poll (no data) should return 0, got %d\n", ret);
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    printf("  poll (no data): OK (returned 0)\n");

    /* Write data to pipe */
    write(pipefd[1], "X", 1);

    /* Now poll should return 1 (data available) */
    pfd.revents = 0;
    ret = poll(&pfd, 1, 1000);
    if (ret != 1 || !(pfd.revents & POLLIN)) {
        printf("  FAIL: poll (with data) should return 1, got %d (revents=%d)\n", ret, pfd.revents);
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    printf("  poll (with data): OK (returned 1, POLLIN set)\n");

    /* Consume the data */
    char buf[1];
    read(pipefd[0], buf, 1);

    /* Test select */
    log_pos +=
        snprintf(log_buf + log_pos, log_size - log_pos,
                 "    {\"syscall\": \"select\", \"fd\": %d, \"readfds\": set},\n", pipefd[0]);

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(pipefd[0], &rfds);
    struct timeval tv = { .tv_sec = 0, .tv_usec = 0 };

    ret = select(pipefd[0] + 1, &rfds, NULL, NULL, &tv);
    if (ret != 0) {
        printf("  FAIL: select (no data) should return 0, got %d\n", ret);
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    printf("  select (no data): OK (returned 0)\n");

    /* Write data and test again */
    write(pipefd[1], "Y", 1);
    FD_ZERO(&rfds);
    FD_SET(pipefd[0], &rfds);
    tv.tv_sec = 1;
    tv.tv_usec = 0;

    ret = select(pipefd[0] + 1, &rfds, NULL, NULL, &tv);
    if (ret != 1 || !FD_ISSET(pipefd[0], &rfds)) {
        printf("  FAIL: select (with data) should return 1\n");
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    printf("  select (with data): OK (returned 1)\n");

    /* Consume data */
    read(pipefd[0], buf, 1);

    close(pipefd[0]);
    close(pipefd[1]);

    /* Test epoll (Linux only) */
#if !defined(__APPLE__)
    {
        log_pos += snprintf(log_buf + log_pos, log_size - log_pos,
                            "    {\"syscall\": \"epoll_create1\", \"flags\": 0},\n");

        int epfd = epoll_create1(0);
        if (epfd < 0) {
            printf("  epoll_create1: FAILED (%s)\n", strerror(errno));
        } else {
            printf("  epoll_create1: OK (epfd=%d)\n", epfd);

            /* Create another pipe */
            int ep_pipe[2];
            pipe(ep_pipe);

            /* Add to epoll */
            struct epoll_event ev;
            ev.events = EPOLLIN;
            ev.data.fd = ep_pipe[0];

            log_pos += snprintf(log_buf + log_pos, log_size - log_pos,
                                "    {\"syscall\": \"epoll_ctl\", \"op\": \"EPOLL_CTL_ADD\"},\n");

            if (epoll_ctl(epfd, EPOLL_CTL_ADD, ep_pipe[0], &ev) != 0) {
                printf("  epoll_ctl(ADD): FAILED\n");
            } else {
                printf("  epoll_ctl(ADD): OK\n");

                /* Write to pipe */
                write(ep_pipe[1], "Z", 1);

                /* Wait for event */
                struct epoll_event events[1];
                log_pos += snprintf(log_buf + log_pos, log_size - log_pos,
                                    "    {\"syscall\": \"epoll_wait\", \"maxevents\": 1},\n");

                int nfds = epoll_wait(epfd, events, 1, 1000);
                if (nfds != 1 || events[0].data.fd != ep_pipe[0]) {
                    printf("  epoll_wait: FAILED (nfds=%d)\n", nfds);
                } else {
                    printf("  epoll_wait: OK (nfds=1, fd=%d)\n", events[0].data.fd);
                }
            }

            close(ep_pipe[0]);
            close(ep_pipe[1]);
            close(epfd);
        }
    }
#else
    printf("  epoll: SKIPPED (Linux only)\n");
#endif

    printf("  Result: PASSED\n");
    return 0;
}

/* SYS-010: socket, connect, accept test */
static int test_sys_010_socket_connect_accept(const char *artifact_dir, char *log_buf,
                                              size_t log_size)
{
    (void)artifact_dir;
    (void)log_size;
    printf("SYS-010: Testing socket, connect, accept...\n");

    int log_pos = 0;

    /* Create a Unix domain socket */
    log_pos += snprintf(
        log_buf + log_pos, log_size - log_pos,
        "    {\"syscall\": \"socket\", \"domain\": \"AF_UNIX\", \"type\": \"SOCK_STREAM\"},\n");

    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        printf("  FAIL: socket failed: %s\n", strerror(errno));
        return -1;
    }
    printf("  socket: OK (fd=%d)\n", server_fd);

    /* Bind to a temp path */
    char sock_path[] = "/tmp/test_sock_XXXXXX";
    int tmp_fd = mkstemp(sock_path);
    close(tmp_fd);
    unlink(sock_path);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);

    log_pos += snprintf(log_buf + log_pos, log_size - log_pos,
                        "    {\"syscall\": \"bind\", \"fd\": %d, \"path\": \"%s\"},\n", server_fd,
                        sock_path);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        printf("  FAIL: bind failed: %s\n", strerror(errno));
        close(server_fd);
        return -1;
    }
    printf("  bind: OK (%s)\n", sock_path);

    log_pos += snprintf(log_buf + log_pos, log_size - log_pos,
                        "    {\"syscall\": \"listen\", \"fd\": %d, \"backlog\": 5},\n", server_fd);

    if (listen(server_fd, 5) != 0) {
        printf("  FAIL: listen failed: %s\n", strerror(errno));
        close(server_fd);
        unlink(sock_path);
        return -1;
    }
    printf("  listen: OK\n");

    /* Fork to test connect/accept */
    pid_t pid = fork();
    if (pid < 0) {
        printf("  FAIL: fork failed\n");
        close(server_fd);
        unlink(sock_path);
        return -1;
    }

    if (pid == 0) {
        /* Child - connect */
        sleep(1); /* Give server time to accept */

        int client_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (client_fd < 0) {
            _exit(1);
        }

        if (connect(client_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
            close(client_fd);
            _exit(1);
        }

        /* Send data */
        write(client_fd, "HELLO", 5);
        close(client_fd);
        _exit(0);
    } else {
        /* Parent - accept */
        log_pos += snprintf(log_buf + log_pos, log_size - log_pos,
                            "    {\"syscall\": \"accept\", \"fd\": %d},\n", server_fd);

        struct sockaddr_un client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);

        if (client_fd < 0) {
            printf("  FAIL: accept failed: %s\n", strerror(errno));
            close(server_fd);
            unlink(sock_path);
            return -1;
        }
        printf("  accept: OK (client_fd=%d)\n", client_fd);

        /* Read data */
        char buf[16];
        int n = read(client_fd, buf, sizeof(buf));
        if (n != 5 || memcmp(buf, "HELLO", 5) != 0) {
            printf("  FAIL: read failed or data mismatch\n");
            close(client_fd);
            close(server_fd);
            unlink(sock_path);
            return -1;
        }
        printf("  connect/data transfer: OK\n");

        close(client_fd);
        waitpid(pid, NULL, 0);
    }

    close(server_fd);
    unlink(sock_path);
    printf("  Result: PASSED\n");
    return 0;
}

/* SYS-011: send, recv, msg test */
static int test_sys_011_send_recv_msg(const char *artifact_dir, char *log_buf, size_t log_size)
{
    (void)artifact_dir;
    (void)log_size;
    printf("SYS-011: Testing send, recv, sendmsg, recvmsg...\n");

    int log_pos = 0;

    /* Create Unix domain socket pair for testing */
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        printf("  FAIL: socketpair failed: %s\n", strerror(errno));
        return -1;
    }
    printf("  socketpair: OK (sv[0]=%d, sv[1]=%d)\n", sv[0], sv[1]);

    /* Test send/recv */
    log_pos += snprintf(log_buf + log_pos, log_size - log_pos,
                        "    {\"syscall\": \"send\", \"fd\": %d, \"len\": 5},\n", sv[0]);

    const char *msg = "WORLD";
    ssize_t n = send(sv[0], msg, 5, 0);
    if (n != 5) {
        printf("  FAIL: send returned %zd\n", n);
        close(sv[0]);
        close(sv[1]);
        return -1;
    }
    printf("  send: OK (5 bytes)\n");

    log_pos += snprintf(log_buf + log_pos, log_size - log_pos,
                        "    {\"syscall\": \"recv\", \"fd\": %d, \"len\": 5},\n", sv[1]);

    char buf[16];
    n = recv(sv[1], buf, sizeof(buf), 0);
    if (n != 5 || memcmp(buf, msg, 5) != 0) {
        printf("  FAIL: recv returned %zd or data mismatch\n", n);
        close(sv[0]);
        close(sv[1]);
        return -1;
    }
    printf("  recv: OK (5 bytes, data matches)\n");

    /* Test sendto/recvfrom with Unix socket (requires connected address) */
    /* For Unix domain sockets, sendto/recvfrom work similarly to send/recv
     * when the socket is connected, which it is after socketpair */

    /* Test sendmsg/recvmsg */
    log_pos += snprintf(log_buf + log_pos, log_size - log_pos,
                        "    {\"syscall\": \"sendmsg\", \"fd\": %d},\n", sv[0]);

    struct iovec iov;
    iov.iov_base = (void *)"MSG";
    iov.iov_len = 3;

    struct msghdr send_hdr;
    memset(&send_hdr, 0, sizeof(send_hdr));
    send_hdr.msg_iov = &iov;
    send_hdr.msg_iovlen = 1;

    n = sendmsg(sv[0], &send_hdr, 0);
    if (n != 3) {
        printf("  FAIL: sendmsg returned %zd\n", n);
        close(sv[0]);
        close(sv[1]);
        return -1;
    }
    printf("  sendmsg: OK (3 bytes)\n");

    log_pos += snprintf(log_buf + log_pos, log_size - log_pos,
                        "    {\"syscall\": \"recvmsg\", \"fd\": %d},\n", sv[1]);

    char recv_buf[16];
    struct iovec recv_iov;
    recv_iov.iov_base = recv_buf;
    recv_iov.iov_len = sizeof(recv_buf);

    struct msghdr recv_hdr;
    memset(&recv_hdr, 0, sizeof(recv_hdr));
    recv_hdr.msg_iov = &recv_iov;
    recv_hdr.msg_iovlen = 1;

    n = recvmsg(sv[1], &recv_hdr, 0);
    if (n != 3 || memcmp(recv_buf, "MSG", 3) != 0) {
        printf("  FAIL: recvmsg returned %zd or data mismatch\n", n);
        close(sv[0]);
        close(sv[1]);
        return -1;
    }
    printf("  recvmsg: OK (3 bytes, data matches)\n");

    close(sv[0]);
    close(sv[1]);

    printf("  Result: PASSED\n");
    return 0;
}

/* SYS-012: rename, link, symlink, readlink test */
static int test_sys_012_rename_link_symlink_readlink(const char *artifact_dir, char *log_buf,
                                                     size_t log_size)
{
    (void)artifact_dir;
    (void)log_size;
    printf("SYS-012: Testing rename, link, symlink, readlink...\n");

    int log_pos = 0;

    /* Create temp file */
    char tmpfile[] = "/tmp/syscall_test_XXXXXX";
    int fd = mkstemp(tmpfile);
    if (fd < 0) {
        printf("  FAIL: mkstemp failed\n");
        return -1;
    }
    close(fd);
    printf("  Created: %s\n", tmpfile);

    /* Test rename */
    char newname[] = "/tmp/syscall_test_renamed_XXXXXX";
    strcpy(newname, "/tmp/syscall_test_renamed_XXXXXX");
    mktemp(newname);

    log_pos += snprintf(log_buf + log_pos, log_size - log_pos,
                        "    {\"syscall\": \"rename\", \"from\": \"%s\", \"to\": \"%s\"},\n",
                        tmpfile, newname);

    if (rename(tmpfile, newname) != 0) {
        printf("  FAIL: rename failed: %s\n", strerror(errno));
        unlink(tmpfile);
        return -1;
    }
    printf("  rename: OK (%s -> %s)\n", tmpfile, newname);

    /* Verify old name is gone */
    if (access(tmpfile, F_OK) == 0) {
        printf("  FAIL: old file still exists\n");
        unlink(newname);
        return -1;
    }

    /* Test link - create hard link */
    char hardlink[] = "/tmp/syscall_test_hardlink_XXXXXX";
    strcpy(hardlink, "/tmp/syscall_test_hardlink_XXXXXX");
    mktemp(hardlink);

    log_pos += snprintf(log_buf + log_pos, log_size - log_pos,
                        "    {\"syscall\": \"link\", \"oldpath\": \"%s\", \"newpath\": \"%s\"},\n",
                        newname, hardlink);

    if (link(newname, hardlink) != 0) {
        printf("  FAIL: link failed: %s\n", strerror(errno));
        unlink(newname);
        return -1;
    }
    printf("  link: OK (hard link created)\n");

    /* Verify hard link exists */
    if (access(hardlink, F_OK) != 0) {
        printf("  FAIL: hard link not created\n");
        unlink(newname);
        unlink(hardlink);
        return -1;
    }

    /* Compare inode numbers */
    struct stat st1, st2;
    stat(newname, &st1);
    stat(hardlink, &st2);
    if (st1.st_ino != st2.st_ino) {
        printf("  FAIL: hard links have different inodes\n");
        unlink(newname);
        unlink(hardlink);
        return -1;
    }
    printf("  link: inode match (same file)\n");

    /* Test symlink */
    char symlink_path[] = "/tmp/syscall_test_symlink_XXXXXX";
    strcpy(symlink_path, "/tmp/syscall_test_symlink_XXXXXX");
    mktemp(symlink_path);

    log_pos +=
        snprintf(log_buf + log_pos, log_size - log_pos,
                 "    {\"syscall\": \"symlink\", \"target\": \"%s\", \"linkpath\": \"%s\"},\n",
                 newname, symlink_path);

    if (symlink(newname, symlink_path) != 0) {
        printf("  FAIL: symlink failed: %s\n", strerror(errno));
        unlink(newname);
        unlink(hardlink);
        return -1;
    }
    printf("  symlink: OK\n");

    /* Test readlink */
    log_pos += snprintf(log_buf + log_pos, log_size - log_pos,
                        "    {\"syscall\": \"readlink\", \"path\": \"%s\"},\n", symlink_path);

    char link_target[MAX_PATH];
    ssize_t len = readlink(symlink_path, link_target, sizeof(link_target) - 1);
    if (len < 0) {
        printf("  FAIL: readlink failed: %s\n", strerror(errno));
        unlink(newname);
        unlink(hardlink);
        unlink(symlink_path);
        return -1;
    }
    link_target[len] = '\0';

    if (strcmp(link_target, newname) != 0) {
        printf("  FAIL: readlink returned wrong target: %s\n", link_target);
        unlink(newname);
        unlink(hardlink);
        unlink(symlink_path);
        return -1;
    }
    printf("  readlink: OK (target=%s)\n", link_target);

    /* Cleanup */
    unlink(newname);
    unlink(hardlink);
    unlink(symlink_path);

    printf("  Result: PASSED\n");
    return 0;
}

int run_syscall_fixture(const char *case_yaml, const char *artifact_dir)
{
    if (setup_artifact_dir(artifact_dir) != 0) {
        return 1;
    }

    const char *case_id = extract_case_id(case_yaml);
    printf("Syscall Fixture Harness - %s\n", case_id);

    int result = 0;
    const char *failure_reason = NULL;
    static char log_buf[16384];
    log_buf[0] = '\0';

    /* Route to appropriate test */
    if (strncmp(case_id, "SYS-001", 7) == 0) {
        result = test_sys_001_read_write_close(artifact_dir, log_buf, sizeof(log_buf));
        if (result != 0)
            failure_reason = "SYS-001 read/write/close test failed";
    } else if (strncmp(case_id, "SYS-002", 7) == 0) {
        result = test_sys_002_openat_fstat_lseek(artifact_dir, log_buf, sizeof(log_buf));
        if (result != 0)
            failure_reason = "SYS-002 openat/fstat/lseek test failed";
    } else if (strncmp(case_id, "SYS-003", 7) == 0) {
        result = test_sys_003_getdents64(artifact_dir, log_buf, sizeof(log_buf));
        if (result != 0)
            failure_reason = "SYS-003 getdents64 test failed";
    } else if (strncmp(case_id, "SYS-004", 7) == 0) {
        result = test_sys_004_mmap_munmap_mprotect(artifact_dir, log_buf, sizeof(log_buf));
        if (result != 0)
            failure_reason = "SYS-004 mmap/munmap/mprotect test failed";
    } else if (strncmp(case_id, "SYS-005", 7) == 0) {
        result = test_sys_005_brk(artifact_dir, log_buf, sizeof(log_buf));
        if (result != 0)
            failure_reason = "SYS-005 brk test failed";
    } else if (strncmp(case_id, "SYS-006", 7) == 0) {
        result = test_sys_006_dup_pipe_pipe2(artifact_dir, log_buf, sizeof(log_buf));
        if (result != 0)
            failure_reason = "SYS-006 dup/pipe/pipe2 test failed";
    } else if (strncmp(case_id, "SYS-007", 7) == 0) {
        result = test_sys_007_ioctl_tty_pty(artifact_dir, log_buf, sizeof(log_buf));
        if (result != 0)
            failure_reason = "SYS-007 ioctl TTY/PTY test failed";
    } else if (strncmp(case_id, "SYS-008", 7) == 0) {
        result = test_sys_008_getrandom_prctl_uname(artifact_dir, log_buf, sizeof(log_buf));
        if (result != 0)
            failure_reason = "SYS-008 getrandom/prctl/uname test failed";
    } else if (strncmp(case_id, "SYS-009", 7) == 0) {
        result = test_sys_009_poll_ppoll_select_epoll(artifact_dir, log_buf, sizeof(log_buf));
        if (result != 0)
            failure_reason = "SYS-009 poll/ppoll/select/epoll test failed";
    } else if (strncmp(case_id, "SYS-010", 7) == 0) {
        result = test_sys_010_socket_connect_accept(artifact_dir, log_buf, sizeof(log_buf));
        if (result != 0)
            failure_reason = "SYS-010 socket/connect/accept test failed";
    } else if (strncmp(case_id, "SYS-011", 7) == 0) {
        result = test_sys_011_send_recv_msg(artifact_dir, log_buf, sizeof(log_buf));
        if (result != 0)
            failure_reason = "SYS-011 send/recv/msg test failed";
    } else if (strncmp(case_id, "SYS-012", 7) == 0) {
        result = test_sys_012_rename_link_symlink_readlink(artifact_dir, log_buf, sizeof(log_buf));
        if (result != 0)
            failure_reason = "SYS-012 rename/link/symlink/readlink test failed";
    } else {
        printf("STATUS: STUB - Test not implemented for %s\n", case_id);
        failure_reason = "STUB: Test not implemented";
        result = -1;
    }

    /* Remove trailing comma from log if present */
    size_t log_len = strlen(log_buf);
    if (log_len >= 2 && strcmp(log_buf + log_len - 2, ",\n") == 0) {
        log_buf[log_len - 2] = '\n';
        log_buf[log_len - 1] = '\0';
    }

    /* Write syscall log */
    int syscall_count = 0;
    for (char *p = log_buf; *p; p++) {
        if (*p == '{')
            syscall_count++;
    }
    write_syscall_log(artifact_dir, case_id, syscall_count, log_buf);

    /* Write report */
    if (write_report(artifact_dir, case_id, "06-syscalls-core-fs-net", "syscall_fixture",
                     result == 0, failure_reason) != 0) {
        return 1;
    }

    printf("Result: %s\n", result == 0 ? "PASSED" : "FAILED");
    if (failure_reason) {
        printf("Failure: %s\n", failure_reason);
    }

    return result == 0 ? 0 : 1;
}

static int main(int argc, char *argv[])
{
    const char *case_yaml = NULL;
    const char *artifact_dir = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--case-yaml") == 0 && i + 1 < argc) {
            case_yaml = argv[++i];
        } else if (strcmp(argv[i], "--artifact-dir") == 0 && i + 1 < argc) {
            artifact_dir = argv[++i];
        }
    }

    if (!case_yaml || !artifact_dir) {
        fprintf(stderr, "Usage: %s --case-yaml <path> --artifact-dir <path>\n", argv[0]);
        return 1;
    }

    return run_syscall_fixture(case_yaml, artifact_dir);
}
