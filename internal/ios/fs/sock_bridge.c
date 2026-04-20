#include "sock_bridge.h"

#import <IXLandLinuxRuntime/fs/sock.h>
#import <IXLandLinuxRuntime/util/debug.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

int32_t socket_impl(int32_t domain, int32_t type, int32_t protocol)
{
    int sock = socket(domain, type, protocol);
    if (sock < 0)
        return -errno;
    return sock;
}

int32_t setsockopt_strip_impl(int32_t sockfd)
{
    int one = 1;
    int ret = setsockopt(sockfd, IPPROTO_IP, IP_STRIPHDR, &one, sizeof(one));
    if (ret < 0)
        return -errno;
    return 0;
}

int32_t bind_impl(int32_t sockfd, const void *addr, uint32_t addr_len)
{
    int ret = bind(sockfd, addr, addr_len);
    if (ret < 0)
        return -errno;
    return 0;
}

int32_t connect_impl(int32_t sockfd, const void *addr, uint32_t addr_len)
{
    int ret = connect(sockfd, addr, addr_len);
    if (ret < 0)
        return -errno;
    return 0;
}

int32_t listen_impl(int32_t sockfd, int32_t backlog)
{
    int ret = listen(sockfd, backlog);
    if (ret < 0)
        return -errno;
    return 0;
}

int32_t accept_impl(int32_t sockfd, void *addr, uint32_t *addr_len)
{
    socklen_t len = addr_len ? *addr_len : 0;
    int ret = accept(sockfd, addr, addr_len ? &len : NULL);
    if (ret < 0)
        return -errno;
    if (addr_len)
        *addr_len = (uint32_t)len;
    return ret;
}

int32_t shutdown_impl(int32_t sockfd, int32_t how)
{
    int ret = shutdown(sockfd, how);
    if (ret < 0)
        return -errno;
    return 0;
}

int32_t setsockopt_impl(int32_t sockfd, int32_t level, int32_t opt, const void *val, int32_t len)
{
    int ret = setsockopt(sockfd, level, opt, val, (socklen_t)len);
    if (ret < 0)
        return -errno;
    return 0;
}

int32_t getsockopt_impl(int32_t sockfd, int32_t level, int32_t opt, void *val, int32_t *len)
{
    socklen_t slen = (socklen_t)*len;
    int ret = getsockopt(sockfd, level, opt, val, &slen);
    if (ret < 0)
        return -errno;
    *len = (int32_t)slen;
    return 0;
}

struct sockaddr_result getsockname_impl(int32_t sockfd, void *addr, uint32_t addr_len)
{
    struct sockaddr_result r;
    socklen_t len = (socklen_t)addr_len;
    r.ret = getsockname(sockfd, addr, &len);
    if (r.ret < 0)
        r.ret = -errno;
    r.addr_len = (uint32_t)len;
    return r;
}

struct sockaddr_result getpeername_impl(int32_t sockfd, void *addr, uint32_t addr_len)
{
    struct sockaddr_result r;
    socklen_t len = (socklen_t)addr_len;
    r.ret = getpeername(sockfd, addr, &len);
    if (r.ret < 0)
        r.ret = -errno;
    r.addr_len = (uint32_t)len;
    return r;
}

int64_t sendto_impl(int32_t sockfd, const void *buf, uint32_t len, int32_t flags, const void *addr,
                    uint32_t addr_len)
{
    ssize_t ret = sendto(sockfd, buf, len, flags, addr, addr_len);
    if (ret < 0)
        return -errno;
    return (int64_t)ret;
}

int64_t recvfrom_impl(int32_t sockfd, void *buf, uint32_t len, int32_t flags, void *addr,
                      uint32_t *addr_len)
{
    socklen_t slen = addr_len ? (socklen_t)*addr_len : 0;
    ssize_t ret = recvfrom(sockfd, buf, len, flags, addr, addr_len ? &slen : NULL);
    if (ret < 0)
        return -errno;
    if (addr_len)
        *addr_len = (uint32_t)slen;
    return (int64_t)ret;
}

static void translate_msghdr_to_darwin(struct host_msghdr *src, struct msghdr *dst)
{
    dst->msg_name = src->msg_name;
    dst->msg_namelen = src->msg_namelen;
    dst->msg_iov = src->msg_iov;
    dst->msg_iovlen = (int)src->msg_iovlen;
    dst->msg_control = src->msg_control;
    dst->msg_controllen = (socklen_t)src->msg_controllen;
    dst->msg_flags = src->msg_flags;
}

static void translate_msghdr_from_darwin(struct msghdr *src, struct host_msghdr *dst)
{
    dst->msg_name = src->msg_name;
    dst->msg_namelen = src->msg_namelen;
    dst->msg_iov = src->msg_iov;
    dst->msg_iovlen = (size_t)src->msg_iovlen;
    dst->msg_control = src->msg_control;
    dst->msg_controllen = (size_t)src->msg_controllen;
    dst->msg_flags = src->msg_flags;
}

int64_t sendmsg_impl(int32_t sockfd, struct host_msghdr *msg, int32_t flags)
{
    struct msghdr darwin_msg;
    translate_msghdr_to_darwin(msg, &darwin_msg);
    ssize_t ret = sendmsg(sockfd, &darwin_msg, flags);
    if (ret < 0)
        return -errno;
    return (int64_t)ret;
}

int64_t recvmsg_impl(int32_t sockfd, struct host_msghdr *msg, int32_t flags)
{
    struct msghdr darwin_msg;
    translate_msghdr_to_darwin(msg, &darwin_msg);
    ssize_t ret = recvmsg(sockfd, &darwin_msg, flags);
    if (ret < 0)
        return -errno;
    translate_msghdr_from_darwin(&darwin_msg, msg);
    return (int64_t)ret;
}

int32_t socketpair_impl(int32_t domain, int32_t type, int32_t protocol, int32_t sv[2])
{
    int fds[2];
    int ret = socketpair(domain, type, protocol, fds);
    if (ret < 0)
        return -errno;
    sv[0] = fds[0];
    sv[1] = fds[1];
    return 0;
}

struct tcp_info_result get_tcp_info_impl(int32_t sockfd)
{
    struct tcp_info_result r = { 0 };
    struct tcp_connection_info conn_info;
    socklen_t conn_info_size = sizeof(conn_info);
    int ret = getsockopt(sockfd, IPPROTO_TCP, TCP_CONNECTION_INFO, &conn_info, &conn_info_size);
    if (ret < 0) {
        r.ret = -errno;
        return r;
    }
    static const uint8_t tcp_state_table[] = {
        7, 10, 2, 3, 1, 8, 4, 11, 9, 5, 6,
    };
    r.ret = 0;
    r.state =
        conn_info.tcpi_state < sizeof(tcp_state_table) ? tcp_state_table[conn_info.tcpi_state] : 0;
    r.options = (uint8_t)conn_info.tcpi_options;
    r.snd_wscale = (uint8_t)conn_info.tcpi_snd_wscale;
    r.rcv_wscale = (uint8_t)conn_info.tcpi_rcv_wscale;
    r.rto = (uint32_t)(conn_info.tcpi_rto * 1000);
    r.snd_mss = (uint32_t)conn_info.tcpi_maxseg;
    r.rtt = (uint32_t)(conn_info.tcpi_srtt * 1000);
    r.rttvar = (uint32_t)(conn_info.tcpi_rttvar * 1000);
    r.snd_ssthresh = (uint32_t)conn_info.tcpi_snd_ssthresh;
    r.snd_cwnd =
        conn_info.tcpi_maxseg > 0 ? (uint32_t)(conn_info.tcpi_snd_cwnd / conn_info.tcpi_maxseg) : 0;
    r.total_retrans = (uint32_t)conn_info.tcpi_txretransmitpackets;
    return r;
}

int32_t get_so_error_impl(int32_t sockfd)
{
    int real_error;
    socklen_t real_error_len = sizeof(real_error);
    int ret = getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &real_error, &real_error_len);
    if (ret < 0)
        return -errno;
    return real_error;
}

void sockaddr_set_family_impl(void *sockaddr, int family)
{
    ((struct sockaddr *)sockaddr)->sa_family = (sa_family_t)family;
}

int sockaddr_get_family_impl(void *sockaddr)
{
    return ((struct sockaddr *)sockaddr)->sa_family;
}

uint32_t sockaddr_inet_size_impl(void)
{
    return (uint32_t)sizeof(struct sockaddr_in);
}

uint32_t sockaddr_inet6_size_impl(void)
{
    return (uint32_t)sizeof(struct sockaddr_in6);
}

uint32_t sockaddr_un_fill_path_impl(void *sockaddr, uint32_t buf_size, const char *prefix,
                                    pid_t pid, uint32_t socket_id)
{
    struct sockaddr_un *sun = sockaddr;
    if (buf_size < sizeof(*sun))
        return 0;
    memset(sun, 0, sizeof(*sun));
    sun->sun_family = AF_UNIX;
    snprintf(sun->sun_path, sizeof(sun->sun_path), "%s%d.%u", prefix, (int)pid, socket_id);
    return (uint32_t)(offsetof(struct sockaddr_un, sun_path) + strlen(sun->sun_path) + 1);
}

void sockaddr_un_unlink_impl(void *sockaddr)
{
    struct sockaddr_un *sun = sockaddr;
    unlink(sun->sun_path);
}

int sock_family_to_real(int fake)
{
    switch (fake) {
    case PF_LOCAL_:
        return PF_LOCAL;
    case PF_INET_:
        return PF_INET;
    case PF_INET6_:
        return PF_INET6;
    }
    return -1;
}

int sock_family_from_real(int fake)
{
    switch (fake) {
    case PF_LOCAL:
        return PF_LOCAL_;
    case PF_INET:
        return PF_INET_;
    case PF_INET6:
        return PF_INET6_;
    }
    return -1;
}

int sock_type_to_real(int type, int protocol)
{
    switch (type & 0xff) {
    case SOCK_STREAM_:
        if (protocol != 0 && protocol != IPPROTO_TCP)
            return -1;
        return SOCK_STREAM;
    case SOCK_DGRAM_:
        switch (protocol) {
        default:
            return -1;
        case 0:
        case IPPROTO_UDP:
        case IPPROTO_ICMP:
        case IPPROTO_ICMPV6:
            break;
        }
        return SOCK_DGRAM;
    case SOCK_RAW_:
        switch (protocol) {
        default:
            return -1;
        case IPPROTO_RAW:
        case IPPROTO_UDP:
        case IPPROTO_ICMP:
        case IPPROTO_ICMPV6:
            break;
        }
        return SOCK_DGRAM;
    }
    return -1;
}

int sock_flags_to_real(int fake)
{
    int real = 0;
    if (fake & MSG_OOB_)
        real |= MSG_OOB;
    if (fake & MSG_PEEK_)
        real |= MSG_PEEK;
    if (fake & MSG_CTRUNC_)
        real |= MSG_CTRUNC;
    if (fake & MSG_TRUNC_)
        real |= MSG_TRUNC;
    if (fake & MSG_DONTWAIT_)
        real |= MSG_DONTWAIT;
    if (fake & MSG_EOR_)
        real |= MSG_EOR;
    if (fake & MSG_WAITALL_)
        real |= MSG_WAITALL;
    if (fake & ~(MSG_OOB_ | MSG_PEEK_ | MSG_CTRUNC_ | MSG_TRUNC_ | MSG_DONTWAIT_ | MSG_EOR_ |
                 MSG_WAITALL_))
        TRACE("unimplemented socket flags %d\n", fake);
    return real;
}

int sock_flags_from_real(int real)
{
    int fake = 0;
    if (real & MSG_OOB)
        fake |= MSG_OOB_;
    if (real & MSG_PEEK)
        fake |= MSG_PEEK_;
    if (real & MSG_CTRUNC)
        fake |= MSG_CTRUNC_;
    if (real & MSG_TRUNC)
        fake |= MSG_TRUNC_;
    if (real & MSG_DONTWAIT)
        fake |= MSG_DONTWAIT_;
    if (real & MSG_EOR)
        fake |= MSG_EOR_;
    if (real & MSG_WAITALL)
        fake |= MSG_WAITALL_;
    if (real &
        ~(MSG_OOB | MSG_PEEK | MSG_CTRUNC | MSG_TRUNC | MSG_DONTWAIT | MSG_EOR | MSG_WAITALL))
        TRACE("unimplemented socket flags %d\n", real);
    return fake;
}

int sock_opt_to_real(int fake, int level)
{
    switch (level) {
    case SOL_SOCKET_:
        switch (fake) {
        case SO_REUSEADDR_:
            return SO_REUSEADDR;
        case SO_TYPE_:
            return SO_TYPE;
        case SO_ERROR_:
            return SO_ERROR;
        case SO_BROADCAST_:
            return SO_BROADCAST;
        case SO_KEEPALIVE_:
            return SO_KEEPALIVE;
        case SO_LINGER_:
            return SO_LINGER;
        case SO_SNDBUF_:
            return SO_SNDBUF;
        case SO_RCVBUF_:
            return SO_RCVBUF;
        case SO_TIMESTAMP_:
            return SO_TIMESTAMP;
        case SO_RCVTIMEO_:
            return SO_RCVTIMEO;
        case SO_SNDTIMEO_:
            return SO_SNDTIMEO;
        }
        break;
    case IPPROTO_TCP:
        switch (fake) {
        case TCP_NODELAY_:
            return TCP_NODELAY;
        case TCP_DEFER_ACCEPT_:
            return 0;
        case TCP_INFO_:
            return 0;
        case TCP_CONGESTION_:
            return 0;
        }
        break;
    case IPPROTO_IP:
        switch (fake) {
        case IP_TOS_:
            return IP_TOS;
        case IP_TTL_:
            return IP_TTL;
        case IP_HDRINCL_:
            return IP_HDRINCL;
        case IP_RETOPTS_:
            return IP_RETOPTS;
        case IP_RECVTTL_:
            return IP_RECVTTL;
        case IP_RECVTOS_:
            return IP_RECVTOS;
        }
        break;
    case IPPROTO_IPV6:
        switch (fake) {
        case IPV6_UNICAST_HOPS_:
            return IPV6_UNICAST_HOPS;
        case IPV6_TCLASS_:
            return IPV6_TCLASS;
        case IPV6_V6ONLY_:
            return IPV6_V6ONLY;
        }
        break;
    }
    return -1;
}

int sock_level_to_real(int fake)
{
    if (fake == SOL_SOCKET_)
        return SOL_SOCKET;
    return fake;
}
