/*
 * aarch64 syscall dispatch table
 *
 * Maps aarch64 Linux syscall numbers (from arch/arm64/include/uapi/asm/unistd.h)
 * to iSH syscall handlers.
 */

#import <IXLandLinuxRuntime/fs/sock.h>
#import <IXLandLinuxRuntime/kernel/aarch64/calls.h>
#import <IXLandLinuxRuntime/kernel/calls.h>
#import <IXLandLinuxRuntime/kernel/errno.h>
#import <IXLandLinuxRuntime/kernel/signal.h>
#include <stddef.h>

// Stub for unimplemented syscalls
static uint64_t sys_enosys_stub(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e,
                                uint64_t f)
{
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)e;
    (void)f;
    return _ENOSYS;
}

// Forward declarations for syscalls referenced in table but not in standard headers
extern uint32_t sys_newfstatat(fd_t, addr_t, addr_t, int64_t);
extern uint32_t sys_pselect6(fd_t, addr_t, addr_t, addr_t, addr_t, addr_t);
extern uint32_t sys_execveat(fd_t, addr_t, addr_t, addr_t, int64_t);
extern uint32_t sys_rt_sigreturn_aarch64(void);


// ============================================================================
// STUB IMPLEMENTATIONS FOR AARCH64 BRING-UP
// These are intentionally verbose TODOs to prevent silent failures
// ============================================================================

// TODO: STUB - sys_execveat needs proper implementation for AArch64 bring-up
uint32_t sys_execveat(fd_t dirfd, addr_t pathname, addr_t argv, addr_t envp, int64_t flags)
{
    (void)dirfd;
    (void)pathname;
    (void)argv;
    (void)envp;
    (void)flags;
    FIXME("TODO: sys_execveat NOT IMPLEMENTED - NEEDS PROPER AARCH64 PROCESS SUPPORT");
    return _ENOSYS;
}

// TODO: STUB - sys_newfstatat needs proper implementation for AArch64 bring-up
uint32_t sys_newfstatat(fd_t dirfd, addr_t pathname, addr_t statbuf, int64_t flags)
{
    (void)dirfd;
    (void)pathname;
    (void)statbuf;
    (void)flags;
    FIXME("TODO: sys_newfstatat NOT IMPLEMENTED - NEEDS PROPER AARCH64 FILE SUPPORT");
    return _ENOSYS;
}

// TODO: STUB - sys_pselect6 needs proper implementation for AArch64 bring-up
uint32_t sys_pselect6(fd_t nfds, addr_t readfds, addr_t writefds, addr_t exceptfds, addr_t timeout,
                      addr_t sigmask)
{
    (void)nfds;
    (void)readfds;
    (void)writefds;
    (void)exceptfds;
    (void)timeout;
    (void)sigmask;
    FIXME("TODO: sys_pselect6 NOT IMPLEMENTED - NEEDS PROPER AARCH64 FILE SUPPORT");
    return _ENOSYS;
}

#define A64_RET_S32(expr) ((uint64_t)(int64_t)(int32_t)(expr))
#define A64_RET_S64(expr) ((uint64_t)(int64_t)(expr))
#define A64_RET_U64(expr) ((uint64_t)(expr))

#define A64_WRAP0(name, retcast)                                                                   \
    static uint64_t a64_wrap_##name(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,            \
                                    uint64_t a4, uint64_t a5)                                      \
    {                                                                                              \
        (void)a0;                                                                                  \
        (void)a1;                                                                                  \
        (void)a2;                                                                                  \
        (void)a3;                                                                                  \
        (void)a4;                                                                                  \
        (void)a5;                                                                                  \
        return retcast(name());                                                                    \
    }
#define A64_WRAP1(name, retcast, t1)                                                               \
    static uint64_t a64_wrap_##name(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,            \
                                    uint64_t a4, uint64_t a5)                                      \
    {                                                                                              \
        (void)a1;                                                                                  \
        (void)a2;                                                                                  \
        (void)a3;                                                                                  \
        (void)a4;                                                                                  \
        (void)a5;                                                                                  \
        return retcast(name((t1)a0));                                                              \
    }
#define A64_WRAP2(name, retcast, t1, t2)                                                           \
    static uint64_t a64_wrap_##name(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,            \
                                    uint64_t a4, uint64_t a5)                                      \
    {                                                                                              \
        (void)a2;                                                                                  \
        (void)a3;                                                                                  \
        (void)a4;                                                                                  \
        (void)a5;                                                                                  \
        return retcast(name((t1)a0, (t2)a1));                                                      \
    }
#define A64_WRAP3(name, retcast, t1, t2, t3)                                                       \
    static uint64_t a64_wrap_##name(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,            \
                                    uint64_t a4, uint64_t a5)                                      \
    {                                                                                              \
        (void)a3;                                                                                  \
        (void)a4;                                                                                  \
        (void)a5;                                                                                  \
        return retcast(name((t1)a0, (t2)a1, (t3)a2));                                              \
    }
#define A64_WRAP4(name, retcast, t1, t2, t3, t4)                                                   \
    static uint64_t a64_wrap_##name(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,            \
                                    uint64_t a4, uint64_t a5)                                      \
    {                                                                                              \
        (void)a4;                                                                                  \
        (void)a5;                                                                                  \
        return retcast(name((t1)a0, (t2)a1, (t3)a2, (t4)a3));                                      \
    }
#define A64_WRAP5(name, retcast, t1, t2, t3, t4, t5)                                               \
    static uint64_t a64_wrap_##name(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,            \
                                    uint64_t a4, uint64_t a5)                                      \
    {                                                                                              \
        (void)a5;                                                                                  \
        return retcast(name((t1)a0, (t2)a1, (t3)a2, (t4)a3, (t5)a4));                              \
    }
#define A64_WRAP6(name, retcast, t1, t2, t3, t4, t5, t6)                                           \
    static uint64_t a64_wrap_##name(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,            \
                                    uint64_t a4, uint64_t a5)                                      \
    {                                                                                              \
        return retcast(name((t1)a0, (t2)a1, (t3)a2, (t4)a3, (t5)a4, (t6)a5));                      \
    }

A64_WRAP3(sys_read, A64_RET_S32, fd_t, addr_t, uint32_t)
A64_WRAP3(sys_write, A64_RET_S32, fd_t, addr_t, uint32_t)
A64_WRAP4(sys_openat, A64_RET_S32, fd_t, addr_t, uint32_t, mode_t_)
A64_WRAP1(sys_close, A64_RET_S32, fd_t)
A64_WRAP3(sys_lseek, A64_RET_S32, fd_t, uint32_t, uint32_t)
A64_WRAP3(sys_ioctl, A64_RET_S32, fd_t, uint32_t, uint32_t)
A64_WRAP3(sys_fcntl, A64_RET_S32, fd_t, uint32_t, uint32_t)
A64_WRAP1(sys_dup, A64_RET_S32, fd_t)
A64_WRAP3(sys_dup3, A64_RET_S32, fd_t, fd_t, int64_t)
A64_WRAP1(sys_fsync, A64_RET_S32, fd_t)
A64_WRAP2(sys_flock, A64_RET_S32, fd_t, uint32_t)
A64_WRAP3(sys_readv, A64_RET_S32, fd_t, addr_t, uint32_t)
A64_WRAP3(sys_writev, A64_RET_S32, fd_t, addr_t, uint32_t)
A64_WRAP4(sys_pread, A64_RET_S32, fd_t, addr_t, uint32_t, off_t_)
A64_WRAP4(sys_pwrite, A64_RET_S32, fd_t, addr_t, uint32_t, off_t_)
A64_WRAP6(sys_mmap_native, A64_RET_U64, addr_t, uint32_t, uint32_t, uint32_t, fd_t, off_t_)
A64_WRAP1(sys_brk, A64_RET_U64, addr_t)
A64_WRAP3(sys_mprotect, A64_RET_S64, addr_t, uint64_t, int64_t)
A64_WRAP2(sys_munmap, A64_RET_S64, addr_t, uint64_t)
A64_WRAP4(sys_mremap, A64_RET_S64, addr_t, uint32_t, uint32_t, uint32_t)
A64_WRAP3(sys_madvise, A64_RET_S32, addr_t, uint32_t, uint32_t)
A64_WRAP5(sys_clone, A64_RET_S32, uint32_t, addr_t, addr_t, addr_t, addr_t)
A64_WRAP0(sys_fork, A64_RET_S32)
A64_WRAP0(sys_vfork, A64_RET_S32)
A64_WRAP1(sys_exit, A64_RET_S32, uint32_t)
A64_WRAP1(sys_exit_group, A64_RET_S32, uint32_t)
A64_WRAP4(sys_wait4, A64_RET_S32, pid_t_, addr_t, uint32_t, addr_t)
A64_WRAP3(sys_execve, A64_RET_S32, addr_t, addr_t, addr_t)
A64_WRAP5(sys_execveat, A64_RET_S32, fd_t, addr_t, addr_t, addr_t, int64_t)
A64_WRAP0(sys_getpid, A64_RET_S32)
A64_WRAP0(sys_getppid, A64_RET_S32)
A64_WRAP1(sys_getpgid, A64_RET_S32, pid_t_)
A64_WRAP2(sys_setpgid, A64_RET_S32, pid_t_, pid_t_)
A64_WRAP0(sys_getsid, A64_RET_S32)
A64_WRAP0(sys_setsid, A64_RET_S32)
A64_WRAP0(sys_gettid, A64_RET_S32)
A64_WRAP1(sys_set_tid_address, A64_RET_S32, addr_t)
A64_WRAP2(sys_nanosleep, A64_RET_S32, addr_t, addr_t)
A64_WRAP2(sys_getrusage, A64_RET_S32, uint32_t, addr_t)
A64_WRAP0(sys_sched_yield, A64_RET_S64)
static uint64_t a64_wrap_sys_getrlimit(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
                                       uint64_t a4, uint64_t a5)
{
    (void)a2;
    (void)a3;
    (void)a4;
    (void)a5;
    return A64_RET_S32(sys_prlimit64(0, (uint32_t)a0, 0, (addr_t)a1));
}

static uint64_t a64_wrap_sys_setrlimit(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
                                       uint64_t a4, uint64_t a5)
{
    (void)a2;
    (void)a3;
    (void)a4;
    (void)a5;
    return A64_RET_S32(sys_prlimit64(0, (uint32_t)a0, (addr_t)a1, 0));
}
A64_WRAP2(sys_gettimeofday, A64_RET_S32, addr_t, addr_t)
A64_WRAP2(sys_settimeofday, A64_RET_S32, addr_t, addr_t)
A64_WRAP1(sys_times, A64_RET_S32, addr_t)
A64_WRAP1(sys_setuid, A64_RET_S64, uid_t)
A64_WRAP0(sys_getuid, A64_RET_S32)
A64_WRAP1(sys_setgid, A64_RET_S64, uid_t)
A64_WRAP0(sys_getgid, A64_RET_S32)
A64_WRAP0(sys_geteuid, A64_RET_S32)
A64_WRAP0(sys_getegid, A64_RET_S32)
A64_WRAP2(sys_setreuid, A64_RET_S64, uid_t_, uid_t_)
A64_WRAP2(sys_setregid, A64_RET_S64, uid_t_, uid_t_)
A64_WRAP3(sys_setresuid, A64_RET_S32, uid_t_, uid_t_, uid_t_)
A64_WRAP3(sys_getresuid, A64_RET_S64, addr_t, addr_t, addr_t)
A64_WRAP3(sys_setresgid, A64_RET_S32, uid_t_, uid_t_, uid_t_)
A64_WRAP3(sys_getresgid, A64_RET_S64, addr_t, addr_t, addr_t)
A64_WRAP2(sys_setgroups, A64_RET_S64, uint32_t, addr_t)
A64_WRAP2(sys_getgroups, A64_RET_S64, uint32_t, addr_t)
A64_WRAP2(sys_kill, A64_RET_S32, pid_t_, uint32_t)
A64_WRAP2(sys_tkill, A64_RET_S32, pid_t_, uint32_t)
A64_WRAP3(sys_tgkill, A64_RET_S32, pid_t_, pid_t_, uint32_t)
A64_WRAP2(sys_sigaltstack, A64_RET_S32, addr_t, addr_t)
A64_WRAP4(sys_rt_sigaction, A64_RET_S32, uint32_t, addr_t, addr_t, uint32_t)
A64_WRAP4(sys_rt_sigprocmask, A64_RET_S32, uint32_t, addr_t, addr_t, uint32_t)
A64_WRAP0(sys_rt_sigreturn_aarch64, A64_RET_S32)
A64_WRAP2(sys_rt_sigsuspend, A64_RET_S64, addr_t, uint32_t)
A64_WRAP1(sys_rt_sigpending, A64_RET_S64, addr_t)
A64_WRAP3(sys_mkdirat, A64_RET_S32, fd_t, addr_t, mode_t_)
A64_WRAP4(sys_mknodat, A64_RET_S32, fd_t, addr_t, mode_t_, dev_t_)
A64_WRAP3(sys_unlinkat, A64_RET_S32, fd_t, addr_t, int64_t)
A64_WRAP3(sys_symlinkat, A64_RET_S32, addr_t, fd_t, addr_t)
A64_WRAP4(sys_linkat, A64_RET_S32, fd_t, addr_t, fd_t, addr_t)
A64_WRAP4(sys_renameat, A64_RET_S32, fd_t, addr_t, fd_t, addr_t)
A64_WRAP5(sys_renameat2, A64_RET_S32, fd_t, addr_t, fd_t, addr_t, int64_t)
A64_WRAP5(sys_fchownat, A64_RET_S32, fd_t, addr_t, uint32_t, uint32_t, int)
A64_WRAP4(sys_faccessat, A64_RET_S32, fd_t, addr_t, mode_t_, uint32_t)
A64_WRAP4(sys_readlinkat, A64_RET_S32, fd_t, addr_t, addr_t, uint32_t)
A64_WRAP4(sys_newfstatat, A64_RET_S32, fd_t, addr_t, addr_t, int64_t)
A64_WRAP2(sys_fstat64, A64_RET_S32, fd_t, addr_t)
A64_WRAP2(sys_fstatfs, A64_RET_S32, fd_t, addr_t)
A64_WRAP4(sys_utimensat, A64_RET_S32, fd_t, addr_t, addr_t, uint32_t)
A64_WRAP3(sys_fchmodat, A64_RET_S32, fd_t, addr_t, uint32_t)
A64_WRAP1(sys_fchdir, A64_RET_S32, fd_t)
A64_WRAP2(sys_getcwd, A64_RET_S32, addr_t, uint32_t)
A64_WRAP1(sys_chdir, A64_RET_S32, addr_t)
A64_WRAP1(sys_chroot, A64_RET_S32, addr_t)
A64_WRAP2(sys_statfs, A64_RET_S32, addr_t, addr_t)
A64_WRAP2(sys_pipe2, A64_RET_S64, addr_t, int32_t)
A64_WRAP1(sys_epoll_create, A64_RET_S64, int64_t)
A64_WRAP4(sys_epoll_ctl, A64_RET_S64, fd_t, int64_t, fd_t, addr_t)
A64_WRAP6(sys_epoll_pwait, A64_RET_S64, fd_t, addr_t, int64_t, int64_t, addr_t, uint32_t)
A64_WRAP3(sys_socket, A64_RET_S64, uint32_t, uint32_t, uint32_t)
A64_WRAP4(sys_socketpair, A64_RET_S64, uint32_t, uint32_t, uint32_t, addr_t)
A64_WRAP3(sys_bind, A64_RET_S64, fd_t, addr_t, uint32_t)
A64_WRAP3(sys_connect, A64_RET_S64, fd_t, addr_t, uint32_t)
A64_WRAP2(sys_listen, A64_RET_S64, fd_t, int64_t)
A64_WRAP3(sys_accept, A64_RET_S64, fd_t, addr_t, addr_t)
A64_WRAP4(sys_accept4, A64_RET_S64, fd_t, addr_t, addr_t, int64_t)
A64_WRAP3(sys_getsockname, A64_RET_S64, fd_t, addr_t, addr_t)
A64_WRAP3(sys_getpeername, A64_RET_S64, fd_t, addr_t, addr_t)
A64_WRAP6(sys_sendto, A64_RET_S64, fd_t, addr_t, uint32_t, uint32_t, addr_t, uint32_t)
A64_WRAP6(sys_recvfrom, A64_RET_S64, fd_t, addr_t, uint32_t, uint32_t, addr_t, addr_t)
A64_WRAP3(sys_sendmsg, A64_RET_S64, fd_t, addr_t, int64_t)
A64_WRAP3(sys_recvmsg, A64_RET_S64, fd_t, addr_t, int64_t)
A64_WRAP2(sys_shutdown, A64_RET_S64, fd_t, uint32_t)
A64_WRAP5(sys_setsockopt, A64_RET_S64, fd_t, uint32_t, uint32_t, addr_t, uint32_t)
A64_WRAP5(sys_getsockopt, A64_RET_S64, fd_t, uint32_t, uint32_t, addr_t, uint32_t)
A64_WRAP5(sys_ppoll, A64_RET_S32, addr_t, uint32_t, addr_t, addr_t, uint32_t)
A64_WRAP6(sys_pselect6, A64_RET_S32, fd_t, addr_t, addr_t, addr_t, addr_t, addr_t)
A64_WRAP3(sys_getrandom, A64_RET_S32, addr_t, uint32_t, uint32_t)
A64_WRAP1(sys_uname, A64_RET_S32, addr_t)
A64_WRAP2(sys_sethostname, A64_RET_S32, addr_t, uint32_t)
A64_WRAP1(sys_sysinfo, A64_RET_S32, addr_t)
A64_WRAP5(sys_prctl, A64_RET_S64, uint32_t, uint64_t, uint64_t, uint64_t, uint64_t)
A64_WRAP3(sys_reboot, A64_RET_S64, int64_t, int64_t, int64_t)

#define A64_WRAP(name) a64_wrap_##name

a64_syscall_t syscall_table_a64[A64_SYS_MAX] = {
    // File operations
    [A64_SYS_read] = A64_WRAP(sys_read),
    [A64_SYS_write] = A64_WRAP(sys_write),
    [A64_SYS_openat] = A64_WRAP(sys_openat),
    [A64_SYS_close] = A64_WRAP(sys_close),
    [A64_SYS_lseek] = A64_WRAP(sys_lseek),
    [A64_SYS_ioctl] = A64_WRAP(sys_ioctl),
    [A64_SYS_fcntl] = A64_WRAP(sys_fcntl),
    [A64_SYS_dup] = A64_WRAP(sys_dup),
    [A64_SYS_dup3] = A64_WRAP(sys_dup3),
    [A64_SYS_fsync] = A64_WRAP(sys_fsync),
    [A64_SYS_flock] = A64_WRAP(sys_flock),
    [A64_SYS_readv] = A64_WRAP(sys_readv),
    [A64_SYS_writev] = A64_WRAP(sys_writev),
    [A64_SYS_pread64] = A64_WRAP(sys_pread),
    [A64_SYS_pwrite64] = A64_WRAP(sys_pwrite),

    // Memory
    [A64_SYS_brk] = A64_WRAP(sys_brk),
    [A64_SYS_mmap] = A64_WRAP(sys_mmap_native),
    [A64_SYS_mprotect] = A64_WRAP(sys_mprotect),
    [A64_SYS_munmap] = A64_WRAP(sys_munmap),
    [A64_SYS_mremap] = A64_WRAP(sys_mremap),
    [A64_SYS_madvise] = A64_WRAP(sys_madvise),

    // Process/thread
    [A64_SYS_clone] = A64_WRAP(sys_clone),
    [A64_SYS_fork] = A64_WRAP(sys_fork),
    [A64_SYS_vfork] = A64_WRAP(sys_vfork),
    [A64_SYS_exit] = A64_WRAP(sys_exit),
    [A64_SYS_exit_group] = A64_WRAP(sys_exit_group),
    [A64_SYS_wait4] = A64_WRAP(sys_wait4),
    [A64_SYS_execve] = A64_WRAP(sys_execve),
    [A64_SYS_execveat] = A64_WRAP(sys_execveat),
    [A64_SYS_getpid] = A64_WRAP(sys_getpid),
    [A64_SYS_getppid] = A64_WRAP(sys_getppid),
    [A64_SYS_getpgid] = A64_WRAP(sys_getpgid),
    [A64_SYS_setpgid] = A64_WRAP(sys_setpgid),
    [A64_SYS_getsid] = A64_WRAP(sys_getsid),
    [A64_SYS_setsid] = A64_WRAP(sys_setsid),
    [A64_SYS_gettid] = A64_WRAP(sys_gettid),
    [A64_SYS_set_tid_address] = A64_WRAP(sys_set_tid_address),
    [A64_SYS_nanosleep] = A64_WRAP(sys_nanosleep),
    [A64_SYS_getrusage] = A64_WRAP(sys_getrusage),
    [A64_SYS_sched_yield] = A64_WRAP(sys_sched_yield),
    [A64_SYS_setrlimit] = a64_wrap_sys_setrlimit,
    [A64_SYS_getrlimit] = a64_wrap_sys_getrlimit,
    [A64_SYS_gettimeofday] = A64_WRAP(sys_gettimeofday),
    [A64_SYS_settimeofday] = A64_WRAP(sys_settimeofday),
    [A64_SYS_times] = A64_WRAP(sys_times),

    // Uid/gid
    [A64_SYS_setuid] = A64_WRAP(sys_setuid),
    [A64_SYS_getuid] = A64_WRAP(sys_getuid),
    [A64_SYS_setgid] = A64_WRAP(sys_setgid),
    [A64_SYS_getgid] = A64_WRAP(sys_getgid),
    [A64_SYS_geteuid] = A64_WRAP(sys_geteuid),
    [A64_SYS_getegid] = A64_WRAP(sys_getegid),
    [A64_SYS_setreuid] = A64_WRAP(sys_setreuid),
    [A64_SYS_setregid] = A64_WRAP(sys_setregid),
    [A64_SYS_setresuid] = A64_WRAP(sys_setresuid),
    [A64_SYS_getresuid] = A64_WRAP(sys_getresuid),
    [A64_SYS_setresgid] = A64_WRAP(sys_setresgid),
    [A64_SYS_getresgid] = A64_WRAP(sys_getresgid),
    [A64_SYS_setgroups] = A64_WRAP(sys_setgroups),
    [A64_SYS_getgroups] = A64_WRAP(sys_getgroups),

    // Signals
    [A64_SYS_kill] = A64_WRAP(sys_kill),
    [A64_SYS_tkill] = A64_WRAP(sys_tkill),
    [A64_SYS_tgkill] = A64_WRAP(sys_tgkill),
    [A64_SYS_sigaltstack] = A64_WRAP(sys_sigaltstack),
    [A64_SYS_rt_sigaction] = A64_WRAP(sys_rt_sigaction),
    [A64_SYS_rt_sigprocmask] = A64_WRAP(sys_rt_sigprocmask),
    [A64_SYS_rt_sigreturn] = A64_WRAP(sys_rt_sigreturn_aarch64),
    [A64_SYS_rt_sigsuspend] = A64_WRAP(sys_rt_sigsuspend),
    [A64_SYS_rt_sigpending] = A64_WRAP(sys_rt_sigpending),

    // Filesystem
    [A64_SYS_mkdirat] = A64_WRAP(sys_mkdirat),
    [A64_SYS_mknodat] = A64_WRAP(sys_mknodat),
    [A64_SYS_unlinkat] = A64_WRAP(sys_unlinkat),
    [A64_SYS_symlinkat] = A64_WRAP(sys_symlinkat),
    [A64_SYS_linkat] = A64_WRAP(sys_linkat),
    [A64_SYS_renameat] = A64_WRAP(sys_renameat),
    [A64_SYS_renameat2] = A64_WRAP(sys_renameat2),
    [A64_SYS_fchownat] = A64_WRAP(sys_fchownat),
    [A64_SYS_faccessat] = A64_WRAP(sys_faccessat),
    [A64_SYS_readlinkat] = A64_WRAP(sys_readlinkat),
    [A64_SYS_newfstatat] = A64_WRAP(sys_newfstatat),
    [A64_SYS_fstat] = A64_WRAP(sys_fstat64),
    [A64_SYS_fstatfs] = A64_WRAP(sys_fstatfs),
    [A64_SYS_utimensat] = A64_WRAP(sys_utimensat),
    [A64_SYS_fchmodat] = A64_WRAP(sys_fchmodat),
    [A64_SYS_fchdir] = A64_WRAP(sys_fchdir),
    [A64_SYS_getcwd] = A64_WRAP(sys_getcwd),
    [A64_SYS_chdir] = A64_WRAP(sys_chdir),
    [A64_SYS_chroot] = A64_WRAP(sys_chroot),
    [A64_SYS_sync] = sys_enosys_stub,
    [A64_SYS_statfs] = A64_WRAP(sys_statfs),

    // Pipes/sockets
    [A64_SYS_pipe2] = A64_WRAP(sys_pipe2),
    [A64_SYS_epoll_create1] = A64_WRAP(sys_epoll_create),
    [A64_SYS_epoll_ctl] = A64_WRAP(sys_epoll_ctl),
    [A64_SYS_epoll_pwait] = A64_WRAP(sys_epoll_pwait),
    [A64_SYS_socket] = A64_WRAP(sys_socket),
    [A64_SYS_socketpair] = A64_WRAP(sys_socketpair),
    [A64_SYS_bind] = A64_WRAP(sys_bind),
    [A64_SYS_connect] = A64_WRAP(sys_connect),
    [A64_SYS_listen] = A64_WRAP(sys_listen),
    [A64_SYS_accept] = A64_WRAP(sys_accept),
    [A64_SYS_accept4] = A64_WRAP(sys_accept4),
    [A64_SYS_getsockname] = A64_WRAP(sys_getsockname),
    [A64_SYS_getpeername] = A64_WRAP(sys_getpeername),
    [A64_SYS_sendto] = A64_WRAP(sys_sendto),
    [A64_SYS_recvfrom] = A64_WRAP(sys_recvfrom),
    [A64_SYS_sendmsg] = A64_WRAP(sys_sendmsg),
    [A64_SYS_recvmsg] = A64_WRAP(sys_recvmsg),
    [A64_SYS_shutdown] = A64_WRAP(sys_shutdown),
    [A64_SYS_setsockopt] = A64_WRAP(sys_setsockopt),
    [A64_SYS_getsockopt] = A64_WRAP(sys_getsockopt),

    // Poll/select
    [A64_SYS_ppoll] = A64_WRAP(sys_ppoll),
    [A64_SYS_pselect6] = A64_WRAP(sys_pselect6),

    // Misc
    [A64_SYS_getrandom] = A64_WRAP(sys_getrandom),
    [A64_SYS_uname] = A64_WRAP(sys_uname),
    [A64_SYS_sethostname] = A64_WRAP(sys_sethostname),
    [A64_SYS_sysinfo] = A64_WRAP(sys_sysinfo),
    [A64_SYS_prctl] = A64_WRAP(sys_prctl),
    [A64_SYS_reboot] = A64_WRAP(sys_reboot),
};
