#include "rootfs_metadata.h"
#include "path_host.h"

#include <IXLandLinuxRuntime/util/misc.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <string.h>

static const char *kLinuxStatXattr = "com.ixland.rootfs.linuxstat";

static void rootfs_full_path(struct mount *mount, const char *path, char *buffer, size_t buffer_size) {
    path_host_full_path(mount, path, buffer, buffer_size);
}

bool rootfs_read_full_path_stat(const char *path, struct rootfs_stat *linux_stat, bool nofollow) {
    int options = nofollow ? XATTR_NOFOLLOW : 0;
    ssize_t size = getxattr(path, kLinuxStatXattr, linux_stat, sizeof(*linux_stat), 0, options);
    return size == sizeof(*linux_stat);
}

bool rootfs_write_full_path_stat(const char *path, const struct rootfs_stat *linux_stat, bool nofollow) {
    int options = nofollow ? XATTR_NOFOLLOW : 0;
    return setxattr(path, kLinuxStatXattr, linux_stat, sizeof(*linux_stat), 0, options) == 0;
}

static bool rootfs_read_path_xattr(struct mount *mount, const char *path, struct rootfs_stat *linux_stat) {
    char full_path[MAX_PATH + 1];
    rootfs_full_path(mount, path, full_path, sizeof(full_path));
    return rootfs_read_full_path_stat(full_path, linux_stat, true);
}

bool rootfs_write_path_stat(struct mount *mount, const char *path, const struct rootfs_stat *linux_stat) {
    char full_path[MAX_PATH + 1];
    rootfs_full_path(mount, path, full_path, sizeof(full_path));
    return rootfs_write_full_path_stat(full_path, linux_stat, true);
}

bool rootfs_read_path_stat(struct mount *mount, const char *path, struct rootfs_stat *linux_stat) {
    return rootfs_read_path_xattr(mount, path, linux_stat);
}

bool rootfs_read_fd_stat(struct fd *fd, struct rootfs_stat *linux_stat) {
    ssize_t size = fgetxattr(fd->real_fd, kLinuxStatXattr, linux_stat, sizeof(*linux_stat), 0, 0);
    return size == sizeof(*linux_stat);
}

bool rootfs_write_fd_stat(struct fd *fd, const struct rootfs_stat *linux_stat) {
    return fsetxattr(fd->real_fd, kLinuxStatXattr, linux_stat, sizeof(*linux_stat), 0, 0) == 0;
}

bool rootfs_remove_path_stat(struct mount *mount, const char *path) {
    char full_path[MAX_PATH + 1];
    rootfs_full_path(mount, path, full_path, sizeof(full_path));
    return removexattr(full_path, kLinuxStatXattr, XATTR_NOFOLLOW) == 0 || errno == ENOATTR;
}
