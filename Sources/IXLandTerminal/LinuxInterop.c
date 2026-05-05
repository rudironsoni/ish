//
//  LinuxInterop.c
//  iSH
//
//  Created by Theodore Dubois on 7/3/21.
//

#import "LinuxInterop.h"

#include <CoreFoundation/CoreFoundation.h>
#import <IXLandLinuxRuntime/fs/devices.h>
#import <IXLandLinuxRuntime/fs/tty.h>
#import <IXLandLinuxRuntime/kernel/calls.h>
#import <IXLandLinuxRuntime/kernel/errno.h>
#import <IXLandLinuxRuntime/kernel/init.h>
#import <IXLandLinuxRuntime/kernel/task.h>
#import <IXLandLinuxRuntime/util/misc.h>
#include <stdlib.h>
#include <string.h>

nsobj_t objc_get(nsobj_t object)
{
    if (object == NULL)
        return NULL;
    return (nsobj_t)CFRetain((CFTypeRef)object);
}

void objc_put(nsobj_t object)
{
    if (object != NULL)
        CFRelease((CFTypeRef)object);
}

static int build_pts_path(int tty_num, char *buf, size_t size)
{
    static const char prefix[] = "/dev/pts/";
    char digits[16];
    size_t digit_count = 0;
    unsigned int value = (unsigned int)tty_num;

    if (size <= sizeof(prefix))
        return -_ENAMETOOLONG;

    memcpy(buf, prefix, sizeof(prefix) - 1);

    do {
        digits[digit_count++] = (char)('0' + (value % 10));
        value /= 10;
    } while (value != 0 && digit_count < sizeof(digits));

    if ((sizeof(prefix) - 1) + digit_count + 1 > size)
        return -_ENAMETOOLONG;

    for (size_t i = 0; i < digit_count; i++)
        buf[(sizeof(prefix) - 1) + i] = digits[digit_count - i - 1];
    buf[(sizeof(prefix) - 1) + digit_count] = '\0';
    return 0;
}

static char *flatten_argv(const char *const *argv, size_t *argc_out)
{
    size_t argc = 0;
    size_t total_size = 1;

    while (argv[argc] != NULL) {
        total_size += strlen(argv[argc]) + 1;
        argc++;
    }

    char *flat_argv = malloc(total_size);
    if (flat_argv == NULL)
        return NULL;

    char *cursor = flat_argv;
    for (size_t i = 0; i < argc; i++) {
        size_t arg_size = strlen(argv[i]) + 1;
        memcpy(cursor, argv[i], arg_size);
        cursor += arg_size;
    }
    *cursor = '\0';

    *argc_out = argc;
    return flat_argv;
}

static bool current_is_session_leader(void)
{
    bool is_session_leader;

    lock(&pids_lock);
    is_session_leader = current->group->sid == current->pid;
    unlock(&pids_lock);

    return is_session_leader;
}

void linux_start_session(const char *exe, const char *const *argv, const char *envp,
                         StartSessionDoneBlock done)
{
    nsobj_t terminal = NULL;
    struct tty *tty = ios_pty_open(&terminal);
    if (IS_ERR(tty)) {
        done((int)PTR_ERR(tty), 0, NULL);
        return;
    }

    int tty_num = tty->num;
    int err = (int)sys_setsid();
    if (err < 0 && !(err == _EPERM && current_is_session_leader()))
        goto fail_with_tty;

    char pts_path[32];
    err = build_pts_path(tty_num, pts_path, sizeof(pts_path));
    if (err < 0)
        goto fail_with_tty;

    err = create_stdio(pts_path, TTY_PSEUDO_SLAVE_MAJOR, tty_num);
    if (err < 0)
        goto fail;

    struct fd *stdio = current->files->files[0];
    if (stdio == NULL || stdio->ops == NULL || stdio->ops->ioctl == NULL) {
        err = -_ENOTTY;
        goto fail;
    }

    err = stdio->ops->ioctl(stdio, TIOCSCTTY_, NULL);
    if (err < 0)
        goto fail;

    size_t argc = 0;
    char *flat_argv = flatten_argv(argv, &argc);
    if (flat_argv == NULL) {
        err = -_ENOMEM;
        goto fail;
    }

    err = do_execve(exe, argc, flat_argv, envp);
    free(flat_argv);
    if (err < 0)
        goto fail;

    int pid = current->pid;
    done(0, pid, objc_get(terminal));
    task_start(current);
    current = NULL;
    objc_put(terminal);
    return;

fail_with_tty:
    if (tty != NULL) {
        tty_release(tty);
        tty = NULL;
    }
fail:
    if (tty != NULL)
        tty_release(tty);
    objc_put(terminal);
    done(err, 0, NULL);
}
