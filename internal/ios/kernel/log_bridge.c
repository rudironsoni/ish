#include "log_bridge.h"

#include <os/log.h>
#include <stdlib.h>

static os_log_t g_log_kernel;
static os_log_t g_log_emulator;
static os_log_t g_log_crash;

static const char *const kISHOSLogSubsystem = "app.ixland.terminal";

__attribute__((constructor)) static void host_log_init_impl(void)
{
    g_log_kernel = os_log_create(kISHOSLogSubsystem, "kernel");
    g_log_emulator = os_log_create(kISHOSLogSubsystem, "emulator");
    g_log_crash = os_log_create(kISHOSLogSubsystem, "crash");
}

void host_log_init(void) {}

static os_log_t log_for_category(const char *category)
{
    if (category && strcmp(category, "crash") == 0)
        return g_log_crash;
    if (category && strcmp(category, "emulator") == 0)
        return g_log_emulator;
    return g_log_kernel;
}

static os_log_type_t log_type_for_level(enum host_log_level level)
{
    switch (level) {
    case HOST_LOG_DEBUG:
        return OS_LOG_TYPE_DEBUG;
    case HOST_LOG_INFO:
        return OS_LOG_TYPE_INFO;
    case HOST_LOG_ERROR:
        return OS_LOG_TYPE_ERROR;
    case HOST_LOG_FAULT:
        return OS_LOG_TYPE_FAULT;
    case HOST_LOG_DEFAULT:
        return OS_LOG_TYPE_DEFAULT;
    }
    return OS_LOG_TYPE_DEFAULT;
}

void host_log_emit(enum host_log_level level, const char *subsystem, const char *category,
                   const char *msg)
{
    os_log_t log = log_for_category(category);
    os_log_type_t type = log_type_for_level(level);
    switch (type) {
    case OS_LOG_TYPE_DEBUG:
        os_log_debug(log, "%{public}s", msg);
        break;
    case OS_LOG_TYPE_INFO:
        os_log_info(log, "%{public}s", msg);
        break;
    case OS_LOG_TYPE_ERROR:
        os_log_error(log, "%{public}s", msg);
        break;
    case OS_LOG_TYPE_FAULT:
        os_log_fault(log, "%{public}s", msg);
        break;
    default:
        os_log(log, "%{public}s", msg);
        break;
    }
}

void host_log_fault_fatal(const char *msg)
{
    os_log_fault(g_log_crash, "FATAL: %{public}s", msg);
}
