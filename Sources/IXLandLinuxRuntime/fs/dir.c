#import <IXLandLinuxRuntime/fs/fd.h>
#import <IXLandLinuxRuntime/kernel/calls.h>
#import <IXLandLinuxRuntime/kernel/errno.h>
#import <IXLandLinuxRuntime/kernel/fs.h>
#include <string.h>
#include <sys/stat.h>

static unsigned long fd_telldir(struct fd *fd)
{
    unsigned long off = fd->offset;
    if (fd->ops->telldir)
        off = fd->ops->telldir(fd);
    return off;
}

static void fd_seekdir(struct fd *fd, unsigned long off)
{
    fd->offset = off;
    if (fd->ops->seekdir)
        fd->ops->seekdir(fd, off);
}

struct linux_dirent_ {
    uint32_t inode;
    uint32_t offset;
    uint16_t reclen;
    char name[];
} __attribute__((packed));

struct linux_dirent64_ {
    uint64_t inode;
    uint64_t offset;
    uint16_t reclen;
    uint8_t type;
    char name[];
} __attribute__((packed));

static size_t dirent_align(size_t reclen, size_t alignment)
{
    return (reclen + alignment - 1) & ~(alignment - 1);
}

size_t fill_dirent_32(void *dirent_data, ino_t inode, off_t_ offset, const char *name, int type)
{
    struct linux_dirent_ *dirent = dirent_data;
    uint16_t reclen =
        (uint16_t)dirent_align(offsetof(struct linux_dirent_, name) + strlen(name) + 2,
                               4); // name, null, type
    memset(dirent_data, 0, reclen);
    dirent->inode = (uint32_t)inode;
    dirent->offset = (uint32_t)offset;
    dirent->reclen = reclen;
    strcpy(dirent->name, name);
    *((char *)dirent + dirent->reclen - 1) = type;
    return dirent->reclen;
}

size_t fill_dirent_64(void *dirent_data, ino_t inode, off_t_ offset, const char *name, int type)
{
    struct linux_dirent64_ *dirent = dirent_data;
    uint16_t reclen = (uint16_t)dirent_align(
        offsetof(struct linux_dirent64_, name) + strlen(name) + 1, 8); // name, null terminator
    memset(dirent_data, 0, reclen);
    dirent->inode = inode;
    dirent->offset = offset;
    dirent->reclen = reclen;
    dirent->type = type;
    strcpy(dirent->name, name);
    return dirent->reclen;
}

int64_t sys_getdents_common(fd_t f, addr_t dirents, uint64_t count,
                            size_t (*fill_dirent)(void *, ino_t, off_t_, const char *, int))
{
    STRACE("getdents(%d, %#x, %#x)", f, dirents, count);
    struct fd *fd = f_get(f);
    if (fd == NULL) {
        trace_record_event(TRACE_ORIGIN_KERNEL, "getdents.fail.badfd");
        return _EBADF;
    }
    if (!S_ISDIR(fd->type) || fd->ops->readdir == NULL) {
        trace_record_event(TRACE_ORIGIN_KERNEL, "getdents.fail.notdir");
        return _ENOTDIR;
    }
    char fd_path[MAX_PATH];
    bool is_root_dir = generic_getpath(fd, fd_path) == 0 && strcmp(fd_path, "/") == 0;
    if (is_root_dir)
        trace_record_event(TRACE_ORIGIN_KERNEL, "getdents.root.enter");

    uint32_t orig_count = (uint32_t)count;

    long ptr;
    int err;
    int printed = 0;
    bool rewind_to_last_entry = false;
    while (true) {
        ptr = fd_telldir(fd);
        struct dir_entry entry;
        trace_record_event(TRACE_ORIGIN_KERNEL, "getdents.readdir.enter");
        err = fd->ops->readdir(fd, &entry);
        if (err < 0) {
            if (is_root_dir)
                trace_record_event(TRACE_ORIGIN_KERNEL, "getdents.root.fail");
            if (err == _ENOMEM)
                trace_record_event(TRACE_ORIGIN_KERNEL, "getdents.fail.enomem");
            trace_record_event(TRACE_ORIGIN_KERNEL, "getdents.fail.readdir");
            return err;
        }
        if (err == 0)
            break;
        trace_record_event(TRACE_ORIGIN_KERNEL, "getdents.readdir.ok");

        size_t max_reclen =
            dirent_align(sizeof(struct linux_dirent64_) + strlen(entry.name) + 4, 8);
        char dirent_data[max_reclen];
        ino_t inode = entry.inode;
        off_t_ offset = fd_telldir(fd);
        const char *name = entry.name;
        int type = 0;
        size_t reclen = fill_dirent(dirent_data, inode, offset, name, type);
        if (printed < 20) {
            STRACE(" {inode=%d, offset=%d, name=%s, type=%d, reclen=%d}", inode, offset, name, type,
                   reclen);
            printed++;
        }

        if (reclen > count) {
            rewind_to_last_entry = true;
            break;
        }
        if (user_write(dirents, dirent_data, reclen)) {
            trace_record_event(TRACE_ORIGIN_KERNEL, "getdents.fail.user_write");
            return _EFAULT;
        }
        dirents += reclen;
        count -= reclen;
    }

    if (rewind_to_last_entry)
        fd_seekdir(fd, ptr);
    if (is_root_dir)
        trace_record_event(TRACE_ORIGIN_KERNEL, "getdents.root.ok");
    trace_record_event(TRACE_ORIGIN_KERNEL, "getdents.ok");
    return orig_count - count;
}

int64_t sys_getdents(fd_t f, addr_t dirents, uint64_t count)
{
    return sys_getdents_common(f, dirents, count, fill_dirent_32);
}

int64_t sys_getdents64(fd_t f, addr_t dirents, uint64_t count)
{
    return sys_getdents_common(f, dirents, count, fill_dirent_64);
}
