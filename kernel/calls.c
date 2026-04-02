#include "kernel/calls.h"

#include "emu/interrupt.h"
#include "kernel/memory.h"
#include "kernel/signal.h"
#include "kernel/task.h"

#include "debug.h"

#include <string.h>

dword_t syscall_stub(void)
{
    return _ENOSYS;
}
// While identical, this version of the stub doesn't log below. Use this for
// syscalls that are optional (i.e. fallback on something else) but called
// frequently.
dword_t syscall_silent_stub(void)
{
    return _ENOSYS;
}
dword_t syscall_success_stub(void)
{
    return 0;
}

// Stub implementations for missing aarch64 syscalls
dword_t sys_execveat(fd_t dirfd, addr_t pathname, addr_t argv, addr_t envp, int_t flags)
{
    (void)dirfd;
    (void)pathname;
    (void)argv;
    (void)envp;
    (void)flags;
    return _ENOSYS;
}

dword_t sys_newfstatat(fd_t dirfd, addr_t pathname, addr_t statbuf, int_t flags)
{
    (void)dirfd;
    (void)pathname;
    (void)statbuf;
    (void)flags;
    return _ENOSYS;
}

dword_t sys_pselect6(fd_t nfds, addr_t readfds, addr_t writefds, addr_t exceptfds, addr_t timeout,
                     addr_t sigmask)
{
    (void)nfds;
    (void)readfds;
    (void)writefds;
    (void)exceptfds;
    (void)timeout;
    (void)sigmask;
    return _ENOSYS;
}

// Forward declarations for x86 syscall table entries
// These are expected to be defined elsewhere in the codebase
extern dword_t sys_stat;
extern dword_t sys_oldumount;
extern dword_t sys_fstat;
extern dword_t sys_nice;
extern dword_t sys_sync;
extern dword_t sys_signal;
extern dword_t sys_acct;
extern dword_t sys_umount;
extern dword_t sys_oldolduname;
extern dword_t sys_ustat;
extern dword_t sys_sigsuspend;
extern dword_t sys_sigpending;
extern dword_t sys_setrlimit;
extern dword_t sys_old_getrlimit;
extern dword_t sys_old_select;
extern dword_t sys_lstat;
extern dword_t sys_uselib;
extern dword_t sys_swapon;
extern dword_t sys_readdir;

syscall_t syscall_table[] = {
    [1] = (syscall_t)sys_exit,
    [2] = (syscall_t)sys_fork,
    [3] = (syscall_t)sys_read,
    [4] = (syscall_t)sys_write,
    [5] = (syscall_t)sys_open,
    [6] = (syscall_t)sys_close,
    [7] = (syscall_t)sys_waitpid,
    [9] = (syscall_t)sys_link,
    [10] = (syscall_t)sys_unlink,
    [11] = (syscall_t)sys_execve,
    [12] = (syscall_t)sys_chdir,
    [13] = (syscall_t)sys_time,
    [14] = (syscall_t)sys_mknod,
    [15] = (syscall_t)sys_chmod,
    [16] = (syscall_t)sys_lchown,
    [18] = (syscall_t)sys_stat,
    [19] = (syscall_t)sys_lseek,
    [20] = (syscall_t)sys_getpid,
    [21] = (syscall_t)sys_mount,
    [22] = (syscall_t)sys_oldumount,
    [23] = (syscall_t)sys_setuid,
    [24] = (syscall_t)sys_getuid,
    [25] = (syscall_t)sys_stime,
    [26] = (syscall_t)sys_ptrace,
    [27] = (syscall_t)sys_alarm,
    [28] = (syscall_t)sys_fstat,
    [29] = (syscall_t)sys_pause,
    [30] = (syscall_t)sys_utime,
    [33] = (syscall_t)sys_access,
    [34] = (syscall_t)sys_nice,
    [36] = (syscall_t)sys_sync,
    [37] = (syscall_t)sys_kill,
    [38] = (syscall_t)sys_rename,
    [39] = (syscall_t)sys_mkdir,
    [40] = (syscall_t)sys_rmdir,
    [41] = (syscall_t)sys_dup,
    [42] = (syscall_t)sys_pipe,
    [43] = (syscall_t)sys_times,
    [45] = (syscall_t)sys_brk,
    [46] = (syscall_t)sys_setgid,
    [47] = (syscall_t)sys_getgid,
    [48] = (syscall_t)sys_signal,
    [49] = (syscall_t)sys_geteuid,
    [50] = (syscall_t)sys_getegid,
    [51] = (syscall_t)sys_acct,
    [52] = (syscall_t)sys_umount,
    [54] = (syscall_t)sys_ioctl,
    [55] = (syscall_t)sys_fcntl,
    [57] = (syscall_t)sys_setpgid,
    [59] = (syscall_t)sys_oldolduname,
    [60] = (syscall_t)sys_umask,
    [61] = (syscall_t)sys_chroot,
    [62] = (syscall_t)sys_ustat,
    [63] = (syscall_t)sys_dup2,
    [64] = (syscall_t)sys_getppid,
    [65] = (syscall_t)sys_getpgrp,
    [66] = (syscall_t)sys_setsid,
    [67] = (syscall_t)sys_sigaction,
    [70] = (syscall_t)sys_setreuid,
    [71] = (syscall_t)sys_setregid,
    [72] = (syscall_t)sys_sigsuspend,
    [73] = (syscall_t)sys_sigpending,
    [74] = (syscall_t)sys_sethostname,
    [75] = (syscall_t)sys_setrlimit,
    [76] = (syscall_t)sys_old_getrlimit,
    [77] = (syscall_t)sys_getrusage,
    [78] = (syscall_t)sys_gettimeofday,
    [79] = (syscall_t)sys_settimeofday,
    [80] = (syscall_t)sys_getgroups,
    [81] = (syscall_t)sys_setgroups,
    [82] = (syscall_t)sys_old_select,
    [83] = (syscall_t)sys_symlink,
    [84] = (syscall_t)sys_lstat,
    [85] = (syscall_t)sys_readlink,
    [86] = (syscall_t)sys_uselib,
    [87] = (syscall_t)sys_swapon,
    [88] = (syscall_t)sys_reboot,
    [89] = (syscall_t)sys_readdir,
    [90] = (syscall_t)sys_mmap,
    [91] = (syscall_t)sys_munmap,
    [92] = (syscall_t)sys_truncate,
    [93] = (syscall_t)sys_ftruncate,
    [94] = (syscall_t)sys_fchmod,
    [95] = (syscall_t)sys_fchown,
    [96] = (syscall_t)sys_getpriority,
    [97] = (syscall_t)sys_setpriority,
    [99] = (syscall_t)sys_statfs,
    [100] = (syscall_t)sys_fstatfs,
    [101] = (syscall_t)sys_ioperm,
    [102] = (syscall_t)sys_socketcall,
    [103] = (syscall_t)sys_syslog,
    [104] = (syscall_t)sys_setitimer,
    [105] = (syscall_t)sys_getitimer,
    [106] = (syscall_t)sys_newstat,
    [107] = (syscall_t)sys_newlstat,
    [108] = (syscall_t)sys_newfstat,
    [111] = (syscall_t)sys_vhangup,
    [114] = (syscall_t)sys_wait4,
    [115] = (syscall_t)sys_swapoff,
    [116] = (syscall_t)sys_sysinfo,
    [117] = (syscall_t)sys_ipc,
    [118] = (syscall_t)sys_fsync,
    [119] = (syscall_t)sys_sigreturn,
    [120] = (syscall_t)sys_clone,
    [121] = (syscall_t)sys_setdomainname,
    [122] = (syscall_t)sys_newuname,
    [123] = (syscall_t)sys_modify_ldt,
    [124] = (syscall_t)sys_adjtimex,
    [125] = (syscall_t)sys_mprotect,
    [126] = (syscall_t)sys_sigprocmask,
    [128] = (syscall_t)sys_init_module,
    [129] = (syscall_t)sys_delete_module,
    [131] = (syscall_t)sys_quotactl,
    [132] = (syscall_t)sys_getpgid,
    [133] = (syscall_t)sys_fchdir,
    [134] = (syscall_t)sys_bdflush,
    [135] = (syscall_t)sys_sysfs,
    [136] = (syscall_t)sys_personality,
    [138] = (syscall_t)sys_setfsuid,
    [139] = (syscall_t)sys_setfsgid,
    [140] = (syscall_t)sys_llseek,
    [141] = (syscall_t)sys_getdents,
    [142] = (syscall_t)sys_select,
    [143] = (syscall_t)sys_flock,
    [144] = (syscall_t)sys_msync,
    [145] = (syscall_t)sys_readv,
    [146] = (syscall_t)sys_writev,
    [147] = (syscall_t)sys_getsid,
    [148] = (syscall_t)sys_fdatasync,
    [150] = (syscall_t)sys_mlock,
    [151] = (syscall_t)sys_munlock,
    [152] = (syscall_t)sys_mlockall,
    [153] = (syscall_t)sys_munlockall,
    [154] = (syscall_t)sys_sched_setparam,
    [155] = (syscall_t)sys_sched_getparam,
    [156] = (syscall_t)sys_sched_setscheduler,
    [157] = (syscall_t)sys_sched_getscheduler,
    [158] = (syscall_t)sys_sched_yield,
    [159] = (syscall_t)sys_sched_get_priority_max,
    [160] = (syscall_t)sys_sched_get_priority_min,
    [161] = (syscall_t)sys_sched_rr_get_interval,
    [162] = (syscall_t)sys_nanosleep,
    [163] = (syscall_t)sys_mremap,
    [164] = (syscall_t)sys_setresuid,
    [165] = (syscall_t)sys_getresuid,
    [166] = (syscall_t)sys_vm86,
    [168] = (syscall_t)sys_poll,
    [170] = (syscall_t)sys_setresgid,
    [171] = (syscall_t)sys_getresgid,
    [172] = (syscall_t)sys_prctl,
    [173] = (syscall_t)sys_rt_sigreturn,
    [174] = (syscall_t)sys_rt_sigaction,
    [175] = (syscall_t)sys_rt_sigprocmask,
    [176] = (syscall_t)sys_rt_sigpending,
    [177] = (syscall_t)sys_rt_sigtimedwait,
    [178] = (syscall_t)sys_rt_sigqueueinfo,
    [179] = (syscall_t)sys_rt_sigsuspend,
    [180] = (syscall_t)sys_pread,
    [181] = (syscall_t)sys_pwrite,
    [182] = (syscall_t)sys_chown,
    [183] = (syscall_t)sys_getcwd,
    [184] = (syscall_t)sys_capget,
    [185] = (syscall_t)sys_capset,
    [186] = (syscall_t)sys_sigaltstack,
    [187] = (syscall_t)sys_sendfile,
    [190] = (syscall_t)sys_vfork,
    [191] = (syscall_t)sys_getrlimit,
    [192] = (syscall_t)sys_mmap2,
    [193] = (syscall_t)sys_truncate64,
    [194] = (syscall_t)sys_ftruncate64,
    [195] = (syscall_t)sys_stat64,
    [196] = (syscall_t)sys_lstat64,
    [197] = (syscall_t)sys_fstat64,
    [198] = (syscall_t)sys_lchown,
    [199] = (syscall_t)sys_getuid,
    [200] = (syscall_t)sys_getgid,
    [201] = (syscall_t)sys_geteuid,
    [202] = (syscall_t)sys_getegid,
    [203] = (syscall_t)sys_setreuid,
    [204] = (syscall_t)sys_setregid,
    [205] = (syscall_t)sys_getgroups,
    [206] = (syscall_t)sys_setgroups,
    [207] = (syscall_t)sys_fchown,
    [208] = (syscall_t)sys_setresuid,
    [209] = (syscall_t)sys_getresuid,
    [210] = (syscall_t)sys_setresgid,
    [211] = (syscall_t)sys_getresgid,
    [212] = (syscall_t)sys_chown,
    [213] = (syscall_t)sys_setuid,
    [214] = (syscall_t)sys_setgid,
    [215] = (syscall_t)sys_setfsuid,
    [216] = (syscall_t)sys_setfsgid,
    [217] = (syscall_t)sys_pivot_root,
    [218] = (syscall_t)sys_mincore,
    [219] = (syscall_t)sys_madvise,
    [220] = (syscall_t)sys_getdents64,
    [221] = (syscall_t)sys_fcntl64,
    [222] = (syscall_t)sys_gettid,
    [223] = (syscall_t)sys_readahead,
    [224] = (syscall_t)sys_setxattr,
    [225] = (syscall_t)sys_lsetxattr,
    [226] = (syscall_t)sys_fsetxattr,
    [227] = (syscall_t)sys_getxattr,
    [228] = (syscall_t)sys_lgetxattr,
    [229] = (syscall_t)sys_fgetxattr,
    [230] = (syscall_t)sys_listxattr,
    [231] = (syscall_t)sys_llistxattr,
    [232] = (syscall_t)sys_flistxattr,
    [233] = (syscall_t)sys_removexattr,
    [234] = (syscall_t)sys_lremovexattr,
    [235] = (syscall_t)sys_fremovexattr,
    [236] = (syscall_t)sys_tkill,
    [237] = (syscall_t)sys_sendfile64,
    [238] = (syscall_t)sys_futex,
    [239] = (syscall_t)sys_sched_setaffinity,
    [240] = (syscall_t)sys_sched_getaffinity,
    [241] = (syscall_t)sys_set_thread_area,
    [243] = (syscall_t)sys_set_tid_address,
    [244] = (syscall_t)sys_fadvise64,
    [245] = (syscall_t)sys_timer_create,
    [246] = (syscall_t)sys_timer_settime,
    [247] = (syscall_t)sys_timer_gettime,
    [248] = (syscall_t)sys_timer_getoverrun,
    [249] = (syscall_t)sys_timer_delete,
    [250] = (syscall_t)sys_clock_settime,
    [251] = (syscall_t)sys_clock_gettime,
    [252] = (syscall_t)sys_clock_getres,
    [253] = (syscall_t)sys_clock_nanosleep,
    [254] = (syscall_t)sys_exit_group,
    [255] = (syscall_t)sys_timer_getoverrun,
    [256] = (syscall_t)sys_timer_delete,
    [257] = (syscall_t)sys_clock_settime,
    [258] = (syscall_t)sys_clock_gettime,
    [259] = (syscall_t)sys_clock_getres,
    [260] = (syscall_t)sys_clock_nanosleep,
    [265] = (syscall_t)sys_statfs64,
    [266] = (syscall_t)sys_fstatfs64,
    [267] = (syscall_t)sys_tgkill,
    [268] = (syscall_t)sys_utimes,
    [271] = (syscall_t)sys_mq_open,
    [272] = (syscall_t)sys_mq_unlink,
    [273] = (syscall_t)sys_mq_timedsend,
    [274] = (syscall_t)sys_mq_timedreceive,
    [275] = (syscall_t)sys_mq_notify,
    [276] = (syscall_t)sys_mq_getsetattr,
    [277] = (syscall_t)sys_mq_getsetattr,
    [278] = (syscall_t)sys_vserver,
    [279] = (syscall_t)sys_waitid,
    [280] = (syscall_t)sys_ioprio_set,
    [281] = (syscall_t)sys_ioprio_get,
    [282] = (syscall_t)sys_inotify_init,
    [283] = (syscall_t)sys_inotify_add_watch,
    [284] = (syscall_t)sys_inotify_rm_watch,
    [285] = (syscall_t)sys_spu_run,
    [286] = (syscall_t)sys_spu_create,
    [287] = (syscall_t)sys_pselect6,
    [288] = (syscall_t)sys_ppoll,
    [289] = (syscall_t)sys_unshare,
    [290] = (syscall_t)sys_set_robust_list,
    [291] = (syscall_t)sys_get_robust_list,
    [292] = (syscall_t)sys_splice,
    [293] = (syscall_t)sys_tee,
    [294] = (syscall_t)sys_sync_file_range,
    [295] = (syscall_t)sys_tee,
    [296] = (syscall_t)sys_sync_file_range,
    [297] = (syscall_t)sys_utimensat,
    [298] = (syscall_t)sys_signalfd,
    [299] = (syscall_t)sys_timerfd,
    [300] = (syscall_t)sys_eventfd,
    [304] = (syscall_t)sys_get_thread_area,
    [305] = (syscall_t)sys_setxattr,
    [306] = (syscall_t)sys_lsetxattr,
    [307] = (syscall_t)sys_fsetxattr,
    [308] = (syscall_t)sys_getxattr,
    [309] = (syscall_t)sys_lgetxattr,
    [310] = (syscall_t)sys_fgetxattr,
    [311] = (syscall_t)sys_listxattr,
    [312] = (syscall_t)sys_llistxattr,
    [313] = (syscall_t)sys_flistxattr,
    [314] = (syscall_t)sys_removexattr,
    [315] = (syscall_t)sys_lremovexattr,
    [316] = (syscall_t)sys_fremovexattr,
    [318] = (syscall_t)sys_getcpu,
    [319] = (syscall_t)sys_epoll_wait,
    [320] = (syscall_t)sys_epoll_ctl,
    [321] = (syscall_t)sys_epoll_pwait,
    [323] = (syscall_t)sys_signalfd4,
    [324] = (syscall_t)sys_eventfd2,
    [325] = (syscall_t)sys_timerfd_settime,
    [326] = (syscall_t)sys_timerfd_gettime,
    [327] = (syscall_t)sys_signalfd4,
    [328] = (syscall_t)sys_timerfd_settime,
    [329] = (syscall_t)sys_timerfd_gettime,
    [330] = (syscall_t)sys_eventfd2,
    [331] = (syscall_t)sys_epoll_create1,
    [333] = (syscall_t)sys_seccomp,
    [338] = (syscall_t)sys_statx,
    [339] = (syscall_t)sys_seccomp,
    [340] = (syscall_t)sys_getrandom,
    [341] = (syscall_t)sys_memfd_create,
    [342] = (syscall_t)sys_bpf,
    [343] = (syscall_t)sys_execveat,
    [344] = (syscall_t)sys_userfaultfd,
    [345] = (syscall_t)sys_membarrier,
    [347] = (syscall_t)sys_openat,
    [348] = (syscall_t)sys_mkdirat,
    [349] = (syscall_t)sys_mknodat,
    [350] = (syscall_t)sys_fchownat,
    [351] = (syscall_t)sys_futimesat,
    [352] = (syscall_t)sys_newfstatat,
    [353] = (syscall_t)sys_unlinkat,
    [354] = (syscall_t)sys_renameat,
    [355] = (syscall_t)sys_linkat,
    [356] = (syscall_t)sys_symlinkat,
    [357] = (syscall_t)sys_readlinkat,
    [358] = (syscall_t)sys_fchmodat,
    [359] = (syscall_t)sys_faccessat,
    [360] = (syscall_t)sys_pselect6,
    [361] = (syscall_t)sys_ppoll,
    [362] = (syscall_t)sys_signalfd4,
    [363] = (syscall_t)sys_vmsplice,
    [364] = (syscall_t)sys_splice,
    [365] = (syscall_t)sys_tee,
    [366] = (syscall_t)sys_sync_file_range,
    [367] = (syscall_t)sys_utimensat,
    [368] = (syscall_t)sys_epoll_pwait,
    [369] = (syscall_t)sys_accept4,
    [370] = (syscall_t)sys_timerfd_settime,
    [371] = (syscall_t)sys_timerfd_gettime,
    [372] = (syscall_t)sys_perf_event_open,
    [373] = (syscall_t)sys_cloned,
    [374] = (syscall_t)sys_fsync,
    [375] = (syscall_t)sys_fdatasync,
    [376] = (syscall_t)sys_sync_file_range2,
    [377] = (syscall_t)sys_sync_file_range2,
    [378] = (syscall_t)sys_sync_file_range2,
    [379] = (syscall_t)sys_fallocate,
    [380] = (syscall_t)sys_old_readdir,
    [381] = (syscall_t)sys_socket,
    [382] = (syscall_t)sys_socketpair,
    [383] = (syscall_t)sys_bind,
    [384] = (syscall_t)sys_connect,
    [385] = (syscall_t)sys_getsockname,
    [386] = (syscall_t)sys_getpeername,
    [387] = (syscall_t)sys_sendto,
    [388] = (syscall_t)sys_recvfrom,
    [389] = (syscall_t)sys_setsockopt,
    [390] = (syscall_t)sys_getsockopt,
    [391] = (syscall_t)sys_shutdown,
    [392] = (syscall_t)sys_sendmsg,
    [393] = (syscall_t)sys_recvmsg,
    [394] = (syscall_t)sys_accept4,
    [395] = (syscall_t)sys_recvmmsg,
    [396] = (syscall_t)sys_sendmmsg,
    [400] = (syscall_t)sys_fstatat64,
    [401] = (syscall_t)sys_fstatfs64,
    [402] = (syscall_t)sys_fstatfs64,
    [403] = (syscall_t)sys_fstatfs64,
    [424] = (syscall_t)sys_fchmodat2,
    [512] = (syscall_t)sys_getrandom,
    [513] = (syscall_t)sys_memfd_create,
    [514] = (syscall_t)sys_bpf,
    [515] = (syscall_t)sys_execveat,
    [516] = (syscall_t)sys_userfaultfd,
    [517] = (syscall_t)sys_membarrier,
};
;

#define NUM_SYSCALLS (sizeof(syscall_table) / sizeof(syscall_table[0]))

void dump_stack(int lines);

void handle_interrupt(int interrupt)
{
    struct cpu_state *cpu = &current->cpu;
    if (interrupt == INT_SYSCALL) {
        // aarch64 syscall ABI: x8 = syscall num, x0-x5 = args
        unsigned syscall_num = cpu->x[8];
        if (syscall_num >= NUM_SYSCALLS || syscall_table[syscall_num] == NULL) {
            printk("%d(%s) missing syscall %d\n", current->pid, current->comm, syscall_num);
            cpu->x[0] = _ENOSYS;
        } else {
            if (syscall_table[syscall_num] == (syscall_t)syscall_stub) {
                printk("%d(%s) stub syscall %d\n", current->pid, current->comm, syscall_num);
            }
            STRACE("%d call %-3d ", current->pid, syscall_num);
            int result = syscall_table[syscall_num](cpu->x[0], cpu->x[1], cpu->x[2], cpu->x[3],
                                                    cpu->x[4], cpu->x[5]);
            STRACE(" = 0x%x\n", result);
            cpu->x[0] = result;
        }
    } else if (interrupt == INT_GPF) {
        // DIAGNOSTIC: Log fault entry state
        page_t fault_page = PAGE(cpu->fault_addr);
        struct pt_entry *entry_before = mem_pt(current->mem, fault_page);
        struct pt_entry *entry_below = mem_pt(current->mem, fault_page + 1);

        printk("[GPF-DIAG] pc=0x%llx fault_addr=0x%llx is_write=%d sp=0x%llx\n",
               (unsigned long long)cpu->pc, (unsigned long long)cpu->fault_addr,
               cpu->fault_was_write, (unsigned long long)cpu->sp);
        printk("[GPF-DIAG] page=0x%lx mapped_before=%d\n", (unsigned long)fault_page,
               entry_before != NULL);
        if (entry_below) {
            printk("[GPF-DIAG] page_below flags=0x%x growsdown=%d\n", entry_below->flags,
                   !!(entry_below->flags & P_GROWSDOWN));
        } else {
            printk("[GPF-DIAG] page_below=NULL\n");
        }

        // some page faults, such as stack growing or CoW clones, are handled by mem_ptr
        read_wrlock(&current->mem->lock);
        void *ptr =
            mem_ptr(current->mem, cpu->fault_addr, cpu->fault_was_write ? MEM_WRITE : MEM_READ);
        read_wrunlock(&current->mem->lock);

        // DIAGNOSTIC: Log state after handling
        struct pt_entry *entry_after = mem_pt(current->mem, fault_page);
        printk("[GPF-DIAG] mapped_after=%d ptr=%p\n", entry_after != NULL, ptr);

        if (ptr == NULL) {
            printk("[GPF-DIAG] SIGNAL: SIGSEGV delivered\n");
            printk("%d page fault on 0x%llx at 0x%llx\n", current->pid,
                   (unsigned long long)cpu->fault_addr, (unsigned long long)cpu->pc);
            struct siginfo_ info = {
                .code = mem_segv_reason(current->mem, cpu->fault_addr),
                .fault.addr = cpu->fault_addr,
            };
            dump_stack(8);
            deliver_signal(current, SIGSEGV_, info);
        } else {
            printk("[GPF-DIAG] HANDLED: mapping created/expanded\n");
        }
    } else if (interrupt == INT_UNDEFINED) {
        printk("%d illegal instruction at 0x%llx: ", current->pid, (unsigned long long)cpu->pc);
        for (int i = 0; i < 8; i++) {
            uint8_t b;
            if (user_get(cpu->pc + i, b))
                break;
            printk("%02x ", b);
        }
        printk("\n");
        dump_stack(8);
        struct siginfo_ info = {
            .code = SI_KERNEL_,
            .fault.addr = cpu->pc,
        };
        deliver_signal(current, SIGILL_, info);
    } else if (interrupt == INT_BREAKPOINT) {
        lock(&pids_lock);
        send_signal(current, SIGTRAP_,
                    (struct siginfo_){
                        .sig = SIGTRAP_,
                        .code = SI_KERNEL_,
                    });
        unlock(&pids_lock);
    } else if (interrupt == INT_DEBUG) {
        lock(&pids_lock);
        send_signal(current, SIGTRAP_,
                    (struct siginfo_){
                        .sig = SIGTRAP_,
                        .code = TRAP_TRACE_,
                    });
        unlock(&pids_lock);
    } else if (interrupt != INT_TIMER) {
        printk("%d unhandled interrupt %d\n", current->pid, interrupt);
        sys_exit(interrupt);
    }

    receive_signals();
    struct tgroup *group = current->group;
    lock(&group->lock);
    while (group->stopped)
        wait_for_ignore_signals(&group->stopped_cond, &group->lock, NULL);
    unlock(&group->lock);
}

void dump_maps(void)
{
    extern void proc_maps_dump(struct task * task, struct proc_data * buf);
    struct proc_data buf = {};
    proc_maps_dump(current, &buf);
    // go a line at a time because it can be fucking enormous
    char *orig_data = buf.data;
    while (buf.size > 0) {
        size_t chunk_size = buf.size;
        if (chunk_size > 1024)
            chunk_size = 1024;
        printk("%.*s", chunk_size, buf.data);
        buf.data += chunk_size;
        buf.size -= chunk_size;
    }
    free(orig_data);
}

void dump_mem(addr_t start, uint_t len)
{
    const int width = 8;
    for (addr_t addr = start; addr < start + len; addr += sizeof(dword_t)) {
        unsigned from_left = (addr - start) / sizeof(dword_t) % width;
        if (from_left == 0)
            printk("%08x: ", addr);
        dword_t word;
        if (user_get(addr, word))
            break;
        printk("%08x ", word);
        if (from_left == width - 1)
            printk("\n");
    }
}

void dump_stack(int lines)
{
    printk("stack at %llx, base at %llx, ip at %llx\n", (unsigned long long)current->cpu.sp,
           (unsigned long long)current->cpu.x[6], (unsigned long long)current->cpu.pc);
    dump_mem(current->cpu.sp, lines * sizeof(qword_t) * 8);
}

// TODO find a home for this
#ifdef LOG_OVERRIDE
int log_override = 0;
#endif
