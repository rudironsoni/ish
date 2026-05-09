#import "pty_bridge.h"

#import "Terminal.h"

#import <IXLandLinuxRuntime/fs/tty.h>
#import <IXLandLinuxRuntime/kernel/errno.h>
#import <IXLandLinuxRuntime/util/misc.h>

extern struct tty_driver terminal_pty_driver;

struct tty *guest_terminal_pty_open(nsobj_t *terminal_out)
{
    struct tty *tty = pty_open_guest_terminal(&terminal_pty_driver);
    if (IS_ERR(tty))
        return tty;

    if (!Terminal_bindGuestTTY(tty, terminal_out)) {
        tty_release(tty);
        return ERR_PTR(_ENOMEM);
    }

    return tty;
}
