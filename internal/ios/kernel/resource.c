#import <IXLandLinuxRuntime/kernel/resource.h>
#include <mach/mach.h>

struct rusage_ rusage_get_current(void)
{
    struct rusage_ rusage;
    thread_basic_info_data_t info;
    mach_msg_type_number_t count = THREAD_BASIC_INFO_COUNT;
    thread_info(mach_thread_self(), THREAD_BASIC_INFO, (thread_info_t)&info, &count);
    rusage.utime.sec = info.user_time.seconds;
    rusage.utime.usec = info.user_time.microseconds;
    rusage.stime.sec = info.system_time.seconds;
    rusage.stime.usec = info.system_time.microseconds;
    return rusage;
}
