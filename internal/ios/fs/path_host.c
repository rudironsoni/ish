#include "path_host.h"

#include <IXLandLinuxRuntime/fs/fix_path.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

const char *path_host_relative(const char *path) {
    return fix_path(path);
}

int path_host_get(int fd, char *buf) {
    return fcntl(fd, F_GETPATH, buf);
}

void path_host_full_path(const struct mount *mount, const char *path, char *buffer,
                         size_t buffer_size) {
    const char *relative = path_host_relative(path);
    if (strcmp(relative, ".") == 0) {
        strlcpy(buffer, mount->source, buffer_size);
        return;
    }
    snprintf(buffer, buffer_size, "%s/%s", mount->source, relative);
}
