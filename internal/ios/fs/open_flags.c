#include "open_flags.h"

#include <IXLandLinuxRuntime/kernel/fs.h>
#include <fcntl.h>

int open_flags_host_from_linux(int flags) {
    int host_flags = 0;
    if (flags & O_WRONLY_)
        host_flags |= O_WRONLY;
    if (flags & O_RDWR_)
        host_flags |= O_RDWR;
    if (flags & O_CREAT_)
        host_flags |= O_CREAT;
    if (flags & O_EXCL_)
        host_flags |= O_EXCL;
    if (flags & O_TRUNC_)
        host_flags |= O_TRUNC;
    if (flags & O_APPEND_)
        host_flags |= O_APPEND;
    if (flags & O_NONBLOCK_)
        host_flags |= O_NONBLOCK;
    if (flags & O_NOFOLLOW_)
        host_flags |= O_NOFOLLOW;
    return host_flags;
}

int open_flags_linux_from_host(int flags) {
    int linux_flags = 0;
    if (flags & O_WRONLY)
        linux_flags |= O_WRONLY_;
    if (flags & O_RDWR)
        linux_flags |= O_RDWR_;
    if (flags & O_CREAT)
        linux_flags |= O_CREAT_;
    if (flags & O_EXCL)
        linux_flags |= O_EXCL_;
    if (flags & O_TRUNC)
        linux_flags |= O_TRUNC_;
    if (flags & O_APPEND)
        linux_flags |= O_APPEND_;
    if (flags & O_NONBLOCK)
        linux_flags |= O_NONBLOCK_;
    return linux_flags;
}
