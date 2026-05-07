#ifndef IXLAND_ROOTFS_METADATA_H
#define IXLAND_ROOTFS_METADATA_H

#include <IXLandLinuxRuntime/kernel/fs.h>
#include <stdbool.h>

struct rootfs_stat {
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint32_t rdev;
};

bool rootfs_read_path_stat(struct mount *mount, const char *path, struct rootfs_stat *linux_stat);
bool rootfs_write_path_stat(struct mount *mount, const char *path, const struct rootfs_stat *linux_stat);
bool rootfs_read_fd_stat(struct fd *fd, struct rootfs_stat *linux_stat);
bool rootfs_write_fd_stat(struct fd *fd, const struct rootfs_stat *linux_stat);
bool rootfs_remove_path_stat(struct mount *mount, const char *path);
bool rootfs_read_full_path_stat(const char *path, struct rootfs_stat *linux_stat, bool nofollow);
bool rootfs_write_full_path_stat(const char *path, const struct rootfs_stat *linux_stat, bool nofollow);

#endif
