#ifndef IXLAND_IOS_SESSION_BRIDGE_H
#define IXLAND_IOS_SESSION_BRIDGE_H

#import "LinuxInterop.h"

typedef void (^StartSessionDoneBlock)(int retval, int pid, nsobj_t terminal);
void linux_start_session(const char *exe, const char *const *argv, const char *envp,
                         StartSessionDoneBlock done);

#endif
