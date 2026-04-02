/*
 * aarch64 syscall dispatch table
 *
 * Maps aarch64 Linux syscall numbers (from arch/arm64/include/uapi/asm/unistd.h)
 * to iSH syscall handlers.
 */

#include "fs/sock.h"
#include "kernel/aarch64/calls.h"
#include "kernel/calls.h"
#include "kernel/errno.h"
#include "kernel/signal.h"

#include <stddef.h>

// Stub for unimplemented syscalls
static qword_t sys_enosys_stub(qword_t a, qword_t b, qword_t c, qword_t d, qword_t e, qword_t f)
{
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)e;
    (void)f;
    return _ENOSYS;
}

// Map missing aarch64 syscalls to existing x86-style syscalls
// aarch64 doesn't have llseek, uses lseek with 64-bit offset
#define sys_llseek sys__llseek

// aarch64 doesn't have separate access syscall, uses faccessat
#define sys_access sys_faccessat

// aarch64 doesn't have creat, uses openat
#define sys_creat sys_open

// aarch64 doesn't have select, uses pselect6
#define sys_select sys_pselect6

// aarch64 doesn't have poll, uses ppoll
#define sys_poll sys_ppoll

// aarch64 doesn't have pause, uses rt_sigsuspend
#define sys_pause sys_rt_sigsuspend

// aarch64 doesn't have epoll_create, uses epoll_create1
#define sys_epoll_create sys_epoll_create1

// aarch64 doesn't have getrlimit, uses prlimit64
#define sys_getrlimit sys_prlimit64

// aarch64 doesn't have setrlimit, uses prlimit64
#define sys_setrlimit sys_prlimit64

// aarch64 doesn't have sync, uses syncfs (stub for now)
#define sys_sync sys_enosys_stub

// Forward declarations for syscalls referenced in table but not in standard headers
extern dword_t sys_newfstatat(fd_t, addr_t, addr_t, int_t);
extern dword_t sys_pselect6(fd_t, addr_t, addr_t, addr_t, addr_t, addr_t);
extern dword_t sys_epoll_create1(dword_t);
extern dword_t sys_execveat(fd_t, addr_t, addr_t, addr_t, int_t);


// ============================================================================
// STUB IMPLEMENTATIONS FOR AARCH64 BRING-UP
// These are intentionally verbose TODOs to prevent silent failures
// ============================================================================

// TODO: STUB - sys_execveat needs proper implementation for AArch64 bring-up
dword_t sys_execveat(fd_t dirfd, addr_t pathname, addr_t argv, addr_t envp, int_t flags)
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
dword_t sys_newfstatat(fd_t dirfd, addr_t pathname, addr_t statbuf, int_t flags)
{
    (void)dirfd;
    (void)pathname;
    (void)statbuf;
    (void)flags;
    FIXME("TODO: sys_newfstatat NOT IMPLEMENTED - NEEDS PROPER AARCH64 FILE SUPPORT");
    return _ENOSYS;
}

// TODO: STUB - sys_pselect6 needs proper implementation for AArch64 bring-up
dword_t sys_pselect6(fd_t nfds, addr_t readfds, addr_t writefds, addr_t exceptfds,
                     addr_t timeout, addr_t sigmask)
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

a64_syscall_t syscall_table_a64[A64_SYS_MAX] = {
    // File operations
    [A64_SYS_read] = (a64_syscall_t)sys_read,
    [A64_SYS_write] = (a64_syscall_t)sys_write,
    [A64_SYS_openat] = (a64_syscall_t)sys_openat,
    [A64_SYS_close] = (a64_syscall_t)sys_close,
    [A64_SYS_lseek] = (a64_syscall_t)sys_lseek,
    [A64_SYS_llseek] = (a64_syscall_t)sys_llseek,
    [A64_SYS_ioctl] = (a64_syscall_t)sys_ioctl,
    [A64_SYS_fcntl] = (a64_syscall_t)sys_fcntl,
    [A64_SYS_dup] = (a64_syscall_t)sys_dup,
    [A64_SYS_dup3] = (a64_syscall_t)sys_dup3,
    [A64_SYS_fsync] = (a64_syscall_t)sys_fsync,
    [A64_SYS_flock] = (a64_syscall_t)sys_flock,
    [A64_SYS_readv] = (a64_syscall_t)sys_readv,
    [A64_SYS_writev] = (a64_syscall_t)sys_writev,
    [A64_SYS_pread64] = (a64_syscall_t)sys_pread,
    [A64_SYS_pwrite64] = (a64_syscall_t)sys_pwrite,

    // Memory
    [A64_SYS_brk] = (a64_syscall_t)sys_brk,
    [A64_SYS_mmap] = (a64_syscall_t)sys_mmap,
    [A64_SYS_mprotect] = (a64_syscall_t)sys_mprotect,
    [A64_SYS_munmap] = (a64_syscall_t)sys_munmap,
    [A64_SYS_mremap] = (a64_syscall_t)sys_mremap,
    [A64_SYS_madvise] = (a64_syscall_t)sys_madvise,

    // Process/thread
    [A64_SYS_clone] = (a64_syscall_t)sys_clone,
    [A64_SYS_fork] = (a64_syscall_t)sys_fork,
    [A64_SYS_vfork] = (a64_syscall_t)sys_vfork,
    [A64_SYS_exit] = (a64_syscall_t)sys_exit,
    [A64_SYS_exit_group] = (a64_syscall_t)sys_exit_group,
    [A64_SYS_wait4] = (a64_syscall_t)sys_wait4,
    [A64_SYS_execve] = (a64_syscall_t)sys_execve,
    [A64_SYS_execveat] = (a64_syscall_t)sys_execveat,
    [A64_SYS_getpid] = (a64_syscall_t)sys_getpid,
    [A64_SYS_getppid] = (a64_syscall_t)sys_getppid,
    [A64_SYS_getpgrp] = (a64_syscall_t)sys_getpgrp,
    [A64_SYS_getpgid] = (a64_syscall_t)sys_getpgid,
    [A64_SYS_setpgid] = (a64_syscall_t)sys_setpgid,
    [A64_SYS_getsid] = (a64_syscall_t)sys_getsid,
    [A64_SYS_setsid] = (a64_syscall_t)sys_setsid,
    [A64_SYS_gettid] = (a64_syscall_t)sys_gettid,
    [A64_SYS_set_tid_address] = (a64_syscall_t)sys_set_tid_address,
    [A64_SYS_nanosleep] = (a64_syscall_t)sys_nanosleep,
    [A64_SYS_getrusage] = (a64_syscall_t)sys_getrusage,
    [A64_SYS_sched_yield] = (a64_syscall_t)sys_sched_yield,
    [A64_SYS_setrlimit] = (a64_syscall_t)sys_setrlimit,
    [A64_SYS_getrlimit] = (a64_syscall_t)sys_getrlimit,
    [A64_SYS_gettimeofday] = (a64_syscall_t)sys_gettimeofday,
    [A64_SYS_settimeofday] = (a64_syscall_t)sys_settimeofday,
    [A64_SYS_times] = (a64_syscall_t)sys_times,

    // Uid/gid
    [A64_SYS_setuid] = (a64_syscall_t)sys_setuid,
    [A64_SYS_getuid] = (a64_syscall_t)sys_getuid,
    [A64_SYS_setgid] = (a64_syscall_t)sys_setgid,
    [A64_SYS_getgid] = (a64_syscall_t)sys_getgid,
    [A64_SYS_geteuid] = (a64_syscall_t)sys_geteuid,
    [A64_SYS_getegid] = (a64_syscall_t)sys_getegid,
    [A64_SYS_setreuid] = (a64_syscall_t)sys_setreuid,
    [A64_SYS_setregid] = (a64_syscall_t)sys_setregid,
    [A64_SYS_setresuid] = (a64_syscall_t)sys_setresuid,
    [A64_SYS_getresuid] = (a64_syscall_t)sys_getresuid,
    [A64_SYS_setresgid] = (a64_syscall_t)sys_setresgid,
    [A64_SYS_getresgid] = (a64_syscall_t)sys_getresgid,
    [A64_SYS_setgroups] = (a64_syscall_t)sys_setgroups,
    [A64_SYS_getgroups] = (a64_syscall_t)sys_getgroups,

    // Signals
    [A64_SYS_kill] = (a64_syscall_t)sys_kill,
    [A64_SYS_tkill] = (a64_syscall_t)sys_tkill,
    [A64_SYS_tgkill] = (a64_syscall_t)sys_tgkill,
    [A64_SYS_sigaltstack] = (a64_syscall_t)sys_sigaltstack,
    [A64_SYS_rt_sigaction] = (a64_syscall_t)sys_rt_sigaction,
    [A64_SYS_rt_sigprocmask] = (a64_syscall_t)sys_rt_sigprocmask,
    [A64_SYS_rt_sigreturn] = (a64_syscall_t)sys_rt_sigreturn,
    [A64_SYS_rt_sigsuspend] = (a64_syscall_t)sys_rt_sigsuspend,
    [A64_SYS_rt_sigpending] = (a64_syscall_t)sys_rt_sigpending,

    // Filesystem
    [A64_SYS_mkdirat] = (a64_syscall_t)sys_mkdirat,
    [A64_SYS_mknodat] = (a64_syscall_t)sys_mknodat,
    [A64_SYS_unlinkat] = (a64_syscall_t)sys_unlinkat,
    [A64_SYS_symlinkat] = (a64_syscall_t)sys_symlinkat,
    [A64_SYS_linkat] = (a64_syscall_t)sys_linkat,
    [A64_SYS_renameat] = (a64_syscall_t)sys_renameat,
    [A64_SYS_renameat2] = (a64_syscall_t)sys_renameat2,
    [A64_SYS_fchownat] = (a64_syscall_t)sys_fchownat,
    [A64_SYS_faccessat] = (a64_syscall_t)sys_faccessat,
    [A64_SYS_readlinkat] = (a64_syscall_t)sys_readlinkat,
    [A64_SYS_newfstatat] = (a64_syscall_t)sys_newfstatat,
    [A64_SYS_fstat] = (a64_syscall_t)sys_fstat64,
    [A64_SYS_fstatfs] = (a64_syscall_t)sys_fstatfs,
    [A64_SYS_utimensat] = (a64_syscall_t)sys_utimensat,
    [A64_SYS_fchmodat] = (a64_syscall_t)sys_fchmodat,
    // [A64_SYS_ftruncate] = (a64_syscall_t)sys_ftruncate,  // Not implemented
    // [A64_SYS_fchown] = (a64_syscall_t)sys_fchown,        // Not implemented
    [A64_SYS_fchdir] = (a64_syscall_t)sys_fchdir,
    [A64_SYS_getcwd] = (a64_syscall_t)sys_getcwd,
    [A64_SYS_chdir] = (a64_syscall_t)sys_chdir,
    [A64_SYS_chroot] = (a64_syscall_t)sys_chroot,
    [A64_SYS_access] = (a64_syscall_t)sys_access,
    [A64_SYS_sync] = (a64_syscall_t)sys_sync,
    [A64_SYS_fsync] = (a64_syscall_t)sys_fsync,
    // [A64_SYS_fdatasync] = (a64_syscall_t)sys_fdatasync,  // Not implemented
    [A64_SYS_statfs] = (a64_syscall_t)sys_statfs,
    // [A64_SYS_statfs64] = (a64_syscall_t)sys_statfs64,    // Not implemented
    // [A64_SYS_fstatfs64] = (a64_syscall_t)sys_fstatfs64,  // Not implemented
    // [A64_SYS_readlink] = (a64_syscall_t)sys_readlink,    // Not defined
    // [A64_SYS_truncate] = (a64_syscall_t)sys_truncate,    // Not implemented
    [A64_SYS_flock] = (a64_syscall_t)sys_flock,

    // Pipes/sockets
    [A64_SYS_pipe2] = (a64_syscall_t)sys_pipe2,
    [A64_SYS_socket] = (a64_syscall_t)sys_socket,
    [A64_SYS_socketpair] = (a64_syscall_t)sys_socketpair,
    [A64_SYS_bind] = (a64_syscall_t)sys_bind,
    [A64_SYS_connect] = (a64_syscall_t)sys_connect,
    [A64_SYS_listen] = (a64_syscall_t)sys_listen,
    [A64_SYS_accept] = (a64_syscall_t)sys_accept,
    [A64_SYS_accept4] = (a64_syscall_t)sys_accept4,
    [A64_SYS_getsockname] = (a64_syscall_t)sys_getsockname,
    [A64_SYS_getpeername] = (a64_syscall_t)sys_getpeername,
    [A64_SYS_sendto] = (a64_syscall_t)sys_sendto,
    [A64_SYS_recvfrom] = (a64_syscall_t)sys_recvfrom,
    [A64_SYS_sendmsg] = (a64_syscall_t)sys_sendmsg,
    [A64_SYS_recvmsg] = (a64_syscall_t)sys_recvmsg,
    [A64_SYS_shutdown] = (a64_syscall_t)sys_shutdown,
    [A64_SYS_setsockopt] = (a64_syscall_t)sys_setsockopt,
    [A64_SYS_getsockopt] = (a64_syscall_t)sys_getsockopt,

    // Poll/select
    [A64_SYS_ppoll] = (a64_syscall_t)sys_ppoll,
    [A64_SYS_pselect6] = (a64_syscall_t)sys_pselect6,
    [A64_SYS_poll] = (a64_syscall_t)sys_poll,
    [A64_SYS_select] = (a64_syscall_t)sys_select,

    // Misc
    [A64_SYS_ioctl] = (a64_syscall_t)sys_ioctl,
    [A64_SYS_fcntl] = (a64_syscall_t)sys_fcntl,
    [A64_SYS_getrandom] = (a64_syscall_t)sys_getrandom,
    [A64_SYS_uname] = (a64_syscall_t)sys_uname,
    [A64_SYS_sethostname] = (a64_syscall_t)sys_sethostname,
    [A64_SYS_sysinfo] = (a64_syscall_t)sys_sysinfo,
    [A64_SYS_prctl] = (a64_syscall_t)sys_prctl,
    [A64_SYS_set_tid_address] = (a64_syscall_t)sys_set_tid_address,
    [A64_SYS_reboot] = (a64_syscall_t)sys_reboot,
    [A64_SYS_sync] = (a64_syscall_t)sys_sync,
};
