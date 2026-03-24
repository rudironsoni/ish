/*
 * aarch64 syscall dispatch table
 *
 * Maps aarch64 Linux syscall numbers to iSH's existing syscall handlers.
 * This bridges the aarch64 ABI to iSH's internal implementation.
 */

#include "kernel/aarch64/calls.h"
#include "kernel/calls.h"
#include "kernel/signal.h"
#include "kernel/errno.h"
#include "fs/sock.h"
#include <stddef.h>

// Syscall function type defined in kernel/calls.h
// All syscall declarations are in kernel/calls.h, kernel/signal.h, and fs/sock.h

// Stub for unimplemented syscalls
static int sys_enosys_stub(dword_t a, dword_t b, dword_t c, 
                           dword_t d, dword_t e, dword_t f) {
    (void)a; (void)b; (void)c; (void)d; (void)e; (void)f;
    return _ENOSYS;
}

// Map missing aarch64 syscalls to existing x86-style syscalls
// aarch64 doesn't have llseek, uses lseek with 64-bit offset
#define sys_llseek sys__llseek

// aarch64 doesn't have separate access syscall, uses faccessat
#define sys_access sys_faccessat

// Forward declarations for syscalls referenced in table but not in standard headers
extern dword_t sys_newfstatat(fd_t, addr_t, addr_t, int_t);
extern dword_t sys_pselect6(fd_t, addr_t, addr_t, addr_t, addr_t, addr_t);
extern dword_t sys_epoll_create1(dword_t);
extern dword_t sys_accept4(fd_t, addr_t, addr_t, int_t);

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

/*
 * aarch64 syscall dispatch table
 *
 * Maps aarch64 syscall numbers (from arch/arm64/include/uapi/asm/unistd.h)
 * to iSH syscall handlers.
 */

#include "kernel/aarch64/calls.h"
#include "kernel/calls.h"
#include "kernel/errno.h"
#include <stddef.h>

// Syscall function type is defined in kernel/calls.h
// typedef int (*syscall_t)(dword_t, dword_t, dword_t, dword_t, dword_t, dword_t);

// Stub for unimplemented syscalls
static dword_t sys_enosys(void) {
    return _ENOSYS;
}

// All syscall declarations are in kernel/calls.h, kernel/signal.h, and fs/sock.h
// No forward declarations needed here

/*
 * aarch64 syscall dispatch table
 *
 * Maps aarch64 syscall numbers (from arch/arm64/include/uapi/asm/unistd.h)
 * to iSH syscall handlers.
 */
syscall_t syscall_table_a64[A64_SYS_MAX] = {
    // File operations
    [A64_SYS_read] = (syscall_t)sys_read,
    [A64_SYS_write] = (syscall_t)sys_write,
    [A64_SYS_openat] = (syscall_t)sys_openat,
    [A64_SYS_close] = (syscall_t)sys_close,
    [A64_SYS_lseek] = (syscall_t)sys_lseek,
    [A64_SYS_llseek] = (syscall_t)sys_llseek,
    [A64_SYS_ioctl] = (syscall_t)sys_ioctl,
    [A64_SYS_fcntl] = (syscall_t)sys_fcntl,
    [A64_SYS_dup] = (syscall_t)sys_dup,
    [A64_SYS_dup3] = (syscall_t)sys_dup3,
    [A64_SYS_fsync] = (syscall_t)sys_fsync,
    [A64_SYS_flock] = (syscall_t)sys_flock,
    [A64_SYS_readv] = (syscall_t)sys_readv,
    [A64_SYS_writev] = (syscall_t)sys_writev,
    [A64_SYS_pread64] = (syscall_t)sys_pread,
    [A64_SYS_pwrite64] = (syscall_t)sys_pwrite,

    // Memory
    [A64_SYS_brk] = (syscall_t)sys_brk,
    [A64_SYS_mmap] = (syscall_t)sys_mmap,
    [A64_SYS_munmap] = (syscall_t)sys_munmap,
    [A64_SYS_mprotect] = (syscall_t)sys_mprotect,
    [A64_SYS_madvise] = (syscall_t)sys_madvise,

    // Process
    [A64_SYS_exit] = (syscall_t)sys_exit,
    [A64_SYS_exit_group] = (syscall_t)sys_exit_group,
    [A64_SYS_fork] = (syscall_t)sys_fork,
    [A64_SYS_vfork] = (syscall_t)sys_vfork,
    [A64_SYS_clone] = (syscall_t)sys_clone,
    [A64_SYS_execve] = (syscall_t)sys_execve,
    [A64_SYS_wait4] = (syscall_t)sys_wait4,
    [A64_SYS_getpid] = (syscall_t)sys_getpid,
    [A64_SYS_getppid] = (syscall_t)sys_getppid,
    [A64_SYS_gettid] = (syscall_t)sys_gettid,

    // User/Group
    [A64_SYS_getuid] = (syscall_t)sys_getuid,
    [A64_SYS_getgid] = (syscall_t)sys_getgid,
    [A64_SYS_geteuid] = (syscall_t)sys_geteuid,
    [A64_SYS_getegid] = (syscall_t)sys_getegid,
    [A64_SYS_setuid] = (syscall_t)sys_setuid,
    [A64_SYS_setgid] = (syscall_t)sys_setgid,
    [A64_SYS_getgroups] = (syscall_t)sys_getgroups,
    [A64_SYS_setgroups] = (syscall_t)sys_setgroups,

    // Signals
    [A64_SYS_rt_sigaction] = (syscall_t)sys_rt_sigaction,
    [A64_SYS_rt_sigprocmask] = (syscall_t)sys_rt_sigprocmask,
    [A64_SYS_rt_sigreturn] = (syscall_t)sys_rt_sigreturn,
    [A64_SYS_kill] = (syscall_t)sys_kill,
    [A64_SYS_tkill] = (syscall_t)sys_tkill,
    [A64_SYS_tgkill] = (syscall_t)sys_tgkill,
    [A64_SYS_sigaltstack] = (syscall_t)sys_sigaltstack,
    [A64_SYS_pause] = (syscall_t)sys_pause,
    [A64_SYS_rt_sigsuspend] = (syscall_t)sys_rt_sigsuspend,

    // Time
    [A64_SYS_gettimeofday] = (syscall_t)sys_gettimeofday,
    [A64_SYS_settimeofday] = (syscall_t)sys_settimeofday,
    [A64_SYS_nanosleep] = (syscall_t)sys_nanosleep,
    [A64_SYS_clock_gettime] = (syscall_t)sys_clock_gettime,
    [A64_SYS_clock_getres] = (syscall_t)sys_clock_getres,
    [A64_SYS_getrusage] = (syscall_t)sys_getrusage,
    [A64_SYS_times] = (syscall_t)sys_times,

    // Filesystem
    [A64_SYS_openat] = (syscall_t)sys_openat,
    [A64_SYS_creat] = (syscall_t)sys_creat,
    [A64_SYS_access] = (syscall_t)sys_access,
    [A64_SYS_faccessat] = (syscall_t)sys_faccessat,
    [A64_SYS_statfs] = (syscall_t)sys_statfs,
    [A64_SYS_fstatfs] = (syscall_t)sys_fstatfs,
    [A64_SYS_statx] = (syscall_t)sys_newfstatat,  // Map to fstatat for now
    [A64_SYS_readlinkat] = (syscall_t)sys_readlinkat,
    [A64_SYS_symlinkat] = (syscall_t)sys_symlinkat,
    [A64_SYS_linkat] = (syscall_t)sys_linkat,
    [A64_SYS_unlinkat] = (syscall_t)sys_unlinkat,
    [A64_SYS_renameat] = (syscall_t)sys_renameat,
    [A64_SYS_mkdirat] = (syscall_t)sys_mkdirat,
    [A64_SYS_mknodat] = (syscall_t)sys_mknodat,
    [A64_SYS_fchmodat] = (syscall_t)sys_fchmodat,
    [A64_SYS_fchownat] = (syscall_t)sys_fchownat,
    [A64_SYS_chdir] = (syscall_t)sys_chdir,
    [A64_SYS_fchdir] = (syscall_t)sys_fchdir,
    [A64_SYS_getcwd] = (syscall_t)sys_getcwd,
    [A64_SYS_chroot] = (syscall_t)sys_chroot,
    [A64_SYS_umask] = (syscall_t)sys_umask,

    // Directory
    [A64_SYS_getdents64] = (syscall_t)sys_getdents64,

    // Polling
    [A64_SYS_poll] = (syscall_t)sys_poll,
    [A64_SYS_ppoll] = (syscall_t)sys_ppoll,
    [A64_SYS_select] = (syscall_t)sys_select,
    [A64_SYS_pselect6] = (syscall_t)sys_pselect,
    [A64_SYS_epoll_create1] = (syscall_t)sys_epoll_create1,
    [A64_SYS_epoll_ctl] = (syscall_t)sys_epoll_ctl,
    [A64_SYS_epoll_pwait] = (syscall_t)sys_epoll_pwait,

    // Pipes
    [A64_SYS_pipe2] = (syscall_t)sys_pipe2,

    // Eventfd
    [A64_SYS_eventfd2] = (syscall_t)sys_eventfd2,

    // Sockets
    [A64_SYS_socket] = (syscall_t)sys_socket,
    [A64_SYS_socketpair] = (syscall_t)sys_socketpair,
    [A64_SYS_bind] = (syscall_t)sys_bind,
    [A64_SYS_connect] = (syscall_t)sys_connect,
    [A64_SYS_listen] = (syscall_t)sys_listen,
    [A64_SYS_accept] = (syscall_t)sys_accept,
    [A64_SYS_accept4] = (syscall_t)sys_accept4,
    [A64_SYS_getsockname] = (syscall_t)sys_getsockname,
    [A64_SYS_getpeername] = (syscall_t)sys_getpeername,
    [A64_SYS_sendto] = (syscall_t)sys_sendto,
    [A64_SYS_recvfrom] = (syscall_t)sys_recvfrom,
    [A64_SYS_sendmsg] = (syscall_t)sys_sendmsg,
    [A64_SYS_recvmsg] = (syscall_t)sys_recvmsg,
    [A64_SYS_setsockopt] = (syscall_t)sys_setsockopt,
    [A64_SYS_getsockopt] = (syscall_t)sys_getsockopt,
    [A64_SYS_shutdown] = (syscall_t)sys_shutdown,

    // Futex
    [A64_SYS_futex] = (syscall_t)sys_futex,

    // Resources
    [A64_SYS_getrlimit] = (syscall_t)sys_getrlimit,
    [A64_SYS_setrlimit] = (syscall_t)sys_setrlimit,
    [A64_SYS_prlimit64] = (syscall_t)sys_prlimit64,

    // Misc
    [A64_SYS_uname] = (syscall_t)sys_uname,
    [A64_SYS_sethostname] = (syscall_t)sys_sethostname,
    [A64_SYS_sysinfo] = (syscall_t)sys_sysinfo,
    [A64_SYS_prctl] = (syscall_t)sys_prctl,
    [A64_SYS_set_tid_address] = (syscall_t)sys_set_tid_address,
    [A64_SYS_reboot] = (syscall_t)sys_reboot,
    [A64_SYS_sync] = (syscall_t)sys_sync,
};
