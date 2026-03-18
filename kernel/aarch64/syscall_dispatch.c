/*
 * aarch64 syscall dispatch table
 *
 * Maps aarch64 Linux syscall numbers to iSH's existing syscall handlers.
 * This bridges the aarch64 ABI to iSH's internal implementation.
 */

#include "kernel/aarch64/calls.h"
#include "kernel/calls.h"
#include "kernel/errno.h"
#include <stddef.h>

// Syscall function type matching iSH's convention
typedef dword_t (*syscall_t)(dword_t, dword_t, dword_t, dword_t, dword_t, dword_t);

// Stub for unimplemented syscalls
static dword_t sys_enosys(void) {
    return _ENOSYS;
}

// Forward declarations for syscall handlers
// File operations
dword_t sys_read(fd_t fd_no, addr_t buf_addr, dword_t size);
dword_t sys_write(fd_t fd_no, addr_t buf_addr, dword_t size);
dword_t sys_openat(fd_t at_f, addr_t path_addr, dword_t flags, dword_t mode);
dword_t sys_close(fd_t fd);
dword_t sys_lseek(fd_t f, dword_t off, dword_t whence);
dword_t sys_llseek(fd_t f, dword_t off_high, dword_t off_low, addr_t res_addr, dword_t whence);
dword_t sys_ioctl(fd_t f, dword_t cmd, dword_t arg);
dword_t sys_fcntl(fd_t f, dword_t cmd, dword_t arg);
dword_t sys_dup(fd_t fd);
dword_t sys_dup2(fd_t fd, fd_t new_fd);
dword_t sys_dup3(fd_t f, fd_t new_f, int_t flags);
dword_t sys_fsync(fd_t f);
dword_t sys_flock(fd_t fd, dword_t operation);

// Memory management
dword_t sys_brk(addr_t new_brk);
dword_t sys_mmap(addr_t addr, dword_t len, dword_t prot, dword_t flags, fd_t fd_no, dword_t offset);
dword_t sys_munmap(addr_t addr, dword_t len);
dword_t sys_mprotect(addr_t addr, dword_t len, dword_t prot);
dword_t sys_madvise(addr_t addr, dword_t len, dword_t advice);

// Process management
dword_t sys_exit(dword_t status);
dword_t sys_exit_group(dword_t status);
dword_t sys_fork(void);
dword_t sys_vfork(void);
dword_t sys_clone(dword_t flags, addr_t stack, addr_t ptid, addr_t tls, addr_t ctid);
dword_t sys_execve(addr_t file, addr_t argv, addr_t envp);
dword_t sys_wait4(pid_t_ pid, addr_t status_addr, dword_t options, addr_t rusage_addr);
dword_t sys_waitpid(pid_t_ pid, addr_t status_addr, dword_t options);
dword_t sys_getpid(void);
dword_t sys_getppid(void);
dword_t sys_gettid(void);

// User/Group
dword_t sys_getuid(void);
dword_t sys_getgid(void);
dword_t sys_geteuid(void);
dword_t sys_getegid(void);
dword_t sys_setuid(dword_t uid);
dword_t sys_setgid(dword_t gid);
dword_t sys_getgroups(dword_t size, addr_t list);
dword_t sys_setgroups(dword_t size, addr_t list);

// Signals
dword_t sys_rt_sigaction(int_t sig, addr_t act_addr, addr_t oact_addr, dword_t sigset_size);
dword_t sys_rt_sigprocmask(dword_t how, addr_t set_addr, addr_t oldset_addr, dword_t sigset_size);
dword_t sys_rt_sigreturn(void);
dword_t sys_kill(pid_t_ pid, dword_t sig);
dword_t sys_tkill(pid_t_ tid, dword_t sig);
dword_t sys_tgkill(pid_t_ tgid, pid_t_ tid, dword_t sig);
dword_t sys_sigaltstack(addr_t ss_addr, addr_t old_ss_addr);
dword_t sys_pause(void);
dword_t sys_rt_sigsuspend(addr_t mask_addr, dword_t size);

// Time
dword_t sys_gettimeofday(addr_t tv_addr, addr_t tz_addr);
dword_t sys_settimeofday(addr_t tv_addr, addr_t tz_addr);
dword_t sys_nanosleep(addr_t req_addr, addr_t rem_addr);
dword_t sys_clock_gettime(dword_t clock, addr_t ts_addr);
dword_t sys_clock_getres(dword_t clock, addr_t ts_addr);
dword_t sys_getrusage(dword_t who, addr_t rusage_addr);
dword_t sys_times(addr_t tbuf_addr);

// Filesystem
dword_t sys_open(addr_t path_addr, dword_t flags, dword_t mode);
dword_t sys_creat(addr_t path_addr, dword_t mode);
dword_t sys_access(addr_t path_addr, dword_t mode);
dword_t sys_faccessat(fd_t at_f, addr_t path, mode_t_ mode, dword_t flags);
dword_t sys_stat64(addr_t path_addr, addr_t statbuf_addr);
dword_t sys_lstat64(addr_t path_addr, addr_t statbuf_addr);
dword_t sys_fstat64(fd_t fd, addr_t statbuf_addr);
dword_t sys_newfstatat(fd_t at_f, addr_t path_addr, addr_t statbuf_addr, dword_t flags);
dword_t sys_readlink(addr_t path, addr_t buf, dword_t bufsize);
dword_t sys_readlinkat(fd_t at_f, addr_t path, addr_t buf, dword_t bufsize);
dword_t sys_symlink(addr_t target_addr, addr_t link_addr);
dword_t sys_symlinkat(addr_t target_addr, fd_t at_f, addr_t link_addr);
dword_t sys_link(addr_t src_addr, addr_t dst_addr);
dword_t sys_linkat(fd_t src_at_f, addr_t src_addr, fd_t dst_at_f, addr_t dst_addr);
dword_t sys_unlink(addr_t path_addr);
dword_t sys_unlinkat(fd_t at_f, addr_t path_addr, int_t flags);
dword_t sys_rmdir(addr_t path_addr);
dword_t sys_rename(addr_t src_addr, addr_t dst_addr);
dword_t sys_renameat(fd_t src_at_f, addr_t src_addr, fd_t dst_at_f, addr_t dst_addr);
dword_t sys_mkdir(addr_t path_addr, dword_t mode);
dword_t sys_mkdirat(fd_t at_f, addr_t path_addr, dword_t mode);
dword_t sys_mknod(addr_t path_addr, mode_t_ mode, dev_t_ dev);
dword_t sys_mknodat(fd_t at_f, addr_t path_addr, mode_t_ mode, dev_t_ dev);
dword_t sys_chmod(addr_t path_addr, dword_t mode);
dword_t sys_fchmod(fd_t f, dword_t mode);
dword_t sys_fchmodat(fd_t at_f, addr_t path_addr, dword_t mode, dword_t flags);
dword_t sys_chown(addr_t path_addr, dword_t owner, dword_t group);
dword_t sys_lchown(addr_t path_addr, dword_t owner, dword_t group);
dword_t sys_fchown(fd_t f, dword_t owner, dword_t group);
dword_t sys_fchownat(fd_t at_f, addr_t path_addr, dword_t owner, dword_t group, int_t flags);
dword_t sys_chdir(addr_t path_addr);
dword_t sys_fchdir(fd_t f);
dword_t sys_getcwd(addr_t buf_addr, dword_t size);
dword_t sys_chroot(addr_t path_addr);
dword_t sys_umask(dword_t mask);

// Directory operations
dword_t sys_getdents64(fd_t f, addr_t dirents_addr, dword_t count);

// Polling
dword_t sys_poll(addr_t fds, dword_t nfds, int_t timeout);
dword_t sys_select(fd_t nfds, addr_t readfds_addr, addr_t writefds_addr, addr_t exceptfds_addr, addr_t timeout_addr);
dword_t sys_pselect(fd_t nfds, addr_t readfds_addr, addr_t writefds_addr, addr_t exceptfds_addr, addr_t timeout_addr, addr_t sigmask_addr);
dword_t sys_ppoll(addr_t fds, dword_t nfds, addr_t timeout_addr, addr_t sigmask_addr, dword_t sigsetsize);
dword_t sys_epoll_create(dword_t flags);
dword_t sys_epoll_create1(dword_t flags);
dword_t sys_epoll_ctl(fd_t epoll_f, dword_t op, fd_t f, addr_t event_addr);
dword_t sys_epoll_pwait(fd_t epoll_f, addr_t events_addr, dword_t maxevents, dword_t timeout, addr_t sigmask_addr, dword_t sigsetsize);
dword_t sys_epoll_wait(fd_t epoll_f, addr_t events_addr, dword_t maxevents, dword_t timeout);

// Pipes
dword_t sys_pipe(addr_t pipes_addr);
dword_t sys_pipe2(addr_t pipes_addr, dword_t flags);

// Eventfd
dword_t sys_eventfd(dword_t initval);
dword_t sys_eventfd2(dword_t initval, dword_t flags);

// Socket-related (socketcall multiplexes these on x86, separate on aarch64)
dword_t sys_socket(dword_t domain, dword_t type, dword_t protocol);
dword_t sys_socketpair(dword_t domain, dword_t type, dword_t protocol, addr_t socks_addr);
dword_t sys_bind(fd_t sock_fd, addr_t sockaddr_addr, dword_t sockaddr_len);
dword_t sys_connect(fd_t sock_fd, addr_t sockaddr_addr, dword_t sockaddr_len);
dword_t sys_listen(fd_t sock_fd, dword_t backlog);
dword_t sys_accept(fd_t sock_fd, addr_t sockaddr_addr, addr_t sockaddr_len_addr);
dword_t sys_accept4(fd_t sock_fd, addr_t sockaddr_addr, addr_t sockaddr_len_addr, dword_t flags);
dword_t sys_getsockname(fd_t sock_fd, addr_t sockaddr_addr, addr_t sockaddr_len_addr);
dword_t sys_getpeername(fd_t sock_fd, addr_t sockaddr_addr, addr_t sockaddr_len_addr);
dword_t sys_sendto(fd_t sock_fd, addr_t buffer_addr, dword_t len, dword_t flags, addr_t sockaddr_addr, dword_t sockaddr_len);
dword_t sys_recvfrom(fd_t sock_fd, addr_t buffer_addr, dword_t len, dword_t flags, addr_t sockaddr_addr, addr_t sockaddr_len_addr);
dword_t sys_sendmsg(fd_t sock_fd, addr_t msghdr_addr, dword_t flags);
dword_t sys_recvmsg(fd_t sock_fd, addr_t msghdr_addr, dword_t flags);
dword_t sys_setsockopt(fd_t sock_fd, dword_t level, dword_t option, addr_t value_addr, dword_t value_len);
dword_t sys_getsockopt(fd_t sock_fd, dword_t level, dword_t option, addr_t value_addr, addr_t value_len_addr);
dword_t sys_shutdown(fd_t sock_fd, dword_t how);

// Futex
dword_t sys_futex(addr_t uaddr, dword_t op, dword_t val, addr_t timeout_or_val2, addr_t uaddr2, dword_t val3);

// Resources
dword_t sys_getrlimit(dword_t resource, addr_t rlimit_addr);
dword_t sys_setrlimit(dword_t resource, addr_t rlimit_addr);
dword_t sys_prlimit64(pid_t_ pid, dword_t resource, addr_t new_limit_addr, addr_t old_limit_addr);
dword_t sys_getrusage(dword_t who, addr_t rusage_addr);

// Misc
dword_t sys_uname(addr_t uts_addr);
dword_t sys_sethostname(addr_t hostname_addr, dword_t len);
dword_t sys_sysinfo(addr_t info_addr);
dword_t sys_prctl(dword_t option, dword_t arg2, dword_t arg3, dword_t arg4, dword_t arg5);
dword_t sys_arch_prctl(dword_t option, addr_t arg);
dword_t sys_set_tid_address(addr_t tid_addr);
dword_t sys_reboot(dword_t magic, dword_t magic2, dword_t cmd, addr_t arg);
dword_t sys_sync(void);
dword_t sys_ioctl(fd_t f, dword_t cmd, dword_t arg);

// Stub for socketcall (aarch64 has separate syscalls)
dword_t sys_socketcall(dword_t call, addr_t args_addr);

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
