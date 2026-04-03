/*
 * Syscall Wrapper Functions
 * 
 * Wraps syscalls with varying signatures to match syscall_t (6 dword_t args).
 * This eliminates undefined behavior from casting incompatible function pointers.
 * 
 * Based on Linux kernel's SYSCALL_DEFINE pattern.
 */

#ifndef SYSCALL_WRAPPERS_H
#define SYSCALL_WRAPPERS_H

#import <IXLandLinuxRuntime/kernel/calls.h>

/* Helper to silence unused parameter warnings */
#define UNUSED(x) ((void)(x))

/* Wrapper for syscalls with 0 args */
#define WRAP_SYS_0(name) \
    static int wrap_##name(dword_t a, dword_t b, dword_t c, dword_t d, dword_t e, dword_t f) { \
        UNUSED(a); UNUSED(b); UNUSED(c); UNUSED(d); UNUSED(e); UNUSED(f); \
        return (int)name(); \
    }

/* Wrapper for syscalls with 1 arg (dword_t) */
#define WRAP_SYS_1(name) \
    static int wrap_##name(dword_t a, dword_t b, dword_t c, dword_t d, dword_t e, dword_t f) { \
        UNUSED(b); UNUSED(c); UNUSED(d); UNUSED(e); UNUSED(f); \
        return (int)name((dword_t)a); \
    }

/* Wrapper for syscalls with 2 args (dword_t, dword_t) */
#define WRAP_SYS_2(name) \
    static int wrap_##name(dword_t a, dword_t b, dword_t c, dword_t d, dword_t e, dword_t f) { \
        UNUSED(c); UNUSED(d); UNUSED(e); UNUSED(f); \
        return (int)name((dword_t)a, (dword_t)b); \
    }

/* Wrapper for syscalls with 3 args */
#define WRAP_SYS_3(name, t1, t2, t3) \
    static int wrap_##name(dword_t a, dword_t b, dword_t c, dword_t d, dword_t e, dword_t f) { \
        UNUSED(d); UNUSED(e); UNUSED(f); \
        return (int)name((t1)a, (t2)b, (t3)c); \
    }

/* Wrapper for syscalls with 4 args */
#define WRAP_SYS_4(name, t1, t2, t3, t4) \
    static int wrap_##name(dword_t a, dword_t b, dword_t c, dword_t d, dword_t e, dword_t f) { \
        UNUSED(e); UNUSED(f); \
        return (int)name((t1)a, (t2)b, (t3)c, (t4)d); \
    }

/* Wrapper for syscalls with 5 args */
#define WRAP_SYS_5(name, t1, t2, t3, t4, t5) \
    static int wrap_##name(dword_t a, dword_t b, dword_t c, dword_t d, dword_t e, dword_t f) { \
        UNUSED(f); \
        return (int)name((t1)a, (t2)b, (t3)c, (t4)d, (t5)e); \
    }

/* Wrapper for syscalls with 6 args */
#define WRAP_SYS_6(name, t1, t2, t3, t4, t5, t6) \
    static int wrap_##name(dword_t a, dword_t b, dword_t c, dword_t d, dword_t e, dword_t f) { \
        return (int)name((t1)a, (t2)b, (t3)c, (t4)d, (t5)e, (t6)f); \
    }

/* Convenience: Common syscall patterns */

/* File read/write family: (fd_t, addr_t, dword_t) */
#define WRAP_SYS_IO(name) WRAP_SYS_3(name, fd_t, addr_t, dword_t)

/* Open family: (addr_t, dword_t, mode_t_) */
#define WRAP_SYS_OPEN(name) WRAP_SYS_3(name, addr_t, dword_t, mode_t_)

/* Mode family: (addr_t, mode_t_) */
#define WRAP_SYS_MODE(name) WRAP_SYS_2(name)

/* Path family: (addr_t) */
#define WRAP_SYS_PATH(name) WRAP_SYS_1(name)

/* Uid family: (uid_t_) */
#define WRAP_SYS_UID(name) WRAP_SYS_1(name)

/* Pid family: (pid_t_) */
#define WRAP_SYS_PID(name) WRAP_SYS_1(name)

/* Signal family: (int_t, pid_t_) */
#define WRAP_SYS_KILL(name) WRAP_SYS_2(name)

/* Fd dup family: (fd_t, fd_t) */
#define WRAP_SYS_DUP(name) WRAP_SYS_2(name)

/* Fd flock family: (fd_t, dword_t) */
#define WRAP_SYS_FLOCK(name) WRAP_SYS_2(name)

/* 2-arg addr family: (addr_t, addr_t) */
#define WRAP_SYS_ADDR2(name) WRAP_SYS_2(name)

/* 2-arg dword family: (dword_t, dword_t) */
#define WRAP_SYS_DWORD2(name) WRAP_SYS_2(name)

/* mmap family: (addr_t, dword_t, dword_t, dword_t, fd_t, dword_t) */
#define WRAP_SYS_MMAP(name) \
    static int wrap_##name(dword_t a, dword_t b, dword_t c, dword_t d, dword_t e, dword_t f) { \
        return (int)name((addr_t)a, (dword_t)b, (dword_t)c, (dword_t)d, (fd_t)e, (dword_t)f); \
    }

/* clone: (dword_t, addr_t, addr_t, addr_t, addr_t) */
#define WRAP_SYS_CLONE(name) \
    static int wrap_##name(dword_t a, dword_t b, dword_t c, dword_t d, dword_t e, dword_t f) { \
        UNUSED(f); \
        return (int)name((dword_t)a, (addr_t)b, (addr_t)c, (addr_t)d, (addr_t)e); \
    }

/* execve: (addr_t, addr_t, addr_t) */
#define WRAP_SYS_EXECVE(name) \
    static int wrap_##name(dword_t a, dword_t b, dword_t c, dword_t d, dword_t e, dword_t f) { \
        UNUSED(d); UNUSED(e); UNUSED(f); \
        return (int)name((addr_t)a, (addr_t)b, (addr_t)c); \
    }

/* wait4: (pid_t_, addr_t, dword_t, addr_t) */
#define WRAP_SYS_WAIT4(name) \
    static int wrap_##name(dword_t a, dword_t b, dword_t c, dword_t d, dword_t e, dword_t f) { \
        UNUSED(e); UNUSED(f); \
        return (int)name((pid_t_)a, (addr_t)b, (dword_t)c, (addr_t)d); \
    }

/* setresuid/setresgid: (uid_t_, uid_t_, uid_t_) */
#define WRAP_SYS_SETRES(name) \
    static int wrap_##name(dword_t a, dword_t b, dword_t c, dword_t d, dword_t e, dword_t f) { \
        UNUSED(d); UNUSED(e); UNUSED(f); \
        return (int)name((uid_t_)a, (uid_t_)b, (uid_t_)c); \
    }

/* prlimit64: (pid_t_, dword_t, addr_t, addr_t) */
#define WRAP_SYS_PRLIMIT(name) \
    static int wrap_##name(dword_t a, dword_t b, dword_t c, dword_t d, dword_t e, dword_t f) { \
        UNUSED(e); UNUSED(f); \
        return (int)name((pid_t_)a, (dword_t)b, (addr_t)c, (addr_t)d); \
    }

/* Generate wrappers for common syscalls */

/* 0 args */
WRAP_SYS_0(sys_fork)
WRAP_SYS_0(sys_vfork)
WRAP_SYS_0(sys_getpid)
WRAP_SYS_0(sys_gettid)
WRAP_SYS_0(sys_getppid)
WRAP_SYS_0(sys_getpgrp)
WRAP_SYS_0(sys_geteuid)
WRAP_SYS_0(sys_getegid)
WRAP_SYS_0(sys_getuid)
WRAP_SYS_0(sys_getgid)
WRAP_SYS_0(sys_pause)
WRAP_SYS_0(sys_sync)
WRAP_SYS_0(sys_getsid)
WRAP_SYS_0(sys_setsid)
WRAP_SYS_0(sys_setpgrp)
WRAP_SYS_0(sys_sched_yield)
WRAP_SYS_0(sys_getpgid)
WRAP_SYS_0(sys_getpgrp)
WRAP_SYS_0(sys_getsid)
WRAP_SYS_0(sys_getgroups)
WRAP_SYS_0(sys_setgroups)
WRAP_SYS_0(sys_getsid)
WRAP_SYS_0(sys_getsid)
WRAP_SYS_0(sys_rt_sigreturn)
WRAP_SYS_0(sys_alarm)

/* 1 arg - dword_t */
WRAP_SYS_1(sys_exit)
WRAP_SYS_1(sys_exit_group)
WRAP_SYS_1(sys_dup)
WRAP_SYS_1(sys_close)
WRAP_SYS_1(sys_chdir)
WRAP_SYS_1(sys_time)
WRAP_SYS_1(sys_unlink)
WRAP_SYS_1(sys_rmdir)
WRAP_SYS_1(sys_times)
WRAP_SYS_1(sys_brk)
WRAP_SYS_1(sys_umask)
WRAP_SYS_1(sys_chroot)
WRAP_SYS_1(sys_fchdir)
WRAP_SYS_1(sys_set_tid_address)
WRAP_SYS_1(sys_epoll_create)
WRAP_SYS_1(sys_epoll_create1)
WRAP_SYS_1(sys_eventfd)
WRAP_SYS_1(sys_getdents)
WRAP_SYS_1(sys_getdents64)
WRAP_SYS_1(sys_uname)
WRAP_SYS_1(sys_sysinfo)
WRAP_SYS_1(sys_syncfs)

/* 1 arg - uid_t */
WRAP_SYS_UID(sys_setuid)
WRAP_SYS_UID(sys_setgid)

/* 2 args */
WRAP_SYS_KILL(sys_kill)
WRAP_SYS_DUP(sys_dup2)
WRAP_SYS_DUP(sys_dup3)
WRAP_SYS_FLOCK(sys_flock)
WRAP_SYS_FLOCK(sys_fsync)
WRAP_SYS_FLOCK(sys_fchmod)
WRAP_SYS_FLOCK(sys_fchown32)
WRAP_SYS_ADDR2(sys_link)
WRAP_SYS_ADDR2(sys_symlink)
WRAP_SYS_ADDR2(sys_rename)
WRAP_SYS_ADDR2(sys_mount)
WRAP_SYS_ADDR2(sys_umount2)
WRAP_SYS_ADDR2(sys_statfs)
WRAP_SYS_ADDR2(sys_fstatfs)
WRAP_SYS_ADDR2(sys_statfs64)
WRAP_SYS_ADDR2(sys_fstatfs64)
WRAP_SYS_ADDR2(sys_gettimeofday)
WRAP_SYS_ADDR2(sys_settimeofday)
WRAP_SYS_ADDR2(sys_munmap)
WRAP_SYS_ADDR2(sys_mprotect)
WRAP_SYS_ADDR2(sys_mlock)
WRAP_SYS_ADDR2(sys_msync)

/* 3 args - IO (read/write) */
WRAP_SYS_IO(sys_read)
WRAP_SYS_IO(sys_write)
WRAP_SYS_IO(sys_pread)
WRAP_SYS_IO(sys_pwrite)
WRAP_SYS_IO(sys_readv)
WRAP_SYS_IO(sys_writev)

/* 3 args - open style */
WRAP_SYS_OPEN(sys_open)
WRAP_SYS_OPEN(sys_openat)
WRAP_SYS_OPEN(sys_creat)
WRAP_SYS_OPEN(sys_mkdir)
WRAP_SYS_OPEN(sys_mknod)

/* 3 args - mode style */
WRAP_SYS_MODE(sys_chmod)
WRAP_SYS_MODE(sys_chown)
WRAP_SYS_MODE(sys_lchown)
WRAP_SYS_MODE(sys_fchmodat)
WRAP_SYS_MODE(sys_fchownat)

/* 3 args - path + size */
WRAP_SYS_3(sys_access, addr_t, dword_t, int_t)
WRAP_SYS_3(sys_readlink, addr_t, addr_t, dword_t)
WRAP_SYS_3(sys_waitpid, pid_t_, addr_t, dword_t)
WRAP_SYS_3(sys_waitid, int_t, pid_t_, addr_t)
WRAP_SYS_3(sys_setpgid, pid_t_, pid_t_, int_t)
WRAP_SYS_3(sys_getpriority, int_t, int_t, int_t)
WRAP_SYS_3(sys_setpriority, int_t, int_t, int_t)
WRAP_SYS_3(sys_nanosleep, addr_t, addr_t, int_t)
WRAP_SYS_3(sys_madvise, addr_t, dword_t, dword_t)
WRAP_SYS_3(sys_ioctl, fd_t, dword_t, dword_t)
WRAP_SYS_3(sys_fcntl, fd_t, dword_t, dword_t)
WRAP_SYS_3(sys_fcntl32, fd_t, dword_t, dword_t)
WRAP_SYS_3(sys_lseek, fd_t, dword_t, dword_t)

/* 4 args */
WRAP_SYS_4(sys_mknodat, fd_t, addr_t, mode_t_, dev_t_)
WRAP_SYS_4(sys_linkat, fd_t, addr_t, fd_t, addr_t)
WRAP_SYS_4(sys_symlinkat, addr_t, fd_t, addr_t, int_t)
WRAP_SYS_4(sys_renameat, fd_t, addr_t, fd_t, addr_t)
WRAP_SYS_4(sys_renameat2, fd_t, addr_t, fd_t, addr_t)
WRAP_SYS_4(sys_fchmodat, fd_t, addr_t, mode_t_, int_t)
WRAP_SYS_4(sys_fchownat, fd_t, addr_t, uid_t_, uid_t_)
WRAP_SYS_4(sys_newfstatat, fd_t, addr_t, addr_t, int_t)
WRAP_SYS_4(sys_fstatat64, fd_t, addr_t, addr_t, int_t)
WRAP_SYS_4(sys_waitid, int_t, pid_t_, addr_t, int_t)

/* Socket syscalls */
WRAP_SYS_4(sys_socket, int_t, int_t, int_t, int_t)
WRAP_SYS_4(sys_socketpair, int_t, int_t, int_t, addr_t)
WRAP_SYS_4(sys_bind, fd_t, addr_t, int_t, int_t)
WRAP_SYS_4(sys_connect, fd_t, addr_t, int_t, int_t)
WRAP_SYS_4(sys_listen, fd_t, int_t, int_t, int_t)
WRAP_SYS_4(sys_accept, fd_t, addr_t, addr_t, int_t)
WRAP_SYS_4(sys_accept4, fd_t, addr_t, addr_t, int_t)
WRAP_SYS_4(sys_getsockname, fd_t, addr_t, addr_t, int_t)
WRAP_SYS_4(sys_getpeername, fd_t, addr_t, addr_t, int_t)
WRAP_SYS_4(sys_shutdown, fd_t, int_t, int_t, int_t)
WRAP_SYS_4(sys_getsockopt, fd_t, int_t, int_t, addr_t)
WRAP_SYS_4(sys_setsockopt, fd_t, int_t, int_t, addr_t)
WRAP_SYS_4(sys_sendto, fd_t, addr_t, dword_t, dword_t)
WRAP_SYS_4(sys_recvfrom, fd_t, addr_t, dword_t, dword_t)
WRAP_SYS_4(sys_sendmsg, fd_t, addr_t, dword_t, int_t)
WRAP_SYS_4(sys_recvmsg, fd_t, addr_t, dword_t, int_t)

/* Special cases */
WRAP_SYS_CLONE(sys_clone)
WRAP_SYS_EXECVE(sys_execve)
WRAP_SYS_WAIT4(sys_wait4)
WRAP_SYS_SETRES(sys_setresuid)
WRAP_SYS_SETRES(sys_setresgid)
WRAP_SYS_PRLIMIT(sys_prlimit64)
WRAP_SYS_MMAP(sys_mmap)
WRAP_SYS_MMAP(sys_mmap2)

#endif /* SYSCALL_WRAPPERS_H */
