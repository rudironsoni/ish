#import <IXLandLinuxRuntime/kernel/calls.h>
#import <IXLandLinuxRuntime/platform/platform.h>
#include <string.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>

const char *uname_version = "SUPER AWESOME";
const char *uname_hostname_override = NULL;

void do_uname(struct uname *uts)
{
    struct utsname real_uname;
    uname(&real_uname);
    const char *hostname = real_uname.nodename;
    if (uname_hostname_override)
        hostname = uname_hostname_override;

    memset(uts, 0, sizeof(struct uname));
    strcpy(uts->system, "Linux");
    strcpy(uts->hostname, hostname);
    strcpy(uts->release, "4.20.69-ish");
    snprintf(uts->version, sizeof(uts->version), "%s %s %s", uname_version, __DATE__, __TIME__);
    strcpy(uts->arch, "aarch64");
    strcpy(uts->domain, "(none)");
}

uint32_t sys_uname(addr_t uts_addr)
{
    struct uname uts;
    do_uname(&uts);
    if (user_put(uts_addr, uts))
        return _EFAULT;
    return 0;
}

uint32_t sys_sethostname(addr_t UNUSED(hostname_addr), uint32_t UNUSED(hostname_len))
{
    return _EPERM;
}

static uint64_t get_total_ram()
{
    uint64_t total_ram;
    sysctl((int[]){ CTL_DEBUG, HW_PHYSMEM }, 2, &total_ram, NULL, NULL, 0);
    return total_ram;
}
static void sysinfo_specific(struct sys_info *info)
{
    info->totalram = get_total_ram();
    // TODO: everything else
}

uint32_t sys_sysinfo(addr_t info_addr)
{
    struct sys_info info = { 0 };
    struct uptime_info uptime = get_uptime();
    info.uptime = uptime.uptime_ticks;
    info.loads[0] = uptime.load_1m;
    info.loads[1] = uptime.load_5m;
    info.loads[2] = uptime.load_15m;
    sysinfo_specific(&info);

    if (user_put(info_addr, info))
        return _EFAULT;
    return 0;
}
