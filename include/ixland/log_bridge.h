#ifndef IXLAND_LOG_BRIDGE_H
#define IXLAND_LOG_BRIDGE_H

#include <stdint.h>

enum log_level {
    LOG_DEFAULT = 0,
    LOG_INFO,
    LOG_DEBUG,
    LOG_ERROR,
    LOG_FAULT,
};

void log_init_impl(void);
void log_emit_impl(enum log_level level, const char *subsystem, const char *category,
                   const char *msg);
void log_fault_fatal_impl(const char *msg);

#endif
