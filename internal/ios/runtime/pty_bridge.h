#ifndef IXLAND_IOS_PTY_BRIDGE_H
#define IXLAND_IOS_PTY_BRIDGE_H

#import "LinuxInterop.h"

struct tty *guest_terminal_pty_open(nsobj_t *terminal_out);

#endif
