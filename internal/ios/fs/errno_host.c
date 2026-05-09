#include "errno_host.h"

#include <IXLandLinuxRuntime/kernel/errno.h>

int errno_host_map(void) {
    return errno_map();
}

int errno_host_or_value(int result) {
    if (result < 0)
        return errno_host_map();
    return result;
}

ssize_t errno_host_or_size(ssize_t result) {
    if (result < 0)
        return errno_host_map();
    return result;
}
