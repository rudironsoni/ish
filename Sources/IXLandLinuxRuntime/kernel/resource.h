#ifndef RESOURCE_H
#define RESOURCE_H
#import <IXLandLinuxRuntime/kernel/time.h>

typedef uint64_t rlim_t_;
typedef uint32_t rlim32_t_;
#define RLIM_INFINITY_ ((rlim_t_) - 1)

struct rlimit_ {
    rlim_t_ cur;
    rlim_t_ max;
};

struct rlimit32_ {
    rlim32_t_ cur;
    rlim32_t_ max;
};

#define RLIMIT_CPU_        0
#define RLIMIT_FSIZE_      1
#define RLIMIT_DATA_       2
#define RLIMIT_STACK_      3
#define RLIMIT_CORE_       4
#define RLIMIT_RSS_        5
#define RLIMIT_NPROC_      6
#define RLIMIT_NOFILE_     7
#define RLIMIT_MEMLOCK_    8
#define RLIMIT_AS_         9
#define RLIMIT_LOCKS_      10
#define RLIMIT_SIGPENDING_ 11
#define RLIMIT_MSGQUEUE_   12
#define RLIMIT_NICE_       13
#define RLIMIT_RTPRIO_     14
#define RLIMIT_RTTIME_     15
#define RLIMIT_NLIMITS_    16

uint32_t sys_getrlimit32(uint32_t resource, addr_t rlim_addr);
uint32_t sys_setrlimit32(uint32_t resource, addr_t rlim_addr);
uint32_t sys_prlimit64(pid_t_ pid, uint32_t resource, addr_t new_limit_addr, addr_t old_limit_addr);
uint32_t sys_old_getrlimit32(uint32_t resource, addr_t rlim_addr);

rlim_t_ rlimit(int resource);

struct rusage_ {
    struct timeval_ utime;
    struct timeval_ stime;
    uint32_t maxrss;
    uint32_t ixrss;
    uint32_t idrss;
    uint32_t isrss;
    uint32_t minflt;
    uint32_t majflt;
    uint32_t nswap;
    uint32_t inblock;
    uint32_t oublock;
    uint32_t msgsnd;
    uint32_t msgrcv;
    uint32_t nsignals;
    uint32_t nvcsw;
    uint32_t nivcsw;
};

struct rusage_ rusage_get_current(void);
void rusage_add(struct rusage_ *dst, struct rusage_ *src);
#define RUSAGE_SELF_     0
#define RUSAGE_CHILDREN_ -1
uint32_t sys_getrusage(uint32_t who, addr_t rusage_addr);

int64_t sys_sched_getaffinity(pid_t_ pid, uint32_t cpusetsize, addr_t cpuset_addr);
int64_t sys_sched_setaffinity(pid_t_ pid, uint32_t cpusetsize, addr_t cpuset_addr);
int64_t sys_getpriority(int64_t which, pid_t_ who);
int64_t sys_setpriority(int64_t which, pid_t_ who, int64_t prio);

int64_t sys_sched_getparam(pid_t_ pid, addr_t param_addr);
int64_t sys_sched_getscheduler(pid_t_ UNUSED(pid));
int64_t sys_sched_setscheduler(pid_t_ UNUSED(pid), int64_t policy, addr_t param_addr);
int64_t sys_sched_get_priority_max(int64_t policy);

int64_t sys_ioprio_set(int64_t UNUSED(which), int64_t UNUSED(who), int64_t UNUSED(ioprio));
int64_t sys_ioprio_get(int64_t UNUSED(which), int64_t UNUSED(who), int64_t UNUSED(ioprio));

#endif
