#import "Sources/IXLandTerminal/Terminal.h"
#import "Sources/IXLandTerminal/LinuxInterop.h"

#import <ISHInstrumentation.h>
#import <IXLandLinuxRuntime/fs/devices.h>
#import <IXLandLinuxRuntime/fs/tty.h>

void Terminal_setLinuxTTY(nsobj_t _self, struct linux_tty *tty)
{
    Terminal *terminal = (__bridge Terminal *) _self;
    [terminal attachLinuxTTY:tty];
}

int Terminal_sendOutput_length(nsobj_t _self, const char *data, int size)
{
    return [(__bridge Terminal *) _self sendOutput:data length:size];
}

int Terminal_roomForOutput(nsobj_t _self)
{
    return [(__bridge Terminal *) _self roomForOutput];
}

void Terminal_releaseBoundTTYData(nsobj_t terminal)
{
    if (terminal != NULL)
        CFBridgingRelease((void *) terminal);
}

bool Terminal_bindGuestTTY(struct tty *tty, nsobj_t *terminal_out)
{
    if (terminal_out != NULL)
        *terminal_out = NULL;
    if (tty == NULL || terminal_out == NULL)
        return false;

    Terminal *terminal = (__bridge Terminal *) tty->driver_data;
    if (terminal == NULL) {
        terminal = [Terminal terminalWithType:tty->type number:tty->num];
        if (terminal == NULL)
            return false;

        lock(&tty->lock);
        if (tty->driver_data == NULL) {
            tty->driver_data = (void *) CFBridgingRetain(terminal);
            [terminal attachTTY:tty];
        }
        unlock(&tty->lock);
        terminal = (__bridge Terminal *) tty->driver_data;
    } else {
        [terminal attachTTY:tty];
    }

    *terminal_out = objc_get((__bridge nsobj_t) terminal);
    return *terminal_out != NULL;
}

static int terminal_tty_init(struct tty *tty)
{
    unlock(&ttys_lock);
    void (^init_block)(void) = ^{
        Terminal *terminal = [Terminal terminalWithType:tty->type number:tty->num];
        tty->driver_data = (void *) CFBridgingRetain(terminal);
        [terminal attachTTY:tty];
    };
    if ([NSThread isMainThread])
        init_block();
    else
        dispatch_sync(dispatch_get_main_queue(), init_block);

    lock(&ttys_lock);
    return 0;
}

static int terminal_tty_write(struct tty *tty, const void *buf, size_t len, bool blocking)
{
    Terminal *terminal = (__bridge Terminal *) tty->driver_data;
    [ISHInstrumentation recordEvent:@"terminal.tty.write.callback"
                         attributes:@{ @"byte_count": @((NSInteger) len),
                                       @"blocking": @(blocking) }];
    return [terminal sendOutput:buf length:(int) len];
}

static void terminal_tty_cleanup(struct tty *tty)
{
    Terminal *terminal = CFBridgingRelease(tty->driver_data);
    tty->driver_data = NULL;
    [terminal attachTTY:NULL];
}

static struct tty_driver_ops terminal_tty_ops = {
    .init = terminal_tty_init,
    .write = terminal_tty_write,
    .cleanup = terminal_tty_cleanup,
};
DEFINE_TTY_DRIVER(terminal_console_driver, &terminal_tty_ops, TTY_CONSOLE_MAJOR, 64);
struct tty_driver terminal_pty_driver = { .ops = &terminal_tty_ops };
