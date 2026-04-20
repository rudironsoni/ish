#ifndef IXLAND_SOCK_BRIDGE_H
#define IXLAND_SOCK_BRIDGE_H

#include <ixland/host_socket_types.h>
#include <ixland/linux_types.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

struct sockaddr_result {
    int32_t ret;
    uint32_t addr_len;
};

struct tcp_info_result {
    int32_t ret;
    uint8_t state;
    uint8_t options;
    uint8_t snd_wscale;
    uint8_t rcv_wscale;
    uint32_t rto;
    uint32_t snd_mss;
    uint32_t rtt;
    uint32_t rttvar;
    uint32_t snd_ssthresh;
    uint32_t snd_cwnd;
    uint32_t total_retrans;
};

int32_t socket_impl(int32_t domain, int32_t type, int32_t protocol);
int32_t setsockopt_strip_impl(int32_t sockfd);
int32_t bind_impl(int32_t sockfd, const void *addr, uint32_t addr_len);
int32_t connect_impl(int32_t sockfd, const void *addr, uint32_t addr_len);
int32_t listen_impl(int32_t sockfd, int32_t backlog);
int32_t accept_impl(int32_t sockfd, void *addr, uint32_t *addr_len);
int32_t shutdown_impl(int32_t sockfd, int32_t how);
int32_t setsockopt_impl(int32_t sockfd, int32_t level, int32_t opt, const void *val, int32_t len);
int32_t getsockopt_impl(int32_t sockfd, int32_t level, int32_t opt, void *val, int32_t *len);
struct sockaddr_result getsockname_impl(int32_t sockfd, void *addr, uint32_t addr_len);
struct sockaddr_result getpeername_impl(int32_t sockfd, void *addr, uint32_t addr_len);
int64_t sendto_impl(int32_t sockfd, const void *buf, uint32_t len, int32_t flags, const void *addr,
                    uint32_t addr_len);
int64_t recvfrom_impl(int32_t sockfd, void *buf, uint32_t len, int32_t flags, void *addr,
                      uint32_t *addr_len);
int64_t sendmsg_impl(int32_t sockfd, struct host_msghdr *msg, int32_t flags);
int64_t recvmsg_impl(int32_t sockfd, struct host_msghdr *msg, int32_t flags);
int32_t socketpair_impl(int32_t domain, int32_t type, int32_t protocol, int32_t sv[2]);

struct tcp_info_result get_tcp_info_impl(int32_t sockfd);
int32_t get_so_error_impl(int32_t sockfd);

void sockaddr_set_family_impl(void *sockaddr, int family);
int sockaddr_get_family_impl(void *sockaddr);
uint32_t sockaddr_inet_size_impl(void);
uint32_t sockaddr_inet6_size_impl(void);
uint32_t sockaddr_un_fill_path_impl(void *sockaddr, uint32_t buf_size, const char *prefix,
                                    pid_t pid, uint32_t socket_id);
void sockaddr_un_unlink_impl(void *sockaddr);

#endif
