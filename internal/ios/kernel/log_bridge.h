#ifndef IXLAND_LOG_BRIDGE_H
#define IXLAND_LOG_BRIDGE_H

#include <stdint.h>

enum host_log_level {
    HOST_LOG_DEFAULT = 0,
    HOST_LOG_INFO,
    HOST_LOG_DEBUG,
    HOST_LOG_ERROR,
    HOST_LOG_FAULT,
};

void host_log_init(void);
void host_log_emit(enum host_log_level level, const char *subsystem, const char *category,
                   const char *msg);
void host_log_fault_fatal(const char *msg);

#endif
