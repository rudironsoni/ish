#include "rootfs_metadata.h"

#include <IXLandLinuxRuntime/fs/inode.h>
#include <IXLandLinuxRuntime/fs/real.h>
#include <IXLandLinuxRuntime/kernel/errno.h>
#include <IXLandLinuxRuntime/kernel/task.h>
#include <sys/stat.h>

static struct fd_ops rootfs_fdops;

static int rootfs_mount(struct mount *mount) {
    return realfs.mount(mount);
}

static void rootfs_linux_stat_from_host(const struct statbuf *host_stat,
                                        struct rootfs_stat *linux_stat) {
    linux_stat->mode = host_stat->mode;
    linux_stat->uid = host_stat->uid;
    linux_stat->gid = host_stat->gid;
    linux_stat->rdev = (uint32_t) host_stat->rdev;
}

static void rootfs_overlay_stat(struct statbuf *stat, const struct rootfs_stat *linux_stat) {
    stat->mode = linux_stat->mode;
    stat->uid = linux_stat->uid;
    stat->gid = linux_stat->gid;
    stat->rdev = linux_stat->rdev;
}

static void rootfs_linux_stat_setattr(struct rootfs_stat *linux_stat, struct attr attr) {
    switch (attr.type) {
    case attr_uid:
        linux_stat->uid = attr.uid;
        break;
    case attr_gid:
        linux_stat->gid = attr.gid;
        break;
    case attr_mode:
        linux_stat->mode = (linux_stat->mode & S_IFMT) | (attr.mode & ~S_IFMT);
        break;
    case attr_size:
        break;
    }
}

static bool rootfs_uses_placeholder_inode(mode_t_ mode) {
    return S_ISCHR(mode) || S_ISBLK(mode) || S_ISSOCK(mode);
}

static mode_t_ rootfs_host_mode_for_creation(mode_t_ mode) {
    if (rootfs_uses_placeholder_inode(mode)) {
        return S_IFREG | 0600;
    }
    return mode;
}

static void rootfs_init_linux_stat(const struct statbuf *host_stat, mode_t_ mode, dev_t_ dev,
                                   struct rootfs_stat *linux_stat) {
    rootfs_linux_stat_from_host(host_stat, linux_stat);
    linux_stat->mode = mode;
    linux_stat->uid = current->euid;
    linux_stat->gid = current->egid;
    linux_stat->rdev = (uint32_t) dev;
}

static struct fd *rootfs_open(struct mount *mount, const char *path, int flags, int mode) {
    struct fd *fd = realfs.open(mount, path, flags, 0666);
    if (IS_ERR(fd)) {
        return fd;
    }

    if ((flags & O_CREAT_) != 0) {
        struct statbuf host_stat;
        if (realfs.fstat(fd, &host_stat) == 0) {
            struct rootfs_stat linux_stat;
            if (!rootfs_read_fd_stat(fd, &linux_stat)) {
                rootfs_init_linux_stat(&host_stat, S_IFREG | mode, 0, &linux_stat);
                rootfs_write_fd_stat(fd, &linux_stat);
            }
        }
    }

    fd->ops = &rootfs_fdops;
    return fd;
}

static int rootfs_link(struct mount *mount, const char *src, const char *dst) {
    return realfs.link(mount, src, dst);
}

static int rootfs_unlink(struct mount *mount, const char *path) {
    rootfs_remove_path_stat(mount, path);
    return realfs.unlink(mount, path);
}

static int rootfs_rmdir(struct mount *mount, const char *path) {
    rootfs_remove_path_stat(mount, path);
    return realfs.rmdir(mount, path);
}

static int rootfs_rename(struct mount *mount, const char *src, const char *dst) {
    return realfs.rename(mount, src, dst);
}

static int rootfs_symlink(struct mount *mount, const char *target, const char *link) {
    return realfs.symlink(mount, target, link);
}

static int rootfs_mknod(struct mount *mount, const char *path, mode_t_ mode, dev_t_ dev) {
    int err = realfs.mknod(mount, path, rootfs_host_mode_for_creation(mode), 0);
    if (err < 0) {
        return err;
    }

    struct statbuf host_stat;
    err = realfs.stat(mount, path, &host_stat);
    if (err < 0) {
        return err;
    }

    struct rootfs_stat linux_stat;
    rootfs_init_linux_stat(&host_stat, mode, dev, &linux_stat);
    if (!rootfs_write_path_stat(mount, path, &linux_stat)) {
        return _EIO;
    }
    return 0;
}

static int rootfs_stat(struct mount *mount, const char *path, struct statbuf *stat) {
    int err = realfs.stat(mount, path, stat);
    if (err < 0) {
        return err;
    }
    struct rootfs_stat linux_stat;
    if (rootfs_read_path_stat(mount, path, &linux_stat)) {
        rootfs_overlay_stat(stat, &linux_stat);
    }
    return 0;
}

static int rootfs_fstat(struct fd *fd, struct statbuf *stat) {
    int err = realfs.fstat(fd, stat);
    if (err < 0) {
        return err;
    }
    struct rootfs_stat linux_stat;
    if (rootfs_read_fd_stat(fd, &linux_stat)) {
        rootfs_overlay_stat(stat, &linux_stat);
    }
    return 0;
}

static int rootfs_setattr(struct mount *mount, const char *path, struct attr attr) {
    if (attr.type == attr_size) {
        return realfs.setattr(mount, path, attr);
    }

    struct statbuf stat;
    int err = realfs.stat(mount, path, &stat);
    if (err < 0) {
        return err;
    }

    struct rootfs_stat linux_stat;
    if (!rootfs_read_path_stat(mount, path, &linux_stat)) {
        rootfs_linux_stat_from_host(&stat, &linux_stat);
    }
    rootfs_linux_stat_setattr(&linux_stat, attr);
    if (!rootfs_write_path_stat(mount, path, &linux_stat)) {
        return _EIO;
    }
    return 0;
}

static int rootfs_fsetattr(struct fd *fd, struct attr attr) {
    if (attr.type == attr_size) {
        return realfs.fsetattr(fd, attr);
    }

    struct statbuf stat;
    int err = realfs.fstat(fd, &stat);
    if (err < 0) {
        return err;
    }

    struct rootfs_stat linux_stat;
    if (!rootfs_read_fd_stat(fd, &linux_stat)) {
        rootfs_linux_stat_from_host(&stat, &linux_stat);
    }
    rootfs_linux_stat_setattr(&linux_stat, attr);
    if (!rootfs_write_fd_stat(fd, &linux_stat)) {
        return _EIO;
    }
    return 0;
}

static int rootfs_mkdir(struct mount *mount, const char *path, mode_t_ mode) {
    int err = realfs.mkdir(mount, path, 0777);
    if (err < 0) {
        return err;
    }

    struct statbuf host_stat;
    err = realfs.stat(mount, path, &host_stat);
    if (err < 0) {
        return err;
    }

    struct rootfs_stat linux_stat;
    rootfs_init_linux_stat(&host_stat, S_IFDIR | mode, 0, &linux_stat);
    if (!rootfs_write_path_stat(mount, path, &linux_stat)) {
        return _EIO;
    }
    return 0;
}

static ssize_t rootfs_readlink(struct mount *mount, const char *path, char *buf, size_t bufsize) {
    return realfs.readlink(mount, path, buf, bufsize);
}

static void __attribute__((constructor)) init_rootfs_fdops(void) {
    rootfs_fdops = realfs_fdops;
}

const struct fs_ops rootfs = {
    .name = "rootfs",
    .magic = 0x726f6f74,
    .mount = rootfs_mount,
    .statfs = realfs_statfs,
    .open = rootfs_open,
    .readlink = rootfs_readlink,
    .link = rootfs_link,
    .unlink = rootfs_unlink,
    .rmdir = rootfs_rmdir,
    .rename = rootfs_rename,
    .symlink = rootfs_symlink,
    .mknod = rootfs_mknod,
    .close = realfs_close,
    .stat = rootfs_stat,
    .fstat = rootfs_fstat,
    .flock = realfs_flock,
    .setattr = rootfs_setattr,
    .fsetattr = rootfs_fsetattr,
    .getpath = realfs_getpath,
    .utime = realfs_utime,
    .mkdir = rootfs_mkdir,
};
