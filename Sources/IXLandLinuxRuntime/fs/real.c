#import <IXLandLinuxRuntime/fs/dev.h>
#import <IXLandLinuxRuntime/fs/real.h>
#import <IXLandLinuxRuntime/fs/tty.h>
#import <IXLandLinuxRuntime/kernel/calls.h>
#import <IXLandLinuxRuntime/kernel/errno.h>
#import <IXLandLinuxRuntime/kernel/fs.h>
#import <IXLandLinuxRuntime/util/debug.h>
#import <IXLandLinuxRuntime/util/fchdir.h>
#include "internal/ios/fs/errno_host.h"
#include "internal/ios/fs/open_flags.h"
#include "internal/ios/fs/path_host.h"
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/xattr.h>
#include <termios.h>
#include <unistd.h>

static int realfs_host_whence_from_linux(int whence)
{
    switch (whence) {
    case LSEEK_SET:
        return SEEK_SET;
    case LSEEK_CUR:
        return SEEK_CUR;
    case LSEEK_END:
        return SEEK_END;
    default:
        return -1;
    }
}

static int realfs_host_lock_operation_from_linux(int operation)
{
    int host_operation = 0;
    if (operation & LOCK_SH_)
        host_operation |= LOCK_SH;
    if (operation & LOCK_EX_)
        host_operation |= LOCK_EX;
    if (operation & LOCK_UN_)
        host_operation |= LOCK_UN;
    if (operation & LOCK_NB_)
        host_operation |= LOCK_NB;
    return host_operation;
}

static void realfs_copy_statbuf_from_host(struct statbuf *linux_stat, const struct stat *host_stat)
{
    long host_blksize = host_stat->st_blksize;
    if (host_blksize <= 0 || host_blksize > 65536)
        host_blksize = 4096;

    linux_stat->dev = dev_fake_from_real(host_stat->st_dev);
    linux_stat->inode = host_stat->st_ino;
    linux_stat->mode = host_stat->st_mode;
    linux_stat->nlink = host_stat->st_nlink;
    linux_stat->uid = host_stat->st_uid;
    linux_stat->gid = host_stat->st_gid;
    linux_stat->rdev = dev_fake_from_real(host_stat->st_rdev);
    linux_stat->size = host_stat->st_size;
    linux_stat->blksize = (uint32_t)host_blksize;
    linux_stat->blocks = host_stat->st_blocks;
    linux_stat->atime = (uint32_t) host_stat->st_atime;
    linux_stat->mtime = (uint32_t) host_stat->st_mtime;
    linux_stat->ctime = (uint32_t) host_stat->st_ctime;
#define TIMESPEC(x) st_##x##timespec
    linux_stat->atime_nsec = (uint32_t) host_stat->TIMESPEC(a).tv_nsec;
    linux_stat->mtime_nsec = (uint32_t) host_stat->TIMESPEC(m).tv_nsec;
    linux_stat->ctime_nsec = (uint32_t) host_stat->TIMESPEC(c).tv_nsec;
#undef TIMESPEC
}

static int realfs_stat_from_host_fd(int fd_no, struct statbuf *linux_stat)
{
    struct stat host_stat;
    if (fstat(fd_no, &host_stat) < 0)
        return errno_host_map();
    realfs_copy_statbuf_from_host(linux_stat, &host_stat);
    return 0;
}

static int realfs_stat_from_mount_path(struct mount *mount, const char *path, struct statbuf *linux_stat)
{
    struct stat host_stat;
    if (fstatat(mount->root_fd, path_host_relative(path), &host_stat, AT_SYMLINK_NOFOLLOW) < 0)
        return errno_host_map();
    realfs_copy_statbuf_from_host(linux_stat, &host_stat);
    return 0;
}

static int realfs_mount_open(struct mount *mount, const char *path, int flags, mode_t mode)
{
    return openat(mount->root_fd, path_host_relative(path), flags, mode);
}

struct fd *realfs_open(struct mount *mount, const char *path, int flags, int mode)
{
    int real_flags = open_flags_host_from_linux(flags);
    int fd_no = realfs_mount_open(mount, path, real_flags, mode);
    if (fd_no < 0)
        return ERR_PTR(errno_host_map());
    struct fd *fd = fd_create(&realfs_fdops);
    if (fd == NULL) {
        close(fd_no);
        trace_record_event(TRACE_ORIGIN_KERNEL, "realfs.open.fd_create_fail.enomem");
        return ERR_PTR(_ENOMEM);
    }
    fd->real_fd = fd_no;
    fd->dir = NULL;
    return fd;
}

int realfs_close(struct fd *fd)
{
    if (fd->dir != NULL)
        closedir(fd->dir);
    int err = close(fd->real_fd);
    if (err < 0)
        return errno_host_map();
    return 0;
}

int realfs_stat(struct mount *mount, const char *path, struct statbuf *fake_stat)
{
    return realfs_stat_from_mount_path(mount, path, fake_stat);
}

int realfs_fstat(struct fd *fd, struct statbuf *fake_stat)
{
    return realfs_stat_from_host_fd(fd->real_fd, fake_stat);
}

ssize_t realfs_read(struct fd *fd, void *buf, size_t bufsize)
{
    return errno_host_or_size(read(fd->real_fd, buf, bufsize));
}

ssize_t realfs_write(struct fd *fd, const void *buf, size_t bufsize)
{
    return errno_host_or_size(write(fd->real_fd, buf, bufsize));
}

ssize_t realfs_pread(struct fd *fd, void *buf, size_t bufsize, off_t off)
{
    return errno_host_or_size(pread(fd->real_fd, buf, bufsize, off));
}

ssize_t realfs_pwrite(struct fd *fd, const void *buf, size_t bufsize, off_t off)
{
    return errno_host_or_size(pwrite(fd->real_fd, buf, bufsize, off));
}

static void realfs_ensure_dir_stream(struct fd *fd)
{
    if (fd->dir == NULL) {
        int dirfd = dup(fd->real_fd);
        fd->dir = fdopendir(dirfd);
        // this should never get called on a non-directory
        assert(fd->dir != NULL);
    }
}

int realfs_readdir(struct fd *fd, struct dir_entry *entry)
{
    realfs_ensure_dir_stream(fd);
    errno = 0;
    struct dirent *dirent = readdir(fd->dir);
    if (dirent == NULL) {
        if (errno != 0)
            return errno_host_map();
        else
            return 0;
    }
    entry->inode = dirent->d_ino;
    strcpy(entry->name, dirent->d_name);
    return 1;
}

unsigned long realfs_telldir(struct fd *fd)
{
    realfs_ensure_dir_stream(fd);
    return telldir(fd->dir);
}

void realfs_seekdir(struct fd *fd, unsigned long ptr)
{
    realfs_ensure_dir_stream(fd);
    seekdir(fd->dir, ptr);
}

off_t realfs_lseek(struct fd *fd, off_t offset, int whence)
{
    if (fd->dir != NULL && whence == LSEEK_SET) {
        realfs_seekdir(fd, offset);
        return offset;
    }

    int host_whence = realfs_host_whence_from_linux(whence);
    if (host_whence < 0)
        return _EINVAL;
    off_t res = lseek(fd->real_fd, offset, host_whence);
    if (res < 0)
        return errno_host_map();
    return res;
}

int realfs_poll(struct fd *fd)
{
    struct pollfd p = { .fd = fd->real_fd, .events = POLLPRI };
    // prevent POLLNVAL
    int flags = fcntl(fd->real_fd, F_GETFL, 0);
    if ((flags & O_ACCMODE) != O_WRONLY)
        p.events |= POLLIN;
    if ((flags & O_ACCMODE) != O_RDONLY)
        p.events |= POLLOUT;
    if (poll(&p, 1, 0) <= 0)
        return 0;

#if defined(__APPLE__)
    // this is the "WTF is apple smoking" section

    // https://github.com/apple/darwin-xnu/blob/a449c6a3b8014d9406c2ddbdc81795da24aa7443/bsd/kern/sys_generic.c#L1856
    if (p.revents & POLLHUP)
        p.revents |= POLLOUT;
    // apparently you can sometimes get POLLPRI on a pipe??? please ignore how much of a mess this
    // condition is
    if (is_adhoc_fd(fd) && S_ISFIFO(fd->stat.mode))
        p.revents &= ~POLLPRI;

    if (p.revents & POLLNVAL) {
        printk("pollnval %d flags %d events %d revents %d\n", fd->real_fd, flags, p.events,
               p.revents);
        // Seriously, fuck Darwin. I just want to poll on POLLIN|POLLOUT|POLLPRI.
        // But if there's almost any kind of error, you just get POLLNVAL back,
        // and no information about the bits that are in fact set. So ask for each
        // separately and ignore a POLLNVAL.
        // This is no longer atomic but I don't really know what to do about that.
        int events = 0;
        static const int pollbits[] = { POLLIN, POLLOUT, POLLPRI };
        for (unsigned i = 0; i < sizeof(pollbits) / sizeof(pollbits[0]); i++) {
            p.events = pollbits[i];
            if (poll(&p, 1, 0) > 0 && !(p.revents & POLLNVAL))
                events |= p.revents;
        }
        assert(!(events & POLLNVAL));
        return events;
    }
#endif

    assert(!(p.revents & POLLNVAL));
    return p.revents;
}

int realfs_mmap(struct fd *fd, struct mem *mem, page_t start, pages_t pages, off_t offset, int prot,
                int flags)
{
    int mmap_flags = 0;
    if (flags & MMAP_PRIVATE)
        mmap_flags |= MAP_PRIVATE;
    if (flags & MMAP_SHARED)
        mmap_flags |= MAP_SHARED;
    int mmap_prot = PROT_READ;
    if (prot & P_WRITE)
        mmap_prot |= PROT_WRITE;

    off_t real_offset = (offset / real_page_size) * real_page_size;
    off_t correction = offset - real_offset;
    char *memory = mmap(NULL, (pages * PAGE_SIZE) + correction, mmap_prot, mmap_flags, fd->real_fd,
                        real_offset);
    return pt_map(mem, start, pages, memory, correction, prot);
}

ssize_t realfs_readlink(struct mount *mount, const char *path, char *buf, size_t bufsize)
{
    return errno_host_or_size(readlinkat(mount->root_fd, path_host_relative(path), buf, bufsize));
}

int realfs_getpath(struct fd *fd, char *buf)
{
    int err = path_host_get(fd->real_fd, buf);
    if (err < 0)
        return errno_host_map();
    if (strcmp(fd->mount->source, "/") != 0 || strcmp(buf, "/") == 0) {
        size_t source_len = strlen(fd->mount->source);
        memmove(buf, buf + source_len, MAX_PATH - source_len);
    }
    return 0;
}

int realfs_link(struct mount *mount, const char *src, const char *dst)
{
    return errno_host_or_value(
        linkat(mount->root_fd, path_host_relative(src), mount->root_fd, path_host_relative(dst), 0));
}

int realfs_unlink(struct mount *mount, const char *path)
{
    return errno_host_or_value(unlinkat(mount->root_fd, path_host_relative(path), 0));
}

int realfs_rmdir(struct mount *mount, const char *path)
{
    return errno_host_or_value(unlinkat(mount->root_fd, path_host_relative(path), AT_REMOVEDIR));
}

int realfs_rename(struct mount *mount, const char *src, const char *dst)
{
    return errno_host_or_value(
        renameat(mount->root_fd, path_host_relative(src), mount->root_fd, path_host_relative(dst)));
}

int realfs_symlink(struct mount *mount, const char *target, const char *link)
{
    int err = symlinkat(target, mount->root_fd, link);
    if (err < 0)
        return errno_host_map();
    return err;
}

int realfs_mknod(struct mount *mount, const char *path, mode_t_ mode, dev_t_ UNUSED(dev))
{
    int err;
    if (S_ISFIFO(mode)) {
        lock_fchdir(mount->root_fd);
        err = mkfifo(path_host_relative(path), mode & ~S_IFMT);
        unlock_fchdir();
    } else if (S_ISREG(mode)) {
        err = realfs_mount_open(mount, path, O_CREAT | O_EXCL | O_RDONLY, mode & ~S_IFMT);
        if (err >= 0)
            err = close(err);
    } else {
        return _EPERM;
    }
    if (err < 0)
        return errno_host_map();
    return err;
}

int realfs_truncate(struct mount *mount, const char *path, off_t_ size)
{
    int fd = realfs_mount_open(mount, path, O_RDWR, 0);
    if (fd < 0)
        return errno_host_map();
    int err = 0;
    if (ftruncate(fd, size) < 0)
        err = errno_host_map();
    close(fd);
    return err;
}

int realfs_setattr(struct mount *mount, const char *path, struct attr attr)
{
    path = path_host_relative(path);
    int root = mount->root_fd;
    int err;
    switch (attr.type) {
    case attr_uid:
        err = fchownat(root, path, attr.uid, -1, 0);
        break;
    case attr_gid:
        err = fchownat(root, path, attr.gid, -1, 0);
        break;
    case attr_mode:
        err = fchmodat(root, path, attr.mode, 0);
        break;
    case attr_size:
        return realfs_truncate(mount, path, attr.size);
    default:
        TODO("other attrs");
    }
    if (err < 0)
        return errno_host_map();
    return err;
}

int realfs_fsetattr(struct fd *fd, struct attr attr)
{
    int real_fd = fd->real_fd;
    int err;
    switch (attr.type) {
    case attr_uid:
        err = fchown(real_fd, attr.uid, -1);
        break;
    case attr_gid:
        err = fchown(real_fd, attr.gid, -1);
        break;
    case attr_mode:
        err = fchmod(real_fd, attr.mode);
        break;
    case attr_size:
        err = ftruncate(real_fd, attr.size);
        break;
    default:
        abort();
    }
    if (err < 0)
        return errno_host_map();
    return err;
}

int realfs_utime(struct mount *mount, const char *path, struct timespec atime,
                 struct timespec mtime)
{
    struct timespec times[2] = { atime, mtime };
    return errno_host_or_value(utimensat(mount->root_fd, path_host_relative(path), times, 0));
}

int realfs_mkdir(struct mount *mount, const char *path, mode_t_ mode)
{
    return errno_host_or_value(mkdirat(mount->root_fd, path_host_relative(path), mode));
}

int realfs_flock(struct fd *fd, int operation)
{
    return errno_host_or_value(flock(fd->real_fd, realfs_host_lock_operation_from_linux(operation)));
}

int realfs_statfs(struct mount *mount, struct statfsbuf *stat)
{
    struct statvfs vfs = {};
    fstatvfs(mount->root_fd, &vfs);
    stat->bsize = vfs.f_bsize;
    stat->blocks = vfs.f_blocks;
    stat->bfree = vfs.f_bfree;
    stat->bavail = vfs.f_bavail;
    stat->files = vfs.f_files;
    stat->ffree = vfs.f_ffree;
    stat->namelen = vfs.f_namemax;
    stat->frsize = vfs.f_frsize;
    return 0;
}

int realfs_mount(struct mount *mount)
{
    char *source_realpath = realpath(mount->source, NULL);
    if (source_realpath == NULL)
        return errno_host_map();
    free((void *)mount->source);
    mount->source = source_realpath;

    mount->root_fd = open(mount->source, O_DIRECTORY);
    if (mount->root_fd < 0)
        return errno_host_map();
    return 0;
}

int realfs_fsync(struct fd *fd)
{
    int err = fsync(fd->real_fd);
    if (err < 0)
        return errno_host_map();
    return 0;
}

int realfs_getflags(struct fd *fd)
{
    int flags = fcntl(fd->real_fd, F_GETFL);
    if (flags < 0)
        return errno_host_map();
    return open_flags_linux_from_host(flags);
}

int realfs_setflags(struct fd *fd, uint32_t flags)
{
    int ret = fcntl(fd->real_fd, F_SETFL, open_flags_host_from_linux(flags));
    if (ret < 0)
        return errno_host_map();
    return 0;
}

ssize_t realfs_ioctl_size(int cmd)
{
    if (cmd == FIONREAD_)
        return sizeof(uint32_t);
    return -1;
}

int realfs_ioctl(struct fd *fd, int cmd, void *arg)
{
    int err;
    size_t nread;
    switch (cmd) {
    case FIONREAD_:
        err = ioctl(fd->real_fd, FIONREAD, &nread);
        if (err < 0)
            return errno_host_map();
        *(uint32_t *)arg = (uint32_t)nread;
        return 0;
    }
    return _ENOTTY;
}

const struct fs_ops realfs = {
    .name = "real",
    .magic = 0x7265616c,
    .mount = realfs_mount,
    .statfs = realfs_statfs,

    .open = realfs_open,
    .readlink = realfs_readlink,
    .link = realfs_link,
    .unlink = realfs_unlink,
    .rmdir = realfs_rmdir,
    .rename = realfs_rename,
    .symlink = realfs_symlink,
    .mknod = realfs_mknod,

    .close = realfs_close,
    .stat = realfs_stat,
    .fstat = realfs_fstat,
    .setattr = realfs_setattr,
    .fsetattr = realfs_fsetattr,
    .utime = realfs_utime,
    .getpath = realfs_getpath,
    .flock = realfs_flock,

    .mkdir = realfs_mkdir,
};

const struct fd_ops realfs_fdops = {
    .read = realfs_read,
    .write = realfs_write,
    .pread = realfs_pread,
    .pwrite = realfs_pwrite,
    .readdir = realfs_readdir,
    .telldir = realfs_telldir,
    .seekdir = realfs_seekdir,
    .lseek = realfs_lseek,
    .mmap = realfs_mmap,
    .poll = realfs_poll,
    .ioctl_size = realfs_ioctl_size,
    .ioctl = realfs_ioctl,
    .fsync = realfs_fsync,
    .close = realfs_close,
    .getflags = realfs_getflags,
    .setflags = realfs_setflags,
};
