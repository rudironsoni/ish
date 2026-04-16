#ifndef FS_STAT_H
#define FS_STAT_H

#import <IXLandLinuxRuntime/util/misc.h>

struct statbuf {
    uint64_t dev;
    uint64_t inode;
    uint32_t mode;
    uint32_t nlink;
    uint32_t uid;
    uint32_t gid;
    uint64_t rdev;
    uint64_t size;
    uint32_t blksize;
    uint64_t blocks;
    uint32_t atime;
    uint32_t atime_nsec;
    uint32_t mtime;
    uint32_t mtime_nsec;
    uint32_t ctime;
    uint32_t ctime_nsec;
};

struct oldstat {
    uint16_t dev;
    uint16_t ino;
    uint16_t mode;
    uint16_t nlink;
    uint16_t uid;
    uint16_t gid;
    uint16_t rdev;
    uint64_t size;
    uint64_t atime;
    uint64_t mtime;
    uint64_t ctime;
};

struct newstat {
    uint32_t dev;
    uint32_t ino;
    uint16_t mode;
    uint16_t nlink;
    uint16_t uid;
    uint16_t gid;
    uint32_t rdev;
    uint32_t size;
    uint32_t blksize;
    uint32_t blocks;
    uint32_t atime;
    uint32_t atime_nsec;
    uint32_t mtime;
    uint32_t mtime_nsec;
    uint32_t ctime;
    uint32_t ctime_nsec;
    char pad[8];
};

struct newstat64 {
    uint64_t dev;
    uint32_t _pad1;
    uint32_t fucked_ino;
    uint32_t mode;
    uint32_t nlink;
    uint32_t uid;
    uint32_t gid;
    uint64_t rdev;
    uint32_t _pad2;
    uint64_t size;
    uint32_t blksize;
    uint64_t blocks;
    uint32_t atime;
    uint32_t atime_nsec;
    uint32_t mtime;
    uint32_t mtime_nsec;
    uint32_t ctime;
    uint32_t ctime_nsec;
    uint64_t ino;
} __attribute__((packed));

struct statfsbuf {
    long type;
    long bsize;
    uint64_t blocks;
    uint64_t bfree;
    uint64_t bavail;
    uint64_t files;
    uint64_t ffree;
    uint64_t fsid;
    long namelen;
    long frsize;
    long flags;
    long spare[4];
};

struct statfs_ {
    uint64_t type;
    uint64_t bsize;
    uint64_t blocks;
    uint64_t bfree;
    uint64_t bavail;
    uint64_t files;
    uint64_t ffree;
    uint64_t fsid;
    uint64_t namelen;
    uint64_t frsize;
    uint64_t flags;
    uint64_t spare[4];
} __attribute__((packed));

struct statfs64_ {
    uint64_t type;
    uint64_t bsize;
    uint64_t blocks;
    uint64_t bfree;
    uint64_t bavail;
    uint64_t files;
    uint64_t ffree;
    uint64_t fsid;
    uint64_t namelen;
    uint64_t frsize;
    uint64_t flags;
    uint64_t pad[4];
} __attribute__((packed));

struct statx_timestamp_ {
    int64_t sec;
    uint32_t nsec;
    uint32_t _pad;
};

struct statx_ {
    uint32_t mask;
    uint32_t blksize;
    uint64_t attributes;
    uint32_t nlink;
    uint32_t uid;
    uint32_t gid;
    uint16_t mode;
    uint16_t _pad1;
    uint64_t ino;
    uint64_t size;
    uint64_t blocks;
    uint64_t attributes_mask;
    struct statx_timestamp_ atime;
    struct statx_timestamp_ btime;
    struct statx_timestamp_ ctime;
    struct statx_timestamp_ mtime;
    uint32_t rdev_major;
    uint32_t rdev_minor;
    uint32_t dev_major;
    uint32_t dev_minor;
    uint64_t mnt_id;
    uint32_t dio_mem_align;
    uint32_t dio_offset_align;
    uint32_t _pad2[24];
} __attribute__((packed));

#define STATX_BASIC_STATS_ 0x7ff

#endif
