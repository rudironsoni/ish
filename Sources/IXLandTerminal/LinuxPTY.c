//
//  LinuxPTY.c
//  libiSHLinux
//
//  Created by Theodore Dubois on 12/30/21.
//

#import "LinuxInterop.h"

#import <IXLandLinuxRuntime/fs/tty.h>
#import <IXLandLinuxRuntime/kernel/errno.h>
#import <IXLandLinuxRuntime/util/misc.h>

extern struct tty_driver ios_pty_driver;

struct tty *ios_pty_open(nsobj_t *terminal_out)
{
    struct tty *tty = pty_open_guest_terminal(&ios_pty_driver);
    if (IS_ERR(tty))
        return tty;

    if (!Terminal_bindGuestTTY(tty, terminal_out)) {
        tty_release(tty);
        return ERR_PTR(_ENOMEM);
    }

    return tty;
}
