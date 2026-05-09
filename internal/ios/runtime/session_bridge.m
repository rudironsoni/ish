#import "pty_bridge.h"
#import "session_bridge.h"

#include <CoreFoundation/CoreFoundation.h>
#import <IXLandLinuxRuntime/fs/devices.h>
#import <IXLandLinuxRuntime/fs/tty.h>
#import <IXLandLinuxRuntime/kernel/init.h>
#import <IXLandLinuxRuntime/kernel/errno.h>

nsobj_t objc_get(nsobj_t object)
{
    if (object == NULL)
        return NULL;
    return (nsobj_t) CFRetain((CFTypeRef) object);
}

void objc_put(nsobj_t object)
{
    if (object != NULL)
        CFRelease((CFTypeRef) object);
}

void linux_start_session(const char *exe, const char *const *argv, const char *envp,
                         StartSessionDoneBlock done)
{
    nsobj_t terminal = NULL;
    struct tty *tty = guest_terminal_pty_open(&terminal);
    if (IS_ERR(tty)) {
        done((int) PTR_ERR(tty), 0, NULL);
        return;
    }

    int pid = 0;
    int err = prepare_session_with_tty(exe, argv, envp, tty, &pid);
    if (err < 0)
        goto fail;

    done(0, pid, objc_get(terminal));
    start_prepared_session();
    objc_put(terminal);
    return;

fail:
    if (tty != NULL)
        tty_release(tty);
    objc_put(terminal);
    done(err, 0, NULL);
}
