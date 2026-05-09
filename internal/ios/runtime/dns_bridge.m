#import "dns_bridge.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <resolv.h>
#include <stdlib.h>

#import <Foundation/Foundation.h>
#import <IXLandLinuxRuntime/fs/path.h>
#import <IXLandLinuxRuntime/kernel/calls.h>
#import <IXLandLinuxRuntime/kernel/task.h>

void runtime_configure_dns(void)
{
    struct __res_state res;
    if (EXIT_SUCCESS != res_ninit(&res))
        return;

    NSMutableString *resolvConf = [NSMutableString new];
    if (res.dnsrch[0] != NULL) {
        [resolvConf appendString:@"search"];
        for (int i = 0; res.dnsrch[i] != NULL; i++)
            [resolvConf appendFormat:@" %s", res.dnsrch[i]];
        [resolvConf appendString:@"\n"];
    }

    union res_sockaddr_union servers[NI_MAXSERV];
    int serversFound = res_getservers(&res, servers, NI_MAXSERV);
    char address[NI_MAXHOST];
    for (int i = 0; i < serversFound; i++) {
        union res_sockaddr_union s = servers[i];
        if (s.sin.sin_len == 0)
            continue;
        getnameinfo((struct sockaddr *) &s.sin, s.sin.sin_len,
                    address, sizeof(address),
                    NULL, 0, NI_NUMERICHOST);
        [resolvConf appendFormat:@"nameserver %s\n", address];
    }

    current = pid_get_task(1);
    struct fd *fd = generic_open("/etc/resolv.conf", O_WRONLY_ | O_CREAT_ | O_TRUNC_, 0666);
    if (!IS_ERR(fd)) {
        fd->ops->write(fd, resolvConf.UTF8String, [resolvConf lengthOfBytesUsingEncoding:NSUTF8StringEncoding]);
        fd_close(fd);
    }
}
