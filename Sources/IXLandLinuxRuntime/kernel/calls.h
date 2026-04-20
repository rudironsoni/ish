#ifndef CALLS_H
#define CALLS_H

#import "../fs/fd.h"

#import <IXLandLinuxRuntime/fs/dev.h>
#import <IXLandLinuxRuntime/fs/sock.h>
#import <IXLandLinuxRuntime/kernel/errno.h>
#import <IXLandLinuxRuntime/kernel/fs.h>
#import <IXLandLinuxRuntime/kernel/ptrace.h>
#import <IXLandLinuxRuntime/kernel/resource.h>
#import <IXLandLinuxRuntime/kernel/signal.h>
#import <IXLandLinuxRuntime/kernel/task.h>
#import <IXLandLinuxRuntime/kernel/time.h>
#import <IXLandLinuxRuntime/util/misc.h>

void handle_interrupt(int interrupt);

int must_check user_read(addr_t addr, void *buf, size_t count);
int must_check user_write(addr_t addr, const void *buf, size_t count);
int must_check user_read_task(struct task *task, addr_t addr, void *buf, size_t count);
int must_check user_write_task(struct task *task, addr_t addr, const void *buf, size_t count);
int must_check user_write_task_ptrace(struct task *task, addr_t addr, const void *buf,
                                      size_t count);
int must_check user_read_string(addr_t addr, char *buf, size_t max);
int must_check user_write_string(addr_t addr, const char *buf);
#define user_get(addr, var)            user_read(addr, &(var), sizeof(var))
#define user_put(addr, var)            user_write(addr, &(var), sizeof(var))
#define user_get_task(task, addr, var) user_read_task(task, addr, &(var), sizeof(var))
#define user_put_task(task, addr, var) user_write_task(task, addr, &(var), sizeof(var))

// process lifecycle
uint32_t sys_clone(uint32_t flags, addr_t stack, addr_t ptid, addr_t tls, addr_t ctid);
uint32_t sys_fork(void);
uint32_t sys_vfork(void);
uint32_t sys_execve(addr_t file, addr_t argv, addr_t envp);
int do_execve(const char *file, size_t argc, const char *argv, const char *envp);
uint32_t sys_exit(uint32_t status);
noreturn void do_exit(int status);
noreturn void do_exit_group(int status);
uint32_t sys_exit_group(uint32_t status);
uint32_t sys_wait4(pid_t_ pid, addr_t status_addr, uint32_t options, addr_t rusage_addr);
uint32_t sys_waitid(int64_t idtype, pid_t_ id, addr_t info_addr, int64_t options);
uint32_t sys_waitpid(pid_t_ pid, addr_t status_addr, uint32_t options);

// memory management
addr_t sys_brk(addr_t new_brk);
addr_t sys_mmap_native(addr_t addr, uint32_t len, uint32_t prot, uint32_t flags, fd_t fd_no,
                       off_t_ offset);

#define MMAP_SHARED    0x1
#define MMAP_PRIVATE   0x2
#define MMAP_FIXED     0x10
#define MMAP_ANONYMOUS 0x20
addr_t sys_mmap(addr_t args_addr);
addr_t sys_mmap2(addr_t addr, uint32_t len, uint32_t prot, uint32_t flags, fd_t fd_no,
                 uint32_t offset);
int64_t sys_munmap(addr_t addr, uint64_t len);
int64_t sys_mprotect(addr_t addr, uint64_t len, int64_t prot);
int64_t sys_mremap(addr_t addr, uint32_t old_len, uint32_t new_len, uint32_t flags);
uint32_t sys_madvise(addr_t addr, uint32_t len, uint32_t advice);
uint32_t sys_mbind(addr_t addr, uint32_t len, int64_t mode, addr_t nodemask, uint32_t maxnode,
                   uint64_t flags);
int64_t sys_mlock(addr_t addr, uint32_t len);
int64_t sys_msync(addr_t addr, uint32_t len, int64_t flags);

// file descriptor things
#define LOCK_SH_ 1
#define LOCK_EX_ 2
#define LOCK_NB_ 4
#define LOCK_UN_ 8
struct iovec_ {
    addr_t base;
    uint64_t len;
};
uint32_t sys_read(fd_t fd_no, addr_t buf_addr, uint32_t size);
uint32_t sys_readv(fd_t fd_no, addr_t iovec_addr, uint32_t iovec_count);
uint32_t sys_write(fd_t fd_no, addr_t buf_addr, uint32_t size);
uint32_t sys_writev(fd_t fd_no, addr_t iovec_addr, uint32_t iovec_count);
uint32_t sys__llseek(fd_t f, uint32_t off_high, uint32_t off_low, addr_t res_addr, uint32_t whence);
uint32_t sys_lseek(fd_t f, uint32_t off, uint32_t whence);
uint32_t sys_pread(fd_t f, addr_t buf_addr, uint32_t buf_size, off_t_ off);
uint32_t sys_pwrite(fd_t f, addr_t buf_addr, uint32_t size, off_t_ off);
uint32_t sys_ioctl(fd_t f, uint32_t cmd, uint32_t arg);
uint32_t sys_fcntl(fd_t f, uint32_t cmd, uint32_t arg);
uint32_t sys_fcntl32(fd_t fd, uint32_t cmd, uint32_t arg);
uint32_t sys_dup(fd_t fd);
uint32_t sys_dup2(fd_t fd, fd_t new_fd);
uint32_t sys_dup3(fd_t f, fd_t new_f, int64_t flags);
uint32_t sys_close(fd_t fd);
uint32_t sys_fsync(fd_t f);
uint32_t sys_flock(fd_t fd, uint32_t operation);
int64_t sys_pipe(addr_t pipe_addr);
int64_t sys_pipe2(addr_t pipe_addr, int32_t flags);
struct pollfd_ {
    fd_t fd;
    uint16_t events;
    uint16_t revents;
};
uint32_t sys_poll(addr_t fds, uint32_t nfds, int64_t timeout);
uint32_t sys_select(fd_t nfds, addr_t readfds_addr, addr_t writefds_addr, addr_t exceptfds_addr,
                    addr_t timeout_addr);
uint32_t sys_pselect(fd_t nfds, addr_t readfds_addr, addr_t writefds_addr, addr_t exceptfds_addr,
                     addr_t timeout_addr, addr_t sigmask_addr);
uint32_t sys_ppoll(addr_t fds, uint32_t nfds, addr_t timeout_addr, addr_t sigmask_addr,
                   uint32_t sigsetsize);
fd_t sys_epoll_create(int64_t flags);
fd_t sys_epoll_create0(void);
int64_t sys_epoll_ctl(fd_t epoll, int64_t op, fd_t fd, addr_t event_addr);
int64_t sys_epoll_wait(fd_t epoll, addr_t events_addr, int64_t max_events, int64_t timeout);
int64_t sys_epoll_pwait(fd_t epoll_f, addr_t events_addr, int64_t max_events, int64_t timeout,
                        addr_t sigmask_addr, uint32_t sigsetsize);

int64_t sys_eventfd2(uint64_t initval, int64_t flags);
int64_t sys_eventfd(uint64_t initval);

// file management
fd_t sys_open(addr_t path_addr, uint32_t flags, mode_t_ mode);
fd_t sys_openat(fd_t at, addr_t path_addr, uint32_t flags, mode_t_ mode);
uint32_t sys_close(fd_t fd);
uint32_t sys_link(addr_t src_addr, addr_t dst_addr);
uint32_t sys_linkat(fd_t src_at_f, addr_t src_addr, fd_t dst_at_f, addr_t dst_addr);
uint32_t sys_unlink(addr_t path_addr);
uint32_t sys_unlinkat(fd_t at_f, addr_t path_addr, int64_t flags);
uint32_t sys_rmdir(addr_t path_addr);
uint32_t sys_rename(addr_t src_addr, addr_t dst_addr);
uint32_t sys_renameat(fd_t src_at_f, addr_t src_addr, fd_t dst_at_f, addr_t dst_addr);
uint32_t sys_renameat2(fd_t src_at_f, addr_t src_addr, fd_t dst_at_f, addr_t dst_addr,
                       int64_t flags);
uint32_t sys_symlink(addr_t target_addr, addr_t link_addr);
uint32_t sys_symlinkat(addr_t target_addr, fd_t at_f, addr_t link_addr);
uint32_t sys_mknod(addr_t path_addr, mode_t_ mode, dev_t_ dev);
uint32_t sys_mknodat(fd_t at_f, addr_t path_addr, mode_t_ mode, dev_t_ dev);
uint32_t sys_access(addr_t path_addr, uint32_t mode);
uint32_t sys_faccessat(fd_t at_f, addr_t path, mode_t_ mode, uint32_t flags);
uint32_t sys_readlink(addr_t path, addr_t buf, uint32_t bufsize);
uint32_t sys_readlinkat(fd_t at_f, addr_t path, addr_t buf, uint32_t bufsize);
int64_t sys_getdents(fd_t f, addr_t dirents, uint64_t count);
int64_t sys_getdents64(fd_t f, addr_t dirents, uint64_t count);
int32_t sys_stat64(addr_t path_addr, addr_t statbuf_addr);
int32_t sys_lstat64(addr_t path_addr, addr_t statbuf_addr);
int32_t sys_fstat64(fd_t fd_no, addr_t statbuf_addr);
int32_t sys_fstatat64(fd_t at, addr_t path_addr, addr_t statbuf_addr, int32_t flags);
uint32_t sys_fchmod(fd_t f, uint32_t mode);
uint32_t sys_fchmodat(fd_t at_f, addr_t path_addr, uint32_t mode);
uint32_t sys_chmod(addr_t path_addr, uint32_t mode);
uint32_t sys_fchown32(fd_t f, uint32_t owner, uint32_t group);
uint32_t sys_fchownat(fd_t at_f, addr_t path_addr, uint32_t owner, uint32_t group, int flags);
uint32_t sys_chown32(addr_t path_addr, uid_t_ owner, uid_t_ group);
uint32_t sys_lchown(addr_t path_addr, uid_t_ owner, uid_t_ group);
uint32_t sys_truncate64(addr_t path_addr, uint32_t size_low, uint32_t size_high);
uint32_t sys_ftruncate64(fd_t f, uint32_t size_low, uint32_t size_high);
uint32_t sys_fallocate(fd_t f, uint32_t mode, uint32_t offset_low, uint32_t offset_high,
                       uint32_t len_low, uint32_t len_high);
uint32_t sys_mkdir(addr_t path_addr, mode_t_ mode);
uint32_t sys_mkdirat(fd_t at_f, addr_t path_addr, mode_t_ mode);
uint32_t sys_utimensat(fd_t at_f, addr_t path_addr, addr_t times_addr, uint32_t flags);
uint32_t sys_utimes(addr_t path_addr, addr_t times_addr);
uint32_t sys_utime(addr_t path_addr, addr_t times_addr);
int32_t sys_times(addr_t tbuf);
uint32_t sys_umask(uint32_t mask);

uint32_t sys_sendfile(fd_t out_fd, fd_t in_fd, addr_t offset_addr, uint32_t count);
uint32_t sys_sendfile64(fd_t out_fd, fd_t in_fd, addr_t offset_addr, uint32_t count);
uint32_t sys_splice(fd_t in_fd, addr_t in_off_addr, fd_t out_fd, addr_t out_off_addr,
                    uint32_t count, uint32_t flags);
uint32_t sys_copy_file_range(fd_t in_fd, addr_t in_off, fd_t out_fd, addr_t out_off, uint32_t len,
                             uint64_t flags);

uint32_t sys_statfs(addr_t path_addr, addr_t buf_addr);
uint32_t sys_statfs64(addr_t path_addr, uint32_t buf_size, addr_t buf_addr);
uint32_t sys_fstatfs(fd_t f, addr_t buf_addr);
uint32_t sys_fstatfs64(fd_t f, addr_t buf_addr);
int32_t sys_statx(fd_t at_f, addr_t path_addr, int32_t flags, uint32_t mask, addr_t statx_addr);

#define MS_READONLY_ (1 << 0)
#define MS_NOSUID_   (1 << 1)
#define MS_NODEV_    (1 << 2)
#define MS_NOEXEC_   (1 << 3)
#define MS_SILENT_   (1 << 15)
uint32_t sys_mount(addr_t source_addr, addr_t target_addr, addr_t type_addr, uint32_t flags,
                   addr_t data_addr);
uint32_t sys_umount2(addr_t target_addr, uint32_t flags);

uint32_t sys_xattr_stub(addr_t path_addr, addr_t name_addr, addr_t value_addr, uint32_t size,
                        uint32_t flags);

// process information
pid_t_ sys_getpid(void);
pid_t_ sys_gettid(void);
pid_t_ sys_getppid(void);
pid_t_ sys_getpgid(pid_t_ pid);
uint32_t sys_setpgid(pid_t_ pid, pid_t_ pgid);
pid_t_ sys_getpgrp(void);
uint32_t sys_setpgrp(void);
uid_t_ sys_getuid32(void);
uid_t_ sys_getuid(void);
int64_t sys_setuid(uid_t uid);
uid_t_ sys_geteuid32(void);
uid_t_ sys_geteuid(void);
int64_t sys_setgid(uid_t gid);
uid_t_ sys_getgid32(void);
uid_t_ sys_getgid(void);
uid_t_ sys_getegid32(void);
uid_t_ sys_getegid(void);
uint32_t sys_setresuid(uid_t_ ruid, uid_t_ euid, uid_t_ suid);
uint32_t sys_setresgid(uid_t_ rgid, uid_t_ egid, uid_t_ sgid);
int64_t sys_setreuid(uid_t_ ruid, uid_t_ euid);
int64_t sys_setregid(uid_t_ rgid, uid_t_ egid);
int64_t sys_getresuid(addr_t ruid_addr, addr_t euid_addr, addr_t suid_addr);
int64_t sys_getresgid(addr_t rgid_addr, addr_t egid_addr, addr_t sgid_addr);
int64_t sys_getgroups(uint32_t size, addr_t list);
int64_t sys_setgroups(uint32_t size, addr_t list);
int64_t sys_capget(addr_t header_addr, addr_t data_addr);
int64_t sys_capset(addr_t header_addr, addr_t data_addr);
uint32_t sys_getcwd(addr_t buf_addr, uint32_t size);
uint32_t sys_chdir(addr_t path_addr);
uint32_t sys_chroot(addr_t path_addr);
uint32_t sys_fchdir(fd_t f);
int64_t sys_personality(uint32_t pers);
int task_set_thread_area(struct task *task, addr_t u_info);
int sys_set_thread_area(addr_t u_info);
int sys_set_tid_address(addr_t blahblahblah);
uint32_t sys_setsid(void);
uint32_t sys_getsid(void);

int64_t sys_sched_yield(void);
int64_t sys_prctl(uint32_t option, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5);
int64_t sys_arch_prctl(int64_t code, addr_t addr);
int64_t sys_reboot(int64_t magic, int64_t magic2, int64_t cmd);

// system information
#define UNAME_LENGTH 65
struct uname {
    char system[UNAME_LENGTH];   // Linux
    char hostname[UNAME_LENGTH]; // my-compotar
    char release[UNAME_LENGTH];  // 1.2.3-ish
    char version[UNAME_LENGTH];  // SUPER AWESOME
    char arch[UNAME_LENGTH];     // aarch64
    char domain[UNAME_LENGTH];   // lol
};
void do_uname(struct uname *uts);
uint32_t sys_uname(addr_t uts_addr);
uint32_t sys_sethostname(addr_t hostname_addr, uint32_t hostname_len);

struct sys_info {
    uint32_t uptime;
    uint32_t loads[3];
    uint32_t totalram;
    uint32_t freeram;
    uint32_t sharedram;
    uint32_t bufferram;
    uint32_t totalswap;
    uint32_t freeswap;
    uint16_t procs;
    uint32_t totalhigh;
    uint32_t freehigh;
    uint32_t mem_unit;
    char pad;
};
uint32_t sys_sysinfo(addr_t info_addr);

// futexes
uint32_t sys_futex(addr_t uaddr, uint32_t op, uint32_t val, addr_t timeout_or_val2, addr_t uaddr2,
                   uint32_t val3);
int64_t sys_set_robust_list(addr_t robust_list, uint32_t len);
int64_t sys_get_robust_list(pid_t_ pid, addr_t robust_list_ptr, addr_t len_ptr);

// misc
int32_t sys_getrandom(addr_t buf_addr, uint32_t len, uint32_t flags);
int64_t sys_syslog(int64_t type, addr_t buf_addr, int64_t len);
int64_t sys_ipc(uint64_t call, int64_t first, int64_t second, int64_t third, addr_t ptr,
                int64_t fifth);

typedef int (*syscall_t)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

#endif
