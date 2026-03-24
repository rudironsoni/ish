#ifndef AARCH64_CALLS_H
#define AARCH64_CALLS_H

// Maximum syscall number for aarch64
#define A64_SYS_MAX 1100

// aarch64 Linux syscall numbers
// Reference: arch/arm64/include/uapi/asm/unistd.h

// IO
#define A64_SYS_io_setup 0
#define A64_SYS_io_destroy 1
#define A64_SYS_io_submit 2
#define A64_SYS_io_cancel 3
#define A64_SYS_io_getevents 4
#define A64_SYS_setxattr 5
#define A64_SYS_lsetxattr 6
#define A64_SYS_fsetxattr 7
#define A64_SYS_getxattr 8
#define A64_SYS_lgetxattr 9
#define A64_SYS_fgetxattr 10
#define A64_SYS_listxattr 11
#define A64_SYS_llistxattr 12
#define A64_SYS_flistxattr 13
#define A64_SYS_removexattr 14
#define A64_SYS_lremovexattr 15
#define A64_SYS_fremovexattr 16
#define A64_SYS_getcwd 17
#define A64_SYS_lookup_dcookie 18
#define A64_SYS_eventfd2 19
#define A64_SYS_epoll_create1 20
#define A64_SYS_epoll_ctl 21
#define A64_SYS_epoll_pwait 22
#define A64_SYS_dup 23
#define A64_SYS_dup3 24
#define A64_SYS_fcntl 25
#define A64_SYS_inotify_init1 26
#define A64_SYS_inotify_add_watch 27
#define A64_SYS_inotify_rm_watch 28
#define A64_SYS_ioctl 29
#define A64_SYS_ioprio_set 30
#define A64_SYS_ioprio_get 31
#define A64_SYS_flock 32
#define A64_SYS_mknodat 33
#define A64_SYS_mkdirat 34
#define A64_SYS_unlinkat 35
#define A64_SYS_symlinkat 36
#define A64_SYS_linkat 37
#define A64_SYS_renameat 38
#define A64_SYS_umount2 39
#define A64_SYS_mount 40
#define A64_SYS_pivot_root 41
#define A64_SYS_nfsservctl 42
#define A64_SYS_statfs 43
#define A64_SYS_fstatfs 44
#define A64_SYS_truncate 45
#define A64_SYS_ftruncate 46
#define A64_SYS_fallocate 47
#define A64_SYS_faccessat 48
#define A64_SYS_chdir 49
#define A64_SYS_fchdir 50
#define A64_SYS_chroot 51
#define A64_SYS_fchmod 52
#define A64_SYS_fchmodat 53
#define A64_SYS_fchownat 54
#define A64_SYS_fchown 55
#define A64_SYS_openat 56
#define A64_SYS_close 57
#define A64_SYS_vhangup 58
#define A64_SYS_pipe2 59
#define A64_SYS_quotactl 60
#define A64_SYS_getdents64 61
#define A64_SYS_lseek 62
#define A64_SYS_llseek 62  // aarch64 uses lseek for 64-bit offsets
#define A64_SYS_read 63
#define A64_SYS_write 64
#define A64_SYS_readv 65
#define A64_SYS_writev 66
#define A64_SYS_pread64 67
#define A64_SYS_pwrite64 68
#define A64_SYS_preadv 69
#define A64_SYS_pwritev 70
#define A64_SYS_sendfile 71
#define A64_SYS_pselect6 72
#define A64_SYS_ppoll 73
#define A64_SYS_signalfd4 74
#define A64_SYS_vmsplice 75
#define A64_SYS_splice 76
#define A64_SYS_tee 77
#define A64_SYS_readlinkat 78
#define A64_SYS_newfstatat 79
#define A64_SYS_fstat 80
#define A64_SYS_sync 81
#define A64_SYS_fsync 82
#define A64_SYS_fdatasync 83
#define A64_SYS_sync_file_range 84

// Memory management
#define A64_SYS_brk 12
#define A64_SYS_munmap 215
#define A64_SYS_mprotect 226
#define A64_SYS_madvise 233

// Process management
#define A64_SYS_exit 93
#define A64_SYS_exit_group 94
#define A64_SYS_waitid 95
#define A64_SYS_set_tid_address 96
#define A64_SYS_unshare 97
#define A64_SYS_futex 98
#define A64_SYS_set_robust_list 99
#define A64_SYS_get_robust_list 100
#define A64_SYS_nanosleep 101
#define A64_SYS_getitimer 102
#define A64_SYS_setitimer 103
#define A64_SYS_gettimeofday 169
#define A64_SYS_settimeofday 170
#define A64_SYS_getpid 172
#define A64_SYS_getppid 173
#define A64_SYS_getuid 174
#define A64_SYS_getgid 175
#define A64_SYS_geteuid 176
#define A64_SYS_getegid 177
#define A64_SYS_gettid 178
#define A64_SYS_mmap 222
#define A64_SYS_fork 1079
#define A64_SYS_vfork 1071
#define A64_SYS_clone 1072
#define A64_SYS_execve 1083
#define A64_SYS_wait4 260
#define A64_SYS_kexec_load 104
#define A64_SYS_init_module 105
#define A64_SYS_delete_module 106
#define A64_SYS_timer_create 107
#define A64_SYS_timer_gettime 108
#define A64_SYS_timer_getoverrun 109
#define A64_SYS_timer_settime 110
#define A64_SYS_timer_delete 111
#define A64_SYS_clock_settime 112
#define A64_SYS_clock_gettime 113
#define A64_SYS_clock_getres 114
#define A64_SYS_clock_nanosleep 115
#define A64_SYS_syslog 116
#define A64_SYS_ptrace 117
#define A64_SYS_sched_setparam 118
#define A64_SYS_sched_setscheduler 119
#define A64_SYS_sched_getscheduler 120
#define A64_SYS_sched_getparam 121
#define A64_SYS_sched_setaffinity 122
#define A64_SYS_sched_getaffinity 123
#define A64_SYS_sched_yield 124
#define A64_SYS_sched_get_priority_max 125
#define A64_SYS_sched_get_priority_min 126
#define A64_SYS_sched_rr_get_interval 127
#define A64_SYS_restart_syscall 128
#define A64_SYS_kill 129
#define A64_SYS_tkill 130
#define A64_SYS_tgkill 131
#define A64_SYS_sigaltstack 132
#define A64_SYS_rt_sigsuspend 133
#define A64_SYS_rt_sigaction 134
#define A64_SYS_rt_sigprocmask 135
#define A64_SYS_rt_sigpending 136
#define A64_SYS_rt_sigtimedwait 137
#define A64_SYS_rt_sigqueueinfo 138
#define A64_SYS_rt_sigreturn 139
#define A64_SYS_setpriority 140
#define A64_SYS_getpriority 141
#define A64_SYS_reboot 142
#define A64_SYS_setregid 143
#define A64_SYS_setgid 144
#define A64_SYS_setreuid 145
#define A64_SYS_setuid 146
#define A64_SYS_setresuid 147
#define A64_SYS_getresuid 148
#define A64_SYS_setresgid 149
#define A64_SYS_getresgid 150
#define A64_SYS_setfsuid 151
#define A64_SYS_setfsgid 152
#define A64_SYS_times 153
#define A64_SYS_setpgid 154
#define A64_SYS_getpgid 155
#define A64_SYS_getsid 156
#define A64_SYS_setsid 157
#define A64_SYS_getgroups 158
#define A64_SYS_setgroups 159
#define A64_SYS_uname 160
#define A64_SYS_sethostname 161
#define A64_SYS_setdomainname 162
#define A64_SYS_getrlimit 163
#define A64_SYS_setrlimit 164
#define A64_SYS_getrusage 165
#define A64_SYS_pause 34
#define A64_SYS_creat 85
#define A64_SYS_access 21
#define A64_SYS_poll 7
#define A64_SYS_select 23

// Socket syscalls from Linux UAPI
#define A64_SYS_socket 198
#define A64_SYS_socketpair 199
#define A64_SYS_bind 200
#define A64_SYS_listen 201
#define A64_SYS_accept 202
#define A64_SYS_connect 203
#define A64_SYS_getsockname 204
#define A64_SYS_getpeername 205
#define A64_SYS_sendto 206
#define A64_SYS_recvfrom 207
#define A64_SYS_setsockopt 208
#define A64_SYS_getsockopt 209
#define A64_SYS_shutdown 210
#define A64_SYS_sendmsg 211
#define A64_SYS_recvmsg 212
#define A64_SYS_accept4 242

// Misc syscalls from Linux UAPI
#define A64_SYS_umask 166
#define A64_SYS_prctl 167
#define A64_SYS_sysinfo 179
#define A64_SYS_statx 291
#define A64_SYS_prlimit64 261

// Syscall table structure for aarch64
struct syscall_info {
    const char *name;
    int nargs;
    // Additional metadata as needed
};

extern struct syscall_info a64_syscall_table[];
int a64_max_syscall(void);
const char *a64_syscall_name(int num);

#endif
