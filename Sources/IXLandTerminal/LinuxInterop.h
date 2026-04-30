//
//  LinuxInterop.h
//  iSH
//
//  Created by Theodore Dubois on 7/3/21.
//

#ifndef LinuxInterop_h
#define LinuxInterop_h

#include <sys/types.h>

typedef const void *nsobj_t;

nsobj_t objc_get(nsobj_t object);
void objc_put(nsobj_t object);

struct linux_tty {
    struct linux_tty_callbacks *ops;
};

struct linux_tty_callbacks {
    void (*can_output)(struct linux_tty *tty);
    void (*send_input)(struct linux_tty *tty, const char *data, size_t length);
    void (*resize)(struct linux_tty *tty, int cols, int rows);
    void (*hangup)(struct linux_tty *tty);
};

struct tty;

struct tty *ios_pty_open(nsobj_t *terminal_out);

typedef void (^StartSessionDoneBlock)(int retval, int pid, nsobj_t terminal);
void linux_start_session(const char *exe, const char *const *argv, const char *envp,
                         StartSessionDoneBlock done);

#endif /* LinuxInterop_h */
