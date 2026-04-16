#ifndef TIME_H
#define TIME_H
#import <IXLandLinuxRuntime/fs/fd.h>
#import <IXLandLinuxRuntime/util/misc.h>
#include <time.h>

int32_t sys_time(addr_t time_out);
int32_t sys_stime(addr_t time);
#define CLOCK_REALTIME_           0
#define CLOCK_MONOTONIC_          1
#define CLOCK_PROCESS_CPUTIME_ID_ 2
#define CLOCK_REALTIME_COARSE_    5
int32_t sys_clock_gettime(int32_t clock, addr_t tp);
int32_t sys_clock_settime(int32_t clock, addr_t tp);
int32_t sys_clock_getres(int32_t clock, addr_t res_addr);

struct timeval_ {
    int64_t sec;
    int64_t usec;
};
struct timespec_ {
    int64_t sec;
    int64_t nsec;
};
struct timezone_ {
    int32_t minuteswest;
    int32_t dsttime;
};

static inline clock_t_ clock_from_timeval(struct timeval_ timeval)
{
    return timeval.sec * 100 + timeval.usec / 10000;
}

static inline struct timespec convert_timespec(struct timespec_ t)
{
    struct timespec ts;
    ts.tv_sec = t.sec;
    ts.tv_nsec = t.nsec;
    return ts;
}

static inline struct timespec convert_timeval(struct timeval_ t)
{
    struct timespec ts;
    ts.tv_sec = t.sec;
    ts.tv_nsec = t.usec * 1000;
    return ts;
}

#define ITIMER_REAL_    0
#define ITIMER_VIRTUAL_ 1
#define ITIMER_PROF_    2
struct itimerval_ {
    struct timeval_ interval;
    struct timeval_ value;
};

struct itimerspec_ {
    struct timespec_ interval;
    struct timespec_ value;
};

struct tms_ {
    clock_t_ tms_utime;
    clock_t_ tms_stime;
    clock_t_ tms_cutime;
    clock_t_ tms_cstime;
};

int64_t sys_setitimer(int64_t which, addr_t new_val, addr_t old_val);
uint64_t sys_alarm(uint64_t seconds);
int64_t sys_timer_create(int32_t clock, addr_t sigevent_addr, addr_t timer_addr);
int64_t sys_timer_settime(int32_t timer, int64_t flags, addr_t new_value_addr,
                          addr_t old_value_addr);
int64_t sys_timer_delete(int32_t timer_id);
int32_t sys_timerfd_create(int32_t clockid, int32_t flags);
int64_t sys_timerfd_settime(int32_t f, int32_t flags, addr_t new_value_addr, addr_t old_value_addr);

int32_t sys_times(addr_t tbuf);
int32_t sys_nanosleep(addr_t req, addr_t rem);
int32_t sys_gettimeofday(addr_t tv, addr_t tz);
int32_t sys_settimeofday(addr_t tv, addr_t tz);

#endif
