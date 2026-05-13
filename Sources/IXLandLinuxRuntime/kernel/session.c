#import <IXLandLinuxRuntime/fs/devices.h>
#import <IXLandLinuxRuntime/fs/tty.h>
#import <IXLandLinuxRuntime/kernel/calls.h>
#import <IXLandLinuxRuntime/kernel/errno.h>
#import <IXLandLinuxRuntime/kernel/init.h>
#import <IXLandLinuxRuntime/kernel/task.h>
#import <IXLandLinuxRuntime/util/misc.h>
#include <stdlib.h>
#include <string.h>

static int build_pts_path(int tty_num, char *buf, size_t size)
{
    static const char prefix[] = "/dev/pts/";
    char digits[16];
    size_t digit_count = 0;
    unsigned int value = (unsigned int) tty_num;

    if (size <= sizeof(prefix))
        return -_ENAMETOOLONG;

    memcpy(buf, prefix, sizeof(prefix) - 1);

    do {
        digits[digit_count++] = (char) ('0' + (value % 10));
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

int prepare_session_with_tty(const char *exe, const char *const *argv, const char *envp,
                             struct tty *tty, int *pid_out)
{
    int tty_num = tty->num;
    int err = (int) sys_setsid();
    if (err < 0 && !(err == _EPERM && current_is_session_leader()))
        return err;

    char pts_path[32];
    err = build_pts_path(tty_num, pts_path, sizeof(pts_path));
    if (err < 0)
        return err;

    err = create_stdio(pts_path, TTY_PSEUDO_SLAVE_MAJOR, tty_num);
    if (err < 0)
        return err;

    struct fd *stdio = current->files->files[0];
    if (stdio == NULL || stdio->ops == NULL || stdio->ops->ioctl == NULL)
        return -_ENOTTY;

    err = stdio->ops->ioctl(stdio, TIOCSCTTY_, NULL);
    if (err < 0)
        return err;

    size_t argc = 0;
    char *flat_argv = flatten_argv(argv, &argc);
    if (flat_argv == NULL)
        return -_ENOMEM;

    const char *arg0 = argc > 0 ? flat_argv : "";
    const char *arg1 = argc > 1 ? arg0 + strlen(arg0) + 1 : "";
    const char *arg2 = argc > 2 ? arg1 + strlen(arg1) + 1 : "";
    const char *arg3 = argc > 3 ? arg2 + strlen(arg2) + 1 : "";
    const char *arg4 = argc > 4 ? arg3 + strlen(arg3) + 1 : "";
    trace_attribute_t argv_attrs[] = {
        { "exe", exe != NULL ? exe : "" },
        { "argc", "" },
        { "arg0", arg0 },
        { "arg1", arg1 },
        { "arg2", arg2 },
        { "arg3", arg3 },
        { "arg4", arg4 },
    };
    char argc_buf[32];
    snprintf(argc_buf, sizeof(argc_buf), "%zu", argc);
    argv_attrs[1].value = argc_buf;
    (void) trace_begin_interval(TRACE_ORIGIN_EXEC, "task.proof.session.exec.argv",
                                argv_attrs, sizeof(argv_attrs) / sizeof(argv_attrs[0]));

    err = do_execve(exe, argc, flat_argv, envp);
    free(flat_argv);
    if (err < 0)
        return err;

    if (pid_out != NULL)
        *pid_out = current->pid;
    return 0;
}

void start_prepared_session(void)
{
    task_start(current);
    current = NULL;
}
