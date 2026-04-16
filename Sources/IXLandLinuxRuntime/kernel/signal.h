#ifndef SIGNAL_H
#define SIGNAL_H

#import <IXLandLinuxRuntime/util/list.h>
#import <IXLandLinuxRuntime/util/misc.h>
#import <IXLandLinuxRuntime/util/sync.h>
struct task;

// Linux AArch64 sigset_t is 64-bit
typedef uint64_t sigset_t_;

#define SIG_ERR_ -1
#define SIG_DFL_ 0
#define SIG_IGN_ 1

#define SA_SIGINFO_   4
#define SA_ONSTACK_   0x08000000
#define SA_NODEFER_   0x40000000
#define SA_RESETHAND_ 0x80000000
#define SA_RESTORER_  0x04000000

struct sigaction_ {
    addr_t handler;
    uint32_t flags;
    addr_t restorer;
    sigset_t_ mask;
} __attribute__((packed));

#define NUM_SIGS 64

#define SIGHUP_    1
#define SIGINT_    2
#define SIGQUIT_   3
#define SIGILL_    4
#define SIGTRAP_   5
#define SIGABRT_   6
#define SIGIOT_    6
#define SIGBUS_    7
#define SIGFPE_    8
#define SIGKILL_   9
#define SIGUSR1_   10
#define SIGSEGV_   11
#define SIGUSR2_   12
#define SIGPIPE_   13
#define SIGALRM_   14
#define SIGTERM_   15
#define SIGSTKFLT_ 16
#define SIGCHLD_   17
#define SIGCONT_   18
#define SIGSTOP_   19
#define SIGTSTP_   20
#define SIGTTIN_   21
#define SIGTTOU_   22
#define SIGURG_    23
#define SIGXCPU_   24
#define SIGXFSZ_   25
#define SIGVTALRM_ 26
#define SIGPROF_   27
#define SIGWINCH_  28
#define SIGIO_     29
#define SIGPWR_    30
#define SIGSYS_    31

#define SI_USER_     0
#define SI_TIMER_    -2
#define SI_TKILL_    -6
#define SI_KERNEL_   128
#define TRAP_TRACE_  2
#define SEGV_MAPERR_ 1
#define SEGV_ACCERR_ 2

union sigval_ {
    int64_t sv_int;
    addr_t sv_ptr;
};

struct siginfo_ {
    int64_t sig;
    int64_t sig_errno;
    int64_t code;
    union {
        struct {
            pid_t_ pid;
            uid_t_ uid;
        } kill;
        struct {
            pid_t_ pid;
            uid_t_ uid;
            int64_t status;
            clock_t_ utime;
            clock_t_ stime;
        } child;
        struct {
            addr_t addr;
        } fault;
        struct {
            addr_t addr;
            int64_t syscall;
        } sigsys;
        struct {
            int64_t timer;
            int64_t overrun;
            union sigval_ value;
            int64_t _private;
        } timer;
    };
};

// a reasonable default siginfo
static const struct siginfo_ SIGINFO_NIL = {
    .code = SI_KERNEL_,
};

struct sigqueue {
    struct list queue;
    struct siginfo_ info;
};

struct sigevent_ {
    union sigval_ value;
    int64_t signo;
    int64_t method;
    pid_t_ tid;
};

// send a signal
// you better make sure the task isn't gonna get freed under me (pids_lock or current)
void send_signal(struct task *task, int sig, struct siginfo_ info);
// send a signal without regard for whether the signal is blocked or ignored
void deliver_signal(struct task *task, int sig, struct siginfo_ info);
// send a signal to current if it's not blocked or ignored, return whether that worked
// exists specifically for sending SIGTTIN/SIGTTOU
bool try_self_signal(int sig);
// send a signal to all processes in a group, could return ESRCH
int send_group_signal(uint32_t pgid, int sig, struct siginfo_ info);
// check for and deliver pending signals on current
// must be called without pids_lock, current->group->lock, or current->sighand->lock
void receive_signals(void);
// set the signal mask, restore it to what it was before on the next receive_signals call
void sigmask_set_temp(sigset_t_ mask);

struct sighand {
    atomic_uint refcount;
    struct sigaction_ action[NUM_SIGS];
    addr_t altstack;
    uint32_t altstack_size;
    lock_t lock;
};
struct sighand *sighand_new(void);
struct sighand *sighand_copy(struct sighand *sighand);
void sighand_release(struct sighand *sighand);

int32_t sys_rt_sigaction(int32_t signum, addr_t action_addr, addr_t oldaction_addr,
                         int32_t sigset_size);
int32_t sys_sigaction(int32_t signum, addr_t action_addr, addr_t oldaction_addr);
int32_t sys_rt_sigreturn(void);
int32_t sys_sigreturn(void);

#define SIG_BLOCK_   0
#define SIG_UNBLOCK_ 1
#define SIG_SETMASK_ 2
typedef uint64_t sigset_t_;
int32_t sys_rt_sigprocmask(int32_t how, addr_t set, addr_t oldset, int32_t size);
int64_t sys_rt_sigpending(addr_t set_addr);

static inline sigset_t_ sig_mask(int sig)
{
    assert(sig >= 1 && sig < NUM_SIGS);
    return 1ull << (sig - 1);
}

static inline bool sigset_has(sigset_t_ set, int sig)
{
    return !!(set & sig_mask(sig));
}
static inline void sigset_add(sigset_t_ *set, int sig)
{
    *set |= sig_mask(sig);
}
static inline void sigset_del(sigset_t_ *set, int sig)
{
    *set &= ~sig_mask(sig);
}

struct stack_t_ {
    addr_t stack;
    uint32_t flags;
    uint32_t size;
};
#define SS_ONSTACK_  1
#define SS_DISABLE_  2
#define MINSIGSTKSZ_ 2048
int32_t sys_sigaltstack(addr_t ss, addr_t old_ss);

int64_t sys_rt_sigsuspend(addr_t mask_addr, uint64_t size);
int64_t sys_pause(void);
int64_t sys_rt_sigtimedwait(addr_t set_addr, addr_t info_addr, addr_t timeout_addr,
                            uint64_t set_size);

int32_t sys_kill(pid_t_ pid, int32_t sig);
int32_t sys_tkill(pid_t_ tid, int32_t sig);
int32_t sys_tgkill(pid_t_ tgid, pid_t_ tid, int32_t sig);

// signal frame structs for AArch64 Linux guest

struct sigcontext_ {
    uint64_t pc;
    uint64_t sp;
    uint64_t x[30];
    uint32_t pad;
    uint32_t pstate;
    uint64_t fault_address;
};

struct stack_frame_ {
    uint64_t fp;
    uint64_t lr;
    uint64_t sp;
    uint64_t pc;
};

struct ucontext_ {
    uint64_t flags;
    uint64_t link;
    struct stack_t_ stack;
    struct sigcontext_ mcontext;
    sigset_t_ sigmask;
} __attribute__((packed));

#define SS_ONSTACK_ 1
#define SS_DISABLE_ 2

struct rt_sigframe_ {
    addr_t restorer;
    int64_t sig;
    addr_t pinfo;
    addr_t puc;
    union {
        struct siginfo_ info;
        char __pad[128];
    };
    struct ucontext_ uc;
    char retcode[8];
};

// On 64-bit system, fpu state handling
extern int xsave_extra;
extern int fxsave_extra;

#endif
