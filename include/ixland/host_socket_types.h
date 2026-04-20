#ifndef IXLAND_HOST_SOCKET_TYPES_H
#define IXLAND_HOST_SOCKET_TYPES_H

#include <linux/types.h>
#include <linux/uio.h>
#include <stddef.h>
#include <stdint.h>

typedef uint32_t __host_socklen_t;

struct host_msghdr {
    void *msg_name;
    __host_socklen_t msg_namelen;
    struct iovec *msg_iov;
    int msg_iovlen;
    int __pad1;
    void *msg_control;
    __host_socklen_t msg_controllen;
    int __pad2;
    int msg_flags;
};

struct host_cmsghdr {
    __host_socklen_t cmsg_len;
    int __pad1;
    int cmsg_level;
    int cmsg_type;
};

#define __HOST_CMSG_LEN(cmsg)  (((cmsg)->cmsg_len + sizeof(long) - 1) & ~(long)(sizeof(long) - 1))
#define __HOST_CMSG_NEXT(cmsg) ((unsigned char *)(cmsg) + __HOST_CMSG_LEN(cmsg))
#define __HOST_MHDR_END(mhdr)  ((unsigned char *)(mhdr)->msg_control + (mhdr)->msg_controllen)

#define HOST_CMSG_DATA(cmsg) ((unsigned char *)(((struct host_cmsghdr *)(cmsg)) + 1))
#define HOST_CMSG_NXTHDR(mhdr, cmsg)                                                               \
    ((cmsg)->cmsg_len < sizeof(struct host_cmsghdr) ||                                             \
             __HOST_CMSG_LEN(cmsg) + sizeof(struct host_cmsghdr) >=                                \
                 __HOST_MHDR_END(mhdr) - (unsigned char *)(cmsg)                                   \
         ? (struct host_cmsghdr *)0                                                                \
         : (struct host_cmsghdr *)__HOST_CMSG_NEXT(cmsg))
#define HOST_CMSG_FIRSTHDR(mhdr)                                                                   \
    ((size_t)(mhdr)->msg_controllen >= sizeof(struct host_cmsghdr)                                 \
         ? (struct host_cmsghdr *)(mhdr)->msg_control                                              \
         : (struct host_cmsghdr *)0)
#define HOST_CMSG_ALIGN(len) (((len) + sizeof(size_t) - 1) & (size_t)~(sizeof(size_t) - 1))
#define HOST_CMSG_SPACE(len) (HOST_CMSG_ALIGN(len) + HOST_CMSG_ALIGN(sizeof(struct host_cmsghdr)))
#define HOST_CMSG_LEN(len)   (HOST_CMSG_ALIGN(sizeof(struct host_cmsghdr)) + (len))

#endif
