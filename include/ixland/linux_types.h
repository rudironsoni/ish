#ifndef IXLAND_LINUX_TYPES_H
#define IXLAND_LINUX_TYPES_H

// Include Linux UAPI headers for proper kernel types
#include <linux/types.h>

// Map Linux kernel types to runtime underscore-suffixed types
// These match the Linux kernel ABI for aarch64
typedef __s32 pid_t_;
typedef __u32 uid_t_;
typedef __u32 gid_t_;
typedef __u32 mode_t_;
typedef __u64 addr_t;
typedef __s64 off_t_;
typedef __s64 clock_t_;
typedef __u64 dev_t_;
typedef __u64 ino_t_;
typedef __u32 nlink_t_;
typedef __s64 time_t_;
typedef __s64 ssize_t_;

#endif
