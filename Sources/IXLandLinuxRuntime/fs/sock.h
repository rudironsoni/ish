#ifndef SYS_SOCK_H
#define SYS_SOCK_H

#import <IXLandLinuxRuntime/fs/fd.h>
#import <IXLandLinuxRuntime/kernel/errno.h>
#import <IXLandLinuxRuntime/util/debug.h>
#import <IXLandLinuxRuntime/util/misc.h>

int64_t sys_socketcall(uint32_t call_num, addr_t args_addr);

int64_t sys_socket(uint32_t domain, uint32_t type, uint32_t protocol);
int64_t sys_bind(fd_t sock_fd, addr_t sockaddr_addr, uint32_t sockaddr_len);
int64_t sys_connect(fd_t sock_fd, addr_t sockaddr_addr, uint32_t sockaddr_len);
int64_t sys_listen(fd_t sock_fd, int64_t backlog);
int64_t sys_accept(fd_t sock_fd, addr_t sockaddr_addr, addr_t sockaddr_len_addr);
int64_t sys_getsockname(fd_t sock_fd, addr_t sockaddr_addr, addr_t sockaddr_len_addr);
int64_t sys_getpeername(fd_t sock_fd, addr_t sockaddr_addr, addr_t sockaddr_len_addr);
int64_t sys_socketpair(uint32_t domain, uint32_t type, uint32_t protocol, addr_t sockets_addr);
int64_t sys_sendto(fd_t sock_fd, addr_t buffer_addr, uint32_t len, uint32_t flags,
                   addr_t sockaddr_addr, uint32_t sockaddr_len);
int64_t sys_recvfrom(fd_t sock_fd, addr_t buffer_addr, uint32_t len, uint32_t flags,
                     addr_t sockaddr_addr, addr_t sockaddr_len_addr);
int64_t sys_shutdown(fd_t sock_fd, int32_t how);
int64_t sys_setsockopt(fd_t sock_fd, int32_t level, int32_t option, addr_t value_addr,
                       int32_t value_len);
int64_t sys_getsockopt(fd_t sock_fd, int32_t level, int32_t option, addr_t value_addr,
                       int32_t len_addr);
int64_t sys_sendmsg(fd_t sock_fd, addr_t msghdr_addr, int64_t flags);
int64_t sys_recvmsg(fd_t sock_fd, addr_t msghdr_addr, int64_t flags);
int64_t sys_sendmmsg(fd_t sock_fd, addr_t msgvec_addr, uint64_t msgvec_len, int64_t flags);
int64_t sys_recvmmsg(fd_t sock_fd, addr_t msgvec_addr, uint64_t msgvec_len, int64_t flags,
                     addr_t timeout_addr);
int64_t sys_accept4(fd_t sock_fd, addr_t sockaddr_addr, addr_t sockaddr_len_addr, int64_t flags);

#define SOCKADDR_DATA_MAX 108

struct sockaddr_ {
    uint16_t family;
    char data[14];
};
struct sockaddr_max_ {
    uint16_t family;
    char data[SOCKADDR_DATA_MAX];
};

size_t sockaddr_size(void *p);
struct sockaddr *sockaddr_to_real(void *p);

struct msghdr_ {
    addr_t msg_name;
    uint64_t msg_namelen;
    addr_t msg_iov;
    uint64_t msg_iovlen;
    addr_t msg_control;
    uint64_t msg_controllen;
    int64_t msg_flags;
};

struct cmsghdr_ {
    uint32_t len;
    int64_t level;
    int64_t type;
    uint8_t data[];
};
#define SCM_RIGHTS_      1
#define CMSG_LEN_(cmsg)  (((cmsg)->len + sizeof(uint32_t) - 1) & ~(uint32_t)(sizeof(uint32_t) - 1))
#define CMSG_NEXT_(cmsg) ((uint8_t *)(cmsg) + CMSG_LEN_(cmsg))
#define CMSG_NXTHDR_(cmsg, mhdr_end)                                                               \
    ((cmsg)->len < sizeof(struct cmsghdr_) ||                                                      \
             CMSG_LEN_(cmsg) + sizeof(struct cmsghdr_) >= (size_t)(mhdr_end - (uint8_t *)(cmsg))   \
         ? NULL                                                                                    \
         : (struct cmsghdr_ *)CMSG_NEXT_(cmsg))

struct scm {
    struct list queue;
    unsigned num_fds;
    struct fd *fds[];
};

#define PF_LOCAL_ 1
#define PF_INET_  2
#define PF_INET6_ 10
#define AF_LOCAL_ PF_LOCAL_
#define AF_INET_  PF_INET_
#define AF_INET6_ PF_INET6_

#define SOCK_STREAM_   1
#define SOCK_DGRAM_    2
#define SOCK_RAW_      3
#define SOCK_NONBLOCK_ 0x800
#define SOCK_CLOEXEC_  0x80000

#define MSG_OOB_      0x1
#define MSG_PEEK_     0x2
#define MSG_CTRUNC_   0x8
#define MSG_TRUNC_    0x20
#define MSG_DONTWAIT_ 0x40
#define MSG_EOR_      0x80
#define MSG_WAITALL_  0x100

#define SOL_SOCKET_ 1

#define SO_REUSEADDR_      2
#define SO_TYPE_           3
#define SO_ERROR_          4
#define SO_BROADCAST_      6
#define SO_SNDBUF_         7
#define SO_RCVBUF_         8
#define SO_KEEPALIVE_      9
#define SO_LINGER_         13
#define SO_PEERCRED_       17
#define SO_TIMESTAMP_      29
#define SO_PROTOCOL_       38
#define SO_DOMAIN_         39
#define SO_RCVTIMEO_       66
#define SO_SNDTIMEO_       67
#define IP_TOS_            1
#define IP_TTL_            2
#define IP_HDRINCL_        3
#define IP_RETOPTS_        7
#define IP_MTU_DISCOVER_   10
#define IP_RECVTTL_        12
#define IP_RECVTOS_        13
#define TCP_NODELAY_       1
#define TCP_DEFER_ACCEPT_  9
#define TCP_INFO_          11
#define TCP_CONGESTION_    13
#define IPV6_UNICAST_HOPS_ 16
#define IPV6_V6ONLY_       26
#define IPV6_TCLASS_       67
#define ICMP6_FILTER_      1

int sock_family_to_real(int fake);
int sock_family_from_real(int fake);
int sock_type_to_real(int type, int protocol);
int sock_flags_to_real(int fake);
int sock_flags_from_real(int real);
int sock_opt_to_real(int fake, int level);
int sock_level_to_real(int fake);

extern const char *sock_tmp_prefix;

struct tcp_info_ {
    uint8_t state;
    uint8_t ca_state;
    uint8_t retransmits;
    uint8_t probes;
    uint8_t backoff;
    uint8_t options;
    uint8_t snd_wscale : 4, rcv_wscale : 4;

    uint32_t rto;
    uint32_t ato;
    uint32_t snd_mss;
    uint32_t rcv_mss;

    uint32_t unacked;
    uint32_t sacked;
    uint32_t lost;
    uint32_t retrans;
    uint32_t fackets;

    uint32_t last_data_sent;
    uint32_t last_ack_sent;
    uint32_t last_data_recv;
    uint32_t last_ack_recv;

    uint32_t pmtu;
    uint32_t rcv_ssthresh;
    uint32_t rtt;
    uint32_t rttvar;
    uint32_t snd_ssthresh;
    uint32_t snd_cwnd;
    uint32_t advmss;
    uint32_t reordering;

    uint32_t rcv_rtt;
    uint32_t rcv_space;

    uint32_t total_retrans;
};

#endif
