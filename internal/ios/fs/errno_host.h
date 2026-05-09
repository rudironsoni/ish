#ifndef IXLAND_ERRNO_HOST_H
#define IXLAND_ERRNO_HOST_H

#include <sys/types.h>

int errno_host_map(void);
int errno_host_or_value(int result);
ssize_t errno_host_or_size(ssize_t result);

#endif
