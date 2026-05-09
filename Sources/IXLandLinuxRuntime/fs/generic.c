#import <IXLandInstrumentationTracing/trace.h>
#import <IXLandLinuxRuntime/fs/dev.h>
#import <IXLandLinuxRuntime/fs/fd.h>
#import <IXLandLinuxRuntime/fs/inode.h>
#import <IXLandLinuxRuntime/fs/path.h>
#import <IXLandLinuxRuntime/kernel/errno.h>
#import <IXLandLinuxRuntime/kernel/fs.h>
#import <IXLandLinuxRuntime/kernel/task.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

struct mount *find_mount_and_trim_path(char *path)
{
    struct mount *mount = mount_find(path);
    char *dst = path;
    const char *src = path + strlen(mount->point);
    while (*src != '\0')
        *dst++ = *src++;
    *dst = '\0';
    return mount;
}

bool contains_mount_point(const char *path)
{
    struct mount *mount;
    list_for_each_entry (&mounts, mount, mounts) {
        size_t n = strlen(path);
        if (strncmp(path, mount->point, n) == 0 &&
            (mount->point[n] == '\0' || mount->point[n] == '/'))
            return true;
    }
    return false;
}

static void trace_generic_openat_result(const char *path_raw, const char *normalized,
                                        const char *trimmed, const char *mount_point, int flags,
                                        int mode, long result)
{
    char ev[1024];
    snprintf(ev, sizeof(ev),
             "boot.generic_openat.open=raw:%s,normalized:%s,trimmed:%s,mount:%s,flags:0x%x,"
             "mode:0%o,result:%ld",
             path_raw ? path_raw : "", normalized ? normalized : "", trimmed ? trimmed : "",
             mount_point ? mount_point : "", flags, mode, result);
    trace_record_event(TRACE_ORIGIN_KERNEL, ev);
}

static bool trace_generic_openat_is_musl_loader_path(const char *path)
{
    return path != NULL &&
           (strstr(path, "libc.musl-aarch64.so.1") != NULL ||
            strstr(path, "ld-musl-aarch64.so.1") != NULL);
}

static void trace_musl_loader_openat(const char *phase, const char *path_raw, const char *normalized,
                                     const char *trimmed, const char *mount_point, long result)
{
    char ev[1024];
    snprintf(ev, sizeof(ev),
             "loader.lib.open=phase:%s,raw:%s,normalized:%s,trimmed:%s,mount:%s,result:%ld",
             phase ? phase : "", path_raw ? path_raw : "", normalized ? normalized : "",
             trimmed ? trimmed : "", mount_point ? mount_point : "", result);
    trace_record_event(TRACE_ORIGIN_KERNEL, ev);
}

struct fd *generic_openat(struct fd *at, const char *path_raw, int flags, int mode)
{
    trace_record_event(TRACE_ORIGIN_KERNEL, "boot.generic_openat.entry");
    if (flags & O_RDWR_ && flags & O_WRONLY_)
        return ERR_PTR(_EINVAL);

    // TODO really, really, seriously reconsider what I'm doing with the strings
    char path[MAX_PATH];
    int err = path_normalize(at, path_raw, path,
                             N_SYMLINK_FOLLOW | (flags & O_CREAT_ ? N_PARENT_DIR_WRITE : 0));
    if (err < 0)
        return ERR_PTR(err);
    trace_record_event(TRACE_ORIGIN_KERNEL, "boot.generic_openat.after_path_normalize");
    char normalized[MAX_PATH];
    strncpy(normalized, path, sizeof(normalized));
    normalized[sizeof(normalized) - 1] = '\0';
    struct mount *mount = find_mount_and_trim_path(path);
    bool trace_musl_loader_path = trace_generic_openat_is_musl_loader_path(path_raw) ||
                                  trace_generic_openat_is_musl_loader_path(normalized) ||
                                  trace_generic_openat_is_musl_loader_path(path);
    if (trace_musl_loader_path)
        trace_musl_loader_openat("resolved", path_raw, normalized, path, mount->point, 0);
    trace_record_event(TRACE_ORIGIN_KERNEL, "boot.generic_openat.after_find_mount");
    trace_record_event(TRACE_ORIGIN_KERNEL, "boot.generic_openat.before_fs_open");
    struct fd *fd = mount->fs->open(mount, path, flags, mode);
    if (trace_musl_loader_path)
        trace_musl_loader_openat("opened", path_raw, normalized, path, mount->point,
                                 IS_ERR(fd) ? (long)PTR_ERR(fd) : 0L);
    trace_record_event(TRACE_ORIGIN_KERNEL, "boot.generic_openat.after_fs_open");
    char flags_buf[32];
    char mode_buf[32];
    char result_buf[32];
    snprintf(flags_buf, sizeof(flags_buf), "0x%x", flags);
    snprintf(mode_buf, sizeof(mode_buf), "0%o", mode);
    snprintf(result_buf, sizeof(result_buf), "%ld", IS_ERR(fd) ? (long)PTR_ERR(fd) : 0L);
    trace_attribute_t open_attrs[] = {
        { .key = "raw", .value = path_raw ? path_raw : "" },
        { .key = "normalized", .value = normalized },
        { .key = "trimmed", .value = path },
        { .key = "mount", .value = mount->point ? mount->point : "" },
        { .key = "flags", .value = flags_buf },
        { .key = "mode", .value = mode_buf },
        { .key = "result", .value = result_buf },
    };
    (void)trace_begin_interval(TRACE_ORIGIN_KERNEL, "boot.generic_openat.open", open_attrs,
                               sizeof(open_attrs) / sizeof(open_attrs[0]));
    trace_generic_openat_result(path_raw, normalized, path, mount->point, flags, mode,
                                IS_ERR(fd) ? (long)PTR_ERR(fd) : 0L);
    if (IS_ERR(fd)) {
        // if an error happens after this point, fd_close will release the
        // mount, but right now we need to do it manually
        mount_release(mount);
        return fd;
    }
    fd->mount = mount;

    lock(&inodes_lock); // TODO: don't do this
    struct statbuf stat;
    err = fd->mount->fs->fstat(fd, &stat);
    trace_record_event(TRACE_ORIGIN_KERNEL, "boot.generic_openat.after_fstat");
    if (err < 0) {
        if (err == _ENOMEM)
            trace_record_event(TRACE_ORIGIN_KERNEL, "boot.generic_openat.fstat.fail.enomem");
        unlock(&inodes_lock);
        goto error;
    }
    fd->inode = inode_get_unlocked(mount, stat.inode);
    unlock(&inodes_lock);
    fd->type = stat.mode & S_IFMT;
    fd->flags = flags;

    int accmode;
    if (flags & O_RDWR_)
        accmode = AC_R | AC_W;
    else if (flags & O_WRONLY_)
        accmode = AC_W;
    else
        accmode = AC_R;
    err = access_check(&stat, accmode);
    if (err < 0)
        goto error;

    assert(!S_ISLNK(fd->type)); // would mean path_normalize didn't do its job
    if (S_ISBLK(fd->type) || S_ISCHR(fd->type)) {
        int type;
        if (S_ISBLK(fd->type))
            type = DEV_BLOCK;
        else
            type = DEV_CHAR;
        err = dev_open(dev_major(stat.rdev), dev_minor(stat.rdev), type, fd);
        if (err < 0)
            goto error;
    }
    err = _ENXIO;
    if (S_ISSOCK(fd->type))
        goto error;
    err = _EISDIR;
    if (S_ISDIR(fd->type) && flags & (O_RDWR_ | O_WRONLY_))
        goto error;
    err = _ENOTDIR;
    if (!S_ISDIR(fd->type) && flags & O_DIRECTORY_)
        goto error;
    return fd;

error:
    fd_close(fd);
    return ERR_PTR(err);
}

struct fd *generic_open(const char *path, int flags, int mode)
{
    return generic_openat(AT_PWD, path, flags, mode);
}

int generic_getpath(struct fd *fd, char *buf)
{
    int err = fd->mount->fs->getpath(fd, buf);
    if (err < 0)
        return err;
    if (strlen(buf) + strlen(fd->mount->point) >= MAX_PATH)
        return _ENAMETOOLONG;
    memmove(buf + strlen(fd->mount->point), buf, strlen(buf) + 1);
    memcpy(buf, fd->mount->point, strlen(fd->mount->point));
    if (buf[0] == '\0')
        strcpy(buf, "/");
    return 0;
}

int generic_accessat(struct fd *dirfd, const char *path_raw, int mode)
{
    char path[MAX_PATH];
    int err = path_normalize(dirfd, path_raw, path, N_SYMLINK_FOLLOW);
    if (err < 0)
        return err;

    struct mount *mount = find_mount_and_trim_path(path);
    struct statbuf stat = {};
    err = mount->fs->stat(mount, path, &stat);
    mount_release(mount);
    if (err < 0)
        return err;
    return access_check(&stat, mode);
}

int generic_linkat(struct fd *src_at, const char *src_raw, struct fd *dst_at, const char *dst_raw)
{
    char src[MAX_PATH];
    int err = path_normalize(src_at, src_raw, src, N_SYMLINK_NOFOLLOW);
    if (err < 0)
        return err;
    char dst[MAX_PATH];
    err = path_normalize(dst_at, dst_raw, dst, N_SYMLINK_NOFOLLOW | N_PARENT_DIR_WRITE);
    if (err < 0)
        return err;
    struct mount *mount = find_mount_and_trim_path(src);
    struct mount *dst_mount = find_mount_and_trim_path(dst);
    if (mount != dst_mount)
        err = _EXDEV;
    else if (mount->fs->link == NULL)
        err = _EPERM;
    else
        err = mount->fs->link(mount, src, dst);
    mount_release(mount);
    mount_release(dst_mount);
    return err;
}

int generic_unlinkat(struct fd *at, const char *path_raw)
{
    char path[MAX_PATH];
    int err = path_normalize(at, path_raw, path, N_SYMLINK_NOFOLLOW);
    if (err < 0)
        return err;
    struct mount *mount = find_mount_and_trim_path(path);
    err = _EPERM;
    if (mount->fs->unlink)
        err = mount->fs->unlink(mount, path);
    mount_release(mount);
    return err;
}

int generic_renameat(struct fd *src_at, const char *src_raw, struct fd *dst_at, const char *dst_raw)
{
    char src[MAX_PATH];
    int err = path_normalize(src_at, src_raw, src, N_SYMLINK_NOFOLLOW);
    if (err < 0)
        return err;
    char dst[MAX_PATH];
    err = path_normalize(dst_at, dst_raw, dst, N_SYMLINK_NOFOLLOW | N_PARENT_DIR_WRITE);
    if (err < 0)
        return err;
    if (contains_mount_point(src))
        return _EBUSY;
    struct mount *mount = find_mount_and_trim_path(src);
    struct mount *dst_mount = find_mount_and_trim_path(dst);
    if (mount != dst_mount)
        err = _EXDEV;
    else if (mount->fs->rename == NULL)
        err = _EPERM;
    else
        err = mount->fs->rename(mount, src, dst);
    mount_release(mount);
    mount_release(dst_mount);
    return err;
}

int generic_symlinkat(const char *target, struct fd *at, const char *link_raw)
{
    char link[MAX_PATH];
    int err = path_normalize(at, link_raw, link, N_SYMLINK_NOFOLLOW | N_PARENT_DIR_WRITE);
    if (err < 0)
        return err;
    struct mount *mount = find_mount_and_trim_path(link);
    err = _EPERM;
    if (mount->fs->symlink)
        err = mount->fs->symlink(mount, target, link);
    mount_release(mount);
    return err;
}

int generic_mknodat(struct fd *at, const char *path_raw, mode_t_ mode, dev_t_ dev)
{
    if (S_ISDIR(mode) || S_ISLNK(mode))
        return _EINVAL;
    if (!superuser() && (S_ISBLK(mode) || S_ISCHR(mode)))
        return _EPERM;

    char path[MAX_PATH];
    int err = path_normalize(at, path_raw, path, N_SYMLINK_NOFOLLOW | N_PARENT_DIR_WRITE);
    if (err < 0)
        return err;
    struct mount *mount = find_mount_and_trim_path(path);
    err = _EPERM;
    if (mount->fs->mknod)
        err = mount->fs->mknod(mount, path, mode, dev);
    mount_release(mount);
    return err;
}

int generic_setattrat(struct fd *at, const char *path_raw, struct attr attr, bool follow_links)
{
    char path[MAX_PATH];
    int err =
        path_normalize(at, path_raw, path, follow_links ? N_SYMLINK_FOLLOW : N_SYMLINK_NOFOLLOW);
    if (err < 0)
        return err;
    struct mount *mount = find_mount_and_trim_path(path);
    err = _EPERM;
    if (mount->fs->setattr)
        err = mount->fs->setattr(mount, path, attr);
    mount_release(mount);
    return err;
}

int generic_utime(struct fd *at, const char *path_raw, struct timespec atime, struct timespec mtime,
                  bool follow_links)
{
    char path[MAX_PATH];
    int err =
        path_normalize(at, path_raw, path, follow_links ? N_SYMLINK_FOLLOW : N_SYMLINK_NOFOLLOW);
    if (err < 0)
        return err;
    struct mount *mount = find_mount_and_trim_path(path);
    err = _EPERM;
    if (mount->fs->utime)
        err = mount->fs->utime(mount, path, atime, mtime);
    mount_release(mount);
    return err;
}

ssize_t generic_readlinkat(struct fd *at, const char *path_raw, char *buf, size_t bufsize)
{
    char path[MAX_PATH];
    int err = path_normalize(at, path_raw, path, N_SYMLINK_NOFOLLOW);
    if (err < 0)
        return err;
    struct mount *mount = find_mount_and_trim_path(path);
    err = _EINVAL;
    if (mount->fs->readlink)
        err = (int)mount->fs->readlink(mount, path, buf, bufsize);
    mount_release(mount);
    return err;
}

int generic_mkdirat(struct fd *at, const char *path_raw, mode_t_ mode)
{
    char path[MAX_PATH];
    int err = path_normalize(at, path_raw, path, N_SYMLINK_FOLLOW | N_PARENT_DIR_WRITE);
    if (err < 0)
        return err;
    struct mount *mount = find_mount_and_trim_path(path);
    err = _EPERM;
    if (mount->fs->mkdir)
        err = mount->fs->mkdir(mount, path, mode);
    mount_release(mount);
    return err;
}

int generic_rmdirat(struct fd *at, const char *path_raw)
{
    char path[MAX_PATH];
    int err = path_normalize(at, path_raw, path, N_SYMLINK_FOLLOW | N_PARENT_DIR_WRITE);
    if (err < 0)
        return err;
    if (contains_mount_point(path))
        return _EBUSY;
    struct mount *mount = find_mount_and_trim_path(path);
    err = _EPERM;
    if (mount->fs->rmdir)
        err = mount->fs->rmdir(mount, path);
    mount_release(mount);
    return err;
}

int generic_seek(struct fd *fd, off_t_ off, int whence, size_t size)
{
    off_t_ new_off = fd->offset;
    if (whence == LSEEK_SET) {
        fd->offset = off;
    } else if (whence == LSEEK_CUR) {
        if (__builtin_add_overflow(new_off, off, &new_off) || new_off < 0)
            return _EINVAL;
        fd->offset = new_off;
    } else if (whence == LSEEK_END) {
        new_off = size + off;
        if (new_off < 0)
            return _EINVAL;
        fd->offset = new_off;
    } else {
        return _EINVAL;
    }
    return 0;
}
