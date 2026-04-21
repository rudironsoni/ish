#import <IXLandLinuxRuntime/kernel/calls.h>
#include <limits.h>
#include <string.h>

static bool resource_valid(int resource)
{
    return resource >= 0 && resource < RLIMIT_NLIMITS_;
}

static int rlimit_get(struct task *task, int resource, struct rlimit_ *limit)
{
    if (!resource_valid(resource))
        return _EINVAL;
    struct tgroup *group = task->group;
    lock(&group->lock);
    *limit = group->limits[resource];
    unlock(&group->lock);
    return 0;
}

static int rlimit_set(struct task *task, int resource, struct rlimit_ limit)
{
    if (!resource_valid(resource))
        return _EINVAL;
    struct tgroup *group = task->group;
    lock(&group->lock);
    group->limits[resource] = limit;
    unlock(&group->lock);
    return 0;
}

rlim_t_ rlimit(int resource)
{
    struct rlimit_ limit;
    if (rlimit_get(current, resource, &limit) != 0)
        die("invalid resource %d", resource);
    return limit.cur;
}

static int do_getrlimit32(int resource, struct rlimit32_ *rlimit32)
{
    STRACE("getlimit(%d)", resource);
    struct rlimit_ rlimit;
    int err = rlimit_get(current, resource, &rlimit);
    if (err < 0)
        return err;
    STRACE(" {cur=%#x, max=%#x}", rlimit.cur, rlimit.max);

    rlimit32->max = (rlim32_t_)rlimit.max;
    rlimit32->cur = (rlim32_t_)rlimit.cur;
    return 0;
}

int32_t sys_getrlimit32(int32_t resource, addr_t rlim_addr)
{
    struct rlimit32_ rlimit;
    int err = do_getrlimit32(resource, &rlimit);
    if (err < 0)
        return err;
    if (user_put(rlim_addr, rlimit))
        return _EFAULT;
    return 0;
}

int32_t sys_old_getrlimit32(int32_t resource, addr_t rlim_addr)
{
    struct rlimit32_ rlimit;
    int err = do_getrlimit32(resource, &rlimit);
    if (err < 0)
        return err;

    // This version of the call is for programs that aren't aware of rlim_t
    // being 64 bit. RLIM_INFINITY looks like -1 when truncated to 32 bits.
    if (rlimit.cur > INT_MAX)
        rlimit.cur = INT_MAX;
    if (rlimit.max > INT_MAX)
        rlimit.max = INT_MAX;

    if (user_put(rlim_addr, rlimit))
        return _EFAULT;
    return 0;
}

static int check_setrlimit(int resource, struct rlimit_ new_limit)
{
    if (superuser())
        return 0;
    struct rlimit_ old_limit;
    int err = rlimit_get(current, resource, &old_limit);
    if (err < 0)
        return err;
    if (new_limit.max > old_limit.max)
        return _EPERM;
    return 0;
}

int32_t sys_setrlimit32(int32_t resource, addr_t rlim_addr)
{
    struct rlimit_ rlimit;
    if (user_get(rlim_addr, rlimit))
        return _EFAULT;
    STRACE("setrlimit(%d, {cur=%#x, max=%#x})", resource, rlimit.cur, rlimit.max);
    int err = check_setrlimit(resource, rlimit);
    if (err < 0)
        return err;
    return rlimit_set(current, resource, rlimit);
}

int32_t sys_prlimit64(pid_t_ pid, int32_t resource, addr_t new_limit_addr, addr_t old_limit_addr)
{
    STRACE("prlimit64(%d, %d)", pid, resource);
    if (pid != 0)
        return _EINVAL;

    if (old_limit_addr != 0) {
        struct rlimit_ rlimit;
        int err = rlimit_get(current, resource, &rlimit);
        if (err < 0)
            return err;
        STRACE(" old={cur=%#x, max=%#x}", rlimit.cur, rlimit.max);
        if (user_put(old_limit_addr, rlimit))
            return _EFAULT;
    }

    if (new_limit_addr != 0) {
        struct rlimit_ rlimit;
        if (user_get(new_limit_addr, rlimit))
            return _EFAULT;
        STRACE(" new={cur=%#x, max=%#x}", rlimit.cur, rlimit.max);
        int err = check_setrlimit(resource, rlimit);
        if (err < 0)
            return err;
        return rlimit_set(current, resource, rlimit);
    }
    return 0;
}


static void timeval_add(struct timeval_ *dst, struct timeval_ *src)
{
    dst->sec += src->sec;
    dst->usec += src->usec;
    if (dst->usec >= 1000000) {
        dst->usec -= 1000000;
        dst->sec++;
    }
}

void rusage_add(struct rusage_ *dst, struct rusage_ *src)
{
    timeval_add(&dst->utime, &src->utime);
    timeval_add(&dst->stime, &src->stime);
}

int32_t sys_getrusage(int32_t who, addr_t rusage_addr)
{
    struct rusage_ rusage;
    switch (who) {
    case RUSAGE_SELF_:
        rusage = rusage_get_current();
        break;
    case RUSAGE_CHILDREN_:
        lock(&current->group->lock);
        rusage = current->group->children_rusage;
        unlock(&current->group->lock);
        break;
    default:
        return _EINVAL;
    }
    if (user_put(rusage_addr, rusage))
        return _EFAULT;
    return 0;
}

int64_t sys_sched_getaffinity(pid_t_ pid, uint32_t cpusetsize, addr_t cpuset_addr)
{
    STRACE("sched_getaffinity(%d, %d, %#x)", pid, cpusetsize, cpuset_addr);
    if (pid != 0) {
        lock(&pids_lock);
        struct task *task = pid_get_task(pid);
        unlock(&pids_lock);
        if (task == NULL)
            return _ESRCH;
    }

    unsigned cpus = (unsigned)sysconf(_SC_NPROCESSORS_ONLN);
    char cpuset[cpus / 8 + 1];
    if (cpusetsize < sizeof(cpuset))
        return _EINVAL;
    memset(cpuset, 0, sizeof(cpuset));
    for (unsigned i = 0; i < cpus; i++)
        bit_set(i, cpuset);
    if (user_write(cpuset_addr, cpuset, sizeof(cpuset)))
        return _EFAULT;
    // return the number of bytes written
    return sizeof(cpuset);
}
int64_t sys_sched_setaffinity(pid_t_ UNUSED(pid), uint32_t UNUSED(cpusetsize),
                              addr_t UNUSED(cpuset_addr))
{
    // meh
    return 0;
}

int64_t sys_getpriority(int32_t which, pid_t_ who)
{
    STRACE("getpriority(%d, %d)", which, who);
    return 20;
}
int64_t sys_setpriority(int32_t which, pid_t_ who, int32_t prio)
{
    STRACE("setpriority(%d, %d, %d)", which, who, prio);
    return 0;
}

// realtime scheduling stubs
int64_t sys_sched_getparam(pid_t_ UNUSED(pid), addr_t param_addr)
{
    int32_t sched_priority = 0;
    if (user_put(param_addr, sched_priority))
        return _EFAULT;
    return 0;
}
#define SCHED_OTHER_ 0
int64_t sys_sched_getscheduler(pid_t_ UNUSED(pid))
{
    return SCHED_OTHER_;
}
int64_t sys_sched_setscheduler(pid_t_ UNUSED(pid), int32_t policy, addr_t param_addr)
{
    if (policy != SCHED_OTHER_)
        return _EINVAL;
    int32_t sched_priority;
    if (user_get(param_addr, sched_priority))
        return _EFAULT;
    if (sched_priority != 0)
        return _EINVAL;
    return 0;
}

int64_t sys_sched_get_priority_max(int32_t policy)
{
    STRACE("sched_get_priority_max(%d)", policy);
    if (policy == 0)
        return 0;
    return _EINVAL;
}

int64_t sys_ioprio_get(int32_t UNUSED(which), int32_t UNUSED(who), int32_t UNUSED(ioprio))
{
    return 0;
}
int64_t sys_ioprio_set(int32_t UNUSED(which), int32_t UNUSED(who), int32_t UNUSED(ioprio))
{
    return 0;
}
