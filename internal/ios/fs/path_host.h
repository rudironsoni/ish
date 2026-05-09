#ifndef IXLAND_PATH_HOST_H
#define IXLAND_PATH_HOST_H

#include <IXLandLinuxRuntime/kernel/fs.h>
#include <stddef.h>

const char *path_host_relative(const char *path);
int path_host_get(int fd, char *buf);
void path_host_full_path(const struct mount *mount, const char *path, char *buffer,
                         size_t buffer_size);

#endif
